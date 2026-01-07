#pragma once

#include "golomb.hpp"

// =============================================================================
// GOLOMB RULER SEARCH - MPI V3 (NO HYPERCUBE, STANDARD MPI_ALLREDUCE)
// =============================================================================
// Based on V5 OpenMP optimizations:
//   - BitSet128 (2x uint64_t) for collision detection
//   - reversed_marks encoding: shift computes all differences in O(1)
//   - Prefix generation for better load balancing
//   - Standard MPI_Allreduce for bound synchronization (simpler, no power-of-2)
//
// Compared to V2:
//   - No hypercube requirement (works with any number of processes)
//   - Simpler implementation
//   - May have slightly higher communication overhead for large P
// =============================================================================

void searchGolombMPI_V3(int n, int maxLen, GolombRuler& best);
long long getExploredCountMPI_V3();
