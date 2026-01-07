#pragma once

#include "golomb.hpp"
#include "hypercube.hpp"

// =============================================================================
// GOLOMB RULER SEARCH - MPI V2 (HYPERCUBE + BITSET128 SHIFT OPTIMIZATION)
// =============================================================================
// Based on V5 OpenMP optimizations:
//   - BitSet128 (2x uint64_t) for collision detection
//   - reversed_marks encoding: shift computes all differences in O(1)
//   - Prefix generation for better load balancing
//   - Hypercube topology for O(log P) bound synchronization
// =============================================================================

void searchGolombMPI_V2(int n, int maxLen, GolombRuler& best, HypercubeMPI& hypercube);
long long getExploredCountMPI_V2();
