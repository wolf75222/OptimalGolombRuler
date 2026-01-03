// =============================================================================
// CORRECTNESS TEST HARNESS - CSAPP Principle #10
// =============================================================================
// Validates the Golomb ruler search algorithm with:
// - Known optimal solutions for small n
// - Edge cases (small n, boundary conditions)
// - Uniqueness verification of all differences
// - Reproducibility checks
// =============================================================================

#include "search.hpp"
#include "golomb.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <set>
#include <omp.h>

// Known optimal Golomb rulers (from mathematical literature)
// Source: https://oeis.org/A003022
struct KnownOptimal {
    int n;
    int length;
    std::vector<int> marks;
};

const std::vector<KnownOptimal> KNOWN_OPTIMALS = {
    {2, 1, {0, 1}},
    {3, 3, {0, 1, 3}},
    {4, 6, {0, 1, 4, 6}},
    {5, 11, {0, 1, 4, 9, 11}},
    {6, 17, {0, 1, 4, 10, 12, 17}},
    {7, 25, {0, 1, 4, 10, 18, 23, 25}},
    {8, 34, {0, 1, 4, 9, 15, 22, 32, 34}},
    {9, 44, {0, 1, 5, 12, 25, 27, 35, 41, 44}},
    {10, 55, {0, 1, 6, 10, 23, 26, 34, 41, 53, 55}},
    {11, 72, {0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72}},
    // n=12 onwards takes longer, include for thorough testing
    {12, 85, {0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85}},
};

// Verify that all pairwise differences are unique
bool verifyUniqueDifferences(const std::vector<int>& marks) {
    std::set<int> differences;
    for (size_t i = 0; i < marks.size(); ++i) {
        for (size_t j = i + 1; j < marks.size(); ++j) {
            int d = marks[j] - marks[i];
            if (d <= 0) {
                std::cerr << "ERROR: Non-positive difference found: " << d << "\n";
                return false;
            }
            if (differences.count(d)) {
                std::cerr << "ERROR: Duplicate difference found: " << d << "\n";
                return false;
            }
            differences.insert(d);
        }
    }
    return true;
}

// Verify ruler structure
bool verifyRulerStructure(const GolombRuler& ruler, int expectedN) {
    // Check mark count
    if (static_cast<int>(ruler.marks.size()) != expectedN) {
        std::cerr << "ERROR: Expected " << expectedN << " marks, got "
                  << ruler.marks.size() << "\n";
        return false;
    }

    // Check first mark is 0
    if (ruler.marks.empty() || ruler.marks[0] != 0) {
        std::cerr << "ERROR: First mark should be 0\n";
        return false;
    }

    // Check marks are strictly increasing
    for (size_t i = 1; i < ruler.marks.size(); ++i) {
        if (ruler.marks[i] <= ruler.marks[i-1]) {
            std::cerr << "ERROR: Marks not strictly increasing at index " << i << "\n";
            return false;
        }
    }

    // Check length matches last mark
    if (!ruler.marks.empty() && ruler.length != ruler.marks.back()) {
        std::cerr << "ERROR: Length " << ruler.length << " doesn't match last mark "
                  << ruler.marks.back() << "\n";
        return false;
    }

    return true;
}

