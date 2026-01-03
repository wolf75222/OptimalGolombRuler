#include "search.hpp"
#include "benchmark_log.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <omp.h>

// Description des changements pour ce benchmark
// Modifier cette variable avant chaque run pour documenter les modifications
const std::string CHANGES = "";

// Configuration DEV vs PROD
// DEV  : tailles réduites pour tests rapides sur Windows
// PROD : configuration complète pour cluster HPC/Slurm
#ifdef DEV_MODE
    const std::vector<int> DEFAULT_SIZES = { 6, 7, 8 };
    const int DEFAULT_MAX_LEN = 100;
    const std::vector<int> DEFAULT_THREADS = { 1, 2 };
    const char* MODE_NAME = "DEV";
#else
    const std::vector<int> DEFAULT_SIZES = { 8, 9, 10, 11 };
    const int DEFAULT_MAX_LEN = 200;
    const std::vector<int> DEFAULT_THREADS = { 1, 2, 4, 8, 12, 16, 20 };
    const char* MODE_NAME = "PROD";
#endif

int main() {
    std::cout << "=== Optimal Golomb Ruler Benchmark (OpenMP) ===\n";
    std::cout << "Mode: " << MODE_NAME << "\n";

    std::vector<int> sizes = DEFAULT_SIZES;
    const int maxLen = DEFAULT_MAX_LEN;

    int maxThreads = omp_get_max_threads();
    std::vector<int> threadsToTest = DEFAULT_THREADS;

    // Logger CSV
    BenchmarkLog logger("benchmarks", "openmp");

    if (!CHANGES.empty()) {
        std::cout << "Changes: " << CHANGES << "\n";
    }

    for (int n : sizes) {
        std::cout << "\n>>> Testing n = " << n << " (max threads = " << maxThreads << ")\n";
        std::cout << std::setw(10) << "Threads"
            << std::setw(10) << "Length"
            << std::setw(15) << "Time (s)"
            << std::setw(15) << "Speedup"
            << std::setw(15) << "Efficiency (%)"
            << std::setw(20) << "Explored States" << std::endl;
        std::cout << std::string(85, '-') << std::endl;

        double baseTime = 0.0;

        for (int t : threadsToTest) {
            if (t > maxThreads) break;
            omp_set_num_threads(t);

            GolombRuler best;
            auto start = std::chrono::high_resolution_clock::now();

            searchGolomb(n, maxLen, best);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            double time = elapsed.count();
            if (t == 1) baseTime = time;

            double speedup = baseTime / time;
            double efficiency = (speedup / t) * 100.0;
            long long states = getExploredCount();

            std::cout << std::setw(10) << t
                << std::setw(10) << best.length
                << std::setw(15) << std::fixed << std::setprecision(5) << time
                << std::setw(15) << std::setprecision(2) << speedup
                << std::setw(15) << std::setprecision(1) << efficiency
                << std::setw(20) << states
                << std::endl;

            // Log to CSV
            logger.logOpenMP(n, t, best.length, time, speedup, efficiency, states, CHANGES);
        }
    }

    std::cout << "\n[Results saved to benchmarks/openmp_benchmark.csv]\n";

    return 0;
}
