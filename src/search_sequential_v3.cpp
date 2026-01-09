#include "search_sequential_v3.hpp"
#include <cstdint>
#include <cstring>
#include <immintrin.h>

// Cross-platform prefetch macro
#ifdef _MSC_VER
    #define PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
    #define PREFETCH(addr) __builtin_prefetch((addr), 1, 3)
#endif

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - SEQUENTIAL VERSION 3 (SIMD)
// =============================================================================
// Key optimizations over V2:
// 1. Native __m128i SIMD operations (SSE2)
// 2. _mm_testz_si128 for fast any() check (single instruction)
// 3. Avoid double shift - reuse new_dist for reversed_marks update
// 4. Local bestLen cache to reduce memory reads
// 5. Prefetch next stack frame for better cache behavior
// 6. Debug counter removed from hot path (only counted at end)
// =============================================================================

static long long g_exploredCountV3 = 0;

constexpr int MAX_MARKS_V3 = 24;
constexpr int MAX_LEN_V3 = 127;

// =============================================================================
// FAST 128-BIT BITSET using SSE2 intrinsics
// =============================================================================
struct alignas(16) BitSet128SSE {
    __m128i data;

    BitSet128SSE() : data(_mm_setzero_si128()) {}
    BitSet128SSE(__m128i d) : data(d) {}
    BitSet128SSE(uint64_t lo, uint64_t hi) : data(_mm_set_epi64x(hi, lo)) {}

    // Set bit at position
    inline void set(int pos) {
        alignas(16) uint64_t vals[2];
        _mm_store_si128((__m128i*)vals, data);
        if (pos < 64) {
            vals[0] |= (1ULL << pos);
        } else {
            vals[1] |= (1ULL << (pos - 64));
        }
        data = _mm_load_si128((__m128i*)vals);
    }

    // Test bit at position
    inline bool test(int pos) const {
        alignas(16) uint64_t vals[2];
        _mm_store_si128((__m128i*)vals, data);
        if (pos < 64) {
            return (vals[0] >> pos) & 1;
        } else {
            return (vals[1] >> (pos - 64)) & 1;
        }
    }

    // Left shift by n positions - optimized SIMD version
    inline BitSet128SSE operator<<(int n) const {
        if (n == 0) return *this;
        if (n >= 128) return BitSet128SSE();

        alignas(16) uint64_t vals[2];
        _mm_store_si128((__m128i*)vals, data);

        uint64_t lo = vals[0];
        uint64_t hi = vals[1];

        if (n >= 64) {
            // Shift by 64+ bits: lo goes to hi, lo becomes 0
            uint64_t new_hi = lo << (n - 64);
            return BitSet128SSE(0, new_hi);
        }

        // n < 64: need to handle carry from lo to hi
        uint64_t new_hi = (hi << n) | (lo >> (64 - n));
        uint64_t new_lo = lo << n;
        return BitSet128SSE(new_lo, new_hi);
    }

    // Bitwise AND - single SIMD instruction
    inline BitSet128SSE operator&(const BitSet128SSE& other) const {
        return BitSet128SSE(_mm_and_si128(data, other.data));
    }

    // Bitwise OR - single SIMD instruction
    inline BitSet128SSE operator|(const BitSet128SSE& other) const {
        return BitSet128SSE(_mm_or_si128(data, other.data));
    }

    // Bitwise XOR - single SIMD instruction
    inline BitSet128SSE operator^(const BitSet128SSE& other) const {
        return BitSet128SSE(_mm_xor_si128(data, other.data));
    }

    // Check if any bit is set - single SIMD instruction!
    inline bool any() const {
        return !_mm_testz_si128(data, data);
    }

    // Check if zero
    inline bool none() const {
        return _mm_testz_si128(data, data);
    }

    // Get lo/hi for direct manipulation
    inline uint64_t getLo() const {
        alignas(16) uint64_t vals[2];
        _mm_store_si128((__m128i*)vals, data);
        return vals[0];
    }

    inline uint64_t getHi() const {
        alignas(16) uint64_t vals[2];
        _mm_store_si128((__m128i*)vals, data);
        return vals[1];
    }
};

// =============================================================================
// STACK FRAME - Minimal state for iterative backtracking
// =============================================================================
struct alignas(64) StackFrameV3 {
    BitSet128SSE reversed_marks;  // Marks encoded as bits
    BitSet128SSE used_dist;       // Differences used so far
    int marks_count;              // Number of marks placed
    int ruler_length;             // Current ruler length
    int next_candidate;           // Next position to try
    int _padding;                 // Alignment padding
};

