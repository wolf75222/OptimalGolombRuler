#include "search_mpi_v4.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <omp.h>
#include <mpi.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - MPI V4 (GREEDY INIT + DYNAMIC DISTRIBUTION)
// =============================================================================
// Key features:
//   - Greedy initialization for better starting upper bound
//   - Dynamic prefix distribution: master sends prefixes on-demand
//   - BitSet128 (2x uint64_t) for O(1) collision detection via shift
//   - Works with ANY number of MPI processes (no power-of-2 requirement)
// =============================================================================

static std::atomic<long long> exploredCountMPI_V4{0};

// MPI Tags
constexpr int TAG_REQUEST_WORK = 1;
constexpr int TAG_WORK_ASSIGNMENT = 2;
constexpr int TAG_GLOBAL_BOUND = 3;
constexpr int TAG_NO_MORE_WORK = 4;
constexpr int TAG_FINAL_RESULT = 5;

// Maximum marks we support
constexpr int MAX_MARKS_V4 = 24;
constexpr int MAX_LEN_V4 = 127;  // Max supported with 2x uint64_t

// Sync frequency for OpenMP threads
constexpr int OMP_SYNC_INTERVAL = 512;

// =============================================================================
// FAST 128-BIT BITSET using 2x uint64_t
// =============================================================================
struct alignas(16) BitSet128_V4 {
    uint64_t lo;  // bits 0-63
    uint64_t hi;  // bits 64-127

    BitSet128_V4() : lo(0), hi(0) {}
    BitSet128_V4(uint64_t l, uint64_t h) : lo(l), hi(h) {}

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

    inline BitSet128_V4 operator<<(int n) const {
        if (n == 0) return *this;
        if (n >= 128) return BitSet128_V4(0, 0);
        if (n >= 64) {
            return BitSet128_V4(0, lo << (n - 64));
        }
        uint64_t new_hi = (hi << n) | (lo >> (64 - n));
        uint64_t new_lo = lo << n;
        return BitSet128_V4(new_lo, new_hi);
    }

    inline BitSet128_V4 operator&(const BitSet128_V4& other) const {
        return BitSet128_V4(lo & other.lo, hi & other.hi);
    }

    inline BitSet128_V4 operator^(const BitSet128_V4& other) const {
        return BitSet128_V4(lo ^ other.lo, hi ^ other.hi);
    }

    inline bool any() const {
        return (lo | hi) != 0;
    }

    inline void reset() {
        lo = hi = 0;
    }
};

// =============================================================================
// WORK ITEM - A prefix to explore (for MPI transfer)
// =============================================================================
struct WorkItemMPI_V4 {
    uint64_t reversed_lo;
    uint64_t reversed_hi;
    uint64_t used_lo;
    uint64_t used_hi;
    int marks_count;
    int ruler_length;
};

// =============================================================================
// STACK FRAME - State at each level
// =============================================================================
struct alignas(32) StackFrameMPI_V4 {
    BitSet128_V4 reversed_marks;
    BitSet128_V4 used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
};

// =============================================================================
// THREAD LOCAL BEST
// =============================================================================
struct alignas(64) ThreadBestMPI_V4 {
    int bestLen;
    int bestMarks[MAX_MARKS_V4];
    int bestNumMarks;
};

