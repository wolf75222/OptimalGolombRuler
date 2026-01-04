#pragma once

#include "golomb.hpp"

// Version séquentielle pure - pas de dépendances OpenMP/MPI
// Optimisée avec les mêmes techniques CSAPP que la version parallèle

void searchGolombSequential(int n, int maxLen, GolombRuler& best);
long long getExploredCountSequential();
