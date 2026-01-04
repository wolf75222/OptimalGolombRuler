#include "search_sequential.hpp"
#include "benchmark_log.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cmath>
#include <cassert>

// =============================================================================
// BENCHMARK SÉQUENTIEL - Tests de correctness et performance
// =============================================================================
// Ce benchmark:
// 1. Vérifie la correctness sur petites instances (n=2 à 9)
// 2. Mesure les performances sur n=10, 11, 12
// 3. Compare avec les solutions optimales connues
// =============================================================================

// Solutions optimales connues (pour validation)
// Source: https://oeis.org/A003022
struct KnownOptimal {
    int n;
    int length;
    std::vector<int> ruler;
};

const std::vector<KnownOptimal> KNOWN_OPTIMALS = {
    {2, 1, {0, 1}},
    {3, 3, {0, 1, 3}},
    {4, 6, {0, 1, 4, 6}},
    {5, 11, {0, 1, 4, 9, 11}},
    {6, 17, {0, 1, 4, 10, 12, 17}},
    {7, 25, {0, 1, 4, 10, 18, 23, 25}},
    {8, 34, {0, 1, 4, 9, 15, 22, 32, 34}},
    {9, 44, {0, 1, 5, 12, 25, 27, 35, 41, 44}},
    {10, 55, {0, 1, 6, 10, 23, 26, 34, 41, 53, 55}},
    {11, 72, {0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72}},
    {12, 85, {0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85}},
};

// Configuration
#ifdef DEV_MODE
    const std::vector<int> TEST_SIZES = {2, 3, 4, 5, 6, 7, 8};
    const std::vector<int> BENCH_SIZES = {9, 10};
    const int DEFAULT_MAX_LEN = MAX_DIFF - 1;
    const char* MODE_NAME = "DEV";
#else
    const std::vector<int> TEST_SIZES = {2, 3, 4, 5, 6, 7, 8, 9};
    const std::vector<int> BENCH_SIZES = {10, 11, 12};
    const int DEFAULT_MAX_LEN = 200;
    const char* MODE_NAME = "PROD";
#endif

// =============================================================================
// TEST DE CORRECTNESS
// =============================================================================
bool runCorrectnessTests() {
    std::cout << "\n";
    std::cout << "=============================================================\n";
    std::cout << "                    TESTS DE CORRECTNESS\n";
    std::cout << "=============================================================\n";
    std::cout << std::setw(5) << "n"
              << std::setw(12) << "Expected"
              << std::setw(12) << "Got"
              << std::setw(15) << "Time (ms)"
              << std::setw(12) << "Status" << "\n";
    std::cout << std::string(56, '-') << "\n";

    bool allPassed = true;

    for (int n : TEST_SIZES) {
        // Trouve la solution optimale connue
        int expectedLen = -1;
        for (const auto& opt : KNOWN_OPTIMALS) {
            if (opt.n == n) {
                expectedLen = opt.length;
                break;
            }
        }

        if (expectedLen < 0) {
            std::cout << std::setw(5) << n << " SKIP (no known optimal)\n";
            continue;
        }

        // Lance la recherche
        GolombRuler result;
        auto start = std::chrono::high_resolution_clock::now();
        searchGolombSequential(n, DEFAULT_MAX_LEN, result);
        auto end = std::chrono::high_resolution_clock::now();
        double timeMs = std::chrono::duration<double, std::milli>(end - start).count();

        // Vérifie le résultat
        bool lengthOk = (result.length == expectedLen);
        bool validRuler = GolombRuler::isValid(result.marks);
        bool passed = lengthOk && validRuler;

        std::cout << std::setw(5) << n
                  << std::setw(12) << expectedLen
                  << std::setw(12) << result.length
                  << std::setw(15) << std::fixed << std::setprecision(2) << timeMs
                  << std::setw(12) << (passed ? "PASS" : "FAIL") << "\n";

        if (!passed) {
            allPassed = false;
            if (!lengthOk) {
                std::cout << "    ERROR: Expected length " << expectedLen
                          << ", got " << result.length << "\n";
            }
            if (!validRuler) {
                std::cout << "    ERROR: Ruler has duplicate differences!\n";
            }
        }
    }

    std::cout << std::string(56, '-') << "\n";
    std::cout << "Result: " << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";

    return allPassed;
}