// =============================================================================
// GREEDY SOLVER - Find initial upper bound
// =============================================================================
static int greedySolve(int n, int maxLen, int* outMarks, int& outNumMarks) {
    BitSet128_V4 used_dist;
    BitSet128_V4 reversed_marks;
    reversed_marks.set(0);

    int marks[MAX_MARKS_V4];
    marks[0] = 0;
    int numMarks = 1;
    int rulerLength = 0;

    // Greedy: add marks at the smallest valid position
    for (int pos = 1; numMarks < n && pos < maxLen; ++pos) {
        int offset = pos - rulerLength;
        BitSet128_V4 new_dist = reversed_marks << offset;

        // Check for conflicts
        if ((new_dist & used_dist).any()) {
            continue;
        }

        // Add mark
        reversed_marks = reversed_marks << offset;
        reversed_marks.set(0);
        used_dist = used_dist ^ new_dist;

        marks[numMarks++] = pos;
        rulerLength = pos;
    }

    if (numMarks == n) {
        outNumMarks = numMarks;
        for (int i = 0; i < numMarks; ++i) {
            outMarks[i] = marks[i];
        }
        return rulerLength;
    }

    outNumMarks = 0;
    return maxLen + 1;  // No greedy solution found
}

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarksMPI_V4(const BitSet128_V4& reversed_marks,
                                int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks.test(ruler_length - i)) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// COMPUTE OPTIMAL PREFIX DEPTH
