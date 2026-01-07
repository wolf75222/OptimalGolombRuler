#include "search_v4.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <bitset>
#include <vector>
#include <omp.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION 4
// =============================================================================
// Combines the best of all worlds:
// - PREFIX GENERATION for better load balancing (external code inspiration)
// - ITERATIVE backtracking with manual stack (V1/V3)
// - BITSET SHIFT for O(1) validation (V2/V3)
// - XOR for O(1) add/remove (V2/V3)
// - Cache-aligned structures (V1/V3)
//
// Key insight: Instead of parallelizing on firstMark (maxLen tasks),
// we generate all valid prefixes up to depth D, giving us thousands of
// independent tasks for better load balancing on 192+ threads.
// =============================================================================

static std::atomic<long long> exploredCountV4{0};

constexpr int MAX_DISTANCE_V4 = MAX_DIFF;  // 256
constexpr int MAX_MARKS_V4 = 24;

// =============================================================================
// WORK ITEM - A prefix to explore
// =============================================================================
struct alignas(64) WorkItemV4 {
    std::bitset<MAX_DISTANCE_V4> reversed_marks;
    std::bitset<MAX_DISTANCE_V4> used_dist;
    int marks_count;
    int ruler_length;
};

// =============================================================================
// STACK FRAME - State at each level of the search tree
// =============================================================================
struct alignas(64) StackFrameV4 {
    std::bitset<MAX_DISTANCE_V4> reversed_marks;
    std::bitset<MAX_DISTANCE_V4> used_dist;
    int marks_count;
    int ruler_length;
    int next_candidate;
};

// =============================================================================
// THREAD LOCAL BEST
// =============================================================================
struct alignas(64) ThreadBestV4 {
    int bestLen;
    int bestMarks[MAX_MARKS_V4];
    int bestNumMarks;
};

// =============================================================================
// EXTRACT MARKS FROM reversed_marks
// =============================================================================
static void extractMarksV4(const std::bitset<MAX_DISTANCE_V4>& reversed_marks,
                           int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks[ruler_length - i]) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// PREFIX GENERATION - Generate all valid prefixes up to target depth
// =============================================================================
static void generatePrefixes(
    std::bitset<MAX_DISTANCE_V4>& reversed_marks,
    std::bitset<MAX_DISTANCE_V4>& used_dist,
    int marks_count,
    int ruler_length,
    int target_depth,
    int target_marks,
    int maxLen,
    std::vector<WorkItemV4>& prefixes)
{
    // Reached target depth - save this prefix
    if (marks_count == target_depth) {
        WorkItemV4 item;
        item.reversed_marks = reversed_marks;
        item.used_dist = used_dist;
        item.marks_count = marks_count;
        item.ruler_length = ruler_length;
        prefixes.push_back(item);
        return;
    }

    // Pruning bounds
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
        std::bitset<MAX_DISTANCE_V4> new_dist = reversed_marks;
        new_dist <<= offset;

        // Check conflicts
        if ((new_dist & used_dist).any()) {
            continue;
        }

        // Valid - add mark and recurse
        std::bitset<MAX_DISTANCE_V4> old_reversed = reversed_marks;
        std::bitset<MAX_DISTANCE_V4> old_used = used_dist;

        reversed_marks <<= offset;
        reversed_marks[0] = true;
        used_dist ^= new_dist;

        generatePrefixes(reversed_marks, used_dist, marks_count + 1, pos,
                        target_depth, target_marks, maxLen, prefixes);

        // Backtrack
        reversed_marks = old_reversed;
        used_dist = old_used;
    }
}

