#pragma once

#include "golomb.hpp"
#include "hypercube.hpp"

void searchGolombMPI(int n, int maxLen, GolombRuler& best, HypercubeMPI& hypercube);
long long getExploredCountMPI();