// =============================================================================
static int computePrefixDepthMPI_V4(int n, int numProcesses) {
    if (n <= 6) return 2;
    if (n <= 8) return 3;
    if (n <= 10) return 3;
    if (n <= 12) return 4;
    if (n <= 14) return 4;
    if (n <= 16) return 5;

    int depth = 5;
    if (numProcesses > 16) depth = 6;

    if (depth >= n - 2) {
        depth = n - 3;
    }
    if (depth < 2) depth = 2;

    return depth;
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - V4
// =============================================================================
static void backtrackIterativeMPI_V4(
    ThreadBestMPI_V4& threadBest,
    const int n,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrameMPI_V4* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameMPI_V4& frame = stack[stackTop];

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
            BitSet128_V4 new_dist = frame.reversed_marks << offset;

            // Fast collision check
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;
            }

            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                const int solutionLen = pos;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;

                    BitSet128_V4 final_marks = frame.reversed_marks << offset;
                    final_marks.set(0);

                    extractMarksMPI_V4(final_marks, pos, threadBest.bestMarks, threadBest.bestNumMarks);

                    int expected = globalBestLen.load(std::memory_order_relaxed);
                    while (solutionLen < expected &&
                           !globalBestLen.compare_exchange_weak(expected, solutionLen,
                               std::memory_order_release, std::memory_order_relaxed)) {
                    }
                }
            } else {
                frame.next_candidate = pos + 1;

                StackFrameMPI_V4& newFrame = stack[stackTop + 1];

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
// MASTER: DYNAMIC PREFIX GENERATION AND DISTRIBUTION
// =============================================================================
static void masterDynamicDistribution(
    int n,
    int prefixDepth,
    int& globalBound,
    int size,
    int* greedyMarks,
    int greedyNumMarks)
{
    // State for recursive prefix generation
    struct PrefixState {
        BitSet128_V4 reversed_marks;
        BitSet128_V4 used_dist;
        int marks_count;
        int ruler_length;
        int next_pos;
    };

    std::vector<PrefixState> genStack;
    genStack.reserve(prefixDepth + 1);

    // Initialize with first mark at 0
    PrefixState initial;
    initial.reversed_marks.reset();
    initial.reversed_marks.set(0);
    initial.used_dist.reset();
    initial.marks_count = 1;
    initial.ruler_length = 0;
    initial.next_pos = 1;
    genStack.push_back(initial);

    int workersFinished = 0;
    int totalWorkSent = 0;

    // Process work requests from workers
    while (workersFinished < size - 1) {
        MPI_Status status;
        int workerBound;

        // Wait for a work request from any worker
        MPI_Recv(&workerBound, 1, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST_WORK,
                 MPI_COMM_WORLD, &status);

        int worker = status.MPI_SOURCE;

        // Update global bound with worker's best
        if (workerBound < globalBound) {
            globalBound = workerBound;
        }

        // Try to generate and send the next prefix
        bool foundPrefix = false;

        while (!genStack.empty() && !foundPrefix) {
            PrefixState& state = genStack.back();

            // Check if we've reached the target depth
            if (state.marks_count == prefixDepth + 1) {
                // Send this prefix to the worker
                WorkItemMPI_V4 work;
                work.reversed_lo = state.reversed_marks.lo;
                work.reversed_hi = state.reversed_marks.hi;
                work.used_lo = state.used_dist.lo;
                work.used_hi = state.used_dist.hi;
                work.marks_count = state.marks_count;
                work.ruler_length = state.ruler_length;

                // Send global bound first
                MPI_Send(&globalBound, 1, MPI_INT, worker, TAG_GLOBAL_BOUND, MPI_COMM_WORLD);
                // Send work item
                MPI_Send(&work, sizeof(WorkItemMPI_V4), MPI_BYTE, worker,
                         TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);

                totalWorkSent++;
                foundPrefix = true;
                genStack.pop_back();
                continue;
            }

            // Calculate bounds for next position
            const int r = n - state.marks_count;
            const int min_pos = state.ruler_length + 1;
            const int max_remaining = ((r - 1) * r) / 2;
            const int max_pos = globalBound - max_remaining - 1;

            // Find next valid position
            bool foundChild = false;
            for (int pos = state.next_pos; pos <= max_pos; ++pos) {
                const int offset = pos - state.ruler_length;
                BitSet128_V4 new_dist = state.reversed_marks << offset;

                if ((new_dist & state.used_dist).any()) {
                    continue;
                }

                // Found a valid position, push new state
                PrefixState newState;
                newState.reversed_marks = state.reversed_marks << offset;
                newState.reversed_marks.set(0);
                newState.used_dist = state.used_dist ^ new_dist;
                newState.marks_count = state.marks_count + 1;
                newState.ruler_length = pos;
                newState.next_pos = pos + 1;

                // Update current state to continue from next position
                state.next_pos = pos + 1;

                genStack.push_back(newState);
                foundChild = true;
                break;
            }

            if (!foundChild) {
                // No more children, backtrack
                genStack.pop_back();
            }
        }

        if (!foundPrefix) {
            // No more work, tell worker to finish
            MPI_Send(&globalBound, 1, MPI_INT, worker, TAG_NO_MORE_WORK, MPI_COMM_WORLD);
            workersFinished++;
        }
    }
}

// =============================================================================
// WORKER: REQUEST AND PROCESS WORK
// =============================================================================
static void workerProcessWork(
    int n,
    int& localBestLen,
    int* localBestMarks,
    int& localBestNumMarks,
    int initialBound)
{
    std::atomic<int> globalBestLen(initialBound);
    localBestLen = initialBound;
    localBestNumMarks = 0;

    const int numThreads = omp_get_max_threads();

    // Buffer for work items
    std::vector<WorkItemMPI_V4> workBuffer;
    workBuffer.reserve(64);

    bool done = false;

    while (!done) {
        // Request work from master
        int myBest = globalBestLen.load(std::memory_order_relaxed);
        MPI_Send(&myBest, 1, MPI_INT, 0, TAG_REQUEST_WORK, MPI_COMM_WORLD);

        // Receive global bound
        int newBound;
        MPI_Status status;
        MPI_Recv(&newBound, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_NO_MORE_WORK) {
            done = true;
            break;
        }

        // Update local bound
        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (newBound < expected &&
               !globalBestLen.compare_exchange_weak(expected, newBound,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }

        // Receive work item
        WorkItemMPI_V4 work;
        MPI_Recv(&work, sizeof(WorkItemMPI_V4), MPI_BYTE, 0,
                 TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Process this prefix
        const int currentBound = globalBestLen.load(std::memory_order_acquire);
        const int remaining = n - work.marks_count;
        const int minAdditional = (remaining * (remaining + 1)) / 2;

        if (work.ruler_length + minAdditional >= currentBound) {
            continue;  // Pruned
        }

        // Single-threaded processing for simplicity (each worker gets one prefix at a time)
        ThreadBestMPI_V4 threadBest{};
        threadBest.bestLen = currentBound;
        threadBest.bestNumMarks = 0;
        long long localExplored = 0;

        alignas(64) StackFrameMPI_V4 stack[MAX_MARKS_V4];

        StackFrameMPI_V4& frame0 = stack[0];
        frame0.reversed_marks.lo = work.reversed_lo;
        frame0.reversed_marks.hi = work.reversed_hi;
        frame0.used_dist.lo = work.used_lo;
        frame0.used_dist.hi = work.used_hi;
        frame0.marks_count = work.marks_count;
        frame0.ruler_length = work.ruler_length;
        frame0.next_candidate = 0;

        backtrackIterativeMPI_V4(threadBest, n, globalBestLen, localExplored, stack);

        exploredCountMPI_V4.fetch_add(localExplored, std::memory_order_relaxed);

        // Update local best
        if (threadBest.bestNumMarks > 0 && threadBest.bestLen < localBestLen) {
            localBestLen = threadBest.bestLen;
            localBestNumMarks = threadBest.bestNumMarks;
            for (int i = 0; i < threadBest.bestNumMarks; ++i) {
                localBestMarks[i] = threadBest.bestMarks[i];
            }
        }
    }
}

// =============================================================================
// MAIN SEARCH FUNCTION - MPI V4 (GREEDY + DYNAMIC)
// =============================================================================
void searchGolombMPI_V4(int n, int maxLen, GolombRuler& best)
{
    if (maxLen > MAX_LEN_V4) {
        maxLen = MAX_LEN_V4;
    }

    exploredCountMPI_V4.store(0, std::memory_order_relaxed);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // ==========================================================================
    // PHASE 1: GREEDY INITIALIZATION (on all ranks)
    // ==========================================================================
    int greedyMarks[MAX_MARKS_V4];
    int greedyNumMarks = 0;
    int greedyLen = greedySolve(n, maxLen, greedyMarks, greedyNumMarks);

    int globalBound = maxLen + 1;
    if (greedyNumMarks == n && greedyLen < globalBound) {
        globalBound = greedyLen;
    }

    // Synchronize greedy result across all ranks
    int minGreedyLen;
    MPI_Allreduce(&globalBound, &minGreedyLen, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    globalBound = minGreedyLen;

    if (rank == 0) {
        // printf("Greedy initial bound: %d\n", globalBound);
    }

    // ==========================================================================
    // PHASE 2: DYNAMIC WORK DISTRIBUTION
    // ==========================================================================
    int prefixDepth = computePrefixDepthMPI_V4(n, size);

    int localBestLen = globalBound;
    int localBestMarks[MAX_MARKS_V4] = {0};
    int localBestNumMarks = 0;

    // Keep greedy solution as initial best
    if (greedyNumMarks == n && greedyLen <= globalBound) {
        localBestLen = greedyLen;
        localBestNumMarks = greedyNumMarks;
        for (int i = 0; i < greedyNumMarks; ++i) {
            localBestMarks[i] = greedyMarks[i];
        }
    }

    if (size == 1) {
        // Single process: do sequential search
        std::atomic<int> atomicBound(globalBound);

        // Generate all prefixes and process them
        struct PrefixState {
            BitSet128_V4 reversed_marks;
            BitSet128_V4 used_dist;
            int marks_count;
            int ruler_length;
            int next_pos;
        };

        std::vector<PrefixState> genStack;
        PrefixState initial;
        initial.reversed_marks.reset();
        initial.reversed_marks.set(0);
        initial.used_dist.reset();
        initial.marks_count = 1;
        initial.ruler_length = 0;
        initial.next_pos = 1;
        genStack.push_back(initial);

        ThreadBestMPI_V4 threadBest{};
        threadBest.bestLen = globalBound;
        threadBest.bestNumMarks = 0;
        long long totalExplored = 0;

        alignas(64) StackFrameMPI_V4 stack[MAX_MARKS_V4];

        while (!genStack.empty()) {
            PrefixState& state = genStack.back();

            if (state.marks_count == prefixDepth + 1) {
                // Process this prefix
                int currentBound = atomicBound.load(std::memory_order_acquire);
                const int remaining = n - state.marks_count;
                const int minAdditional = (remaining * (remaining + 1)) / 2;

                if (state.ruler_length + minAdditional < currentBound) {
                    StackFrameMPI_V4& frame0 = stack[0];
                    frame0.reversed_marks = state.reversed_marks;
                    frame0.used_dist = state.used_dist;
                    frame0.marks_count = state.marks_count;
                    frame0.ruler_length = state.ruler_length;
                    frame0.next_candidate = 0;

                    long long localExplored = 0;
                    backtrackIterativeMPI_V4(threadBest, n, atomicBound, localExplored, stack);
                    totalExplored += localExplored;
                }

                genStack.pop_back();
                continue;
            }

            const int r = n - state.marks_count;
            const int max_remaining = ((r - 1) * r) / 2;
            const int max_pos = atomicBound.load(std::memory_order_relaxed) - max_remaining - 1;

            bool foundChild = false;
            for (int pos = state.next_pos; pos <= max_pos; ++pos) {
                const int offset = pos - state.ruler_length;
                BitSet128_V4 new_dist = state.reversed_marks << offset;

                if ((new_dist & state.used_dist).any()) {
                    continue;
                }

                PrefixState newState;
                newState.reversed_marks = state.reversed_marks << offset;
                newState.reversed_marks.set(0);
                newState.used_dist = state.used_dist ^ new_dist;
                newState.marks_count = state.marks_count + 1;
                newState.ruler_length = pos;
                newState.next_pos = pos + 1;

                state.next_pos = pos + 1;
                genStack.push_back(newState);
                foundChild = true;
                break;
            }

            if (!foundChild) {
                genStack.pop_back();
            }
        }

        exploredCountMPI_V4.fetch_add(totalExplored, std::memory_order_relaxed);

        if (threadBest.bestNumMarks > 0 && threadBest.bestLen < localBestLen) {
            localBestLen = threadBest.bestLen;
            localBestNumMarks = threadBest.bestNumMarks;
            for (int i = 0; i < threadBest.bestNumMarks; ++i) {
                localBestMarks[i] = threadBest.bestMarks[i];
            }
        }
    }
    else if (rank == 0) {
        // Master process: distribute work dynamically
        masterDynamicDistribution(n, prefixDepth, globalBound, size, greedyMarks, greedyNumMarks);

        // Keep greedy as local best for master
        if (greedyNumMarks == n) {
            localBestLen = greedyLen;
            localBestNumMarks = greedyNumMarks;
            for (int i = 0; i < greedyNumMarks; ++i) {
                localBestMarks[i] = greedyMarks[i];
            }
        }
    }
    else {
        // Worker process: request and process work
        workerProcessWork(n, localBestLen, localBestMarks, localBestNumMarks, globalBound);
    }

    // ==========================================================================
    // PHASE 3: FINAL GLOBAL REDUCTION
    // ==========================================================================
    MPI_Barrier(MPI_COMM_WORLD);

    int globalMinLen;
    MPI_Allreduce(&localBestLen, &globalMinLen, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    int hasWinner = (localBestLen == globalMinLen && localBestNumMarks > 0) ? rank : size;
    int globalWinner;
    MPI_Allreduce(&hasWinner, &globalWinner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    int bestLen = maxLen + 1;
    int bestMarks[MAX_MARKS_V4] = {0};
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

long long getExploredCountMPI_V4()
{
    long long localCount = exploredCountMPI_V4.load(std::memory_order_relaxed);
    long long globalCount = 0;

    MPI_Reduce(&localCount, &globalCount, 1, MPI_LONG_LONG,
               MPI_SUM, 0, MPI_COMM_WORLD);

    return globalCount;
}
