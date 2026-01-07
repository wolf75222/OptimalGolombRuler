#pragma once

#include "golomb.hpp"

// =============================================================================
// SEARCH V6 - SIMD optimized with __m128i (SSE2)
// =============================================================================
// Key optimizations over V5:
// - Uses __m128i for 128-bit operations (single register)
// - Branchless shift implementation
// - SIMD AND/OR/XOR operations
// - Better register utilization
// =============================================================================

void searchGolombV6(int n, int maxLen, GolombRuler& best, int prefixDepth = 0);
long long getExploredCountV6();
