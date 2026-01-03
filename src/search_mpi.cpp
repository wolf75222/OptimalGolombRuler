#include "search_mpi.hpp"
#include <atomic>
#include <limits>
#include <algorithm>
#include <omp.h>
#include <mpi.h>

static std::atomic<long long> exploredCountMPI = 0;

void backtrackMPI(std::vector<int>& current,
                  std::bitset<MAX_DIFF>& usedDiffs,
                  int n,
                  int& bestLen,
                  std::vector<int>& bestMarks,
                  std::atomic<int>& globalBestLen)
{
    exploredCountMPI++;

    if (current.size() == static_cast<size_t>(n)) {
        int newLen = current.back();
        bestLen = newLen;
        bestMarks = current;

        int expected = globalBestLen.load(std::memory_order_relaxed);
        while (newLen < expected &&
               !globalBestLen.compare_exchange_weak(expected, newLen,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)) {
        }
        return;
    }

    const int start = current.back() + 1;

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
            backtrackMPI(current, usedDiffs, n, bestLen, bestMarks, globalBestLen);

        current.pop_back();
        for (int d : newDiffs) usedDiffs.reset(d);
    }
}

void searchGolombMPI(int n, int maxLen, GolombRuler& best, HypercubeMPI& hypercube)
{
    exploredCountMPI = 0;

    const int rank = hypercube.rank();
    const int size = hypercube.size();

    std::vector<int> base;
    base.reserve(n);
    base.push_back(0);

    int bestLen = maxLen;
    std::vector<int> bestMarks;

    std::atomic<int> globalBestLen(maxLen);

    #pragma omp parallel
    {
        std::bitset<MAX_DIFF> usedDiffsLocal;
        std::vector<int> currentLocal = base;
        std::vector<int> localBestMarks;
        int localBestLen = maxLen;

        // Partitionnement cyclique : chaque rang MPI prend next = rank+1, rank+1+size, rank+1+2*size, ...
        #pragma omp for schedule(dynamic, 1)
        for (int i = 0; i < maxLen; ++i) {
            int next = 1 + rank + i * size;
            if (next > maxLen) continue;

            // Pruning précoce
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

            backtrackMPI(currentLocal, usedDiffsLocal, n, localBestLen,
                        localBestMarks, globalBestLen);

            currentLocal.pop_back();
            for (int d : newDiffs) usedDiffsLocal.reset(d);
        }

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

    // Phase 2: Réduction hypercube pour trouver le minimum global
    int globalMin = bestLen;

    for (int d = 0; d < hypercube.dimensions(); ++d) {
        int partner = hypercube.neighbor(d);
        int recvMin;

        MPI_Sendrecv(&globalMin, 1, MPI_INT, partner, 0,
                    &recvMin, 1, MPI_INT, partner, 0,
                    MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (recvMin < globalMin) {
            globalMin = recvMin;
        }
    }

    // Phase 3: Le processus avec le meilleur résultat broadcast la solution
    int hasGlobalMin = (bestLen == globalMin) ? rank : size;
    int minRank;
    MPI_Allreduce(&hasGlobalMin, &minRank, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (minRank < size) {
        int numMarks = (rank == minRank) ? static_cast<int>(bestMarks.size()) : 0;
        MPI_Bcast(&numMarks, 1, MPI_INT, minRank, MPI_COMM_WORLD);

        if (rank != minRank) {
            bestMarks.resize(numMarks);
        }

        MPI_Bcast(bestMarks.data(), numMarks, MPI_INT, minRank, MPI_COMM_WORLD);
    }

    best.marks = bestMarks;
    best.computeLength();
}

long long getExploredCountMPI()
{
    long long localCount = exploredCountMPI.load();
    long long globalCount = 0;

    MPI_Reduce(&localCount, &globalCount, 1, MPI_LONG_LONG,
               MPI_SUM, 0, MPI_COMM_WORLD);

    return globalCount;
}
