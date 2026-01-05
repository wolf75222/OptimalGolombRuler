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
// - ITERATIVE BACKTRACKING with manual stack (no recursion overhead)
// =============================================================================

static std::atomic<long long> exploredCount{0};

// Maximum marks we support (n <= 24 is reasonable)
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
    int bestLen;                    // Best length found by this thread
    int bestMarks[MAX_MARKS];       // Best solution found by this thread
    int bestNumMarks;               // Number of marks in best solution
};

// Inline bit operations - faster than std::bitset methods
static inline void clearAllBits(uint64_t* bits) {
    std::memset(bits, 0, DIFF_WORDS * sizeof(uint64_t));
}

// =============================================================================
// CORE BACKTRACKING - VERSION ITÉRATIVE AVEC PILE MANUELLE
// =============================================================================
// Avantages vs récursion:
// - Pas d'overhead d'appel de fonction (call/ret, save/restore registres)
// - Pile pré-allouée = pas d'allocation dynamique
// - Meilleure utilisation du cache (pile contiguë en mémoire)
// - Permet au compilateur de mieux optimiser (pas de frontière de fonction)
// =============================================================================
static void backtrackIterative(
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
        // Thread-local best solution
        ThreadBest threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        // Thread-local stack (pré-allouée une seule fois par thread)
        alignas(64) StackFrame stack[MAX_MARKS];

        // Distribute first-level branches across threads
        // Each thread explores its assigned branches completely
        #pragma omp for schedule(dynamic, 1)
        for (int firstMark = 1; firstMark <= maxLen; ++firstMark) {
            // Get current global best for pruning
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);

            // Skip if this branch can't possibly improve
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

            // Explore this branch with iterative backtracking
            backtrackIterative(threadBest, n, maxLen, globalBestLen, threadExplored, stack);
        }

        // Aggregate explored count
        exploredCount.fetch_add(threadExplored, std::memory_order_relaxed);

        // After ALL work is done, merge results
        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_best)
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
