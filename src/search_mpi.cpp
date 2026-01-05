#include "search_mpi.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <omp.h>
#include <mpi.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - MPI+OpenMP HYBRID VERSION
// =============================================================================
// Architecture: Symmetric Hypercube with periodic allreduce for bound sync
//
// HYPERCUBE COMMUNICATION PATTERN:
// - All processes are workers (no master-worker asymmetry)
// - Work is statically distributed: process p handles branches where (branch % size) == p
// - Bound synchronization via hypercube allReduceMin every SYNC_INTERVAL branches
// - O(log P) communication rounds per sync vs O(P) for centralized
// - Better scalability: no central bottleneck
//
// CSAPP-inspired optimizations:
// - Stack-allocated arrays (no heap allocations in hot path)
// - Cache-line aligned structures
// - Direct bit manipulation (uint64_t instead of std::bitset)
// - Hoisted loop-invariant memory accesses
// - ITERATIVE BACKTRACKING with manual stack (no recursion overhead)
// =============================================================================

static std::atomic<long long> exploredCountMPI{0};

// Hypercube sync frequency: synchronize global best every N branches
// Too frequent = communication overhead, too rare = delayed pruning
// Empirically tuned: 8-16 branches gives good balance
constexpr int SYNC_INTERVAL = 8;

// Maximum marks we support
constexpr int MAX_MARKS = 24;

// Number of 64-bit words needed for MAX_DIFF bits (256 bits = 4 words)
constexpr int DIFF_WORDS = (MAX_DIFF + 63) / 64;

// =============================================================================
// STRUCTURE DE FRAME POUR LA PILE MANUELLE
// =============================================================================
struct alignas(64) StackFrame {
    int marks[MAX_MARKS];           // État des marques à ce niveau
    uint64_t usedDiffs[DIFF_WORDS]; // Bitmap des différences à ce niveau
    int numMarks;                   // Nombre de marques à ce niveau
    int nextCandidate;              // Prochain candidat à essayer
};

// Thread-local best solution
struct alignas(64) ThreadBest {
    int bestLen;
    int bestMarks[MAX_MARKS];
    int bestNumMarks;
};

// Inline bit operations
static inline void clearAllBits(uint64_t* bits) {
    std::memset(bits, 0, DIFF_WORDS * sizeof(uint64_t));
}

