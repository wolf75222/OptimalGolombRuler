#include <iostream>
#include <iomanip>
#include <chrono>
#include <omp.h>
#include "search_v6.hpp"

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <n> [prefix_depth]" << std::endl;
        std::cerr << "  n            : number of marks (e.g., 10, 11, 12, 13)" << std::endl;
        std::cerr << "  prefix_depth : optional prefix depth (default: auto)" << std::endl;
        return 1;
    }

    int n = std::atoi(argv[1]);
    if (n < 2 || n > 20) {
        std::cerr << "Error: n must be between 2 and 20" << std::endl;
        return 1;
    }

    int prefixDepth = 0;
    if (argc >= 3) {
        prefixDepth = std::atoi(argv[2]);
    }

    int knownOptimal[] = {0, 0, 1, 3, 6, 11, 17, 25, 34, 44, 55, 72, 85, 106, 127};
    int maxLen = (n <= 14) ? knownOptimal[n] : (n * n);

    int numThreads = omp_get_max_threads();

    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - OPENMP V6 (n=" << n << ")\n";
    std::cout << "=============================================================\n";
    std::cout << "Algorithm: SIMD __m128i + prefix-based + iterative\n";
    std::cout << "Threads: " << numThreads << "\n";
    std::cout << "Prefix depth: " << (prefixDepth > 0 ? std::to_string(prefixDepth) : "auto") << "\n";
    std::cout << std::endl;

    GolombRuler best;

    auto start = std::chrono::high_resolution_clock::now();
    searchGolombV6(n, maxLen, best, prefixDepth);
    auto end = std::chrono::high_resolution_clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();
    long long explored = getExploredCountV6();

    std::cout << "n          : " << n << "\n";
    std::cout << "Length     : " << best.length << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Time       : " << elapsed << " s\n";
    std::cout << "States     : " << explored << "\n";
    std::cout << std::scientific << std::setprecision(2);
    std::cout << "States/sec : " << (explored / elapsed) << "\n";

    bool valid = GolombRuler::isValid(best.marks);
    std::cout << "Valid      : " << (valid ? "YES" : "NO") << "\n";

    std::cout << "\nRuler: { ";
    for (size_t i = 0; i < best.marks.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << best.marks[i];
    }
    std::cout << " }\n";
    std::cout << "=============================================================\n";

    return valid ? 0 : 1;
}
