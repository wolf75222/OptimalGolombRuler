# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++ application that searches for optimal Golomb rulers using parallel backtracking. A Golomb ruler is a set of marks along a ruler such that no two pairs of marks are the same distance apart. This implementation finds rulers with minimal length for a given number of marks.

**Two versions available:**
1. **OpenMP**: Multi-threaded parallelization on single node
2. **MPI+OpenMP Hybrid**: Distributed memory parallelization with hypercube communication topology

## Project Structure

```
OptimalGolombRuler/
├── include/           # Header files
│   ├── golomb.hpp        # GolombRuler struct
│   ├── search.hpp        # OpenMP search interface
│   ├── search_mpi.hpp    # MPI search interface
│   ├── hypercube.hpp     # Hypercube MPI topology
│   └── benchmark_log.hpp # CSV logging utility
├── src/               # Source files
│   ├── golomb.cpp
│   ├── search.cpp     # OpenMP implementation
│   ├── search_mpi.cpp # MPI implementation
│   ├── main_openmp.cpp
│   └── main_mpi.cpp
├── scripts/           # Build scripts (Windows)
│   ├── build_openmp.bat
│   ├── build_mpi.bat
│   └── clean.bat
├── benchmarks/        # CSV benchmark history
│   ├── openmp_benchmark.csv
│   └── mpi_benchmark.csv
├── .vscode/           # VS Code configuration
├── Makefile           # Unix build (g++/mpicxx)
└── build/             # Build output (generated)
```

## Build Commands

### Windows (MSVC)

**OpenMP version:**
```bash
scripts\build_openmp.bat
build\golomb_openmp.exe
```

**MPI version (requires MS-MPI SDK):**
```bash
scripts\build_mpi.bat
mpiexec -n 4 build\golomb_mpi.exe
```

### Unix/Linux (g++/mpicxx)

**OpenMP version:**
```bash
make openmp
./build/golomb_openmp
```

**MPI version:**
```bash
make mpi
mpiexec -n 4 ./build/golomb_mpi
```

### VS Code

Use `Ctrl+Shift+B` to build. Available tasks:
- Build OpenMP (MSVC)
- Build MPI (MSVC)
- Build OpenMP (g++)
- Build MPI (mpicxx)

## Architecture

### Core Components

#### 1. GolombRuler (include/golomb.hpp)
- `GolombRuler` struct represents a ruler with marks and length
- `isValid()` static method validates that all pairwise differences are unique using a bitset
- `MAX_DIFF` constant (256) defines the maximum distance allowed between marks
- Uses `std::bitset<MAX_DIFF>` for O(1) difference checking

#### 2. OpenMP Search (src/search.cpp)
- `backtrack()`: Sequential backtracking function used by individual threads
- `searchGolomb()`: Parallelized search using OpenMP
  - Distributes initial branches across threads using `#pragma omp for schedule(guided, 4)`
  - Each thread maintains local state (current marks, used differences, local best)
  - Uses double-checked locking pattern in critical section to minimize contention
  - Tracks explored states with atomic counter
- `getExploredCount()`: Returns total states explored across all threads

#### 3. MPI+OpenMP Hybrid (src/search_mpi.cpp, include/hypercube.hpp)
- **HypercubeMPI class**: Implements hypercube communication topology
  - `neighbor(dimension)`: Computes neighbor rank via XOR (rank ^ 2^d)
  - `allReduceMin()`: Binomial tree reduction in log2(P) steps
  - `asyncExchangeMin()`: Non-blocking neighbor exchanges for overlapping computation/communication
  - Validates that number of MPI processes is power of 2

- **searchGolombMPI()**: Three-phase hybrid algorithm
  1. **Local Search**: Each MPI rank explores disjoint search space partition with OpenMP threads
  2. **Hypercube Reduction**: log2(P) communication rounds to find global minimum
  3. **Result Broadcast**: Winner broadcasts optimal solution to all ranks

## Key Implementation Details

- **C++20**: Project uses C++20 standard
- **Pruning Strategy**: Branch-and-bound pruning when `next >= bestLen`
- **Thread Safety**:
  - OpenMP: Critical sections with double-check pattern
  - MPI: Lock-free atomic CAS operations for best length updates

## Modifying the Search

- Maximum distance constraint: `MAX_DIFF` in include/golomb.hpp
- Test ruler sizes: `sizes` vector in src/main_*.cpp
- OpenMP thread counts: `threadsToTest` in src/main_*.cpp
- Maximum length limit: `maxLen` in src/main_*.cpp

## Benchmark Tracking

Results are automatically saved to CSV files in `benchmarks/`:
- `openmp_benchmark.csv` - OpenMP benchmark results
- `mpi_benchmark.csv` - MPI+OpenMP benchmark results

**To document changes:** Edit the `CHANGES` variable in `src/main_openmp.cpp` or `src/main_mpi.cpp` before running a benchmark:
```cpp
const std::string CHANGES = "Optimized pruning strategy";
```

CSV columns:
- `timestamp`, `date` - When the benchmark ran
- `n`, `threads`/`mpi_procs`/`omp_threads` - Configuration
- `length`, `time_s`, `speedup`, `efficiency_pct`, `states` - Results
- `changes` - Description of code modifications
