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
    const std::vector<int> DEFAULT_SIZES = { 8, 9, 10, 11, 12 };
    const int DEFAULT_MAX_LEN = 200;
    const std::vector<int> DEFAULT_THREADS = { 1, 2, 4, 8 };
    const char* MODE_NAME = "PROD";
#endif

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    HypercubeMPI hypercube;
    int rank = hypercube.rank();
    int size = hypercube.size();

    // Logger CSV (seulement rank 0)
    BenchmarkLog* logger = nullptr;
    if (rank == 0) {
        logger = new BenchmarkLog("benchmarks", "mpi");
    }

    if (rank == 0) {
        std::cout << "=== Optimal Golomb Ruler MPI+OpenMP Hypercube Benchmark ===\n";
        std::cout << "Mode: " << MODE_NAME << "\n";
        std::cout << "MPI Processes: " << size << " (Hypercube dimension: " << hypercube.dimensions() << ")\n";
        if (!CHANGES.empty()) {
            std::cout << "Changes: " << CHANGES << "\n";
        }
    }

    std::vector<int> sizes = DEFAULT_SIZES;
    const int maxLen = DEFAULT_MAX_LEN;

    int maxThreads = omp_get_max_threads();
    std::vector<int> threadsToTest = DEFAULT_THREADS;

    for (int n : sizes) {
        if (rank == 0) {
            std::cout << "\n>>> Testing n = " << n << " (OpenMP max threads = " << maxThreads << ")\n";
            std::cout << std::setw(12) << "OMP Threads"
                      << std::setw(10) << "Length"
                      << std::setw(15) << "Time (s)"
                      << std::setw(15) << "Speedup"
                      << std::setw(15) << "Efficiency (%)"
                      << std::setw(20) << "Explored States" << std::endl;
            std::cout << std::string(87, '-') << std::endl;
        }

        double baseTime = 0.0;

        for (int t : threadsToTest) {
            if (t > maxThreads) break;
            omp_set_num_threads(t);

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
                if (t == 1) baseTime = maxTime;

                double speedup = baseTime / maxTime;
                double efficiency = (speedup / (t * size)) * 100.0;

                std::cout << std::setw(12) << t
                          << std::setw(10) << best.length
                          << std::setw(15) << std::fixed << std::setprecision(5) << maxTime
                          << std::setw(15) << std::setprecision(2) << speedup
                          << std::setw(15) << std::setprecision(1) << efficiency
                          << std::setw(20) << exploredStates
                          << std::endl;

                // Log to CSV
                logger->logMPI(n, size, t, best.length, maxTime, speedup, efficiency,
                              exploredStates, CHANGES);
            }

            MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    if (rank == 0) {
        std::cout << "\n[Results saved to benchmarks/mpi_benchmark.csv]\n";
        std::cout << "=== Benchmark Complete ===\n";
        delete logger;
    }

    MPI_Finalize();
    return 0;
}
