#include "search_sequential_v4.hpp"
#include "benchmark_log.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cmath>
#include <cstring>

// =============================================================================
// BENCHMARK SEQUENTIAL V4 - Maximum pruning edition
// =============================================================================
// Key improvements:
// - Mirror symmetry breaking at solution time
// - Configurable initial bound for aggressive pruning
// - All BitSet128 optimizations from V2/V3
// =============================================================================

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
    {13, 106, {0, 2, 5, 25, 37, 43, 59, 70, 85, 89, 98, 99, 106}},
};

#ifdef DEV_MODE
    const std::vector<int> TEST_SIZES = {2, 3, 4, 5, 6, 7, 8};
    const std::vector<int> BENCH_SIZES = {9, 10};
    const int DEFAULT_MAX_LEN = 127;
    const char* MODE_NAME = "DEV";
#else
    const std::vector<int> TEST_SIZES = {2, 3, 4, 5, 6, 7, 8, 9};
    const std::vector<int> BENCH_SIZES = {10, 11, 12};
    const int DEFAULT_MAX_LEN = 127;
    const char* MODE_NAME = "PROD";
#endif

// Get optimal length for a given n
static int getOptimalLength(int n) {
    for (const auto& opt : KNOWN_OPTIMALS) {
        if (opt.n == n) return opt.length;
    }
    return -1;
}

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
        int expectedLen = getOptimalLength(n);
        if (expectedLen < 0) {
            std::cout << std::setw(5) << n << " SKIP (no known optimal)\n";
            continue;
        }

        GolombRuler result;
        auto start = std::chrono::high_resolution_clock::now();
        searchGolombSequentialV4(n, DEFAULT_MAX_LEN, result);
        auto end = std::chrono::high_resolution_clock::now();
        double timeMs = std::chrono::duration<double, std::milli>(end - start).count();

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
                std::cout << "    ERROR: Expected " << expectedLen << ", got " << result.length << "\n";
            }
            if (!validRuler) {
                std::cout << "    ERROR: Invalid ruler!\n";
            }
        }
    }

    std::cout << std::string(56, '-') << "\n";
    std::cout << "Result: " << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";

    return allPassed;
}

void runPerformanceBenchmark(bool useOptimalBound) {
    std::cout << "\n";
    std::cout << "=============================================================\n";
    std::cout << "                  BENCHMARK DE PERFORMANCE\n";
    std::cout << "=============================================================\n";
    std::cout << "Sequential V4 Optimizations:\n";
    std::cout << "  - BitSet128 shift-based O(1) collision detection\n";
    std::cout << "  - Mirror symmetry breaking: a_1 < a_{n-1} - a_{n-2}\n";
    std::cout << "  - Prefix symmetry: a_1 <= bestLen/2\n";
    if (useOptimalBound) {
        std::cout << "  - Using KNOWN OPTIMAL as initial bound (fast mode)\n";
    } else {
        std::cout << "  - Using default bound (127)\n";
    }
    std::cout << "=============================================================\n\n";

    std::cout << std::setw(5) << "n"
              << std::setw(10) << "Length"
              << std::setw(15) << "Time (s)"
              << std::setw(18) << "States"
              << std::setw(18) << "States/sec"
              << std::setw(10) << "Valid" << "\n";
    std::cout << std::string(76, '-') << "\n";

    BenchmarkLog logger("benchmarks", "sequential_v4");

    for (int n : BENCH_SIZES) {
        GolombRuler result;

        int initialBound = useOptimalBound ? getOptimalLength(n) : DEFAULT_MAX_LEN;
        if (initialBound < 0) initialBound = DEFAULT_MAX_LEN;

        auto start = std::chrono::high_resolution_clock::now();
        searchGolombSequentialV4WithBound(n, initialBound, result);
        auto end = std::chrono::high_resolution_clock::now();

        double time = std::chrono::duration<double>(end - start).count();
        long long states = getExploredCountSequentialV4();
        double statesPerSec = states / time;
        bool valid = GolombRuler::isValid(result.marks);

        int expectedLen = getOptimalLength(n);

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

        std::cout << "    Ruler: { ";
        for (size_t i = 0; i < result.marks.size(); ++i) {
            std::cout << result.marks[i];
            if (i < result.marks.size() - 1) std::cout << ", ";
        }
        std::cout << " }\n\n";

        std::string note = useOptimalBound ? "Sequential V4 (optimal bound)" : "Sequential V4 (default bound)";
        logger.logOpenMP(n, 1, result.length, time, 1.0, 100.0, states, note);
    }

    std::cout << "=============================================================\n";
    std::cout << "[Results saved to benchmarks/sequential_v4_benchmark.csv]\n";
}

void runSingleN(int n, bool useOptimalBound) {
    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - SEQUENTIAL V4 (n=" << n << ")\n";
    std::cout << "=============================================================\n\n";

    int initialBound = DEFAULT_MAX_LEN;
    if (useOptimalBound) {
        int optLen = getOptimalLength(n);
        if (optLen > 0) {
            initialBound = optLen;
            std::cout << "Using known optimal (" << optLen << ") as initial bound\n\n";
        }
    }

    GolombRuler result;

    auto start = std::chrono::high_resolution_clock::now();
    searchGolombSequentialV4WithBound(n, initialBound, result);
    auto end = std::chrono::high_resolution_clock::now();

    double time = std::chrono::duration<double>(end - start).count();
    long long states = getExploredCountSequentialV4();
    double statesPerSec = states / time;
    bool valid = GolombRuler::isValid(result.marks);

    int expectedLen = getOptimalLength(n);

    std::cout << "n          : " << n << "\n";
    std::cout << "Length     : " << result.length;
    if (expectedLen > 0) {
        std::cout << " (optimal: " << expectedLen << ")";
        if (result.length != expectedLen) std::cout << " MISMATCH!";
    }
    std::cout << "\n";
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

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [n] [--fast]\n";
    std::cout << "  n      : Golomb ruler size (2-24)\n";
    std::cout << "  --fast : Use known optimal as initial bound (much faster)\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << progName << " 12        # Find optimal Golomb(12) from scratch\n";
    std::cout << "  " << progName << " 12 --fast # Verify Golomb(12) with optimal bound\n";
    std::cout << "  " << progName << "           # Run full benchmark\n";
    std::cout << "  " << progName << " --fast    # Run benchmark with optimal bounds\n";
}

int main(int argc, char** argv) {
    bool useOptimalBound = false;
    int n = -1;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--fast") == 0 || strcmp(argv[i], "-f") == 0) {
            useOptimalBound = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            n = std::atoi(argv[i]);
        }
    }

    if (n > 0) {
        if (n < 2 || n > 24) {
            std::cerr << "ERROR: n must be between 2 and 24\n";
            return 1;
        }
        runSingleN(n, useOptimalBound);
        return 0;
    }

    std::cout << "=============================================================\n";
    std::cout << "       OPTIMAL GOLOMB RULER - SEQUENTIAL V4 BENCHMARK\n";
    std::cout << "=============================================================\n";
    std::cout << "Mode: " << MODE_NAME << "\n";
    std::cout << "Optimizations: Mirror symmetry + BitSet128 + configurable bound\n";

    bool testsOk = runCorrectnessTests();

    if (!testsOk) {
        std::cerr << "\nERROR: Correctness tests failed! Aborting benchmark.\n";
        return 1;
    }

    runPerformanceBenchmark(useOptimalBound);

    return 0;
}
