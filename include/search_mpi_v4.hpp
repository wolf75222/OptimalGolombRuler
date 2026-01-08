#pragma once

#include "golomb.hpp"

// =============================================================================
// GOLOMB RULER SEARCH - MPI V4 (GREEDY INIT + DYNAMIC DISTRIBUTION)
// =============================================================================
// Improvements over V3:
//   1. Greedy initialization to find a better starting upper bound
//   2. Dynamic prefix distribution: master sends prefixes on-demand to workers
//      as they complete their tasks (better load balancing than static)
//   3. Continuous bound propagation during prefix distribution
//
// Based on:
//   - BitSet128 (2x uint64_t) for collision detection
//   - reversed_marks encoding: shift computes all differences in O(1)
//   - Master-worker pattern with dynamic task assignment
// =============================================================================

void searchGolombMPI_V4(int n, int maxLen, GolombRuler& best);
long long getExploredCountMPI_V4();
