#include "search_v5.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <omp.h>

#ifdef _MSC_VER
#include <intrin.h>
#define POPCOUNT64(x) __popcnt64(x)
#else
#define POPCOUNT64(x) __builtin_popcountll(x)
#endif

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION 5
// =============================================================================
// Key insight from V4 profiling:
//   - 43% of time spent in (new_dist & used_dist).any()
//   - 33% of time spent in bitset<256>::operator<<=
//
// Solution: Use 2x uint64_t instead of bitset<256>
//   - Direct bit ops without abstraction overhead
//   - Fits in 2 registers (128 bits total)
//   - Sufficient for rulers up to length 127
// =============================================================================

static std::atomic<long long> exploredCountV5{0};

constexpr int MAX_MARKS_V5 = 24;
constexpr int MAX_LEN_V5 = 127;  // Max supported length with 2x uint64_t

// =============================================================================
// FAST 128-BIT BITSET using 2x uint64_t
// =============================================================================
struct alignas(16) BitSet128 {
    uint64_t lo;  // bits 0-63
    uint64_t hi;  // bits 64-127

    BitSet128() : lo(0), hi(0) {}
    BitSet128(uint64_t l, uint64_t h) : lo(l), hi(h) {}

    // Set bit at position
    inline void set(int pos) {
        if (pos < 64) {
            lo |= (1ULL << pos);
        } else {
            hi |= (1ULL << (pos - 64));
        }
    }

    // Test bit at position
    inline bool test(int pos) const {
        if (pos < 64) {
            return (lo >> pos) & 1;
        } else {
            return (hi >> (pos - 64)) & 1;
        }
    }

    // Left shift by n positions (n < 64 for simplicity in hot path)
    inline BitSet128 operator<<(int n) const {
        if (n == 0) return *this;
        if (n >= 128) return BitSet128(0, 0);
        if (n >= 64) {
            return BitSet128(0, lo << (n - 64));
        }
        // n < 64
        uint64_t new_hi = (hi << n) | (lo >> (64 - n));
        uint64_t new_lo = lo << n;
        return BitSet128(new_lo, new_hi);
    }

    // Bitwise AND
    inline BitSet128 operator&(const BitSet128& other) const {
        return BitSet128(lo & other.lo, hi & other.hi);
    }

    // Bitwise OR
    inline BitSet128 operator|(const BitSet128& other) const {
        return BitSet128(lo | other.lo, hi | other.hi);
    }

    // Bitwise XOR
    inline BitSet128 operator^(const BitSet128& other) const {
        return BitSet128(lo ^ other.lo, hi ^ other.hi);
    }

    // Check if any bit is set
    inline bool any() const {
        return (lo | hi) != 0;
    }

    // Reset all bits
    inline void reset() {
        lo = hi = 0;
    }
};

// =============================================================================
// WORK ITEM - A prefix to explore
// =============================================================================
struct alignas(32) WorkItemV5 {
    BitSet128 reversed_marks;
    BitSet128 used_dist;
    int marks_count;
    int ruler_length;
};

// =============================================================================
// STACK FRAME - State at each level of the search tree
// =============================================================================
struct alignas(32) StackFrameV5 {
    BitSet128 reversed_marks;
    BitSet128 used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
};

