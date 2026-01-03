#include "search.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <omp.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION
// =============================================================================
// CSAPP-inspired optimizations:
// - Stack-allocated arrays (no heap allocations in hot path)
// - Cache-friendly data layout (all state fits in L1/L2)
// - Direct bit manipulation instead of std::bitset (reduced overhead)
// - Minimal atomic operations
// - Loop invariants hoisted out of inner loops
// - Fail-fast ordering in validity checks
// =============================================================================

static std::atomic<long long> exploredCount{0};

// Maximum marks we support (n <= 24 is reasonable)
constexpr int MAX_MARKS = 24;

// Number of 64-bit words needed for MAX_DIFF bits (256 bits = 4 words)
constexpr int DIFF_WORDS = (MAX_DIFF + 63) / 64;

// Stack-allocated state for backtracking
struct alignas(64) SearchState {  // Cache-line aligned
    int marks[MAX_MARKS];           // Current ruler marks
    uint64_t usedDiffs[DIFF_WORDS]; // Difference tracking as raw bits
    int numMarks;                    // Current number of marks
    int bestLen;                     // Best length found by this thread
    int bestMarks[MAX_MARKS];        // Best solution found by this thread
    int bestNumMarks;                // Number of marks in best solution
};

// Inline bit operations - faster than std::bitset methods
static inline void clearAllBits(uint64_t* bits) {
    std::memset(bits, 0, DIFF_WORDS * sizeof(uint64_t));
}

// =============================================================================
// CORE BACKTRACKING FUNCTION
// =============================================================================
// CSAPP optimizations applied:
// - #2: Common case fast - validity check is the hot path
// - #3: Loop invariants hoisted (numMarks, marks pointer, etc.)
// - #4: No function calls in inner loop (all inlined)
// - #5: Incremental diff updates (no recomputation)
// - #6: Fail-fast with [[likely]]/[[unlikely]] hints
// - #8: Minimal live variables in inner loop
// =============================================================================
static void backtrackOptimized(
    SearchState& state,
    const int n,
    const int maxLen,                 // Feasibility bound (allow solutions <= maxLen)
    std::atomic<int>& globalBestLen,  // Best solution found so far (shared)
    long long& localExplored)
{
    localExplored++;

    // CSAPP #3: Hoist ALL loop-invariant values
    const int numMarks = state.numMarks;
    const int* const marks = state.marks;
    uint64_t* const usedDiffs = state.usedDiffs;
    const int lastMark = marks[numMarks - 1];
    const int start = lastMark + 1;

    // Check if we already have n marks (edge case for n=2)
    if (numMarks == n) [[unlikely]] {
        const int solutionLen = lastMark;
        // Accept if within feasibility bound AND better than thread's best
        if (solutionLen <= maxLen && solutionLen < state.bestLen) {
            state.bestLen = solutionLen;
            state.bestNumMarks = n;
            for (int i = 0; i < n; ++i) {
                state.bestMarks[i] = marks[i];
            }
            // Update global best atomically
            int expected = globalBestLen.load(std::memory_order_relaxed);
            while (solutionLen < expected &&
                   !globalBestLen.compare_exchange_weak(expected, solutionLen,
                       std::memory_order_release, std::memory_order_relaxed)) {
            }
        }
        return;
    }

    // Local diffs array for THIS recursion level
    // CRITICAL: Must be local to prevent corruption across recursion
    int diffs[MAX_MARKS];

    // CSAPP #2: Make common case fast
    // The loop bound uses maxLen (feasibility) as upper bound
    // Pruning is done inside the loop by checking globalBestLen each iteration
    for (int next = start; next <= maxLen; ++next) {
        // CSAPP #6: Fail-fast pruning
        // Re-read global best each iteration to catch updates from other threads
        // Use strict < for pruning since we want solutions STRICTLY better
        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);
        if (next >= currentGlobalBest) [[unlikely]] {
            // Can't possibly find a better solution with this or higher next values
            break;
        }

        // CSAPP #2/#6: Validity check - this is the HOT PATH
        // Check all differences for conflicts (fail-fast)
        bool valid = true;
        int numNewDiffs = 0;

        // CSAPP #8: Minimal live variables - compute d inline
        for (int i = 0; i < numMarks; ++i) {
            const int d = next - marks[i];
            // Inline bit test to avoid function call overhead
            if (usedDiffs[d >> 6] & (1ULL << (d & 63))) {
                valid = false;
                break;  // Fail-fast
            }
            diffs[numNewDiffs++] = d;
        }

        // CSAPP #6: Most candidates are rejected
        if (!valid) [[likely]] continue;

        // === VALID CANDIDATE - Apply changes ===

        // CSAPP #5: Incremental update (not recomputation)
        for (int i = 0; i < numNewDiffs; ++i) {
            const int d = diffs[i];
            usedDiffs[d >> 6] |= (1ULL << (d & 63));
        }
        state.marks[numMarks] = next;
        state.numMarks = numMarks + 1;

        // Check if complete solution
        if (state.numMarks == n) {
            const int solutionLen = next;  // next == marks[n-1]
            // Accept if within bounds AND better than current best
            if (solutionLen <= maxLen && solutionLen < state.bestLen) {
                state.bestLen = solutionLen;
                state.bestNumMarks = n;
                for (int j = 0; j < n; ++j) {
                    state.bestMarks[j] = state.marks[j];
                }
                // Update global best atomically
                int expected = globalBestLen.load(std::memory_order_relaxed);
                while (solutionLen < expected &&
                       !globalBestLen.compare_exchange_weak(expected, solutionLen,
                           std::memory_order_release, std::memory_order_relaxed)) {
                }
            }
        } else {
            // Recurse to explore deeper
            backtrackOptimized(state, n, maxLen, globalBestLen, localExplored);
        }

        // === BACKTRACK - Undo changes ===
        state.numMarks = numMarks;
        for (int i = 0; i < numNewDiffs; ++i) {
            const int d = diffs[i];
            usedDiffs[d >> 6] &= ~(1ULL << (d & 63));
        }
    }
}