// =============================================================================
// BENCHMARK DE PERFORMANCE
// =============================================================================
void runPerformanceBenchmark() {
    std::cout << "\n";
    std::cout << "=============================================================\n";
    std::cout << "                  BENCHMARK DE PERFORMANCE\n";
    std::cout << "=============================================================\n";
    std::cout << "Optimisations CSAPP appliquees:\n";
    std::cout << "  - Version iterative (pas de recursion)\n";
    std::cout << "  - Loop unrolling 4x pour validation\n";
    std::cout << "  - Shift bits: >> 6, & 63\n";
    std::cout << "  - Direct bit manipulation\n";
    std::cout << "  - Stack-allocated arrays\n";
    std::cout << "  - Cache-line alignment\n";
    std::cout << "  - Fail-fast with [[likely]]/[[unlikely]]\n";
    std::cout << "=============================================================\n\n";

    std::cout << std::setw(5) << "n"
              << std::setw(10) << "Length"
              << std::setw(15) << "Time (s)"
              << std::setw(18) << "States"
              << std::setw(18) << "States/sec"
              << std::setw(10) << "Valid" << "\n";
    std::cout << std::string(76, '-') << "\n";

    // Logger CSV
    BenchmarkLog logger("benchmarks", "sequential");

    for (int n : BENCH_SIZES) {
        GolombRuler result;

        auto start = std::chrono::high_resolution_clock::now();
        searchGolombSequential(n, DEFAULT_MAX_LEN, result);
        auto end = std::chrono::high_resolution_clock::now();

        double time = std::chrono::duration<double>(end - start).count();
        long long states = getExploredCountSequential();
        double statesPerSec = states / time;
        bool valid = GolombRuler::isValid(result.marks);

        // Trouve la solution optimale attendue
        int expectedLen = -1;
        for (const auto& opt : KNOWN_OPTIMALS) {
            if (opt.n == n) {
                expectedLen = opt.length;
                break;
            }
        }

        std::cout << std::setw(5) << n
                  << std::setw(10) << result.length
                  << std::setw(15) << std::fixed << std::setprecision(3) << time
                  << std::setw(18) << states
                  << std::setw(18) << std::scientific << std::setprecision(2) << statesPerSec
                  << std::setw(10) << (valid ? "OK" : "FAIL");

        if (expectedLen > 0 && result.length != expectedLen) {
            std::cout << " (expected " << expectedLen << ")";
        }
        std::cout << "\n";

        // Affiche la règle trouvée
        std::cout << "    Ruler: { ";
        for (size_t i = 0; i < result.marks.size(); ++i) {
            std::cout << result.marks[i];
            if (i < result.marks.size() - 1) std::cout << ", ";
        }
        std::cout << " }\n\n";

        // Log to CSV
        logger.logOpenMP(n, 1, result.length, time, 1.0, 100.0, states, "Sequential optimized");
    }

    std::cout << "=============================================================\n";
    std::cout << "[Results saved to benchmarks/sequential_benchmark.csv]\n";
}

// =============================================================================
// MAIN
// =============================================================================
int main() {
    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - SEQUENTIAL BENCHMARK\n";
    std::cout << "=============================================================\n";
    std::cout << "Mode: " << MODE_NAME << "\n";

    // 1. Tests de correctness
    bool testsOk = runCorrectnessTests();

    if (!testsOk) {
        std::cerr << "\nERROR: Correctness tests failed! Aborting benchmark.\n";
        return 1;
    }

    // 2. Benchmark de performance
    runPerformanceBenchmark();

    return 0;
}
