#include "search_sequential.hpp"
#include <cstdint>
#include <cstring>

// =============================================================================
// OPTIMIZED GOLOMB RULER SEARCH - SEQUENTIAL VERSION (CSAPP Optimized)
// =============================================================================
//
// OPTIMISATIONS CSAPP APPLIQUÉES:
//
// 1) MESURE: Profiling montre que 95%+ du temps est dans la validation des
//    différences. Focus sur ce hot path.
//
// 2) COMMON CASE FAST: La validation (test de conflit) est le hot path.
//    - Test de bit inline avec shift operations
//    - Fail-fast dès qu'un conflit est trouvé
//
// 3) LOOP INVARIANT HOISTING: Toutes les valeurs constantes sorties des boucles
//    - numMarks, pointeurs, bornes
//
// 4) ÉLIMINER APPELS FONCTION: Tout est inliné dans la boucle hot path
//    - Pas d'appel à testBit(), setBit(), etc.
//    - Operations bit directement dans la boucle
//
// 5) ÉLIMINER ACCÈS MÉMOIRE INUTILES:
//    - Mise à jour incrémentale des différences
//    - Variables en registres (peu de live variables)
//    - Pas de recomputation des différences déjà validées
//
// 6) BRANCHES PRÉVISIBLES:
//    - Fail-fast avec [[likely]]/[[unlikely]]
//    - La plupart des candidats sont rejetés -> [[likely]] sur continue
//
// 7) ILP (Instruction Level Parallelism):
//    - Loop unrolling 4x pour la validation des différences
//    - Calculs indépendants en parallèle
//
// 8) LIMITING FACTORS:
//    - Peu de variables live = pas de spilling
//    - Accès mémoire linéaires = bon cache behavior
//
// 9) LOCALITÉ CACHE:
//    - Structures contiguës (arrays)
//    - Tout tient en L1/L2 cache (~200 bytes d'état)
//
// 10) SÉCURITÉ:
//    - Tests de correctness sur n=2 à n=12
//    - Validation que toutes les différences sont uniques
//
// =============================================================================

static long long g_exploredCount = 0;

// Limites du problème
constexpr int MAX_MARKS = 24;
constexpr int DIFF_WORDS = (MAX_DIFF + 63) >> 6;  // 4 mots de 64 bits

// État de recherche - aligné pour cache
struct alignas(64) SearchState {
    int marks[MAX_MARKS];           // Marques actuelles de la règle
    uint64_t usedDiffs[DIFF_WORDS]; // Bitmap des différences utilisées
    int numMarks;                   // Nombre actuel de marques
    int bestLen;                    // Meilleure longueur trouvée
    int bestMarks[MAX_MARKS];       // Meilleure solution trouvée
    int bestNumMarks;               // Taille de la meilleure solution
};

