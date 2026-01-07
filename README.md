# Optimal Golomb Ruler

Recherche de règles de Golomb optimales par backtracking parallèle.

Une règle de Golomb est un ensemble de marques sur une règle tel qu'aucune paire de marques n'a la même distance. Ce projet trouve la règle de longueur minimale pour un nombre donné de marques.

## Versions

### Sequential
| Version | Description | Optimisation |
|---------|-------------|--------------|
| **V1** | Original | Loop unrolling, iterative backtracking |
| **V2** | BitSet128 shift | O(1) collision detection via shift |

### OpenMP (multi-thread)
| Version | Description | Optimisation |
|---------|-------------|--------------|
| **V1** | Original | Iterative + loop unrolling 4x |
| **V2** | Bitset shift | Recursive + bitset<256> shift |
| **V3** | Hybrid | Iterative + bitset shift |
| **V4** | Prefix-based | Prefix generation + bitset shift |
| **V5** | uint64_t ops | BitSet128 (2x uint64_t) + prefix-based |

### MPI+OpenMP (distribué)
| Version | Description | Communication |
|---------|-------------|---------------|
| **V1** | Original | Hypercube + loop unrolling |
| **V2** | BitSet128 + Hypercube | Hypercube O(log P) + BitSet128 shift |
| **V3** | BitSet128 + Allreduce | MPI_Allreduce (any # procs) + BitSet128 |

## Structure

```
├── include/               # Headers
│   ├── golomb.hpp            # Structure GolombRuler
│   ├── search.hpp            # Interface OpenMP V1
│   ├── search_v2.hpp         # Interface OpenMP V2
│   ├── search_v3.hpp         # Interface OpenMP V3
│   ├── search_v4.hpp         # Interface OpenMP V4
│   ├── search_v5.hpp         # Interface OpenMP V5
│   ├── search_sequential.hpp # Interface Sequential V1
│   ├── search_sequential_v2.hpp # Interface Sequential V2
│   ├── search_mpi.hpp        # Interface MPI V1
│   ├── search_mpi_v2.hpp     # Interface MPI V2
│   ├── search_mpi_v3.hpp     # Interface MPI V3
│   ├── hypercube.hpp         # Topologie hypercube MPI
│   └── benchmark_log.hpp     # Export CSV
├── src/                   # Sources
│   ├── search.cpp            # OpenMP V1
│   ├── search_v2.cpp         # OpenMP V2
│   ├── search_v3.cpp         # OpenMP V3
│   ├── search_v4.cpp         # OpenMP V4
│   ├── search_v5.cpp         # OpenMP V5
│   ├── search_sequential.cpp # Sequential V1
│   ├── search_sequential_v2.cpp # Sequential V2
│   ├── search_mpi.cpp        # MPI V1
│   ├── search_mpi_v2.cpp     # MPI V2
│   ├── search_mpi_v3.cpp     # MPI V3
│   └── main_*.cpp            # Entry points
├── scripts/               # Scripts Windows (MSVC)
├── *.slurm                # Scripts SLURM pour HPC Romeo
└── Makefile               # Build Linux/Unix
```

## Compilation

### Linux/Unix (Makefile)

```bash
# Sequential
make sequential           # V1
make sequential_v2        # V2 (BitSet128)

# OpenMP
make openmp               # V1
make openmp_v2            # V2
make openmp_v3            # V3
make openmp_v4            # V4
make openmp_v5            # V5 (fastest)

# MPI+OpenMP
make mpi                  # V1 (hypercube)
make mpi_v2               # V2 (hypercube + BitSet128)
make mpi_v3               # V3 (allreduce + BitSet128)
```

### Windows (MSVC)

```bash
scripts\build_openmp.bat      # OpenMP V1
scripts\build_openmp_v5.bat   # OpenMP V5
scripts\build_mpi.bat         # MPI V1
scripts\build_sequential.bat  # Sequential V1
```

## Exécution

### Local
```bash
# OpenMP (utilise tous les cores disponibles)
./build/golomb_openmp_v5 13

# MPI (procs = puissance de 2 pour V1/V2)
mpiexec -n 4 ./build/golomb_mpi_v2 13
mpiexec -n 8 ./build/golomb_mpi_v3 13  # V3: any number of procs
```

### HPC Romeo (SLURM)
```bash
# OpenMP comparison (x86 et ARM)
sbatch golomb_openmp_compare.slurm
sbatch golomb_openmp_compare_arm.slurm

# MPI comparison
sbatch golomb_mpi_compare.slurm
sbatch golomb_mpi_compare_arm.slurm

# Sequential comparison
sbatch golomb_sequential_compare.slurm
```

## Algorithme

### Backtracking avec Branch-and-Bound

1. Construction incrémentale des marques
2. Pruning agressif : abandon si `length + r*(r+1)/2 >= bestLen`
3. Validation O(1) des différences via BitSet128 shift

### Optimisation clé : BitSet128 shift

```cpp
// reversed_marks contient les positions des marques (inversées)
// Pour ajouter une marque à la position `pos`:
BitSet128 new_dist = reversed_marks << offset;  // Toutes les nouvelles différences!
if ((new_dist & used_dist).any()) continue;     // Collision = skip
```

Cette technique calcule **toutes les nouvelles différences en une seule opération shift**, au lieu de boucler sur chaque marque existante.

### Parallélisation

- **OpenMP** : Distribution des préfixes entre threads (`schedule(dynamic, 1)`)
- **MPI Hypercube** : O(log P) communication pour sync des bornes
- **MPI Allreduce** : MPI_Allreduce standard, fonctionne avec tout nombre de processus

## Résultats connus

| n | Longueur | Règle |
|---|----------|-------|
| 2 | 1 | 0 1 |
| 3 | 3 | 0 1 3 |
| 4 | 6 | 0 1 4 6 |
| 5 | 11 | 0 1 4 9 11 |
| 6 | 17 | 0 1 4 10 12 17 |
| 7 | 25 | 0 1 4 10 18 23 25 |
| 8 | 34 | 0 1 4 9 15 22 32 34 |
| 9 | 44 | 0 1 5 12 25 27 35 41 44 |
| 10 | 55 | 0 1 6 10 23 26 34 41 53 55 |
| 11 | 72 | 0 1 4 13 28 33 47 54 64 70 72 |
| 12 | 85 | 0 2 6 24 29 40 43 55 68 75 76 85 |
| 13 | 106 | 0 2 5 25 37 43 59 70 85 89 98 99 106 |
| 14 | 127 | 0 4 6 20 35 52 59 77 78 86 89 99 122 127 |

## Prérequis

- **Compilateur** : C++20 (g++, clang++, MSVC)
- **OpenMP** : Supporté par le compilateur
- **MPI** : OpenMPI/MPICH (Linux) ou MS-MPI (Windows)