// =============================================================================
// THREAD LOCAL BEST
// =============================================================================
struct alignas(64) ThreadBestV5 {
    int bestLen;
    int bestMarks[MAX_MARKS_V5];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarksV5(const BitSet128& reversed_marks,
                           int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks.test(ruler_length - i)) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// PREFIX GENERATION
// =============================================================================
static void generatePrefixesV5(
    BitSet128 reversed_marks,
    BitSet128 used_dist,
    int marks_count,
    int ruler_length,
    int target_depth,
    int target_marks,
    int maxLen,
    std::vector<WorkItemV5>& prefixes)
{
    if (marks_count == target_depth) {
        WorkItemV5 item;
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

        // Compute new differences via shift
        BitSet128 new_dist = reversed_marks << offset;

        // Check conflicts
        if ((new_dist & used_dist).any()) {
            continue;
        }

        // Valid - add mark and recurse
        BitSet128 new_reversed = reversed_marks << offset;
        new_reversed.set(0);

        BitSet128 new_used = used_dist ^ new_dist;

        generatePrefixesV5(new_reversed, new_used, marks_count + 1, pos,
                          target_depth, target_marks, maxLen, prefixes);
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - OPTIMIZED
// =============================================================================
static void backtrackIterativeV5(
    ThreadBestV5& threadBest,
    const int n,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrameV5* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameV5& frame = stack[stackTop];

        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);

        // Pruning: Golomb lower bound
        const int r = n - frame.marks_count;
        const int minAdditionalLength = (r * (r + 1)) / 2;

        if (frame.ruler_length + minAdditionalLength >= currentGlobalBest) [[unlikely]] {
            stackTop--;
            continue;
        }

        // Compute bounds
        const int min_pos = frame.ruler_length + 1;
        const int max_remaining = ((r - 1) * r) / 2;
        const int max_pos = currentGlobalBest - max_remaining - 1;

        // Start where we left off
        int startNext = frame.next_candidate;
        if (startNext == 0) {
            startNext = min_pos;
        }

        bool pushedChild = false;

        for (int pos = startNext; pos <= max_pos; ++pos) {
            // Re-check global best
            const int newGlobalBest = globalBestLen.load(std::memory_order_relaxed);
            if (pos >= newGlobalBest) [[unlikely]] {
                break;
            }

            const int offset = pos - frame.ruler_length;

            // OPTIMIZED: Direct uint64_t shift instead of bitset
            BitSet128 new_dist = frame.reversed_marks << offset;

            // OPTIMIZED: Direct AND + any() check
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;
            }

            // Valid candidate
            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                // Solution found!
                const int solutionLen = pos;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;

                    BitSet128 final_marks = frame.reversed_marks << offset;
                    final_marks.set(0);

                    extractMarksV5(final_marks, pos, threadBest.bestMarks, threadBest.bestNumMarks);

                    // Update global best atomically
                    int expected = globalBestLen.load(std::memory_order_relaxed);
                    while (solutionLen < expected &&
                           !globalBestLen.compare_exchange_weak(expected, solutionLen,
                               std::memory_order_release, std::memory_order_relaxed)) {
                    }
                }
            } else {
                // Push new frame
                frame.next_candidate = pos + 1;

                StackFrameV5& newFrame = stack[stackTop + 1];

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
static int computePrefixDepthV5(int n, int numThreads) {
    if (n <= 6) return 2;
    if (n <= 8) return 3;
    if (n <= 10) return 3;
    if (n <= 12) return 4;
    if (n <= 14) return 4;
    if (n <= 16) return 5;

    int depth = 5;
    if (depth >= n - 2) {
        depth = n - 3;
    }
    if (depth < 2) depth = 2;

    return depth;
}

// =============================================================================
// MAIN SEARCH FUNCTION - VERSION 5
// =============================================================================
void searchGolombV5(int n, int maxLen, GolombRuler& best, int prefixDepth)
{
    // Check max length constraint
    if (maxLen > MAX_LEN_V5) {
        maxLen = MAX_LEN_V5;
    }

    exploredCountV5.store(0, std::memory_order_relaxed);

    std::atomic<int> globalBestLen(maxLen + 1);

    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS_V5] = {0};
    int finalBestNumMarks = 0;

    int numThreads = omp_get_max_threads();

    // Compute prefix depth if not specified
    if (prefixDepth <= 0) {
        prefixDepth = computePrefixDepthV5(n, numThreads);
    }

    // Ensure prefix depth is valid
    if (prefixDepth >= n) {
        prefixDepth = n - 1;
    }
    if (prefixDepth < 2) {
        prefixDepth = 2;
    }

    // ==========================================================================
    // PHASE 1: Generate all valid prefixes (sequential)
    // ==========================================================================
    std::vector<WorkItemV5> prefixes;
    prefixes.reserve(100000);

    {
        BitSet128 reversed_marks;
        BitSet128 used_dist;
        reversed_marks.set(0);

        generatePrefixesV5(reversed_marks, used_dist, 1, 0,
                          prefixDepth, n, maxLen + 1, prefixes);
    }

    // ==========================================================================
    // PHASE 2: Parallel exploration of prefixes
    // ==========================================================================
    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        ThreadBestV5 threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        // Pre-allocated stack
        alignas(64) StackFrameV5 stack[MAX_MARKS_V5];

        const int numPrefixes = static_cast<int>(prefixes.size());

        #pragma omp for schedule(dynamic, 1)
        for (int i = 0; i < numPrefixes; ++i) {
            const WorkItemV5& prefix = prefixes[static_cast<size_t>(i)];

            // Early pruning
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
            const int remaining = n - prefix.marks_count;
            const int minAdditional = (remaining * (remaining + 1)) / 2;

            if (prefix.ruler_length + minAdditional >= currentGlobal) {
                continue;
            }

            // Setup initial stack frame
            StackFrameV5& frame0 = stack[0];
            frame0.reversed_marks = prefix.reversed_marks;
            frame0.used_dist = prefix.used_dist;
            frame0.marks_count = prefix.marks_count;
            frame0.ruler_length = prefix.ruler_length;
            frame0.next_candidate = 0;

            // Run iterative backtracking
            backtrackIterativeV5(threadBest, n, globalBestLen, threadExplored, stack);
        }

        exploredCountV5.fetch_add(threadExplored, std::memory_order_relaxed);

        // Merge results
        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_best_v5)
            {
                if (threadBest.bestLen < finalBestLen) {
                    finalBestLen = threadBest.bestLen;
                    finalBestNumMarks = threadBest.bestNumMarks;
                    for (int j = 0; j < threadBest.bestNumMarks; ++j) {
                        finalBestMarks[j] = threadBest.bestMarks[j];
                    }
                }
            }
        }
    }

    // Copy final result
    if (finalBestNumMarks > 0) {
        best.marks.assign(finalBestMarks, finalBestMarks + finalBestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountV5()
{
    return exploredCountV5.load(std::memory_order_relaxed);
}
