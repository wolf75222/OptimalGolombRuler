#include "search_mpi_v2.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <omp.h>
#include <mpi.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - MPI V2 (HYPERCUBE + BITSET128)
// =============================================================================
// Key improvements over MPI V1:
//   - BitSet128 (2x uint64_t) instead of 4-word array
//   - reversed_marks encoding: shift computes all differences in O(1)
//   - Prefix-based work distribution for better load balancing
//   - Keeps hypercube topology for O(log P) bound synchronization
// =============================================================================

static std::atomic<long long> exploredCountMPI_V2{0};

// Hypercube sync frequency
constexpr int SYNC_INTERVAL_V2 = 64;

// Maximum marks we support
constexpr int MAX_MARKS_V2 = 24;
constexpr int MAX_LEN_V2 = 127;  // Max supported with 2x uint64_t

// =============================================================================
// FAST 128-BIT BITSET using 2x uint64_t
// =============================================================================
struct alignas(16) BitSet128 {
    uint64_t lo;  // bits 0-63
    uint64_t hi;  // bits 64-127

    BitSet128() : lo(0), hi(0) {}
    BitSet128(uint64_t l, uint64_t h) : lo(l), hi(h) {}

    inline void set(int pos) {
        if (pos < 64) {
            lo |= (1ULL << pos);
        } else {
            hi |= (1ULL << (pos - 64));
        }
    }

    inline bool test(int pos) const {
        if (pos < 64) {
            return (lo >> pos) & 1;
        } else {
            return (hi >> (pos - 64)) & 1;
        }
    }

    inline BitSet128 operator<<(int n) const {
        if (n == 0) return *this;
        if (n >= 128) return BitSet128(0, 0);
        if (n >= 64) {
            return BitSet128(0, lo << (n - 64));
        }
        uint64_t new_hi = (hi << n) | (lo >> (64 - n));
        uint64_t new_lo = lo << n;
        return BitSet128(new_lo, new_hi);
    }

    inline BitSet128 operator&(const BitSet128& other) const {
        return BitSet128(lo & other.lo, hi & other.hi);
    }

    inline BitSet128 operator^(const BitSet128& other) const {
        return BitSet128(lo ^ other.lo, hi ^ other.hi);
    }

    inline bool any() const {
        return (lo | hi) != 0;
    }

    inline void reset() {
        lo = hi = 0;
    }
};

// =============================================================================
// WORK ITEM - A prefix to explore
// =============================================================================
struct alignas(32) WorkItemMPI_V2 {
    BitSet128 reversed_marks;
    BitSet128 used_dist;
    int marks_count;
    int ruler_length;
};

// =============================================================================
// STACK FRAME - State at each level
// =============================================================================
struct alignas(32) StackFrameMPI_V2 {
    BitSet128 reversed_marks;
    BitSet128 used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
};

