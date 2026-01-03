# Optimal Golomb Ruler

Recherche de règles de Golomb optimales par backtracking parallèle.

Une règle de Golomb est un ensemble de marques sur une règle tel qu'aucune paire de marques n'a la même distance. Ce projet trouve la règle de longueur minimale pour un nombre donné de marques.

## Versions

| Version | Description | Parallélisme |
|---------|-------------|--------------|
| **OpenMP** | Multi-thread sur un noeud | Threads CPU |
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
│   ├── main_mpi.cpp      # Benchmark MPI
│   └── test_correctness.cpp # Tests unitaires
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
2. Pruning agressif : abandon si `next >= globalBestLen`
3. Validation O(1) des différences via manipulation de bits directe

### Parallélisation OpenMP

- Distribution des branches initiales entre threads (`schedule(dynamic, 1)`)
- Re-lecture de `globalBestLen` à chaque itération pour pruning agressif
- Mise à jour atomique lock-free du meilleur résultat (CAS)
- Fusion thread-safe des résultats via section critique

### Parallélisation MPI+OpenMP Hybride

1. **Partitionnement** : Espace de recherche divisé entre processus MPI
2. **Recherche locale** : Chaque rang explore sa partition avec OpenMP
3. **Réduction hypercube** : log2(P) étapes de communication pour trouver le minimum global
4. **Broadcast** : Le gagnant diffuse la solution optimale

### Optimisations CSAPP

Le code applique les principes d'optimisation du livre *Computer Systems: A Programmer's Perspective* :

- **#2 Make common case fast** : La boucle de validation des différences est le hot path
- **#3 Eliminate loop inefficiencies** : Invariants hoistés hors des boucles
- **#5 Incremental updates** : Mises à jour incrémentales des différences (pas de recalcul)
- **#6 Predictable branches** : Hints `[[likely]]`/`[[unlikely]]` pour la prédiction
- **#8 Minimal live variables** : Réduction de la pression sur les registres
- **#9 Cache locality** : Structure `SearchState` alignée sur cache-line (64 bytes)

## Benchmark Results

### OpenMP (20 cores, Windows 11)

| n | Threads | Length | Time (s) | Speedup | Efficiency |
|---|---------|--------|----------|---------|------------|
| 10 | 1 | 55 | 0.22 | 1.00x | 100% |
| 10 | 2 | 55 | 0.11 | 1.96x | 98% |
| 10 | 4 | 55 | 0.07 | 3.38x | 85% |
| 10 | 8 | 55 | 0.04 | 5.80x | 73% |
| 10 | 16 | 55 | 0.04 | 4.95x | 31% |
| 11 | 1 | 72 | 4.39 | 1.00x | 100% |
| 11 | 8 | 72 | 0.61 | 7.18x | 90% |
| 12 | 1 | 85 | 39.23 | 1.00x | 100% |
| 12 | 8 | 85 | 5.71 | 6.88x | 86% |
| 12 | 16 | 85 | 4.48 | 8.75x | 55% |

## Résultats connus

| n | Longueur optimale | Règle |
|---|-------------------|-------|
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

## Prérequis

- **Compilateur** : C++20 (MSVC, g++, clang++)
- **OpenMP** : Supporté par le compilateur
- **MPI** : MS-MPI (Windows) ou OpenMPI/MPICH (Linux)
