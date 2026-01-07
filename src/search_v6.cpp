#include "search_v6.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <omp.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION 6
// =============================================================================
// Key optimizations for AMD EPYC (Zen4):
// - Branchless 128-bit shift using conditional moves
// - No SIMD intrinsics overhead (let GCC auto-vectorize with -march=native)
// - Cache-line aligned structures (64 bytes for Zen4)
// - Prefetch hints for stack frames
// - Compiler hints (__builtin_expect, always_inline)
// =============================================================================

static std::atomic<long long> exploredCountV6{0};

constexpr int MAX_MARKS_V6 = 24;
constexpr int MAX_LEN_V6 = 127;

// =============================================================================
// BRANCHLESS 128-BIT BITSET - Optimized for AMD Zen4
// =============================================================================
// Uses 2x uint64_t with branchless operations
// GCC will auto-vectorize with -march=znver4
// =============================================================================
struct alignas(16) BitSet128B {
    uint64_t lo;  // bits 0-63
    uint64_t hi;  // bits 64-127

    // Constructors
    __attribute__((always_inline))
    BitSet128B() : lo(0), hi(0) {}

    __attribute__((always_inline))
    BitSet128B(uint64_t l, uint64_t h) : lo(l), hi(h) {}

    // Set bit at position - branchless version
    __attribute__((always_inline))
    void set(int pos) {
        // Branchless: use arithmetic to select lo or hi
        const uint64_t mask = 1ULL << (pos & 63);
        const uint64_t is_hi = static_cast<uint64_t>(pos >= 64);
        const uint64_t is_lo = 1ULL - is_hi;
        lo |= mask * is_lo;
        hi |= mask * is_hi;
    }

    // Test bit at position - branchless version
    __attribute__((always_inline))
    bool test(int pos) const {
        const int shift = pos & 63;
        const uint64_t is_hi = static_cast<uint64_t>(pos >= 64);
        // Select lo or hi based on pos
        const uint64_t val = (lo & ~(-is_hi)) | (hi & (-is_hi));
        return (val >> shift) & 1;
    }

    // Left shift - FULLY BRANCHLESS version optimized for AMD
    __attribute__((always_inline))
    BitSet128B operator<<(int n) const {
        // Handle edge cases with masks instead of branches
        const uint64_t zero_mask = static_cast<uint64_t>(n < 128) - 1ULL;  // All 1s if n >= 128
        const uint64_t identity_mask = static_cast<uint64_t>(n == 0) - 1ULL;  // All 0s if n == 0

        // For n >= 64: lo goes to hi position, lo becomes 0
        // For n < 64: standard 128-bit shift
        const int n_mod = n & 63;
        const int n_inv = 64 - n_mod;
        const uint64_t ge64 = static_cast<uint64_t>(n >= 64);

        // Compute both cases
        // Case n < 64:
        const uint64_t new_lo_lt64 = lo << n_mod;
        const uint64_t carry = (n_mod == 0) ? 0 : (lo >> n_inv);
        const uint64_t new_hi_lt64 = (hi << n_mod) | carry;

        // Case n >= 64:
        const uint64_t new_lo_ge64 = 0;
        const uint64_t new_hi_ge64 = lo << n_mod;

        // Select based on n >= 64 (branchless)
        const uint64_t new_lo_raw = (new_lo_lt64 & ~(-ge64)) | (new_lo_ge64 & (-ge64));
        const uint64_t new_hi_raw = (new_hi_lt64 & ~(-ge64)) | (new_hi_ge64 & (-ge64));

        // Apply zero mask for n >= 128
        const uint64_t new_lo = new_lo_raw & ~zero_mask;
        const uint64_t new_hi = new_hi_raw & ~zero_mask;

        // Apply identity for n == 0
        const uint64_t final_lo = (new_lo & identity_mask) | (lo & ~identity_mask);
        const uint64_t final_hi = (new_hi & identity_mask) | (hi & ~identity_mask);

        return BitSet128B(final_lo, final_hi);
    }

    // Bitwise AND
    __attribute__((always_inline))
    BitSet128B operator&(const BitSet128B& other) const {
        return BitSet128B(lo & other.lo, hi & other.hi);
    }

    // Bitwise OR
    __attribute__((always_inline))
    BitSet128B operator|(const BitSet128B& other) const {
        return BitSet128B(lo | other.lo, hi | other.hi);
    }

    // Bitwise XOR
    __attribute__((always_inline))
    BitSet128B operator^(const BitSet128B& other) const {
        return BitSet128B(lo ^ other.lo, hi ^ other.hi);
    }

    // Check if any bit is set
    __attribute__((always_inline))
    bool any() const {
        return (lo | hi) != 0;
    }

    // Reset all bits
    __attribute__((always_inline))
    void reset() {
        lo = 0;
        hi = 0;
    }
};

// =============================================================================
// WORK ITEM - A prefix to explore (cache-line aligned for Zen4)
// =============================================================================
struct alignas(64) WorkItemV6 {
    BitSet128B reversed_marks;
    BitSet128B used_dist;
    int marks_count;
    int ruler_length;
    int padding[2];  // Pad to 64 bytes
};

// =============================================================================
// STACK FRAME (cache-line aligned)
// =============================================================================
struct alignas(64) StackFrameV6 {
    BitSet128B reversed_marks;
    BitSet128B used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
    int padding[3];  // Pad to 64 bytes
};