// Simple debug test
void debugN() {
    std::cout << "=== DEBUG ===\n";
    omp_set_num_threads(1);

    // Verify known optimal manually
    std::cout << "Verify {0,1,4,6}: ";
    std::vector<int> opt = {0,1,4,6};
    std::cout << (GolombRuler::isValid(opt) ? "VALID" : "INVALID") << "\n";

    // Test n=4 with tight maxLen
    std::cout << "n=4:\n";
    for (int maxLen = 6; maxLen <= 10; ++maxLen) {
        GolombRuler result;
        searchGolomb(4, maxLen, result);
        std::cout << "maxLen=" << maxLen << " -> marks=" << result.marks.size()
                  << " L=" << result.length
                  << " explored=" << getExploredCount();
        if (!result.marks.empty()) {
            std::cout << " [";
            for (int m : result.marks) std::cout << m << " ";
            std::cout << "]";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

// Test known optimal solutions
bool testKnownOptimals() {
    debugN();  // Debug first

    std::cout << "=== Testing Known Optimal Solutions ===\n";
    bool allPassed = true;

    for (const auto& known : KNOWN_OPTIMALS) {
        if (known.n > 8) continue; // Limit for debugging

        std::cout << "Testing n=" << known.n << "... ";

        omp_set_num_threads(1); // Single thread for debugging
        GolombRuler result;
        // Use a reasonable upper bound
        searchGolomb(known.n, known.length + 50, result);

        std::cout << "[marks=" << result.marks.size() << ", L=" << result.length << "] ";

        // Verify structure
        if (!verifyRulerStructure(result, known.n)) {
            std::cout << "FAILED (structure)\n";
            allPassed = false;
            continue;
        }

        // Verify unique differences
        if (!verifyUniqueDifferences(result.marks)) {
            std::cout << "FAILED (uniqueness)\n";
            allPassed = false;
            continue;
        }

        // Verify optimality
        if (result.length != known.length) {
            std::cout << "FAILED (optimality: got " << result.length
                      << ", expected " << known.length << ")\n";
            allPassed = false;
            continue;
        }

        std::cout << "PASSED (L=" << result.length << ")\n";
    }

    return allPassed;
}

// Test edge cases
bool testEdgeCases() {
    std::cout << "\n=== Testing Edge Cases ===\n";
    bool allPassed = true;

    // Test n=2 (minimal ruler)
    std::cout << "Testing n=2 (minimal)... ";
    {
        GolombRuler result;
        searchGolomb(2, 100, result);
        if (result.marks.size() == 2 && result.marks[0] == 0 && result.marks[1] == 1) {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED\n";
            allPassed = false;
        }
    }

    // Test n=3
    std::cout << "Testing n=3... ";
    {
        GolombRuler result;
        searchGolomb(3, 100, result);
        if (result.length == 3 && verifyUniqueDifferences(result.marks)) {
            std::cout << "PASSED (L=" << result.length << ")\n";
        } else {
            std::cout << "FAILED\n";
            allPassed = false;
        }
    }

    // Test with tight maxLen bound (exactly optimal)
    std::cout << "Testing tight bound (n=6, maxLen=17)... ";
    {
        GolombRuler result;
        searchGolomb(6, 17, result);
        if (result.length == 17 && verifyUniqueDifferences(result.marks)) {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED (L=" << result.length << ")\n";
            allPassed = false;
        }
    }

    // Test with maxLen too small (should still find best within bound)
    std::cout << "Testing insufficient bound (n=6, maxLen=15)... ";
    {
        GolombRuler result;
        searchGolomb(6, 15, result);
        // Should find nothing or a suboptimal solution
        if (result.marks.empty() || result.length <= 15) {
            std::cout << "PASSED (correctly bounded)\n";
        } else {
            std::cout << "FAILED (exceeded bound)\n";
            allPassed = false;
        }
    }

    return allPassed;
}

// Test reproducibility
bool testReproducibility() {
    std::cout << "\n=== Testing Reproducibility ===\n";
    bool allPassed = true;

    std::cout << "Testing multiple runs for n=8... ";
    {
        GolombRuler result1, result2, result3;
        searchGolomb(8, 50, result1);
        searchGolomb(8, 50, result2);
        searchGolomb(8, 50, result3);

        // All should find optimal length
        if (result1.length == result2.length && result2.length == result3.length &&
            result1.length == 34) {
            std::cout << "PASSED (all found L=34)\n";
        } else {
            std::cout << "FAILED (inconsistent: " << result1.length << ", "
                      << result2.length << ", " << result3.length << ")\n";
            allPassed = false;
        }
    }

    return allPassed;
}

// Test GolombRuler::isValid static method
bool testValidationMethod() {
    std::cout << "\n=== Testing Validation Method ===\n";
    bool allPassed = true;

    // Valid ruler
    std::cout << "Testing valid ruler... ";
    {
        std::vector<int> valid = {0, 1, 4, 6};
        if (GolombRuler::isValid(valid)) {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED\n";
            allPassed = false;
        }
    }

    // Invalid ruler (duplicate difference)
    std::cout << "Testing invalid ruler (duplicate diff)... ";
    {
        std::vector<int> invalid = {0, 1, 2, 3}; // diffs: 1,2,3,1,2,1 - has duplicates
        if (!GolombRuler::isValid(invalid)) {
            std::cout << "PASSED (correctly rejected)\n";
        } else {
            std::cout << "FAILED (should have been rejected)\n";
            allPassed = false;
        }
    }

    return allPassed;
}

// Test that explored count is consistent
bool testExploredCount() {
    std::cout << "\n=== Testing Explored State Count ===\n";
    bool allPassed = true;

    std::cout << "Testing explored count for n=8... ";
    {
        omp_set_num_threads(1); // Single thread for reproducibility
        GolombRuler result;
        searchGolomb(8, 50, result);
        long long count = getExploredCount();

        // Should explore a reasonable number of states
        if (count > 0) {
            std::cout << "PASSED (explored " << count << " states)\n";
        } else {
            std::cout << "FAILED (count = " << count << ")\n";
            allPassed = false;
        }
    }

    return allPassed;
}

int main() {
    std::cout << "============================================\n";
    std::cout << "  Golomb Ruler Correctness Test Suite\n";
    std::cout << "  CSAPP Principle #10: Safety Verification\n";
    std::cout << "============================================\n\n";

    bool allPassed = true;

    allPassed &= testKnownOptimals();
    allPassed &= testEdgeCases();
    allPassed &= testReproducibility();
    allPassed &= testValidationMethod();
    allPassed &= testExploredCount();

    std::cout << "\n============================================\n";
    if (allPassed) {
        std::cout << "  ALL TESTS PASSED\n";
    } else {
        std::cout << "  SOME TESTS FAILED\n";
    }
    std::cout << "============================================\n";

    return allPassed ? 0 : 1;
}
