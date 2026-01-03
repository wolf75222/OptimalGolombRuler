#include "search.hpp"
#include <atomic>
#include <limits>
#include <algorithm>
#include <omp.h>

static std::atomic<long long> exploredCount = 0;

// Backtracking avec pruning global partagé entre threads
void backtrack(std::vector<int>& current,
               std::bitset<MAX_DIFF>& usedDiffs,
               int n,
               int& bestLen,
               std::vector<int>& bestMarks,
               std::atomic<int>& globalBestLen)
{
    exploredCount++;

    if (current.size() == static_cast<size_t>(n)) {
        int newLen = current.back();
        bestLen = newLen;
        bestMarks = current;

        // Mise à jour atomique du meilleur global (CAS loop)
        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (newLen < expected &&
               !globalBestLen.compare_exchange_weak(expected, newLen,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)) {
        }
        return;
    }

    const int start = current.back() + 1;

    // Lecture du meilleur global pour pruning agressif
    int currentGlobalBest = globalBestLen.load(std::memory_order_acquire);
    int effectiveBestLen = std::min(bestLen, currentGlobalBest);

    for (int next = start; next <= std::min(effectiveBestLen, MAX_DIFF - 1); ++next) {
        bool valid = true;
        std::vector<int> newDiffs;
        newDiffs.reserve(current.size());

        for (int m : current) {
            int d = next - m;
            if (d >= MAX_DIFF || usedDiffs[d]) { valid = false; break; }
            newDiffs.push_back(d);
        }

        if (!valid) continue;

        for (int d : newDiffs) usedDiffs.set(d);
        current.push_back(next);

        if (next < effectiveBestLen)
            backtrack(current, usedDiffs, n, bestLen, bestMarks, globalBestLen);

        current.pop_back();
        for (int d : newDiffs) usedDiffs.reset(d);
    }
}

void searchGolomb(int n, int maxLen, GolombRuler& best)
{
    exploredCount = 0;

    std::vector<int> base;
    base.reserve(n);
    base.push_back(0);

    int bestLen = maxLen;
    std::vector<int> bestMarks;

    // Variable atomique partagée entre tous les threads pour pruning global
    std::atomic<int> globalBestLen(maxLen);

    #pragma omp parallel
    {
        std::bitset<MAX_DIFF> usedDiffsLocal;
        std::vector<int> currentLocal = base;
        std::vector<int> localBestMarks;
        int localBestLen = maxLen;

        #pragma omp for schedule(dynamic, 1)
        for (int next = 1; next <= maxLen; ++next) {
            // Pruning précoce basé sur le meilleur global
            if (next >= globalBestLen.load(std::memory_order_acquire)) continue;

            bool valid = true;
            std::vector<int> newDiffs;
            for (int m : base) {
                int d = next - m;
                if (d >= MAX_DIFF || usedDiffsLocal[d]) { valid = false; break; }
                newDiffs.push_back(d);
            }

            if (!valid) continue;

            for (int d : newDiffs) usedDiffsLocal.set(d);
            currentLocal.push_back(next);

            backtrack(currentLocal, usedDiffsLocal, n, localBestLen, localBestMarks, globalBestLen);

            currentLocal.pop_back();
            for (int d : newDiffs) usedDiffsLocal.reset(d);
        }

        // Fusion des résultats locaux
        if (localBestLen < bestLen) {
            #pragma omp critical
            {
                if (localBestLen < bestLen) {
                    bestLen = localBestLen;
                    bestMarks = localBestMarks;
                }
            }
        }
    }

    best.marks = bestMarks;
    best.computeLength();
}

long long getExploredCount()
{
    return exploredCount.load();
}