// =============================================================================
// CORE BACKTRACKING - VERSION ITÉRATIVE AVEC PILE MANUELLE (MPI VERSION)
// =============================================================================
static void backtrackIterativeMPI(
    ThreadBest& threadBest,
    const int n,
    const int maxLen,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrame* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrame& frame = stack[stackTop];

        const int numMarks = frame.numMarks;
        int* const marks = frame.marks;
        uint64_t* const usedDiffs = frame.usedDiffs;
        const int lastMark = marks[numMarks - 1];

        // Re-read global best for aggressive pruning
        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);
        const int upperBound = currentGlobalBest - 1;

        // =================================================================
        // PRUNING: Borne inférieure de Golomb
        // =================================================================
        // r = nombre de marques restantes à placer
        // Les r prochaines différences minimales possibles sont 1, 2, ..., r
        // Donc la longueur finale minimale est: lastMark + r*(r+1)/2
        // Si cette borne >= bestLen, on peut couper cette branche
        // =================================================================
        const int r = n - numMarks;  // marques restantes
        const int minAdditionalLength = (r * (r + 1)) / 2;
        if (lastMark + minAdditionalLength >= currentGlobalBest) [[unlikely]] {
            stackTop--;
            continue;
        }

        int startNext = frame.nextCandidate;
        if (startNext == 0) {
            startNext = lastMark + 1;
        }

        bool pushedChild = false;

        // CSAPP #3: Loop invariant hoisting - sortir les constantes
        const int unrollLimit = numMarks - 3;

        for (int next = startNext; next <= upperBound; ++next) {
            // Re-check global best for early exit
            const int newGlobalBest = globalBestLen.load(std::memory_order_relaxed);
            if (next >= newGlobalBest) [[unlikely]] {
                break;
            }

            // === HOT PATH: Validation des différences ===
            bool valid = true;
            int numNewDiffs = 0;
            int newDiffs[MAX_MARKS];
            int i = 0;

            // CSAPP #7: Loop unrolling 4x
            for (; i < unrollLimit; i += 4) {
                const int d0 = next - marks[i];
                const int d1 = next - marks[i + 1];
                const int d2 = next - marks[i + 2];
                const int d3 = next - marks[i + 3];

                const uint64_t mask0 = 1ULL << (d0 & 63);
                const uint64_t mask1 = 1ULL << (d1 & 63);
                const uint64_t mask2 = 1ULL << (d2 & 63);
                const uint64_t mask3 = 1ULL << (d3 & 63);

                const uint64_t word0 = usedDiffs[d0 >> 6];
                const uint64_t word1 = usedDiffs[d1 >> 6];
                const uint64_t word2 = usedDiffs[d2 >> 6];
                const uint64_t word3 = usedDiffs[d3 >> 6];

                // CSAPP #6: Un seul test combiné pour réduire les branches
                if ((word0 & mask0) | (word1 & mask1) |
                    (word2 & mask2) | (word3 & mask3)) [[likely]] {
                    valid = false;
                    break;
                }

                newDiffs[numNewDiffs++] = d0;
                newDiffs[numNewDiffs++] = d1;
                newDiffs[numNewDiffs++] = d2;
                newDiffs[numNewDiffs++] = d3;
            }

            // Tail loop
            if (valid) [[unlikely]] {
                for (; i < numMarks; ++i) {
                    const int d = next - marks[i];
                    if (usedDiffs[d >> 6] & (1ULL << (d & 63))) [[likely]] {
                        valid = false;
                        break;
                    }
                    newDiffs[numNewDiffs++] = d;
                }
            }

            if (!valid) [[likely]] {
                continue;
            }

            // === CANDIDAT VALIDE ===
            const int newNumMarks = numMarks + 1;

            if (newNumMarks == n) {
                const int solutionLen = next;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;
                    threadBest.bestNumMarks = n;
                    for (int j = 0; j < numMarks; ++j) {
                        threadBest.bestMarks[j] = marks[j];
                    }
                    threadBest.bestMarks[numMarks] = next;

                    // Update global best atomically
                    int expected = globalBestLen.load(std::memory_order_relaxed);
                    while (solutionLen < expected &&
                           !globalBestLen.compare_exchange_weak(expected, solutionLen,
                               std::memory_order_release, std::memory_order_relaxed)) {
                    }
                }
            } else {
                // Push new frame
                frame.nextCandidate = next + 1;

                StackFrame& newFrame = stack[stackTop + 1];
                std::memcpy(newFrame.marks, marks, sizeof(int) * numMarks);
                newFrame.marks[numMarks] = next;
                std::memcpy(newFrame.usedDiffs, usedDiffs, sizeof(uint64_t) * DIFF_WORDS);

                for (int j = 0; j < numNewDiffs; ++j) {
                    const int d = newDiffs[j];
                    newFrame.usedDiffs[d >> 6] |= (1ULL << (d & 63));
                }

                newFrame.numMarks = newNumMarks;
                newFrame.nextCandidate = 0;

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
    // SYMMETRIC HYPERCUBE ARCHITECTURE
    // ==========================================================================
    // All processes are workers. Work is distributed in rounds:
    // - Each round, all processes explore SYNC_INTERVAL branches each
    // - After each round, hypercube allReduceMin synchronizes bounds
    // - O(log P) communication per sync, no master bottleneck
    // ==========================================================================

    int localBestLen = maxLen + 1;
    int localBestMarks[MAX_MARKS] = {0};
    int localBestNumMarks = 0;

    // Calculate total branches this process will handle
    // Each process handles branches where (branch-1) % size == rank
    int myBranches = 0;
    for (int b = 1; b <= maxLen; ++b) {
        if ((b - 1) % size == rank) myBranches++;
    }

    // Process branches in rounds with periodic sync
    int branchIndex = 0;
    int round = 0;

    while (branchIndex < myBranches) {
        // Determine how many branches to process this round
        int branchesThisRound = std::min(SYNC_INTERVAL, myBranches - branchIndex);
        int startBranchIndex = branchIndex;
        int endBranchIndex = branchIndex + branchesThisRound;

        #pragma omp parallel shared(globalBestLen, localBestLen, localBestMarks, localBestNumMarks)
        {
            ThreadBest threadBest{};
            threadBest.bestLen = maxLen + 1;
            threadBest.bestNumMarks = 0;
            long long threadExplored = 0;

            // Thread-local stack
            alignas(64) StackFrame stack[MAX_MARKS];

            // Process assigned branches for this round
            #pragma omp for schedule(dynamic, 1)
            for (int idx = startBranchIndex; idx < endBranchIndex; ++idx) {
                // Convert index to actual firstMark value
                // idx-th branch for this rank: firstMark = 1 + rank + idx*size
                int firstMark = 1 + rank + idx * size;

                if (firstMark > maxLen) continue;

                const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
                if (firstMark >= currentGlobal) {
                    continue;
                }

                // Setup first frame for this branch
                StackFrame& frame0 = stack[0];
                std::memset(frame0.marks, 0, sizeof(frame0.marks));
                std::memset(frame0.usedDiffs, 0, sizeof(frame0.usedDiffs));
                frame0.marks[0] = 0;
                frame0.marks[1] = firstMark;
                frame0.numMarks = 2;
                frame0.nextCandidate = 0;
                frame0.usedDiffs[firstMark >> 6] |= (1ULL << (firstMark & 63));

                backtrackIterativeMPI(threadBest, n, maxLen, globalBestLen, threadExplored, stack);
            }

            exploredCountMPI.fetch_add(threadExplored, std::memory_order_relaxed);

            if (threadBest.bestNumMarks > 0) {
                #pragma omp critical(merge_best_mpi)
                {
                    if (threadBest.bestLen < localBestLen) {
                        localBestLen = threadBest.bestLen;
                        localBestNumMarks = threadBest.bestNumMarks;
                        for (int i = 0; i < threadBest.bestNumMarks; ++i) {
                            localBestMarks[i] = threadBest.bestMarks[i];
                        }
                    }
                }
            }
        }

        branchIndex = endBranchIndex;
        round++;

        // =================================================================
        // HYPERCUBE SYNCHRONIZATION - O(log P) communication
        // =================================================================
        // All processes synchronize their best bound after each round
        // This is a collective operation, all processes must participate
        // =================================================================
        int myBest = globalBestLen.load(std::memory_order_acquire);
        int globalMin = hypercube.allReduceMin(myBest);

        // Update local bound if we got a better one from another process
        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (globalMin < expected &&
               !globalBestLen.compare_exchange_weak(expected, globalMin,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    // Handle case where processes have different number of branches
    // Need to participate in final syncs even if we're done
    // Calculate max rounds any process needs
    int maxBranches;
    MPI_Allreduce(&myBranches, &maxBranches, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    int maxRounds = (maxBranches + SYNC_INTERVAL - 1) / SYNC_INTERVAL;

    // Participate in remaining syncs
    while (round < maxRounds) {
        int myBest = globalBestLen.load(std::memory_order_acquire);
        int globalMin = hypercube.allReduceMin(myBest);

        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (globalMin < expected &&
               !globalBestLen.compare_exchange_weak(expected, globalMin,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }
        round++;
    }

    // ==========================================================================
    // FINAL GLOBAL REDUCTION
    // ==========================================================================
    // After all local work is done, find the global best across all processes
    // using hypercube allReduceMin, then gather the actual solution
    // ==========================================================================

    // Synchronize all processes before final reduction
    MPI_Barrier(MPI_COMM_WORLD);

    // Find global minimum length using hypercube
    int globalMinLen = hypercube.allReduceMin(localBestLen);

    // Determine which process has the best solution
    // (there might be ties, we pick the lowest rank with the best)
    int hasWinner = (localBestLen == globalMinLen && localBestNumMarks > 0) ? rank : size;
    int globalWinner;
    MPI_Allreduce(&hasWinner, &globalWinner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    // Broadcast solution from winner to all processes
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

    // Copy result
    if (bestNumMarks > 0) {
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
