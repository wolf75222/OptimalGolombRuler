#pragma once

#include "golomb.hpp"

// =============================================================================
// SEARCH SEQUENTIAL V2 - BitSet128 shift-based optimization
// =============================================================================
// Key optimizations from V5 applied to sequential:
// - BitSet128 (2x uint64_t) instead of explicit marks array
// - reversed_marks encoding: bit i set means mark at (ruler_length - i)
// - O(1) collision detection: (reversed_marks << offset) & used_dist
// - No marks array copy on push - just shift + set bit 0
// - 2x faster than V1 sequential for large n
// =============================================================================

void searchGolombSequentialV2(int n, int maxLen, GolombRuler& best);
long long getExploredCountSequentialV2();
