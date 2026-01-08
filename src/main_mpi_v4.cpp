#include <iostream>
#include <iomanip>
#include <chrono>
#include <mpi.h>
#include <omp.h>
#include "search_mpi_v4.hpp"

int main(int argc, char* argv[])
{
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int n = 11;
    if (argc > 1) {
        n = std::atoi(argv[1]);
        if (n < 2 || n > 24) {
            if (rank == 0) {
                std::cerr << "n must be between 2 and 24" << std::endl;
            }
            MPI_Finalize();
            return 1;
        }
    }

    // Print header only on rank 0
    if (rank == 0) {
        std::cout << "===========================================" << std::endl;
        std::cout << " GOLOMB RULER SEARCH - MPI V4" << std::endl;
        std::cout << " (Greedy Init + Dynamic Distribution)" << std::endl;
        std::cout << "===========================================" << std::endl;
        std::cout << "Searching for optimal Golomb ruler with n = " << n << " marks" << std::endl;
        std::cout << "MPI processes: " << size << std::endl;
        std::cout << "OpenMP threads per process: " << omp_get_max_threads() << std::endl;
        std::cout << "Total workers: " << size * omp_get_max_threads() << std::endl;
        std::cout << std::endl;
    }

    // Known optimal lengths for validation
    int knownOptimal[] = {0, 0, 1, 3, 6, 11, 17, 25, 34, 44, 55, 72, 85, 106, 127};
    int maxN = sizeof(knownOptimal) / sizeof(knownOptimal[0]) - 1;

    int maxLen = (n <= maxN) ? knownOptimal[n] : 200;

    GolombRuler best;

    MPI_Barrier(MPI_COMM_WORLD);
    auto start = std::chrono::high_resolution_clock::now();

    searchGolombMPI_V4(n, maxLen, best);

    MPI_Barrier(MPI_COMM_WORLD);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    long long exploredCount = getExploredCountMPI_V4();

    // Print results only on rank 0
    if (rank == 0) {
        std::cout << "===========================================" << std::endl;
        std::cout << "RESULTS" << std::endl;
        std::cout << "===========================================" << std::endl;

        if (best.marks.empty()) {
            std::cout << "No solution found within maxLen = " << maxLen << std::endl;
        } else {
            std::cout << "Optimal ruler found!" << std::endl;
            std::cout << "Length   : " << best.length << std::endl;
            std::cout << "Marks    : [";
            for (size_t i = 0; i < best.marks.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << best.marks[i];
            }
            std::cout << "]" << std::endl;

            // Validate
            if (n <= maxN && best.length != knownOptimal[n]) {
                std::cout << "WARNING: Expected length " << knownOptimal[n]
                          << " but got " << best.length << std::endl;
            }
        }

        std::cout << std::endl;
        std::cout << "Time     : " << std::fixed << std::setprecision(3)
                  << elapsed.count() << " seconds" << std::endl;
        std::cout << "States   : " << exploredCount << std::endl;

        double statesPerSec = exploredCount / elapsed.count();
        if (statesPerSec >= 1e9) {
            std::cout << "States/sec: " << std::fixed << std::setprecision(2)
                      << statesPerSec / 1e9 << " G/s" << std::endl;
        } else if (statesPerSec >= 1e6) {
            std::cout << "States/sec: " << std::fixed << std::setprecision(2)
                      << statesPerSec / 1e6 << " M/s" << std::endl;
        } else {
            std::cout << "States/sec: " << std::fixed << std::setprecision(0)
                      << statesPerSec << std::endl;
        }
    }

    MPI_Finalize();
    return 0;
}
