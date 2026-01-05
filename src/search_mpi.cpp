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
// - ITERATIVE BACKTRACKING with manual stack (no recursion overhead)
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

        int startNext = frame.nextCandidate;
        if (startNext == 0) {
            startNext = lastMark + 1;
        }

        bool pushedChild = false;

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

            // Loop unrolling 4x
            const int unrollLimit = numMarks - 3;
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

                if ((word0 & mask0) | (word1 & mask1) |
                    (word2 & mask2) | (word3 & mask3)) {
                    valid = false;
                    break;
                }

                newDiffs[numNewDiffs++] = d0;
                newDiffs[numNewDiffs++] = d1;
                newDiffs[numNewDiffs++] = d2;
                newDiffs[numNewDiffs++] = d3;
            }

            // Tail loop
            if (valid) {
                for (; i < numMarks; ++i) {
                    const int d = next - marks[i];
                    if (usedDiffs[d >> 6] & (1ULL << (d & 63))) {
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

// Process a single branch with OpenMP parallelization
// This explores all solutions starting with marks[1] = branchStart
// Now uses OpenMP to parallelize the exploration of second-level branches
// Uses ITERATIVE backtracking with manual stack for performance
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

    // Thread-safe accumulators
    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS] = {0};
    int finalBestNumMarks = 0;

    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        ThreadBest threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        // Thread-local stack
        alignas(64) StackFrame stack[MAX_MARKS];

        // Parallelize over second mark positions (marks[2])
        // Range: branchStart+1 to currentBest-1
        #pragma omp for schedule(dynamic, 1) nowait
        for (int secondMark = branchStart + 1; secondMark <= maxLen; ++secondMark) {
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
            if (secondMark >= currentGlobal) {
                continue;
            }

            // Compute differences for marks[2] = secondMark
            const int d1 = secondMark;
            const int d2 = secondMark - branchStart;

            // Check for conflicts
            if (d2 == branchStart || d1 == d2) {
                continue;
            }

            // Setup first frame for this sub-branch
            StackFrame& frame0 = stack[0];
            std::memset(frame0.marks, 0, sizeof(frame0.marks));
            std::memset(frame0.usedDiffs, 0, sizeof(frame0.usedDiffs));
            frame0.marks[0] = 0;
            frame0.marks[1] = branchStart;
            frame0.marks[2] = secondMark;
            frame0.numMarks = 3;
            frame0.nextCandidate = 0;
            frame0.usedDiffs[branchStart >> 6] |= (1ULL << (branchStart & 63));
            frame0.usedDiffs[d1 >> 6] |= (1ULL << (d1 & 63));
            frame0.usedDiffs[d2 >> 6] |= (1ULL << (d2 & 63));

            backtrackIterativeMPI(threadBest, n, maxLen, globalBestLen, threadExplored, stack);
        }

        // Accumulate explored count
        #pragma omp atomic
        totalExplored += threadExplored;

        // Merge best solution
        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_branch_best)
            {
                if (threadBest.bestLen < finalBestLen) {
                    finalBestLen = threadBest.bestLen;
                    finalBestNumMarks = threadBest.bestNumMarks;
                    for (int i = 0; i < threadBest.bestNumMarks; ++i) {
                        finalBestMarks[i] = threadBest.bestMarks[i];
                    }
                }
            }
        }
    }

    // Copy results
    if (finalBestNumMarks > 0) {
        resultBestLen = finalBestLen;
        resultNumMarks = finalBestNumMarks;
        for (int i = 0; i < finalBestNumMarks; ++i) {
            resultBestMarks[i] = finalBestMarks[i];
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
    // Uses ITERATIVE backtracking with manual stack for performance
    // ==========================================================================
    if (size == 1) {
        int finalBestLen = maxLen + 1;
        int finalBestMarks[MAX_MARKS] = {0};
        int finalBestNumMarks = 0;

        #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
        {
            ThreadBest threadBest{};
            threadBest.bestLen = maxLen + 1;
            threadBest.bestNumMarks = 0;
            long long threadExplored = 0;

            // Thread-local stack
            alignas(64) StackFrame stack[MAX_MARKS];

            #pragma omp for schedule(dynamic, 1)
            for (int firstMark = 1; firstMark <= maxLen; ++firstMark) {
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
                    if (threadBest.bestLen < finalBestLen) {
                        finalBestLen = threadBest.bestLen;
                        finalBestNumMarks = threadBest.bestNumMarks;
                        for (int i = 0; i < threadBest.bestNumMarks; ++i) {
                            finalBestMarks[i] = threadBest.bestMarks[i];
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

                    // Broadcast new bound to all workers using non-blocking sends
                    // Allows master to continue processing while messages are in flight
                    for (int w = 1; w < size; ++w) {
                        if (w != source) {
                            MPI_Request req;
                            MPI_Isend(&bestLen, 1, MPI_INT, w, TAG_BEST_UPDATE,
                                     MPI_COMM_WORLD, &req);
                            MPI_Request_free(&req);  // Fire-and-forget
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
        // Hypercube neighbors for bound relay
        const int dim = hypercube.dimensions();

        while (true) {
            // Check for bound updates from ANY source (hypercube relay)
            int flag;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_BEST_UPDATE, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
            while (flag) {
                MPI_Status updateStatus;
                int newBest;
                MPI_Recv(&newBest, 1, MPI_INT, MPI_ANY_SOURCE, TAG_BEST_UPDATE,
                        MPI_COMM_WORLD, &updateStatus);

                int source = updateStatus.MPI_SOURCE;

                // Update local bound if better
                int expected = globalBestLen.load(std::memory_order_relaxed);
                bool updated = false;
                while (newBest < expected) {
                    if (globalBestLen.compare_exchange_weak(expected, newBest)) {
                        updated = true;
                        break;
                    }
                }

                // Relay to hypercube neighbors (if we updated)
                if (updated) {
                    for (int d = 0; d < dim; ++d) {
                        int neighbor = rank ^ (1 << d);  // XOR with 2^d
                        // Don't send back to source, and only to valid workers
                        if (neighbor != source && neighbor > 0 && neighbor < size) {
                            MPI_Request req;
                            MPI_Isend(&newBest, 1, MPI_INT, neighbor, TAG_BEST_UPDATE,
                                     MPI_COMM_WORLD, &req);
                            MPI_Request_free(&req);
                        }
                    }
                }

                MPI_Iprobe(MPI_ANY_SOURCE, TAG_BEST_UPDATE, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
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
                // Track explored states for this worker
                exploredCountMPI.fetch_add(branchExplored, std::memory_order_relaxed);
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
