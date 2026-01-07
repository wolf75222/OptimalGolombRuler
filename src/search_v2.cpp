#include "search_v2.hpp"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <bitset>
#include <omp.h>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - OpenMP VERSION 2
// =============================================================================
// Algorithme basé sur reversed_marks et bitset shift.
//
// Cet algorithme est une adaptation EXACTE de l'algorithme fourni :
// - GolombRuler avec reversed_marks et used_dist (bitset)
// - try_add_mark : calcule les nouvelles différences via shift
// - remove_last_mark : restore via XOR
// - Mêmes bornes de pruning que V1 pour comparaison équitable
// =============================================================================

static std::atomic<long long> exploredCountV2{0};

// Maximum length supported (same as MAX_DIFF = 256)
constexpr int MAX_DISTANCE = MAX_DIFF;

// Maximum marks we support
constexpr int MAX_MARKS_V2 = 24;

// =============================================================================
// GOLOMB RULER - Version avec reversed_marks (EXACTE copie de l'algo fourni)
// =============================================================================
struct GolombRulerV2 {
    std::bitset<MAX_DISTANCE> reversed_marks;  // Bit 0 = dernière marque
    std::bitset<MAX_DISTANCE> used_dist;       // Différences déjà utilisées
    int marks_count;
    int ruler_length;

    GolombRulerV2() : marks_count(0), ruler_length(0) {}

    // Nombre de marques
    int count() const { return marks_count; }

    // Longueur de la règle
    int length() const { return ruler_length; }

    // Copie des marques vers un bitset externe
    void copyMarks(std::bitset<MAX_DISTANCE>& dest) const {
        dest = reversed_marks;
    }

    // =============================================================================
    // try_add_mark - EXACTE copie de l'algorithme fourni
    // =============================================================================
    bool try_add_mark(int p, std::bitset<MAX_DISTANCE>& new_dist) {
        new_dist = reversed_marks;

        if (ruler_length < p && p < MAX_DISTANCE) {
            int p_offset = p - ruler_length;

            new_dist <<= p_offset;

            // Vérifie si aucune différence n'est déjà utilisée
            if ((new_dist & used_dist).none()) {
                // Ajoute la marque
                reversed_marks <<= p_offset;
                reversed_marks[0] = true;
                used_dist ^= new_dist;

                marks_count++;
                ruler_length = p;

                return true;
            }
        }

        return false;
    }

    // =============================================================================
    // remove_last_mark - EXACTE copie de l'algorithme fourni
    // =============================================================================
    void remove_last_mark(int old_length, std::bitset<MAX_DISTANCE>& new_dist) {
        int p_offset = ruler_length - old_length;

        reversed_marks >>= p_offset;
        used_dist ^= new_dist;

        marks_count--;
        ruler_length = old_length;
    }
};

// =============================================================================
// BORNES DE PRUNING - Mêmes que V1 pour comparaison équitable
// =============================================================================
// min_bound: borne inférieure pour la position de la prochaine marque
// Pour k marques déjà placées, la prochaine doit être au moins à length + 1
inline int min_bound(int marks_count, int ruler_length, int order) {
    (void)marks_count;
    (void)order;
    return ruler_length + 1;
}

// max_bound: borne sur les marques restantes
// r marques restantes nécessitent au minimum r*(r-1)/2 de longueur additionnelle
inline int max_bound(int marks_count, int order) {
    int r = order - marks_count - 1;  // marques restantes après celle qu'on place
    return (r * (r + 1)) / 2;
}

// =============================================================================
// CORE SOLVER - Gère l'état global de la recherche
// =============================================================================
struct CoreSolverV2 {
    int order;
    int max_length;
    int best_length;
    std::bitset<MAX_DISTANCE> best_marks;

    CoreSolverV2(int n, int maxLen)
        : order(n), max_length(maxLen + 1), best_length(maxLen + 1) {}

    int maxLength() const { return max_length; }
    int bestLength() const { return best_length; }
    const std::bitset<MAX_DISTANCE>& bestMarks() const { return best_marks; }

    void setMaxLength(int len) { max_length = len; }

    // =============================================================================
    // backtrack_seq - EXACTE copie de l'algorithme fourni (récursif)
    // =============================================================================
    void backtrack_seq(GolombRulerV2& ruler, long long& explored) {
        explored++;

        // Solution complète trouvée
        if (ruler.count() == order) {
            if (ruler.length() < max_length) {
                max_length = ruler.length();
                best_length = ruler.length();
                ruler.copyMarks(best_marks);
            }
            return;
        }

        // Calcul des bornes (mêmes que V1)
        int min_pos = min_bound(ruler.count(), ruler.length(), order);
        int max_pos = max_length - max_bound(ruler.count(), order) - 1;

        int old_len = ruler.length();
        std::bitset<MAX_DISTANCE> delta;

        for (int pos = min_pos; pos <= max_pos; ++pos) {
            if (ruler.try_add_mark(pos, delta)) {
                backtrack_seq(ruler, explored);
                ruler.remove_last_mark(old_len, delta);
            }
        }
    }
};