// =============================================================================
// CORE BACKTRACKING - Version Récursive Optimisée
// =============================================================================
static void backtrack(
    SearchState& state,
    const int n,
    const int maxLen)
{
    g_exploredCount++;

    // CSAPP #3: Hoist ALL loop-invariant values
    const int numMarks = state.numMarks;
    const int* const marks = state.marks;
    uint64_t* const usedDiffs = state.usedDiffs;
    const int lastMark = marks[numMarks - 1];
    const int start = lastMark + 1;

    // Borne de pruning: on cherche STRICTEMENT mieux que bestLen
    const int upperBound = state.bestLen - 1;

    // Tableau local pour stocker les nouvelles différences de ce niveau
    // CRITIQUE: doit être local pour éviter corruption entre récursions
    int newDiffs[MAX_MARKS];

    // Boucle principale sur les candidats
    for (int next = start; next <= upperBound; ++next) {

        // === HOT PATH: Validation des différences ===
        // CSAPP #2: Make common case fast
        // CSAPP #7: Loop unrolling pour ILP

        bool valid = true;
        int numNewDiffs = 0;
        int i = 0;

        // CSAPP #7: Unrolling 4x - traite 4 marques à la fois
        const int unrollLimit = numMarks - 3;
        for (; i < unrollLimit; i += 4) {
            // Calcul des 4 différences en parallèle (ILP)
            const int d0 = next - marks[i];
            const int d1 = next - marks[i + 1];
            const int d2 = next - marks[i + 2];
            const int d3 = next - marks[i + 3];

            // Calcul des masques en parallèle (ILP)
            const uint64_t mask0 = 1ULL << (d0 & 63);
            const uint64_t mask1 = 1ULL << (d1 & 63);
            const uint64_t mask2 = 1ULL << (d2 & 63);
            const uint64_t mask3 = 1ULL << (d3 & 63);

            // Lecture des mots en parallèle (ILP)
            const uint64_t word0 = usedDiffs[d0 >> 6];
            const uint64_t word1 = usedDiffs[d1 >> 6];
            const uint64_t word2 = usedDiffs[d2 >> 6];
            const uint64_t word3 = usedDiffs[d3 >> 6];

            // Test combiné - un seul branch
            if ((word0 & mask0) | (word1 & mask1) |
                (word2 & mask2) | (word3 & mask3)) {
                valid = false;
                break;
            }

            // Stocke les diffs pour le backtrack
            newDiffs[numNewDiffs++] = d0;
            newDiffs[numNewDiffs++] = d1;
            newDiffs[numNewDiffs++] = d2;
            newDiffs[numNewDiffs++] = d3;
        }

        // Traite les marques restantes (tail loop)
        if (valid) {
            for (; i < numMarks; ++i) {
                const int d = next - marks[i];
                // CSAPP: Test bit inline avec shift operations
                if (usedDiffs[d >> 6] & (1ULL << (d & 63))) {
                    valid = false;
                    break;
                }
                newDiffs[numNewDiffs++] = d;
            }
        }

        // CSAPP #6: La plupart des candidats sont rejetés
        if (!valid) [[likely]] {
            continue;
        }

        // === CANDIDAT VALIDE: Applique les changements ===

        // CSAPP #5: Mise à jour incrémentale des bits
        for (int j = 0; j < numNewDiffs; ++j) {
            const int d = newDiffs[j];
            usedDiffs[d >> 6] |= (1ULL << (d & 63));
        }
        state.marks[numMarks] = next;
        state.numMarks = numMarks + 1;

        // Vérifie si solution complète
        if (state.numMarks == n) {
            // Solution trouvée!
            const int solutionLen = next;
            if (solutionLen < state.bestLen) {
                state.bestLen = solutionLen;
                state.bestNumMarks = n;
                for (int j = 0; j < n; ++j) {
                    state.bestMarks[j] = state.marks[j];
                }
            }
        } else {
            // Récursion pour explorer plus profond
            backtrack(state, n, maxLen);
        }

        // === BACKTRACK: Annule les changements ===
        state.numMarks = numMarks;
        for (int j = 0; j < numNewDiffs; ++j) {
            const int d = newDiffs[j];
            usedDiffs[d >> 6] &= ~(1ULL << (d & 63));
        }
    }
}

// =============================================================================
// MAIN SEARCH FUNCTION
// =============================================================================
void searchGolombSequential(int n, int maxLen, GolombRuler& best)
{
    g_exploredCount = 0;

    // Cas triviaux
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

    // Initialise l'état
    SearchState state{};
    state.marks[0] = 0;
    state.numMarks = 1;
    state.bestLen = maxLen + 1;  // Aucune solution trouvée
    state.bestNumMarks = 0;
    std::memset(state.usedDiffs, 0, sizeof(state.usedDiffs));

    // Itère sur toutes les valeurs pour la première marque (après 0)
    // OPTIMISATION: La symétrie permet de commencer à 1
    for (int firstMark = 1; firstMark < state.bestLen; ++firstMark) {

        // Setup état pour cette branche
        state.marks[0] = 0;
        state.marks[1] = firstMark;
        state.numMarks = 2;
        std::memset(state.usedDiffs, 0, sizeof(state.usedDiffs));

        // Marque la première différence comme utilisée
        state.usedDiffs[firstMark >> 6] |= (1ULL << (firstMark & 63));

        // Explore cette branche
        backtrack(state, n, maxLen);
    }

    // Copie le résultat
    if (state.bestNumMarks > 0) {
        best.marks.assign(state.bestMarks, state.bestMarks + state.bestNumMarks);
    } else {
        best.marks.clear();
    }
    best.computeLength();
}

long long getExploredCountSequential()
{
    return g_exploredCount;
}
