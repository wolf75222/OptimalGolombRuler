#pragma once

#include "golomb.hpp"

// =============================================================================
// SEARCH SEQUENTIAL V4 - Maximum pruning + all optimizations
// =============================================================================
// Key optimizations:
// - BitSet128 shift-based O(1) collision detection
// - Mirror symmetry breaking: a_1 < a_{n-1} - a_{n-2}
// - Configurable initial bound (start with known optimal for faster pruning)
// - Track firstMark separately for symmetry check
// - Local bestLen cache
// - Reuse new_dist to avoid double shift
// =============================================================================

// Standard search with automatic bounds
void searchGolombSequentialV4(int n, int maxLen, GolombRuler& best);

// Search with custom initial bound (use known optimal for faster search)
void searchGolombSequentialV4WithBound(int n, int initialBound, GolombRuler& best);

long long getExploredCountSequentialV4();
