#include "search_mpi.hpp"
#include "benchmark_log.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <omp.h>
#include <mpi.h>

// Description des changements pour ce benchmark
// Modifier cette variable avant chaque run pour documenter les modifications
const std::string CHANGES = "Master-worker dynamique + OpenMP taskloop";

// Configuration DEV vs PROD
// DEV  : tailles réduites pour tests rapides sur Windows
// PROD : configuration complète pour cluster HPC/Slurm
#ifdef DEV_MODE
    const std::vector<int> DEFAULT_SIZES = { 6, 7, 8 };
    const int DEFAULT_MAX_LEN = 100;
    const char* MODE_NAME = "DEV";
#else
    const std::vector<int> DEFAULT_SIZES = { 10, 11, 12 };
    const int DEFAULT_MAX_LEN = 200;
    const char* MODE_NAME = "PROD";
#endif

// =============================================================================
// SINGLE N MODE - Run for a specific n passed as argument
// =============================================================================
void runSingleN(int n, HypercubeMPI& hypercube) {
    int rank = hypercube.rank();
    int size = hypercube.size();
    int ompThreads = omp_get_max_threads();

    if (rank == 0) {
        std::cout << "=============================================================\n";
        std::cout << "       OPTIMAL GOLOMB RULER - MPI+OPENMP (n=" << n << ")\n";
        std::cout << "=============================================================\n";
        std::cout << "MPI Processes: " << size << "\n";
        std::cout << "OpenMP Threads: " << ompThreads << "\n";
        std::cout << "Total cores: " << (size * ompThreads) << "\n\n";
    }

    MPI_Barrier(MPI_COMM_WORLD);

    GolombRuler result;
    double start = MPI_Wtime();
    searchGolombMPI(n, DEFAULT_MAX_LEN, result, hypercube);
    double end = MPI_Wtime();
    double elapsed = end - start;

    double maxTime;
    MPI_Reduce(&elapsed, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    long long states = getExploredCountMPI();

    if (rank == 0) {
        double statesPerSec = states / maxTime;

        std::cout << "n          : " << n << "\n";
        std::cout << "Length     : " << result.length << "\n";
        std::cout << "Time       : " << std::fixed << std::setprecision(3) << maxTime << " s\n";
        std::cout << "States     : " << states << "\n";
        std::cout << "States/sec : " << std::scientific << std::setprecision(2) << statesPerSec << "\n";
        std::cout << "\nRuler: { ";
        for (size_t i = 0; i < result.marks.size(); ++i) {
            std::cout << result.marks[i];
            if (i < result.marks.size() - 1) std::cout << ", ";
        }
        std::cout << " }\n";
        std::cout << "=============================================================\n";
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    HypercubeMPI hypercube;
    int rank = hypercube.rank();
    int size = hypercube.size();

    // Check for single n argument
    if (argc > 1) {
        int n = std::atoi(argv[1]);
        if (n < 2 || n > 24) {
            if (rank == 0) std::cerr << "ERROR: n must be between 2 and 24\n";
            MPI_Finalize();
            return 1;
        }
        runSingleN(n, hypercube);
        MPI_Finalize();
        return 0;
    }

    // Default: run full benchmark
    // Logger CSV (seulement rank 0)
    BenchmarkLog* logger = nullptr;
    if (rank == 0) {
        logger = new BenchmarkLog("benchmarks", "mpi");
    }

    // Utilise OMP_NUM_THREADS de l'environnement
    int ompThreads = omp_get_max_threads();
    int totalCores = size * ompThreads;

    if (rank == 0) {
        std::cout << "=== Optimal Golomb Ruler MPI+OpenMP Hybrid Benchmark ===\n";
        std::cout << "Mode: " << MODE_NAME << "\n";
        std::cout << "MPI Processes: " << size << " (Hypercube dim: " << hypercube.dimensions() << ")\n";
        std::cout << "OpenMP Threads per process: " << ompThreads << "\n";
        std::cout << "Total cores: " << totalCores << "\n";
        if (!CHANGES.empty()) {
            std::cout << "Changes: " << CHANGES << "\n";
        }
    }

    std::vector<int> sizes = DEFAULT_SIZES;
    const int maxLen = DEFAULT_MAX_LEN;

    if (rank == 0) {
        std::cout << "\n" << std::setw(6) << "n"
                  << std::setw(10) << "Length"
                  << std::setw(15) << "Time (s)"
                  << std::setw(20) << "Explored States"
                  << std::setw(15) << "States/sec" << std::endl;
        std::cout << std::string(66, '-') << std::endl;
    }

    for (int n : sizes) {
        MPI_Barrier(MPI_COMM_WORLD);

        GolombRuler best;
        double start = MPI_Wtime();

        searchGolombMPI(n, maxLen, best, hypercube);

        double end = MPI_Wtime();
        double elapsed = end - start;

        double maxTime;
        MPI_Reduce(&elapsed, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        // IMPORTANT: Tous les rangs doivent appeler getExploredCountMPI() car il contient MPI_Reduce
        long long exploredStates = getExploredCountMPI();

        if (rank == 0) {
            double statesPerSec = exploredStates / maxTime;

            std::cout << std::setw(6) << n
                      << std::setw(10) << best.length
                      << std::setw(15) << std::fixed << std::setprecision(3) << maxTime
                      << std::setw(20) << exploredStates
                      << std::setw(15) << std::scientific << std::setprecision(2) << statesPerSec
                      << std::endl;

            // Log to CSV - speedup et efficiency seront calculés à partir des données CSV
            logger->logMPI(n, size, ompThreads, best.length, maxTime, 1.0, 100.0 / totalCores,
                          exploredStates, CHANGES);
        }

        MPI_Barrier(MPI_COMM_WORLD);
    }

    if (rank == 0) {
        std::cout << "\n[Results saved to benchmarks/mpi_benchmark.csv]\n";
        std::cout << "=== Benchmark Complete ===\n";
        delete logger;
    }

    MPI_Finalize();
    return 0;
}
