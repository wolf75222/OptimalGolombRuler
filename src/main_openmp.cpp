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
    const std::vector<int> DEFAULT_SIZES = { 9, 10 };  // Quick test
    const int DEFAULT_MAX_LEN = MAX_DIFF - 1;       // 255 - maximal search space
    const std::vector<int> DEFAULT_THREADS = { 1, 2, 4, 8 };
    const char* MODE_NAME = "DEV";
#else
    const std::vector<int> DEFAULT_SIZES = { 10, 11, 12 };
    const int DEFAULT_MAX_LEN = 200;
    const std::vector<int> DEFAULT_THREADS = { 1, 2, 4, 8, 16 };
    const char* MODE_NAME = "PROD";
#endif

// =============================================================================
// SINGLE N MODE - Run for a specific n passed as argument
// =============================================================================
void runSingleN(int n) {
    int numThreads = omp_get_max_threads();

    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - OPENMP (n=" << n << ")\n";
    std::cout << "=============================================================\n";
    std::cout << "Threads: " << numThreads << "\n\n";

    GolombRuler result;

    auto start = std::chrono::high_resolution_clock::now();
    searchGolomb(n, DEFAULT_MAX_LEN, result);
    auto end = std::chrono::high_resolution_clock::now();

    double time = std::chrono::duration<double>(end - start).count();
    long long states = getExploredCount();
    double statesPerSec = states / time;
    bool valid = result.marks.empty() ? true : GolombRuler::isValid(result.marks);

    std::cout << "n          : " << n << "\n";
    std::cout << "Length     : " << result.length << "\n";
    std::cout << "Time       : " << std::fixed << std::setprecision(3) << time << " s\n";
    std::cout << "States     : " << states << "\n";
    std::cout << "States/sec : " << std::scientific << std::setprecision(2) << statesPerSec << "\n";
    std::cout << "Valid      : " << (valid ? "YES" : "NO") << "\n";
    std::cout << "\nRuler: { ";
    for (size_t i = 0; i < result.marks.size(); ++i) {
        std::cout << result.marks[i];
        if (i < result.marks.size() - 1) std::cout << ", ";
    }
    std::cout << " }\n";
    std::cout << "=============================================================\n";
}

int main(int argc, char** argv) {
    // If argument provided, run single n mode
    if (argc > 1) {
        int n = std::atoi(argv[1]);
        if (n < 2 || n > 24) {
            std::cerr << "ERROR: n must be between 2 and 24\n";
            return 1;
        }
        runSingleN(n);
        return 0;
    }

    // Default: run full benchmark
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

            // Verify solution is valid
            bool valid = best.marks.empty() ? true : GolombRuler::isValid(best.marks);

            std::cout << std::setw(10) << t
                << std::setw(10) << best.length
                << std::setw(15) << std::fixed << std::setprecision(5) << time
                << std::setw(15) << std::setprecision(2) << speedup
                << std::setw(15) << std::setprecision(1) << efficiency
                << std::setw(20) << states;
            if (!valid) std::cout << " INVALID!";
            std::cout << std::endl;

            // Log to CSV
            logger.logOpenMP(n, t, best.length, time, speedup, efficiency, states, CHANGES);
        }
    }

    std::cout << "\n[Results saved to benchmarks/openmp_benchmark.csv]\n";

    return 0;
}