// =============================================================================
// CORE ITERATIVE BACKTRACKING WITH BITSET SHIFT
// =============================================================================
static void backtrackIterativeV4(
    ThreadBestV4& threadBest,
    const int n,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrameV4* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameV4& frame = stack[stackTop];

        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);

        // Pruning: Golomb lower bound
        const int r = n - frame.marks_count;
        const int minAdditionalLength = (r * (r + 1)) / 2;

        if (frame.ruler_length + minAdditionalLength >= currentGlobalBest) [[unlikely]] {
            stackTop--;
            continue;
        }

        // Compute bounds for this frame
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

            // Validation via BITSET SHIFT (O(1))
            const int offset = pos - frame.ruler_length;

            std::bitset<MAX_DISTANCE_V4> new_dist = frame.reversed_marks;
            new_dist <<= offset;

            // Check ALL conflicts in ONE AND operation
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

                    // Build final state for extraction
                    std::bitset<MAX_DISTANCE_V4> final_marks = frame.reversed_marks;
                    final_marks <<= offset;
                    final_marks[0] = true;

                    extractMarksV4(final_marks, pos, threadBest.bestMarks, threadBest.bestNumMarks);

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

                StackFrameV4& newFrame = stack[stackTop + 1];

                newFrame.reversed_marks = frame.reversed_marks;
                newFrame.reversed_marks <<= offset;
                newFrame.reversed_marks[0] = true;

                newFrame.used_dist = frame.used_dist;
                newFrame.used_dist ^= new_dist;

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
static int computePrefixDepth(int n, int numThreads) {
    // Goal: Generate enough prefixes for good load balancing
    // Rule of thumb: ~10-50 tasks per thread for dynamic scheduling

    if (n <= 6) return 2;
    if (n <= 8) return 3;
    if (n <= 10) return 3;
    if (n <= 12) return 4;
    if (n <= 14) return 4;
    if (n <= 16) return 5;

    // For very large n, limit depth to avoid memory explosion
    int depth = 5;
    if (depth >= n - 2) {
        depth = n - 3;
    }
    if (depth < 2) depth = 2;

    return depth;
}

// =============================================================================
// MAIN SEARCH FUNCTION - VERSION 4
// =============================================================================
void searchGolombV4(int n, int maxLen, GolombRuler& best, int prefixDepth)
{
    exploredCountV4.store(0, std::memory_order_relaxed);

    std::atomic<int> globalBestLen(maxLen + 1);

    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS_V4] = {0};
    int finalBestNumMarks = 0;

    int numThreads = omp_get_max_threads();

    // Compute prefix depth if not specified
    if (prefixDepth <= 0) {
        prefixDepth = computePrefixDepth(n, numThreads);
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
    std::vector<WorkItemV4> prefixes;
    prefixes.reserve(100000);  // Pre-allocate for performance

    {
        // Start with mark at 0
        std::bitset<MAX_DISTANCE_V4> reversed_marks;
        std::bitset<MAX_DISTANCE_V4> used_dist;
        reversed_marks[0] = true;

        generatePrefixes(reversed_marks, used_dist, 1, 0,
                        prefixDepth, n, maxLen + 1, prefixes);
    }

    // ==========================================================================
    // PHASE 2: Parallel exploration of prefixes
    // ==========================================================================
    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        ThreadBestV4 threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        // Pre-allocated stack
        alignas(64) StackFrameV4 stack[MAX_MARKS_V4];

        const int numPrefixes = static_cast<int>(prefixes.size());

        #pragma omp for schedule(dynamic, 1)
        for (int i = 0; i < numPrefixes; ++i) {
            const WorkItemV4& prefix = prefixes[static_cast<size_t>(i)];

            // Early pruning based on global best
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
            const int remaining = n - prefix.marks_count;
            const int minAdditional = (remaining * (remaining + 1)) / 2;

            if (prefix.ruler_length + minAdditional >= currentGlobal) {
                continue;
            }

            // Setup initial stack frame from prefix
            StackFrameV4& frame0 = stack[0];
            frame0.reversed_marks = prefix.reversed_marks;
            frame0.used_dist = prefix.used_dist;
            frame0.marks_count = prefix.marks_count;
            frame0.ruler_length = prefix.ruler_length;
            frame0.next_candidate = 0;

            // Run iterative backtracking
            backtrackIterativeV4(threadBest, n, globalBestLen, threadExplored, stack);
        }

        exploredCountV4.fetch_add(threadExplored, std::memory_order_relaxed);

        // Merge results
        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_best_v4)
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

long long getExploredCountV4()
{
    return exploredCountV4.load(std::memory_order_relaxed);
}
