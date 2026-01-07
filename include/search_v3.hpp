#pragma once

#include "golomb.hpp"

// Version 3: Hybride - Le meilleur des deux mondes
// - Backtracking itératif avec pile manuelle (V1)
// - Bitset shift pour validation O(1) (V2)
// - Structures alignées cache (V1)

void searchGolombV3(int n, int maxLen, GolombRuler& best);
long long getExploredCountV3();