// Thread-local best solution
struct alignas(64) ThreadBestV2 {
    int bestLen;
    int bestMarks[MAX_MARKS_V2];
    int bestNumMarks;
    std::bitset<MAX_DISTANCE> bestMarksBitset;
};

// =============================================================================
// EXTRACTION DES MARQUES DEPUIS reversed_marks
// =============================================================================
static void extractMarksV2(const std::bitset<MAX_DISTANCE>& reversed_marks,
                           int ruler_length, int* marks, int& numMarks) {
    numMarks = 0;
    for (int i = 0; i <= ruler_length; ++i) {
        if (reversed_marks[ruler_length - i]) {
            marks[numMarks++] = i;
        }
    }
}

// Forward declaration
static void backtrackWithGlobalSync(
    CoreSolverV2& solver,
    GolombRulerV2& ruler,
    std::atomic<int>& globalBestLen,
    ThreadBestV2& threadBest,
    long long& explored);

// =============================================================================
// MAIN SEARCH FUNCTION - VERSION 2 (parallélisation style v1)
// =============================================================================
void searchGolombV2(int n, int maxLen, GolombRuler& best)
{
    exploredCountV2.store(0, std::memory_order_relaxed);

    std::atomic<int> globalBestLen(maxLen + 1);

    int finalBestLen = maxLen + 1;
    int finalBestMarks[MAX_MARKS_V2] = {0};
    int finalBestNumMarks = 0;

    #pragma omp parallel shared(globalBestLen, finalBestLen, finalBestMarks, finalBestNumMarks)
    {
        ThreadBestV2 threadBest{};
        threadBest.bestLen = maxLen + 1;
        threadBest.bestNumMarks = 0;
        long long threadExplored = 0;

        #pragma omp for schedule(dynamic, 1)
        for (int firstMark = 1; firstMark <= maxLen; ++firstMark) {
            const int currentGlobal = globalBestLen.load(std::memory_order_acquire);
            if (firstMark >= currentGlobal) {
                continue;
            }

            // Créer un solver local avec la borne globale actuelle
            CoreSolverV2 solver(n, currentGlobal - 1);

            // Setup initial ruler: marks at 0 and firstMark
            GolombRulerV2 ruler;
            ruler.reversed_marks[0] = true;  // Mark at 0
            ruler.marks_count = 1;
            ruler.ruler_length = 0;

            // Add first mark
            std::bitset<MAX_DISTANCE> delta;
            if (!ruler.try_add_mark(firstMark, delta)) {
                continue;  // Ne devrait jamais arriver
            }

            // Backtrack from this state
            backtrackWithGlobalSync(solver, ruler, globalBestLen, threadBest, threadExplored);
        }

        exploredCountV2.fetch_add(threadExplored, std::memory_order_relaxed);

        if (threadBest.bestNumMarks > 0) {
            #pragma omp critical(merge_best_v2)
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
}

// =============================================================================
// BACKTRACK WITH GLOBAL SYNC - Synchronise avec globalBestLen
// =============================================================================
static void backtrackWithGlobalSync(
    CoreSolverV2& solver,
    GolombRulerV2& ruler,
    std::atomic<int>& globalBestLen,
    ThreadBestV2& threadBest,
    long long& explored)
{
    explored++;

    // Solution complète trouvée
    if (ruler.count() == solver.order) {
        if (ruler.length() < solver.max_length) {
            solver.max_length = ruler.length();
            solver.best_length = ruler.length();
            ruler.copyMarks(solver.best_marks);

            // Mise à jour thread best
            if (ruler.length() < threadBest.bestLen) {
                threadBest.bestLen = ruler.length();
                threadBest.bestMarksBitset = solver.best_marks;
                extractMarksV2(solver.best_marks, ruler.length(),
                              threadBest.bestMarks, threadBest.bestNumMarks);
            }

            // Update global best atomically
            int expected = globalBestLen.load(std::memory_order_relaxed);
            while (ruler.length() < expected &&
                   !globalBestLen.compare_exchange_weak(expected, ruler.length(),
                       std::memory_order_release, std::memory_order_relaxed)) {
            }
        }
        return;
    }

    // Re-sync avec global best
    const int currentGlobal = globalBestLen.load(std::memory_order_relaxed);
    if (currentGlobal < solver.max_length) {
        solver.max_length = currentGlobal;
    }

    // Calcul des bornes (mêmes que V1)
    int min_pos = min_bound(ruler.count(), ruler.length(), solver.order);
    int max_pos = solver.max_length - max_bound(ruler.count(), solver.order) - 1;

    int old_len = ruler.length();
    std::bitset<MAX_DISTANCE> delta;

    for (int pos = min_pos; pos <= max_pos; ++pos) {
        if (ruler.try_add_mark(pos, delta)) {
            backtrackWithGlobalSync(solver, ruler, globalBestLen, threadBest, explored);
            ruler.remove_last_mark(old_len, delta);
        }
    }
}

long long getExploredCountV2()
{
    return exploredCountV2.load(std::memory_order_relaxed);
}