// =============================================================================
// THREAD LOCAL BEST
// =============================================================================
struct alignas(64) ThreadBestMPI_V2 {
    int bestLen;
    int bestMarks[MAX_MARKS_V2];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarksMPI_V2(const BitSet128& reversed_marks,
                                int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks.test(ruler_length - i)) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// PREFIX GENERATION (sequential, done on all ranks)
// =============================================================================
static void generatePrefixesMPI_V2(
    BitSet128 reversed_marks,
    BitSet128 used_dist,
    int marks_count,
    int ruler_length,
    int target_depth,
    int target_marks,
    int maxLen,
    std::vector<WorkItemMPI_V2>& prefixes)
{
    if (marks_count == target_depth) {
        WorkItemMPI_V2 item;
        item.reversed_marks = reversed_marks;
        item.used_dist = used_dist;
        item.marks_count = marks_count;
        item.ruler_length = ruler_length;
        prefixes.push_back(item);
        return;
    }

    const int remaining = target_marks - marks_count;
    const int min_additional = (remaining * (remaining + 1)) / 2;

    if (ruler_length + min_additional >= maxLen) {
        return;
    }

    const int min_pos = ruler_length + 1;
    const int max_remaining = ((remaining - 1) * remaining) / 2;
    const int max_pos = maxLen - max_remaining - 1;

    for (int pos = min_pos; pos <= max_pos; ++pos) {
        const int offset = pos - ruler_length;

        BitSet128 new_dist = reversed_marks << offset;

        if ((new_dist & used_dist).any()) {
            continue;
        }

        BitSet128 new_reversed = reversed_marks << offset;
        new_reversed.set(0);

        BitSet128 new_used = used_dist ^ new_dist;

        generatePrefixesMPI_V2(new_reversed, new_used, marks_count + 1, pos,
                               target_depth, target_marks, maxLen, prefixes);
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - V2 (BitSet128 shift-based)
// =============================================================================
static void backtrackIterativeMPI_V2(
    ThreadBestMPI_V2& threadBest,
    const int n,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrameMPI_V2* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameMPI_V2& frame = stack[stackTop];

        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);

        // Pruning: Golomb lower bound
        const int r = n - frame.marks_count;
        const int minAdditionalLength = (r * (r + 1)) / 2;

        if (frame.ruler_length + minAdditionalLength >= currentGlobalBest) [[unlikely]] {
            stackTop--;
            continue;
        }

        const int min_pos = frame.ruler_length + 1;
        const int max_remaining = ((r - 1) * r) / 2;
        const int max_pos = currentGlobalBest - max_remaining - 1;

        int startNext = frame.next_candidate;
        if (startNext == 0) {
            startNext = min_pos;
        }

        bool pushedChild = false;

        for (int pos = startNext; pos <= max_pos; ++pos) {
            const int newGlobalBest = globalBestLen.load(std::memory_order_relaxed);
            if (pos >= newGlobalBest) [[unlikely]] {
                break;
            }

            const int offset = pos - frame.ruler_length;

            // KEY OPTIMIZATION: Single shift computes all differences
            BitSet128 new_dist = frame.reversed_marks << offset;

            // Fast collision check
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;
            }

            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                const int solutionLen = pos;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;

                    BitSet128 final_marks = frame.reversed_marks << offset;
                    final_marks.set(0);

                    extractMarksMPI_V2(final_marks, pos, threadBest.bestMarks, threadBest.bestNumMarks);

                    int expected = globalBestLen.load(std::memory_order_relaxed);
                    while (solutionLen < expected &&
                           !globalBestLen.compare_exchange_weak(expected, solutionLen,
                               std::memory_order_release, std::memory_order_relaxed)) {
                    }
                }
            } else {
                frame.next_candidate = pos + 1;

                StackFrameMPI_V2& newFrame = stack[stackTop + 1];

                newFrame.reversed_marks = frame.reversed_marks << offset;
                newFrame.reversed_marks.set(0);

                newFrame.used_dist.lo = frame.used_dist.lo ^ new_dist.lo;
                newFrame.used_dist.hi = frame.used_dist.hi ^ new_dist.hi;

                newFrame.marks_count = newMarksCount;
                newFrame.ruler_length = pos;
                newFrame.next_candidate = 0;

                stackTop++;
                pushedChild = true;
                break;
            }
        }

        if (!pushedChild) {
            stackTop--;
        }
    }
}

// =============================================================================
// COMPUTE OPTIMAL PREFIX DEPTH
// =============================================================================
static int computePrefixDepthMPI_V2(int n, int numProcesses, int threadsPerProcess) {
    int totalWorkers = numProcesses * threadsPerProcess;

    if (n <= 6) return 2;
    if (n <= 8) return 3;
    if (n <= 10) return 3;
    if (n <= 12) return 4;
    if (n <= 14) return 4;
    if (n <= 16) return 5;

    // For large n with many workers, go deeper for better distribution
    int depth = 5;
    if (totalWorkers > 64) depth = 6;

    if (depth >= n - 2) {
        depth = n - 3;
    }
    if (depth < 2) depth = 2;

    return depth;
}

