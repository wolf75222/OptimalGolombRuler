#include "search.hpp"
#include "search_v2.hpp"
#include "search_v3.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <fstream>
#include <omp.h>

// =============================================================================
// BENCHMARK COMPARATIF: V1 vs V2 vs V3
// =============================================================================

const int DEFAULT_MAX_LEN = 200;

struct BenchmarkResult {
    int n;
    int threads;
    double time_v1;
    double time_v2;
    double time_v3;
    int length_v1;
    int length_v2;
    int length_v3;
    long long states_v1;
    long long states_v2;
    long long states_v3;
    bool valid;
};

void printHeader() {
    std::cout << std::setw(4) << "n"
              << std::setw(6) << "Thr"
              << std::setw(10) << "V1 (s)"
              << std::setw(10) << "V2 (s)"
              << std::setw(10) << "V3 (s)"
              << std::setw(8) << "V3/V1"
              << std::setw(8) << "V3/V2"
              << std::setw(6) << "Len"
              << std::setw(12) << "States V1"
              << std::setw(12) << "States V3"
              << std::setw(6) << "OK?"
              << std::endl;
    std::cout << std::string(92, '-') << std::endl;
}

BenchmarkResult runBenchmark(int n, int threads) {
    BenchmarkResult result;
    result.n = n;
    result.threads = threads;

    omp_set_num_threads(threads);

    // Version 1 (original)
    {
        GolombRuler best;
        auto start = std::chrono::high_resolution_clock::now();
        searchGolomb(n, DEFAULT_MAX_LEN, best);
        auto end = std::chrono::high_resolution_clock::now();

        result.time_v1 = std::chrono::duration<double>(end - start).count();
        result.length_v1 = best.length;
        result.states_v1 = getExploredCount();
    }

    // Version 2 (bitset shift récursif)
    {
        GolombRuler best;
        auto start = std::chrono::high_resolution_clock::now();
        searchGolombV2(n, DEFAULT_MAX_LEN, best);
        auto end = std::chrono::high_resolution_clock::now();

        result.time_v2 = std::chrono::duration<double>(end - start).count();
        result.length_v2 = best.length;
        result.states_v2 = getExploredCountV2();
    }

    // Version 3 (hybrid: itératif + bitset shift)
    {
        GolombRuler best;
        auto start = std::chrono::high_resolution_clock::now();
        searchGolombV3(n, DEFAULT_MAX_LEN, best);
        auto end = std::chrono::high_resolution_clock::now();

        result.time_v3 = std::chrono::duration<double>(end - start).count();
        result.length_v3 = best.length;
        result.states_v3 = getExploredCountV3();
    }

    result.valid = (result.length_v1 == result.length_v2) &&
                   (result.length_v2 == result.length_v3);

    return result;
}

void printResult(const BenchmarkResult& r) {
    double speedup_v3_v1 = r.time_v1 / r.time_v3;
    double speedup_v3_v2 = r.time_v2 / r.time_v3;

    std::cout << std::setw(4) << r.n
              << std::setw(6) << r.threads
              << std::setw(10) << std::fixed << std::setprecision(4) << r.time_v1
              << std::setw(10) << std::setprecision(4) << r.time_v2
              << std::setw(10) << std::setprecision(4) << r.time_v3
              << std::setw(7) << std::setprecision(2) << speedup_v3_v1 << "x"
              << std::setw(7) << std::setprecision(2) << speedup_v3_v2 << "x"
              << std::setw(6) << r.length_v3
              << std::setw(12) << r.states_v1
              << std::setw(12) << r.states_v3
              << std::setw(6) << (r.valid ? "OK" : "FAIL")
              << std::endl;
}

void saveResultsCSV(const std::vector<BenchmarkResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    file << "n,threads,time_v1,time_v2,time_v3,speedup_v3_v1,speedup_v3_v2,length,states_v1,states_v2,states_v3,valid\n";

    for (const auto& r : results) {
        double speedup_v3_v1 = r.time_v1 / r.time_v3;
        double speedup_v3_v2 = r.time_v2 / r.time_v3;

        file << r.n << ","
             << r.threads << ","
             << std::fixed << std::setprecision(5) << r.time_v1 << ","
             << r.time_v2 << ","
             << r.time_v3 << ","
             << std::setprecision(3) << speedup_v3_v1 << ","
             << speedup_v3_v2 << ","
             << r.length_v3 << ","
             << r.states_v1 << ","
             << r.states_v2 << ","
             << r.states_v3 << ","
             << (r.valid ? "OK" : "FAIL") << "\n";
    }

    std::cout << "\n[Results saved to " << filename << "]\n";
}

int main(int argc, char** argv) {
    std::cout << "=============================================================\n";
    std::cout << "   GOLOMB RULER BENCHMARK: V1 vs V2 vs V3 (hybrid)\n";
    std::cout << "=============================================================\n";
    std::cout << "V1: Original (iterative + loop unrolling)\n";
    std::cout << "V2: Bitset shift (recursive)\n";
    std::cout << "V3: Hybrid (iterative + bitset shift)\n";
    std::cout << "=============================================================\n\n";

    int maxThreads = omp_get_max_threads();
    std::cout << "Max threads available: " << maxThreads << "\n";
    std::cout << "Max length: " << DEFAULT_MAX_LEN << "\n\n";

    std::vector<int> sizes = {10, 11, 12};
    std::vector<int> threadCounts = {1};

    // Parse arguments for custom config
    if (argc > 1) {
        sizes.clear();
        for (int i = 1; i < argc; ++i) {
            int n = std::atoi(argv[i]);
            if (n >= 2 && n <= 20) {
                sizes.push_back(n);
            }
        }
    }

    std::vector<BenchmarkResult> allResults;

    printHeader();

    for (int n : sizes) {
        for (int t : threadCounts) {
            if (t > maxThreads) continue;

            BenchmarkResult result = runBenchmark(n, t);
            allResults.push_back(result);
            printResult(result);
        }
    }

    // Multi-threaded comparison for largest n
    if (!sizes.empty()) {
        int largestN = sizes.back();
        std::cout << "\n--- Multi-threaded benchmark for n=" << largestN << " ---\n";
        printHeader();

        std::vector<int> multiThreads = {1, 2, 4, 8};
        for (int t : multiThreads) {
            if (t > maxThreads) continue;
            BenchmarkResult result = runBenchmark(largestN, t);
            allResults.push_back(result);
            printResult(result);
        }
    }

    // Save to CSV
    saveResultsCSV(allResults, "benchmarks/compare_v1_v2_v3.csv");

    return 0;
}
