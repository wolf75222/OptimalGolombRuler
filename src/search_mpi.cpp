#include "search_mpi.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <queue>
#include <omp.h>
#include <mpi.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - MPI+OpenMP HYBRID VERSION
// =============================================================================
// Architecture: Master-Worker with Hypercube broadcast for bound propagation
//
// CSAPP-inspired optimizations:
// - Stack-allocated arrays (no heap allocations in hot path)
// - Cache-line aligned structures
// - Direct bit manipulation (uint64_t instead of std::bitset)
// - Hoisted loop-invariant memory accesses
// - Hypercube all-reduce for fast bound propagation
// - Dynamic work distribution with work stealing
// =============================================================================

static std::atomic<long long> exploredCountMPI{0};

// MPI Tags
constexpr int TAG_WORK_ASSIGN = 1;
constexpr int TAG_RESULT = 2;
constexpr int TAG_BEST_UPDATE = 3;
constexpr int TAG_TERMINATE = 4;
constexpr int TAG_MARKS = 5;

// Maximum marks we support
constexpr int MAX_MARKS = 24;

// Number of 64-bit words needed for MAX_DIFF bits (256 bits = 4 words)
constexpr int DIFF_WORDS = (MAX_DIFF + 63) / 64;

// Cache-line aligned search state
struct alignas(64) SearchState {
    int marks[MAX_MARKS];
    uint64_t usedDiffs[DIFF_WORDS];  // Direct bit manipulation
    int numMarks;
    int bestLen;
    int bestMarks[MAX_MARKS];
    int bestNumMarks;
};

// Inline bit operations
static inline void clearAllBits(uint64_t* bits) {
    std::memset(bits, 0, DIFF_WORDS * sizeof(uint64_t));
}

// Optimized backtracking with CSAPP optimizations
static inline void backtrackMPI(
    SearchState& state,
    const int n,
    const int maxLen,
    std::atomic<int>& globalBestLen,
    long long& localExplored)
{
    localExplored++;

    // CSAPP: Hoist loop-invariant memory accesses
    const int numMarks = state.numMarks;
    const int* const marks = state.marks;
    uint64_t* const usedDiffs = state.usedDiffs;
    const int lastMark = marks[numMarks - 1];
    const int start = lastMark + 1;

    // Check if we already have n marks (complete solution)
    if (numMarks == n) [[unlikely]] {
        const int solutionLen = lastMark;
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

    // Loop with pruning - re-read globalBestLen each iteration
    for (int next = start; next <= maxLen; ++next) {
        // Re-read global best each iteration to catch updates from other threads
        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);
        if (next >= currentGlobalBest) [[unlikely]] {
            break;
        }

        // Check all differences for conflicts (fail-fast)
        bool valid = true;
        int numNewDiffs = 0;

        for (int i = 0; i < numMarks; ++i) {
            const int d = next - marks[i];
            if (usedDiffs[d >> 6] & (1ULL << (d & 63))) {
                valid = false;
                break;
            }
            diffs[numNewDiffs++] = d;
        }

        if (!valid) [[likely]] continue;

        // Apply all differences
        for (int i = 0; i < numNewDiffs; ++i) {
            const int d = diffs[i];
            usedDiffs[d >> 6] |= (1ULL << (d & 63));
        }
        state.marks[numMarks] = next;
        state.numMarks = numMarks + 1;

        // Check if complete solution
        if (state.numMarks == n) {
            const int solutionLen = next;
            if (solutionLen <= maxLen && solutionLen < state.bestLen) {
                state.bestLen = solutionLen;
                state.bestNumMarks = n;
                for (int j = 0; j < n; ++j) {
                    state.bestMarks[j] = state.marks[j];
                }
                int expected = globalBestLen.load(std::memory_order_relaxed);
                while (solutionLen < expected &&
                       !globalBestLen.compare_exchange_weak(expected, solutionLen,
                           std::memory_order_release, std::memory_order_relaxed)) {
                }
            }
        } else {
            // Recurse to explore deeper
            backtrackMPI(state, n, maxLen, globalBestLen, localExplored);
        }

        // Undo all differences
        state.numMarks = numMarks;
        for (int i = 0; i < numNewDiffs; ++i) {
            const int d = diffs[i];
            usedDiffs[d >> 6] &= ~(1ULL << (d & 63));
        }
    }
}