// =============================================================================
// MAIN SEARCH FUNCTION - MPI V2 (HYPERCUBE + BITSET128)
// =============================================================================
void searchGolombMPI_V2(int n, int maxLen, GolombRuler& best, HypercubeMPI& hypercube)
{
    if (maxLen > MAX_LEN_V2) {
        maxLen = MAX_LEN_V2;
    }

    exploredCountMPI_V2.store(0, std::memory_order_relaxed);

    const int rank = hypercube.rank();
    const int size = hypercube.size();
    const int numThreads = omp_get_max_threads();

    std::atomic<int> globalBestLen(maxLen + 1);

    int localBestLen = maxLen + 1;
    int localBestMarks[MAX_MARKS_V2] = {0};
    int localBestNumMarks = 0;

    // ==========================================================================
    // PHASE 1: Generate all valid prefixes (done on all ranks identically)
    // ==========================================================================
    int prefixDepth = computePrefixDepthMPI_V2(n, size, numThreads);

    std::vector<WorkItemMPI_V2> allPrefixes;
    allPrefixes.reserve(100000);

    {
        BitSet128 reversed_marks;
        BitSet128 used_dist;
        reversed_marks.set(0);

        generatePrefixesMPI_V2(reversed_marks, used_dist, 1, 0,
                               prefixDepth, n, maxLen + 1, allPrefixes);
    }

    const int totalPrefixes = static_cast<int>(allPrefixes.size());

    // ==========================================================================
    // PHASE 2: Distribute prefixes among MPI ranks (static distribution)
    // ==========================================================================
    // Each rank processes prefixes where (prefix_index % size) == rank
    std::vector<WorkItemMPI_V2> myPrefixes;
    myPrefixes.reserve((totalPrefixes / size) + 1);

    for (int i = 0; i < totalPrefixes; ++i) {
        if (i % size == rank) {
            myPrefixes.push_back(allPrefixes[i]);
        }
    }

    const int myNumPrefixes = static_cast<int>(myPrefixes.size());

    // ==========================================================================
    // PHASE 3: Process prefixes in rounds with periodic hypercube sync
    // ==========================================================================
    int prefixIndex = 0;
    int round = 0;

    while (prefixIndex < myNumPrefixes) {
        int prefixesThisRound = std::min(SYNC_INTERVAL_V2, myNumPrefixes - prefixIndex);
        int startIdx = prefixIndex;
        int endIdx = prefixIndex + prefixesThisRound;

        #pragma omp parallel shared(globalBestLen, localBestLen, localBestMarks, localBestNumMarks)
        {
            ThreadBestMPI_V2 threadBest{};
            threadBest.bestLen = maxLen + 1;
            threadBest.bestNumMarks = 0;
            long long threadExplored = 0;

            alignas(64) StackFrameMPI_V2 stack[MAX_MARKS_V2];

            #pragma omp for schedule(dynamic, 1)
            for (int idx = startIdx; idx < endIdx; ++idx) {
                const WorkItemMPI_V2& prefix = myPrefixes[static_cast<size_t>(idx)];

                const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
                const int remaining = n - prefix.marks_count;
                const int minAdditional = (remaining * (remaining + 1)) / 2;

                if (prefix.ruler_length + minAdditional >= currentGlobal) {
                    continue;
                }

                StackFrameMPI_V2& frame0 = stack[0];
                frame0.reversed_marks = prefix.reversed_marks;
                frame0.used_dist = prefix.used_dist;
                frame0.marks_count = prefix.marks_count;
                frame0.ruler_length = prefix.ruler_length;
                frame0.next_candidate = 0;

                backtrackIterativeMPI_V2(threadBest, n, globalBestLen, threadExplored, stack);
            }

            exploredCountMPI_V2.fetch_add(threadExplored, std::memory_order_relaxed);

            if (threadBest.bestNumMarks > 0) {
                #pragma omp critical(merge_best_mpi_v2)
                {
                    if (threadBest.bestLen < localBestLen) {
                        localBestLen = threadBest.bestLen;
                        localBestNumMarks = threadBest.bestNumMarks;
                        for (int i = 0; i < threadBest.bestNumMarks; ++i) {
                            localBestMarks[i] = threadBest.bestMarks[i];
                        }
                    }
                }
            }
        }

        prefixIndex = endIdx;
        round++;

        // =====================================================================
        // HYPERCUBE SYNCHRONIZATION - O(log P) communication
        // =====================================================================
        int myBest = globalBestLen.load(std::memory_order_acquire);
        int globalMin = hypercube.allReduceMin(myBest);

        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (globalMin < expected &&
               !globalBestLen.compare_exchange_weak(expected, globalMin,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    // Handle processes with different number of prefixes
    int maxPrefixes;
    MPI_Allreduce(&myNumPrefixes, &maxPrefixes, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    int maxRounds = (maxPrefixes + SYNC_INTERVAL_V2 - 1) / SYNC_INTERVAL_V2;

    while (round < maxRounds) {
        int myBest = globalBestLen.load(std::memory_order_acquire);
        int globalMin = hypercube.allReduceMin(myBest);

        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (globalMin < expected &&
               !globalBestLen.compare_exchange_weak(expected, globalMin,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }
        round++;
    }

    // ==========================================================================
    // FINAL GLOBAL REDUCTION
    // ==========================================================================
    MPI_Barrier(MPI_COMM_WORLD);

    int globalMinLen = hypercube.allReduceMin(localBestLen);

    int hasWinner = (localBestLen == globalMinLen && localBestNumMarks > 0) ? rank : size;
    int globalWinner;
    MPI_Allreduce(&hasWinner, &globalWinner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    int bestLen = maxLen + 1;
    int bestMarks[MAX_MARKS_V2] = {0};
    int bestNumMarks = 0;

    if (globalWinner < size) {
        bestLen = globalMinLen;
        if (rank == globalWinner) {
            bestNumMarks = localBestNumMarks;
            for (int i = 0; i < localBestNumMarks; ++i) {
                bestMarks[i] = localBestMarks[i];
            }
        }
        MPI_Bcast(&bestNumMarks, 1, MPI_INT, globalWinner, MPI_COMM_WORLD);
        MPI_Bcast(bestMarks, bestNumMarks, MPI_INT, globalWinner, MPI_COMM_WORLD);
    }

    if (bestNumMarks > 0) {
        best.marks.assign(bestMarks, bestMarks + bestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountMPI_V2()
{
    long long localCount = exploredCountMPI_V2.load(std::memory_order_relaxed);
    long long globalCount = 0;

    MPI_Reduce(&localCount, &globalCount, 1, MPI_LONG_LONG,
               MPI_SUM, 0, MPI_COMM_WORLD);

    return globalCount;
}
