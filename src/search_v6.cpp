#include "search_v6.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <omp.h>
#include <immintrin.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION 6
// =============================================================================
// Key optimizations over V5:
// - __m128i SIMD operations for 128-bit bitset
// - Branchless shift using conditional moves
// - Single-instruction AND/OR/XOR/TEST operations
// =============================================================================

static std::atomic<long long> exploredCountV6{0};

constexpr int MAX_MARKS_V6 = 24;
constexpr int MAX_LEN_V6 = 127;

// =============================================================================
// SIMD 128-BIT BITSET using __m128i
// =============================================================================
struct alignas(16) BitSet128S {
    __m128i data;

    BitSet128S() : data(_mm_setzero_si128()) {}
    BitSet128S(__m128i d) : data(d) {}
    BitSet128S(uint64_t lo, uint64_t hi) : data(_mm_set_epi64x(static_cast<long long>(hi), static_cast<long long>(lo))) {}

    // Get lo/hi parts
    inline uint64_t lo() const { return static_cast<uint64_t>(_mm_cvtsi128_si64(data)); }
    inline uint64_t hi() const { return static_cast<uint64_t>(_mm_extract_epi64(data, 1)); }

    // Set bit at position
    inline void set(int pos) {
        if (pos < 64) {
            uint64_t l = lo() | (1ULL << pos);
            data = _mm_set_epi64x(static_cast<long long>(hi()), static_cast<long long>(l));
        } else {
            uint64_t h = hi() | (1ULL << (pos - 64));
            data = _mm_set_epi64x(static_cast<long long>(h), static_cast<long long>(lo()));
        }
    }

    // Test bit at position
    inline bool test(int pos) const {
        if (pos < 64) {
            return (lo() >> pos) & 1;
        } else {
            return (hi() >> (pos - 64)) & 1;
        }
    }

    // Left shift - optimized branchless version
    inline BitSet128S operator<<(int n) const {
        if (n == 0) return *this;
        if (n >= 128) return BitSet128S();

        uint64_t lo_val = lo();
        uint64_t hi_val = hi();

        if (n >= 64) {
            // Shift >= 64: lo goes to hi, lo becomes 0
            return BitSet128S(0, lo_val << (n - 64));
        }

        // Shift < 64: standard 128-bit shift
        uint64_t new_hi = (hi_val << n) | (lo_val >> (64 - n));
        uint64_t new_lo = lo_val << n;
        return BitSet128S(new_lo, new_hi);
    }

    // Bitwise AND - SIMD
    inline BitSet128S operator&(const BitSet128S& other) const {
        return BitSet128S(_mm_and_si128(data, other.data));
    }

    // Bitwise OR - SIMD
    inline BitSet128S operator|(const BitSet128S& other) const {
        return BitSet128S(_mm_or_si128(data, other.data));
    }

    // Bitwise XOR - SIMD
    inline BitSet128S operator^(const BitSet128S& other) const {
        return BitSet128S(_mm_xor_si128(data, other.data));
    }

    // Check if any bit is set - SIMD optimized
    inline bool any() const {
        return !_mm_testz_si128(data, data);
    }

    // Reset all bits
    inline void reset() {
        data = _mm_setzero_si128();
    }
};

// =============================================================================
// WORK ITEM - A prefix to explore
// =============================================================================
struct alignas(32) WorkItemV6 {
    BitSet128S reversed_marks;
    BitSet128S used_dist;
    int marks_count;
    int ruler_length;
};

// =============================================================================
// STACK FRAME
// =============================================================================
struct alignas(32) StackFrameV6 {
    BitSet128S reversed_marks;
    BitSet128S used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
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
static void extractMarksV6(const BitSet128S& reversed_marks,
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
    BitSet128S reversed_marks,
    BitSet128S used_dist,
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

        BitSet128S new_dist = reversed_marks << offset;

        if ((new_dist & used_dist).any()) {
            continue;
        }

        BitSet128S new_reversed = reversed_marks << offset;
        new_reversed.set(0);

        BitSet128S new_used = used_dist ^ new_dist;

        generatePrefixesV6(new_reversed, new_used, marks_count + 1, pos,
                          target_depth, target_marks, maxLen, prefixes);
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - SIMD OPTIMIZED
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

            // SIMD shift
            BitSet128S new_dist = frame.reversed_marks << offset;

            // SIMD AND + test
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;
            }

            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                const int solutionLen = pos;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;

                    BitSet128S final_marks = frame.reversed_marks << offset;
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

                // SIMD XOR
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
        BitSet128S reversed_marks;
        BitSet128S used_dist;
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
