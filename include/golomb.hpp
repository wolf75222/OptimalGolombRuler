#pragma once

#include <vector>
#include <bitset>
#include <iostream>

constexpr int MAX_DIFF = 256;

struct GolombRuler {
    std::vector<int> marks;
    int length = 0;

    static inline bool isValid(const std::vector<int>& marks) {
        std::bitset<MAX_DIFF> seen;
        const size_t size = marks.size();

        for (size_t i = 0; i < size; ++i) {
            const int mi = marks[i];
            for (size_t j = i + 1; j < size; ++j) {
                const int d = marks[j] - mi;
                if (d >= MAX_DIFF) return false;
                if (seen[d]) return false;
                seen.set(d);
            }
        }
        return true;
    }

    constexpr void computeLength() noexcept {
        length = marks.empty() ? 0 : marks.back();
    }

    friend std::ostream& operator<<(std::ostream& os, const GolombRuler& r) {
        os << "{ ";
        for (auto m : r.marks) os << m << " ";
        os << "} (L=" << r.length << ")";
        return os;
    }
};
