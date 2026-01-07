#include "search_v3.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <bitset>
#include <omp.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION 3 (HYBRID)
// =============================================================================
// Fusion du meilleur de V1 et V2:
// - Backtracking ITÉRATIF avec pile manuelle (V1) : pas d'overhead récursif
// - Bitset SHIFT pour validation O(1) (V2) : calcul des différences en 1 op
// - Structures alignées cache (V1) : performance mémoire optimale
// - XOR pour add/remove (V2) : backtrack ultra-rapide
// =============================================================================

static std::atomic<long long> exploredCountV3{0};

constexpr int MAX_DISTANCE = MAX_DIFF;  // 256
constexpr int MAX_MARKS_V3 = 24;

// =============================================================================
// STACK FRAME - État à chaque niveau de l'arbre de recherche
// =============================================================================
// Contient l'état complet du ruler + infos pour continuer l'exploration
struct alignas(64) StackFrameV3 {
    std::bitset<MAX_DISTANCE> reversed_marks;  // Représentation inversée des marques
    std::bitset<MAX_DISTANCE> used_dist;       // Différences utilisées
    std::bitset<MAX_DISTANCE> last_delta;      // Delta ajouté à ce niveau (pour backtrack)
    int marks_count;                            // Nombre de marques
    int ruler_length;                           // Longueur actuelle
    int next_candidate;                         // Prochain candidat à essayer
};

// Thread-local best solution
struct alignas(64) ThreadBestV3 {
    int bestLen;
    int bestMarks[MAX_MARKS_V3];
    int bestNumMarks;
};

// =============================================================================
// EXTRACTION DES MARQUES DEPUIS reversed_marks
// =============================================================================
static void extractMarks(const std::bitset<MAX_DISTANCE>& reversed_marks,
                         int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks[ruler_length - i]) {
            marks[numMarks++] = i;
        }
    }
}

// =============================================================================
// CORE BACKTRACKING - ITÉRATIF avec BITSET SHIFT
// =============================================================================
static void backtrackIterativeV3(
    ThreadBestV3& threadBest,
    const int n,
    std::atomic<int>& globalBestLen,
    long long& localExplored,
    StackFrameV3* stack)
{
    int stackTop = 0;

    while (stackTop >= 0) {
        localExplored++;

        StackFrameV3& frame = stack[stackTop];

        // Re-read global best for aggressive pruning
        const int currentGlobalBest = globalBestLen.load(std::memory_order_relaxed);

        // =================================================================
        // PRUNING: Borne inférieure de Golomb
        // =================================================================
        const int r = n - frame.marks_count;  // marques restantes
        const int minAdditionalLength = (r * (r + 1)) / 2;
        if (frame.ruler_length + minAdditionalLength >= currentGlobalBest) [[unlikely]] {
            stackTop--;
            continue;
        }

        // Calculer les bornes pour cette frame
        const int min_pos = frame.ruler_length + 1;
        const int max_remaining = ((r - 1) * r) / 2;
        const int max_pos = currentGlobalBest - max_remaining - 1;

        // Commencer là où on s'était arrêté
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

            // =============================================================
            // VALIDATION via BITSET SHIFT (algo V2)
            // =============================================================
            const int offset = pos - frame.ruler_length;

            // Calcule les nouvelles différences en UN SEUL shift
            std::bitset<MAX_DISTANCE> new_dist = frame.reversed_marks;
            new_dist <<= offset;

            // Vérifie TOUS les conflits en UN SEUL AND
            if ((new_dist & frame.used_dist).any()) [[likely]] {
                continue;  // Conflit détecté
            }

            // =============================================================
            // CANDIDAT VALIDE
            // =============================================================
            const int newMarksCount = frame.marks_count + 1;

            if (newMarksCount == n) {
                // Solution trouvée !
                const int solutionLen = pos;
                if (solutionLen < threadBest.bestLen) {
                    threadBest.bestLen = solutionLen;

                    // Construire l'état final pour extraction
                    std::bitset<MAX_DISTANCE> final_marks = frame.reversed_marks;
                    final_marks <<= offset;
                    final_marks[0] = true;

                    extractMarks(final_marks, pos, threadBest.bestMarks, threadBest.bestNumMarks);

                    // Update global best atomically
                    int expected = globalBestLen.load(std::memory_order_relaxed);
                    while (solutionLen < expected &&
                           !globalBestLen.compare_exchange_weak(expected, solutionLen,
                               std::memory_order_release, std::memory_order_relaxed)) {
                    }
                }
                // Continue à chercher d'autres solutions (pas de push)
            } else {
                // Push new frame pour explorer ce sous-arbre
                frame.next_candidate = pos + 1;  // Pour reprendre après retour

                StackFrameV3& newFrame = stack[stackTop + 1];

                // Nouvel état: shift + set bit 0
                newFrame.reversed_marks = frame.reversed_marks;
                newFrame.reversed_marks <<= offset;
                newFrame.reversed_marks[0] = true;

                // Nouvelles différences: XOR pour ajouter
                newFrame.used_dist = frame.used_dist;
                newFrame.used_dist ^= new_dist;

                // Sauvegarder delta pour backtrack (pas utilisé ici car on copie)
                newFrame.last_delta = new_dist;

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
// MAIN SEARCH FUNCTION - VERSION 3
// =============================================================================
void searchGolombV3(int n, int maxLen, GolombRuler& best)
{
    exploredCountV3.store(0, std::memory_order_relaxed);

    std::atomic<int> globalBestLen(maxLen + 1);

    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS_V3] = {0};
    int finalBestNumMarks = 0;

    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        ThreadBestV3 threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        // Pile pré-allouée (comme V1)
        alignas(64) StackFrameV3 stack[MAX_MARKS_V3];

        #pragma omp for schedule(dynamic, 1)
        for (int firstMark = 1; firstMark <= maxLen; ++firstMark) {
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
            if (firstMark >= currentGlobal) {
                continue;
            }

            // Setup initial frame: marques à 0 et firstMark
            StackFrameV3& frame0 = stack[0];

            // État initial: marque à 0
            frame0.reversed_marks.reset();
            frame0.reversed_marks[0] = true;
            frame0.used_dist.reset();
            frame0.marks_count = 1;
            frame0.ruler_length = 0;

            // Ajouter firstMark via shift
            frame0.reversed_marks <<= firstMark;
            frame0.reversed_marks[0] = true;

            // La différence firstMark est utilisée
            frame0.used_dist[firstMark] = true;

            frame0.marks_count = 2;
            frame0.ruler_length = firstMark;
            frame0.next_candidate = 0;

            // Lancer le backtracking itératif
            backtrackIterativeV3(threadBest, n, globalBestLen, threadExplored, stack);
        }

        exploredCountV3.fetch_add(threadExplored, std::memory_order_relaxed);

        // Merge des résultats
        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_best_v3)
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

    // Copier le résultat final
    if (finalBestNumMarks > 0) {
        best.marks.assign(finalBestMarks, finalBestMarks + finalBestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountV3()
{
    return exploredCountV3.load(std::memory_order_relaxed);
}