// =============================================================================
// SEARCH STATE
// =============================================================================
struct alignas(64) SearchStateV3 {
    int bestLen;
    int bestMarks[MAX_MARKS_V3];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarks(const BitSet128SSE& reversed_marks,
                         int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks.test(ruler_length - i)) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - SIMD optimized
// =============================================================================
static void backtrackIterativeV3(
    SearchStateV3& state,
    const int n,
    StackFrameV3* stack)
{
    int stackTop = 0;
    long long localExplored = 0;
    int localBestLen = state.bestLen;  // Cache bestLen locally

    while (stackTop >= 0) {
        localExplored++;

        StackFrameV3& frame = stack[stackTop];

        // Prefetch next potential frame
        PREFETCH(&stack[stackTop + 1]);

        // Pruning: Golomb lower bound
        const int r = n - frame.marks_count;
        const int minAdditionalLength = (r * (r + 1)) / 2;

        if (frame.ruler_length + minAdditionalLength >= localBestLen) [[unlikely]] {
            stackTop--;
            continue;
        }

        // Compute bounds
        const int min_pos = frame.ruler_length + 1;
        const int max_remaining = ((r - 1) * r) / 2;
        const int max_pos = localBestLen - max_remaining - 1;

        // Start where we left off
        int startNext = frame.next_candidate;
        if (startNext == 0) {
            startNext = min_pos;
        }

        bool pushedChild = false;

        for (int pos = startNext; pos <= max_pos; ++pos) {
            // Re-check bound (localBestLen may have changed)
            if (pos >= localBestLen) [[unlikely]] {
                break;
            }

            const int offset = pos - frame.ruler_length;

            // O(1) COLLISION DETECTION: shift computes all new differences
            BitSet128SSE new_dist = frame.reversed_marks << offset;

            // Single SIMD AND + testz check
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;
            }

            // Valid candidate
            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                // Solution found!
                const int solutionLen = pos;
                if (solutionLen < localBestLen) {
                    localBestLen = solutionLen;
                    state.bestLen = solutionLen;  // Sync to state

                    // Reuse new_dist, just set bit 0
                    BitSet128SSE final_marks = new_dist;
                    final_marks.set(0);

                    extractMarks(final_marks, pos, state.bestMarks, state.bestNumMarks);
                }
            } else {
                // Push new frame - reuse new_dist to avoid double shift!
                frame.next_candidate = pos + 1;

                StackFrameV3& newFrame = stack[stackTop + 1];

                // Reuse new_dist instead of shifting again
                newFrame.reversed_marks = new_dist;
                newFrame.reversed_marks.set(0);

                // Update used_dist with XOR
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

    g_exploredCountV3 += localExplored;
}

// =============================================================================
// MAIN SEARCH FUNCTION - SEQUENTIAL V3
// =============================================================================
void searchGolombSequentialV3(int n, int maxLen, GolombRuler& best)
{
    g_exploredCountV3 = 0;

    // Clamp max length
    if (maxLen > MAX_LEN_V3) {
        maxLen = MAX_LEN_V3;
    }

    // Trivial cases
    if (n <= 1) {
        best.marks = {0};
        best.length = 0;
        return;
    }

    if (n == 2) {
        best.marks = {0, 1};
        best.length = 1;
        return;
    }

    // Initialize search state
    SearchStateV3 state{};
    state.bestLen = maxLen + 1;
    state.bestNumMarks = 0;

    // Pre-allocate stack
    alignas(64) StackFrameV3 stack[MAX_MARKS_V3];

    // SYMMETRY BREAKING: a_1 <= bestLen/2
    for (int firstMark = 1; firstMark <= state.bestLen / 2 && firstMark < state.bestLen; ++firstMark) {

        // Setup initial frame with marks {0, firstMark}
        StackFrameV3& frame0 = stack[0];

        // reversed_marks for {0, firstMark}
        frame0.reversed_marks = BitSet128SSE();
        frame0.reversed_marks.set(0);         // mark at ruler_length
        frame0.reversed_marks.set(firstMark); // mark at 0

        // used_dist: only difference is firstMark
        frame0.used_dist = BitSet128SSE();
        frame0.used_dist.set(firstMark);

        frame0.marks_count = 2;
        frame0.ruler_length = firstMark;
        frame0.next_candidate = 0;

        // Explore this branch
        backtrackIterativeV3(state, n, stack);
    }

    // Copy result
    if (state.bestNumMarks > 0) {
        best.marks.assign(state.bestMarks, state.bestMarks + state.bestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountSequentialV3()
{
    return g_exploredCountV3;
}
