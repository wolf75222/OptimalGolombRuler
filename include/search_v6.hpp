#pragma once

#include "golomb.hpp"

// =============================================================================
// SEARCH V6 - Branchless optimized for AMD EPYC Zen4
// =============================================================================
// Key optimizations:
// - Branchless 128-bit shift (no branch misprediction)
// - No SSE2 intrinsics overhead (GCC auto-vectorizes with -march=native)
// - Cache-line aligned structures (64 bytes)
// - Prefetch hints for stack frames
// - __builtin_expect for branch prediction hints
// =============================================================================

void searchGolombV6(int n, int maxLen, GolombRuler& best, int prefixDepth = 0);
long long getExploredCountV6();