// =============================================================================
// THREAD LOCAL BEST
// =============================================================================
struct alignas(64) ThreadBestV6 {
    int bestLen;
    int bestMarks[MAX_MARKS_V6];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS
// =============================================================================
static void extractMarksV6(const BitSet128B& reversed_marks,
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
static void generatePrefixesV6(
    BitSet128B reversed_marks,
    BitSet128B used_dist,
    int marks_count,
    int ruler_length,
    int target_depth,
    int target_marks,
    int maxLen,
    std::vector<WorkItemV6>& prefixes)
{
    if (marks_count == target_depth) {
        WorkItemV6 item;
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

        BitSet128B new_dist = reversed_marks << offset;

        if ((new_dist & used_dist).any()) {
            continue;
        }

        BitSet128B new_reversed = reversed_marks << offset;
        new_reversed.set(0);

        BitSet128B new_used = used_dist ^ new_dist;

        generatePrefixesV6(new_reversed, new_used, marks_count + 1, pos,
                          target_depth, target_marks, maxLen, prefixes);
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - BRANCHLESS OPTIMIZED
// =============================================================================
static void backtrackIterativeV6(
    ThreadBestV6& threadBest,
    const int n,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrameV6* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameV6& frame = stack[stackTop];

        // Prefetch next stack frame
        if (__builtin_expect(stackTop + 1 < MAX_MARKS_V6, 1)) {
            __builtin_prefetch(&stack[stackTop + 1], 1, 3);
        }

        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);

        // Pruning: Golomb lower bound
        const int r = n - frame.marks_count;
        const int minAdditionalLength = (r * (r + 1)) / 2;

        if (__builtin_expect(frame.ruler_length + minAdditionalLength >= currentGlobalBest, 0)) {
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
            if (__builtin_expect(pos >= newGlobalBest, 0)) {
                break;
            }

            const int offset = pos - frame.ruler_length;

            // Branchless shift
            BitSet128B new_dist = frame.reversed_marks << offset;

            // AND + test
            if (__builtin_expect((new_dist & frame.used_dist).any(), 1)) {
                continue;
            }

            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                const int solutionLen = pos;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;

                    BitSet128B final_marks = frame.reversed_marks << offset;
                    final_marks.set(0);

                    extractMarksV6(final_marks, pos, threadBest.bestMarks, threadBest.bestNumMarks);

                    int expected = globalBestLen.load(std::memory_order_relaxed);
                    while (solutionLen < expected &&
                           !globalBestLen.compare_exchange_weak(expected, solutionLen,
                               std::memory_order_release, std::memory_order_relaxed)) {
                    }
                }
            } else {
                frame.next_candidate = pos + 1;

                StackFrameV6& newFrame = stack[stackTop + 1];

                newFrame.reversed_marks = frame.reversed_marks << offset;
                newFrame.reversed_marks.set(0);

                newFrame.used_dist = frame.used_dist ^ new_dist;

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
static int computePrefixDepthV6(int n, int numThreads) {
    (void)numThreads;  // Unused for now
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
// MAIN SEARCH FUNCTION - VERSION 6
// =============================================================================
void searchGolombV6(int n, int maxLen, GolombRuler& best, int prefixDepth)
{
    if (maxLen > MAX_LEN_V6) {
        maxLen = MAX_LEN_V6;
    }

    exploredCountV6.store(0, std::memory_order_relaxed);

    std::atomic<int> globalBestLen(maxLen + 1);

    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS_V6] = {0};
    int finalBestNumMarks = 0;

    int numThreads = omp_get_max_threads();

    if (prefixDepth <= 0) {
        prefixDepth = computePrefixDepthV6(n, numThreads);
    }

    if (prefixDepth >= n) {
        prefixDepth = n - 1;
    }
    if (prefixDepth < 2) {
        prefixDepth = 2;
    }

    // PHASE 1: Generate prefixes
    std::vector<WorkItemV6> prefixes;
    prefixes.reserve(100000);

    {
        BitSet128B reversed_marks;
        BitSet128B used_dist;
        reversed_marks.set(0);

        generatePrefixesV6(reversed_marks, used_dist, 1, 0,
                          prefixDepth, n, maxLen + 1, prefixes);
    }

    // PHASE 2: Parallel exploration
    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        ThreadBestV6 threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        alignas(64) StackFrameV6 stack[MAX_MARKS_V6];

        const int numPrefixes = static_cast<int>(prefixes.size());

        #pragma omp for schedule(dynamic, 1)
        for (int i = 0; i < numPrefixes; ++i) {
            const WorkItemV6& prefix = prefixes[static_cast<size_t>(i)];

            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
            const int remaining = n - prefix.marks_count;
            const int minAdditional = (remaining * (remaining + 1)) / 2;

            if (prefix.ruler_length + minAdditional >= currentGlobal) {
                continue;
            }

            StackFrameV6& frame0 = stack[0];
            frame0.reversed_marks = prefix.reversed_marks;
            frame0.used_dist = prefix.used_dist;
            frame0.marks_count = prefix.marks_count;
            frame0.ruler_length = prefix.ruler_length;
            frame0.next_candidate = 0;

            backtrackIterativeV6(threadBest, n, globalBestLen, threadExplored, stack);
        }

        exploredCountV6.fetch_add(threadExplored, std::memory_order_relaxed);

        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_best_v6)
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

    if (finalBestNumMarks > 0) {
        best.marks.assign(finalBestMarks, finalBestMarks + finalBestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountV6()
{
    return exploredCountV6.load(std::memory_order_relaxed);
}
