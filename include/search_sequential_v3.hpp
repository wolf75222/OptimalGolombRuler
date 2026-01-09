#pragma once

#include "golomb.hpp"

// =============================================================================
// SEARCH SEQUENTIAL V3 - SIMD optimized (SSE2/AVX)
// =============================================================================
// Key optimizations over V2:
// - Native __m128i SIMD operations instead of 2x uint64_t
// - Single instruction any() check with _mm_testz_si128
// - Avoid double shift by reusing new_dist
// - Local bestLen cache to reduce memory access
// - Prefetch next stack frame
// - Removed debug counter from hot path
// =============================================================================

void searchGolombSequentialV3(int n, int maxLen, GolombRuler& best);
long long getExploredCountSequentialV3();
