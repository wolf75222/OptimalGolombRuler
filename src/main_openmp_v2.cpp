#include "search_v2.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <omp.h>

// =============================================================================
// OPENMP VERSION 2 (bitset shift algorithm) - Single n mode
// =============================================================================

const int DEFAULT_MAX_LEN = 200;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <n>\n";
        std::cerr << "  n: number of marks (2-24)\n";
        return 1;
    }

    int n = std::atoi(argv[1]);
    if (n < 2 || n > 24) {
        std::cerr << "ERROR: n must be between 2 and 24\n";
        return 1;
    }

    int numThreads = omp_get_max_threads();

    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - OPENMP V2 (n=" << n << ")\n";
    std::cout << "=============================================================\n";
    std::cout << "Algorithm: Bitset shift (reversed_marks)\n";
    std::cout << "Threads: " << numThreads << "\n\n";

    GolombRuler result;

    auto start = std::chrono::high_resolution_clock::now();
    searchGolombV2(n, DEFAULT_MAX_LEN, result);
    auto end = std::chrono::high_resolution_clock::now();

    double time = std::chrono::duration<double>(end - start).count();
    long long states = getExploredCountV2();
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
