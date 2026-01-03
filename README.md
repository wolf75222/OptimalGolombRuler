# Optimal Golomb Ruler

Recherche de règles de Golomb optimales par backtracking parallèle.

Une règle de Golomb est un ensemble de marques sur une règle tel qu'aucune paire de marques n'a la même distance. Ce projet trouve la règle de longueur minimale pour un nombre donné de marques.

## Versions

| Version | Description | Parallélisme |
|---------|-------------|--------------|
| **OpenMP** | Multi-thread sur un nœud | Threads CPU |
| **MPI+OpenMP** | Distribué avec topologie hypercube | Processus + Threads |

## Structure

```
├── include/           # Headers
│   ├── golomb.hpp        # Structure GolombRuler
│   ├── search.hpp        # Interface OpenMP
│   ├── search_mpi.hpp    # Interface MPI
│   ├── hypercube.hpp     # Topologie hypercube MPI
│   └── benchmark_log.hpp # Export CSV
├── src/               # Sources
│   ├── search.cpp        # Implémentation OpenMP
│   ├── search_mpi.cpp    # Implémentation MPI+OpenMP
│   ├── main_openmp.cpp   # Benchmark OpenMP
│   └── main_mpi.cpp      # Benchmark MPI
├── scripts/           # Scripts Windows (MSVC)
├── benchmarks/        # Historique CSV des benchmarks
└── build/             # Sortie de compilation
```

## Compilation

### Windows (MSVC)

```bash
# OpenMP
scripts\build_openmp.bat

# MPI (nécessite MS-MPI SDK)
scripts\build_mpi.bat
```

### Linux/Unix (g++)

```bash
# OpenMP
make openmp

# MPI
make mpi
```

### VS Code

`Ctrl+Shift+B` pour choisir une tâche de build.

## Exécution

```bash
# OpenMP
build\golomb_openmp.exe        # Windows
./build/golomb_openmp          # Linux

# MPI (nombre de processus = puissance de 2)
mpiexec -n 4 build\golomb_mpi.exe
mpiexec -n 8 build\golomb_mpi.exe
```

## Algorithme

### Backtracking avec Branch-and-Bound

1. Construction incrémentale des marques
2. Pruning agressif : abandon si `next >= bestLen`
3. Validation O(1) des différences via `std::bitset`

### Parallélisation OpenMP

- Distribution des branches initiales entre threads
- `schedule(guided)` pour équilibrer la charge
- Section critique avec double-check pattern
- Compteur atomique des états explorés

### Parallélisation MPI+OpenMP Hybride

1. **Partitionnement** : Espace de recherche divisé entre processus MPI
2. **Recherche locale** : Chaque rang explore sa partition avec OpenMP
3. **Réduction hypercube** : log₂(P) étapes de communication pour trouver le minimum global
4. **Broadcast** : Le gagnant diffuse la solution optimale

### Optimisations

- Mise à jour lock-free du meilleur résultat (CAS atomique)
- `memory_order_acquire/release` pour la synchronisation
- `schedule(dynamic)` pour les charges irrégulières
- `newDiffs.reserve()` pour éviter les réallocations

## Benchmark Tracking

Les résultats sont sauvegardés automatiquement dans `benchmarks/` :

```
benchmarks/
├── openmp_benchmark.csv
└── mpi_benchmark.csv
```

Pour documenter une modification, éditer `CHANGES` avant le run :

```cpp
// src/main_openmp.cpp
const std::string CHANGES = "Optimisation du pruning";
```

## Résultats connus

| n | Longueur optimale | Règle |
|---|-------------------|-------|
| 6 | 17 | 0 1 4 10 12 17 |
| 7 | 25 | 0 1 4 10 18 23 25 |
| 8 | 34 | 0 1 4 9 15 22 32 34 |
| 9 | 44 | 0 1 5 12 25 27 35 41 44 |
| 10 | 55 | 0 1 6 10 23 26 34 41 53 55 |
| 11 | 72 | 0 1 4 13 28 33 47 54 64 70 72 |

## Prérequis

- **Compilateur** : C++20 (MSVC, g++, clang++)
- **OpenMP** : Supporté par le compilateur
- **MPI** : MS-MPI (Windows) ou OpenMPI/MPICH (Linux)
