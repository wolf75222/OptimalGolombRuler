#pragma once

#include "golomb.hpp"

// Version 4: Prefix-based parallelization + Iterative + Bitset shift
// - Generates prefixes up to depth D for better load balancing
// - Iterative backtracking with manual stack (V1/V3)
// - Bitset shift for O(1) validation (V2/V3)
// - Much better scaling on high thread counts (96-192 threads)

void searchGolombV4(int n, int maxLen, GolombRuler& best, int prefixDepth = 0);
long long getExploredCountV4();
