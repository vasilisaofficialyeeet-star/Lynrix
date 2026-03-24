#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace bybit {

// Fast double parsing for price/qty strings from exchange.
// Avoids locale overhead of std::stod.
inline double fast_atof(const char* str, size_t len) noexcept {
    // For short numeric strings, hand-rolled is faster than strtod
    double result = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;
    bool negative = false;
    bool in_fraction = false;

    size_t i = 0;
    if (i < len && str[i] == '-') {
        negative = true;
        ++i;
    }

    for (; i < len; ++i) {
        char c = str[i];
        if (c == '.') {
            in_fraction = true;
            continue;
        }
        if (c < '0' || c > '9') break;

        if (in_fraction) {
            divisor *= 10.0;
            fraction += (c - '0') / divisor;
        } else {
            result = result * 10.0 + (c - '0');
        }
    }

    result += fraction;
    return negative ? -result : result;
}

// Parse double from null-terminated string
inline double fast_atof(const char* str) noexcept {
    return fast_atof(str, std::strlen(str));
}

} // namespace bybit