// Process a single branch with OpenMP parallelization
// This explores all solutions starting with marks[1] = branchStart
static void processBranch(
    int branchStart,
    int n,
    int maxLen,
    std::atomic<int>& globalBestLen,
    int& resultBestLen,
    int resultBestMarks[MAX_MARKS],
    int& resultNumMarks,
    long long& totalExplored)
{
    resultBestLen = maxLen + 1;
    resultNumMarks = 0;
    totalExplored = 0;

    // Use single thread to explore this entire branch
    // (OpenMP parallelization is already at the branch level via MPI)
    SearchState state{};
    state.marks[0] = 0;
    state.marks[1] = branchStart;
    state.numMarks = 2;
    state.bestLen = maxLen + 1;
    state.bestNumMarks = 0;

    clearAllBits(state.usedDiffs);
    state.usedDiffs[branchStart >> 6] |= (1ULL << (branchStart & 63));

    backtrackMPI(state, n, maxLen, globalBestLen, totalExplored);

    if (state.bestNumMarks > 0) {
        resultBestLen = state.bestLen;
        resultNumMarks = state.bestNumMarks;
        for (int i = 0; i < state.bestNumMarks; ++i) {
            resultBestMarks[i] = state.bestMarks[i];
        }
    }
}

void searchGolombMPI(int n, int maxLen, GolombRuler& best, HypercubeMPI& hypercube)
{
    exploredCountMPI.store(0, std::memory_order_relaxed);

    const int rank = hypercube.rank();
    const int size = hypercube.size();

    // Initialize to maxLen+1 to allow finding solutions at exactly maxLen
    int bestLen = maxLen + 1;
    int bestMarks[MAX_MARKS] = {0};
    int bestNumMarks = 0;
    std::atomic<int> globalBestLen(maxLen + 1);

    // ==========================================================================
    // Single process mode: use OpenMP directly (like search.cpp)
    // ==========================================================================
    if (size == 1) {
        int finalBestLen = maxLen + 1;
        int finalBestMarks[MAX_MARKS] = {0};
        int finalBestNumMarks = 0;

        #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
        {
            SearchState state{};
            state.marks[0] = 0;
            state.numMarks = 1;
            state.bestLen = maxLen + 1;
            state.bestNumMarks = 0;
            long long threadExplored = 0;

            #pragma omp for schedule(dynamic, 1)
            for (int firstMark = 1; firstMark <= maxLen; ++firstMark) {
                const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
                if (firstMark >= currentGlobal) {
                    continue;
                }

                // Setup state for this branch
                state.marks[0] = 0;
                state.marks[1] = firstMark;
                state.numMarks = 2;
                clearAllBits(state.usedDiffs);
                state.usedDiffs[firstMark >> 6] |= (1ULL << (firstMark & 63));

                backtrackMPI(state, n, maxLen, globalBestLen, threadExplored);
            }

            exploredCountMPI.fetch_add(threadExplored, std::memory_order_relaxed);

            if (state.bestNumMarks > 0) {
                #pragma omp critical(merge_best_mpi)
                {
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

        if (finalBestNumMarks > 0) {
            best.marks.assign(finalBestMarks, finalBestMarks + finalBestNumMarks);
        } else {
            best.marks.clear();
        }
        best.computeLength();
        return;
    }

    // ==========================================================================
    // Multi-process mode: Master-Worker with Hypercube bound propagation
    // ==========================================================================

    if (rank == 0) {
        // === MASTER ===
        std::queue<int> workQueue;
        for (int branch = 1; branch <= maxLen; ++branch) {
            workQueue.push(branch);
        }

        int activeWorkers = size - 1;
        long long totalExplored = 0;

        // Initial distribution
        for (int w = 1; w < size && !workQueue.empty(); ++w) {
            int branch = workQueue.front();
            workQueue.pop();

            int msg[2] = {branch, bestLen};
            MPI_Send(msg, 2, MPI_INT, w, TAG_WORK_ASSIGN, MPI_COMM_WORLD);
        }

        // Main loop
        while (activeWorkers > 0) {
            MPI_Status status;
            int recvBuf[4]; // [bestLen, numMarks, explored_low, explored_high]

            MPI_Recv(recvBuf, 4, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT,
                    MPI_COMM_WORLD, &status);

            int source = status.MPI_SOURCE;
            int workerBestLen = recvBuf[0];
            int numMarks = recvBuf[1];
            long long workerExplored = (static_cast<long long>(recvBuf[3]) << 32) |
                                       static_cast<unsigned int>(recvBuf[2]);

            totalExplored += workerExplored;

            // Receive marks if worker found a solution
            if (numMarks > 0) {
                int workerMarks[MAX_MARKS];
                MPI_Recv(workerMarks, numMarks, MPI_INT, source,
                        TAG_MARKS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // Update best if this solution is better
                if (workerBestLen < bestLen) {
                    bestLen = workerBestLen;
                    bestNumMarks = numMarks;
                    for (int i = 0; i < numMarks; ++i) {
                        bestMarks[i] = workerMarks[i];
                    }

                    // Broadcast new bound to all workers
                    for (int w = 1; w < size; ++w) {
                        if (w != source) {
                            MPI_Send(&bestLen, 1, MPI_INT, w, TAG_BEST_UPDATE, MPI_COMM_WORLD);
                        }
                    }
                }
            }

            // Prune work queue
            while (!workQueue.empty() && workQueue.front() >= bestLen) {
                workQueue.pop();
            }

            // Assign more work or terminate
            if (!workQueue.empty()) {
                int branch = workQueue.front();
                workQueue.pop();

                int msg[2] = {branch, bestLen};
                MPI_Send(msg, 2, MPI_INT, source, TAG_WORK_ASSIGN, MPI_COMM_WORLD);
            } else {
                MPI_Send(nullptr, 0, MPI_INT, source, TAG_TERMINATE, MPI_COMM_WORLD);
                activeWorkers--;
            }
        }

        exploredCountMPI.store(totalExplored, std::memory_order_relaxed);

    } else {
        // === WORKER ===
        while (true) {
            // Check for bound updates (non-blocking)
            int flag;
            MPI_Iprobe(0, TAG_BEST_UPDATE, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
            while (flag) {
                int newBest;
                MPI_Recv(&newBest, 1, MPI_INT, 0, TAG_BEST_UPDATE,
                        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                int expected = globalBestLen.load(std::memory_order_relaxed);
                while (newBest < expected &&
                       !globalBestLen.compare_exchange_weak(expected, newBest));

                MPI_Iprobe(0, TAG_BEST_UPDATE, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
            }

            // Receive work
            int msg[2];
            MPI_Status status;
            MPI_Recv(msg, 2, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

            if (status.MPI_TAG == TAG_TERMINATE) break;

            int branch = msg[0];
            int masterBest = msg[1];

            // Update local bound
            int expected = globalBestLen.load(std::memory_order_relaxed);
            while (masterBest < expected &&
                   !globalBestLen.compare_exchange_weak(expected, masterBest));

            // Process branch
            int resultBestLen;
            int resultMarks[MAX_MARKS];
            int resultNumMarks;
            long long branchExplored;

            if (branch < globalBestLen.load(std::memory_order_acquire)) {
                processBranch(branch, n, maxLen, globalBestLen,
                             resultBestLen, resultMarks, resultNumMarks, branchExplored);
            } else {
                resultBestLen = globalBestLen.load(std::memory_order_acquire);
                resultNumMarks = 0;
                branchExplored = 0;
            }

            // Send result - always send marks if we found a solution
            int resultBuf[4] = {
                resultBestLen,
                resultNumMarks,
                static_cast<int>(branchExplored & 0xFFFFFFFF),
                static_cast<int>(branchExplored >> 32)
            };
            MPI_Send(resultBuf, 4, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);

            // Send marks if we found any solution (master will decide if it's better)
            if (resultNumMarks > 0) {
                MPI_Send(resultMarks, resultNumMarks, MPI_INT, 0, TAG_MARKS, MPI_COMM_WORLD);
            }
        }
    }

    // Final broadcast of solution
    MPI_Bcast(&bestLen, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&bestNumMarks, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (bestNumMarks > 0) {
        MPI_Bcast(bestMarks, bestNumMarks, MPI_INT, 0, MPI_COMM_WORLD);
        best.marks.assign(bestMarks, bestMarks + bestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountMPI()
{
    long long localCount = exploredCountMPI.load(std::memory_order_relaxed);
    long long globalCount = 0;

    MPI_Reduce(&localCount, &globalCount, 1, MPI_LONG_LONG,
               MPI_SUM, 0, MPI_COMM_WORLD);

    return globalCount;
}
