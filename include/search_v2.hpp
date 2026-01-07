#pragma once

#include "golomb.hpp"

// Version 2: Algorithme avec reversed_marks et bitset shift
// Cette version utilise une représentation inversée des marques
// et calcule les nouvelles différences via des décalages bitset

void searchGolombV2(int n, int maxLen, GolombRuler& best);
long long getExploredCountV2();