// =============================================================================
// MAIN SEARCH FUNCTION
// =============================================================================
void searchGolomb(int n, int maxLen, GolombRuler& best)
{
    exploredCount.store(0, std::memory_order_relaxed);

    // globalBestLen starts at maxLen+1 (meaning "no solution found yet")
    // This allows finding solutions at exactly the maxLen boundary
    std::atomic<int> globalBestLen(maxLen + 1);

    // Final results - will be determined after all threads complete
    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS] = {0};
    int finalBestNumMarks = 0;

    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        // Thread-local state - completely independent per thread
        SearchState state{};
        state.marks[0] = 0;
        state.numMarks = 1;
        state.bestLen = maxLen + 1;
        state.bestNumMarks = 0;
        long long threadExplored = 0;

        // Distribute first-level branches across threads
        // Each thread explores its assigned branches completely
        #pragma omp for schedule(dynamic, 1)
        for (int firstMark = 1; firstMark <= maxLen; ++firstMark) {
            // Get current global best for pruning
            // This is purely for optimization - doesn't affect correctness
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);

            // Skip if this branch can't possibly improve (pruning optimization)
            if (firstMark >= currentGlobal) {
                continue;
            }

            // Setup state for this branch
            state.marks[0] = 0;
            state.marks[1] = firstMark;
            state.numMarks = 2;
            clearAllBits(state.usedDiffs);
            state.usedDiffs[firstMark >> 6] |= (1ULL << (firstMark & 63));

            // Explore this branch - state.bestLen will be updated if better found
            backtrackOptimized(state, n, maxLen, globalBestLen, threadExplored);
        }

        // Aggregate explored count
        exploredCount.fetch_add(threadExplored, std::memory_order_relaxed);

        // After ALL work is done, merge results
        // The implicit barrier at end of 'omp for' ensures all threads have finished
        if (state.bestNumMarks > 0) {
            #pragma omp critical(merge_best)
            {
                // Strict comparison: only update if strictly better
                if (state.bestLen < finalBestLen) {
                    finalBestLen = state.bestLen;
                    finalBestNumMarks = state.bestNumMarks;
                    for (int i = 0; i < state.bestNumMarks; ++i) {
                        finalBestMarks[i] = state.bestMarks[i];
                    }
                }
            }
        }
    }

    // Copy result
    if (finalBestNumMarks > 0) {
        best.marks.assign(finalBestMarks, finalBestMarks + finalBestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCount()
{
    return exploredCount.load(std::memory_order_relaxed);
}
