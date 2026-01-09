#include "search_sequential_v4.hpp"
#include <cstdint>
#include <cstring>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - SEQUENTIAL VERSION 4
// =============================================================================
// Combines ALL optimizations:
// 1. BitSet128 (2x uint64_t) for O(1) collision detection via shift
// 2. Mirror symmetry breaking: skip solutions where a_1 >= a_{n-1} - a_{n-2}
// 3. Configurable initial bound for aggressive early pruning
// 4. Track firstMark separately for symmetry check at solution time
// 5. Reuse new_dist to avoid computing shift twice
// 6. Local bestLen cache to minimize memory access
// 7. Tight bounds: a_1 <= bestLen/2 (prefix symmetry)
// =============================================================================

static long long g_exploredCountV4 = 0;

constexpr int MAX_MARKS_V4 = 24;
constexpr int MAX_LEN_V4 = 127;

// =============================================================================
// FAST 128-BIT BITSET using 2x uint64_t (proven fastest for this use case)
// =============================================================================
struct alignas(16) BitSet128V4 {
    uint64_t lo;  // bits 0-63
    uint64_t hi;  // bits 64-127

    BitSet128V4() : lo(0), hi(0) {}
    BitSet128V4(uint64_t l, uint64_t h) : lo(l), hi(h) {}

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

    inline BitSet128V4 shiftLeft(int n) const {
        if (n == 0) return *this;
        if (n >= 128) return BitSet128V4(0, 0);
        if (n >= 64) {
            return BitSet128V4(0, lo << (n - 64));
        }
        uint64_t new_hi = (hi << n) | (lo >> (64 - n));
        uint64_t new_lo = lo << n;
        return BitSet128V4(new_lo, new_hi);
    }

    inline bool hasOverlap(const BitSet128V4& other) const {
        return ((lo & other.lo) | (hi & other.hi)) != 0;
    }

    inline BitSet128V4 xorWith(const BitSet128V4& other) const {
        return BitSet128V4(lo ^ other.lo, hi ^ other.hi);
    }
};

// =============================================================================
// STACK FRAME - State at each level
// =============================================================================
struct alignas(64) StackFrameV4 {
    BitSet128V4 reversed_marks;
    BitSet128V4 used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
    int first_mark;  // Track a_1 for symmetry breaking
};

// =============================================================================
// SEARCH STATE
// =============================================================================
struct alignas(64) SearchStateV4 {
    int bestLen;
    int bestMarks[MAX_MARKS_V4];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarksV4(const BitSet128V4& reversed_marks,
                           int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks.test(ruler_length - i)) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// CORE BACKTRACKING - All optimizations combined
// =============================================================================
static void backtrackIterativeV4(
    SearchStateV4& state,
    const int n,
    StackFrameV4* stack)
{
    int stackTop = 0;
    long long localExplored = 0;
    int localBestLen = state.bestLen;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameV4& frame = stack[stackTop];

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

        int startNext = frame.next_candidate;
        if (startNext == 0) {
            startNext = min_pos;
        }

        bool pushedChild = false;

        for (int pos = startNext; pos <= max_pos; ++pos) {
            if (pos >= localBestLen) [[unlikely]] {
                break;
            }

            const int offset = pos - frame.ruler_length;

            // O(1) collision detection via shift
            BitSet128V4 new_dist = frame.reversed_marks.shiftLeft(offset);

            if (new_dist.hasOverlap(frame.used_dist)) [[likely]] {
                continue;
            }

            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                // Solution found - apply mirror symmetry breaking
                // Skip if a_1 >= a_{n-1} - a_{n-2}
                // a_{n-1} = pos (solution length)
                // a_{n-2} = frame.ruler_length (last mark before this)
                // Condition to KEEP: first_mark < pos - frame.ruler_length
                const int lastGap = pos - frame.ruler_length;

                if (frame.first_mark >= lastGap) {
                    // This is a mirror solution - skip it
                    continue;
                }

                if (pos < localBestLen) {
                    localBestLen = pos;
                    state.bestLen = pos;

                    BitSet128V4 final_marks = new_dist;
                    final_marks.set(0);

                    extractMarksV4(final_marks, pos, state.bestMarks, state.bestNumMarks);
                }
            } else {
                frame.next_candidate = pos + 1;

                StackFrameV4& newFrame = stack[stackTop + 1];

                // Reuse new_dist instead of shifting again
                newFrame.reversed_marks = new_dist;
                newFrame.reversed_marks.set(0);

                newFrame.used_dist = frame.used_dist.xorWith(new_dist);
                newFrame.marks_count = newMarksCount;
                newFrame.ruler_length = pos;
                newFrame.next_candidate = 0;
                newFrame.first_mark = frame.first_mark;  // Propagate firstMark

                stackTop++;
                pushedChild = true;
                break;
            }
        }

        if (!pushedChild) {
            stackTop--;
        }
    }

    g_exploredCountV4 += localExplored;
}

// =============================================================================
// MAIN SEARCH FUNCTION - V4 with configurable bound
// =============================================================================
void searchGolombSequentialV4WithBound(int n, int initialBound, GolombRuler& best)
{
    g_exploredCountV4 = 0;

    if (initialBound > MAX_LEN_V4) {
        initialBound = MAX_LEN_V4;
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

    SearchStateV4 state{};
    state.bestLen = initialBound + 1;
    state.bestNumMarks = 0;

    alignas(64) StackFrameV4 stack[MAX_MARKS_V4];

    // SYMMETRY BREAKING: a_1 <= bestLen/2
    for (int firstMark = 1; firstMark <= state.bestLen / 2 && firstMark < state.bestLen; ++firstMark) {

        StackFrameV4& frame0 = stack[0];

        frame0.reversed_marks = BitSet128V4();
        frame0.reversed_marks.set(0);
        frame0.reversed_marks.set(firstMark);

        frame0.used_dist = BitSet128V4();
        frame0.used_dist.set(firstMark);

        frame0.marks_count = 2;
        frame0.ruler_length = firstMark;
        frame0.next_candidate = 0;
        frame0.first_mark = firstMark;  // Track for symmetry breaking

        backtrackIterativeV4(state, n, stack);
    }

    if (state.bestNumMarks > 0) {
        best.marks.assign(state.bestMarks, state.bestMarks + state.bestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

// Standard version with default bound
void searchGolombSequentialV4(int n, int maxLen, GolombRuler& best)
{
    searchGolombSequentialV4WithBound(n, maxLen, best);
}

long long getExploredCountSequentialV4()
{
    return g_exploredCountV4;
}
