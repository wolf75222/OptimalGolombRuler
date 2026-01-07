#pragma once

#include "golomb.hpp"

// =============================================================================
// SEARCH V5 - Optimized with native uint64_t operations
// =============================================================================
// Key optimizations over V4:
// - Uses 2x uint64_t (128 bits) instead of std::bitset<256>
// - Direct bit operations without std::bitset overhead
// - Branchless conflict detection with OR + popcount
// - Better cache locality with smaller state structures
// =============================================================================

void searchGolombV5(int n, int maxLen, GolombRuler& best, int prefixDepth = 0);
long long getExploredCountV5();
