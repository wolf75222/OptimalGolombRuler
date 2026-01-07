#include "search_sequential_v2.hpp"
#include <cstdint>
#include <cstring>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - SEQUENTIAL VERSION 2
// =============================================================================
// Key insight from V5 profiling:
//   - V1 sequential spends O(k) per candidate checking differences
//   - V5 uses BitSet128 shift trick: O(1) collision detection
//
// This version applies the V5 optimization to sequential code:
//   - BitSet128 (2x uint64_t) for reversed_marks and used_dist
//   - reversed_marks << offset computes ALL new differences in one op
//   - Single (new_dist & used_dist).any() for collision test
//   - No explicit marks array - marks encoded in reversed_marks bits
// =============================================================================

static long long g_exploredCountV2 = 0;

constexpr int MAX_MARKS_V2 = 24;
constexpr int MAX_LEN_V2 = 127;

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

    // Left shift by n positions
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

    // Bitwise XOR
    inline BitSet128 operator^(const BitSet128& other) const {
        return BitSet128(lo ^ other.lo, hi ^ other.hi);
    }

    // Check if any bit is set
    inline bool any() const {
        return (lo | hi) != 0;
    }
};

// =============================================================================
// STACK FRAME - Minimal state for iterative backtracking
// =============================================================================
struct alignas(64) StackFrameV2 {
    BitSet128 reversed_marks;  // Marks encoded as bits (position = ruler_length - mark)
    BitSet128 used_dist;       // Differences used so far
    int marks_count;           // Number of marks placed
    int ruler_length;          // Current ruler length (= last mark position)
    int next_candidate;        // Next position to try (for resuming iteration)
};

// =============================================================================
// SEARCH STATE
// =============================================================================
struct alignas(64) SearchStateV2 {
    int bestLen;
    int bestMarks[MAX_MARKS_V2];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarks(const BitSet128& reversed_marks,
                         int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks.test(ruler_length - i)) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING - BitSet128 shift-based
// =============================================================================
// Key optimization: O(1) collision detection instead of O(k)
//
// reversed_marks encoding:
//   - Bit i is set if there's a mark at position (ruler_length - i)
//   - Example: marks {0,1,4,9} with ruler_length=9
//     -> bits at positions {9,8,5,0}
//
// When adding mark at position pos:
//   - offset = pos - ruler_length
//   - new_dist = reversed_marks << offset
//   - This computes ALL new differences automatically!
//   - If old mark was at (ruler_length - i), new diff is:
//     pos - (ruler_length - i) = offset + i
//   - Shift puts bit i at position (offset + i) = the new difference
// =============================================================================
static void backtrackIterativeV2(
    SearchStateV2& state,
    const int n,
    StackFrameV2* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        g_exploredCountV2++;

        StackFrameV2& frame = stack[stackTop];

        // Pruning: Golomb lower bound
        const int r = n - frame.marks_count;
        const int minAdditionalLength = (r * (r + 1)) / 2;

        if (frame.ruler_length + minAdditionalLength >= state.bestLen) [[unlikely]] {
            stackTop--;
            continue;
        }

        // Compute bounds
        const int min_pos = frame.ruler_length + 1;
        const int max_remaining = ((r - 1) * r) / 2;
        const int max_pos = state.bestLen - max_remaining - 1;

        // Start where we left off
        int startNext = frame.next_candidate;
        if (startNext == 0) {
            startNext = min_pos;
        }

        bool pushedChild = false;

        for (int pos = startNext; pos <= max_pos; ++pos) {
            // Re-check bound (bestLen may have changed)
            if (pos >= state.bestLen) [[unlikely]] {
                break;
            }

            const int offset = pos - frame.ruler_length;

            // O(1) COLLISION DETECTION: shift computes all new differences
            BitSet128 new_dist = frame.reversed_marks << offset;

            // Single AND + any() check
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;
            }

            // Valid candidate
            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                // Solution found!
                const int solutionLen = pos;
                if (solutionLen < state.bestLen) {
                    state.bestLen = solutionLen;

                    BitSet128 final_marks = frame.reversed_marks << offset;
                    final_marks.set(0);

                    extractMarks(final_marks, pos, state.bestMarks, state.bestNumMarks);
                }
            } else {
                // Push new frame
                frame.next_candidate = pos + 1;

                StackFrameV2& newFrame = stack[stackTop + 1];

                // Update reversed_marks: shift + set bit 0
                newFrame.reversed_marks = frame.reversed_marks << offset;
                newFrame.reversed_marks.set(0);

                // Update used_dist: XOR in new differences
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
// MAIN SEARCH FUNCTION - SEQUENTIAL V2
// =============================================================================
void searchGolombSequentialV2(int n, int maxLen, GolombRuler& best)
{
    g_exploredCountV2 = 0;

    // Clamp max length
    if (maxLen > MAX_LEN_V2) {
        maxLen = MAX_LEN_V2;
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
    SearchStateV2 state{};
    state.bestLen = maxLen + 1;
    state.bestNumMarks = 0;

    // Pre-allocate stack
    alignas(64) StackFrameV2 stack[MAX_MARKS_V2];

    // SYMMETRY BREAKING: a_1 <= bestLen/2
    // For every ruler and its mirror, at least one has a_1 in the left half
    for (int firstMark = 1; firstMark <= state.bestLen / 2 && firstMark < state.bestLen; ++firstMark) {

        // Setup initial frame with marks {0, firstMark}
        StackFrameV2& frame0 = stack[0];

        // reversed_marks for {0, firstMark} with ruler_length = firstMark:
        // - mark 0 -> bit at (firstMark - 0) = firstMark
        // - mark firstMark -> bit at (firstMark - firstMark) = 0
        frame0.reversed_marks = BitSet128();
        frame0.reversed_marks.set(0);         // mark at ruler_length
        frame0.reversed_marks.set(firstMark); // mark at 0

        // used_dist: only difference is firstMark
        frame0.used_dist = BitSet128();
        frame0.used_dist.set(firstMark);

        frame0.marks_count = 2;
        frame0.ruler_length = firstMark;
        frame0.next_candidate = 0;

        // Explore this branch
        backtrackIterativeV2(state, n, stack);
    }

    // Copy result
    if (state.bestNumMarks > 0) {
        best.marks.assign(state.bestMarks, state.bestMarks + state.bestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountSequentialV2()
{
    return g_exploredCountV2;
}
