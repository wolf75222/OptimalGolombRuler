#include "search_v4.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <omp.h>

// =============================================================================
// OPENMP VERSION 4 (prefix-based + iterative + bitset shift) - Single n mode
// =============================================================================

const int DEFAULT_MAX_LEN = 200;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <n> [prefix_depth]\n";
        std::cerr << "  n: number of marks (2-24)\n";
        std::cerr << "  prefix_depth: depth for prefix generation (0 = auto)\n";
        return 1;
    }

    int n = std::atoi(argv[1]);
    if (n < 2 || n > 24) {
        std::cerr << "ERROR: n must be between 2 and 24\n";
        return 1;
    }

    int prefixDepth = 0;  // Auto
    if (argc > 2) {
        prefixDepth = std::atoi(argv[2]);
    }

    int numThreads = omp_get_max_threads();

    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - OPENMP V4 (n=" << n << ")\n";
    std::cout << "=============================================================\n";
    std::cout << "Algorithm: Prefix-based + iterative + bitset shift\n";
    std::cout << "Threads: " << numThreads << "\n";
    if (prefixDepth > 0) {
        std::cout << "Prefix depth: " << prefixDepth << " (manual)\n\n";
    } else {
        std::cout << "Prefix depth: auto\n\n";
    }

    GolombRuler result;

    auto start = std::chrono::high_resolution_clock::now();
    searchGolombV4(n, DEFAULT_MAX_LEN, result, prefixDepth);
    auto end = std::chrono::high_resolution_clock::now();

    double time = std::chrono::duration<double>(end - start).count();
    long long states = getExploredCountV4();
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

    return 0;
}
