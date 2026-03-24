#pragma once
// ---- Semantic Strong Types for Low-Latency Trading Engine --------------------
//
// Each type is a distinct struct. NOT a template instantiation.
// This prevents:
//   TimestampNs + TimestampNs  (compile error: meaningless)
//   Price + Qty                (compile error: incompatible units)
//   Price * Qty                (compile error: use notional() free function)
//   SequenceNumber * 2         (compile error: meaningless)
//
// Allowed cross-type operations are free functions with explicit result types:
//   notional(Price, Qty) -> Notional
//   price_diff_bps(Price, Price) -> BasisPoints
//   TimestampNs + DurationNs -> TimestampNs
//   TimestampNs - TimestampNs -> DurationNs
//
// DurationNs is SIGNED (int64_t). Negative durations represent deadline overruns.
// TimestampNs is UNSIGNED (uint64_t). Monotonic, always positive.
//
// No feature flag. Types are always strong. Migration uses compat aliases at call sites.

#include <cstdint>
#include <cstring>
#include <cmath>
#include <type_traits>

namespace bybit {

// ---- Price ------------------------------------------------------------------
// Domain: signal/execution layer. Representation: double.
// The order book uses int64 fixed-point (FixedPrice) separately.
// Conversion between domains is explicit via from_fixed() / to_fixed().

struct Price {
    double v{};

    constexpr Price() noexcept = default;
    constexpr explicit Price(double x) noexcept : v(x) {}

    constexpr double raw() const noexcept { return v; }

    // Same-type arithmetic (averaging, spread)
    friend constexpr Price operator+(Price a, Price b) noexcept { return Price{a.v + b.v}; }
    friend constexpr Price operator-(Price a, Price b) noexcept { return Price{a.v - b.v}; }
    friend constexpr Price& operator+=(Price& a, Price b) noexcept { a.v += b.v; return a; }
    friend constexpr Price& operator-=(Price& a, Price b) noexcept { a.v -= b.v; return a; }

    // Scalar (dimensionless) scaling
    friend constexpr Price operator*(Price p, double s) noexcept { return Price{p.v * s}; }
    friend constexpr Price operator*(double s, Price p) noexcept { return Price{s * p.v}; }
    friend constexpr Price operator/(Price p, double s) noexcept { return Price{p.v / s}; }
    friend constexpr Price& operator*=(Price& p, double s) noexcept { p.v *= s; return p; }
    friend constexpr Price& operator/=(Price& p, double s) noexcept { p.v /= s; return p; }

    // Price / Price -> dimensionless ratio
    friend constexpr double operator/(Price a, Price b) noexcept { return a.v / b.v; }

    // Comparison
    friend constexpr bool operator==(Price a, Price b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(Price a, Price b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(Price a, Price b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(Price a, Price b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(Price a, Price b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(Price a, Price b) noexcept { return a.v >= b.v; }

    // Unary
    constexpr Price operator-() const noexcept { return Price{-v}; }
    constexpr Price abs() const noexcept { return Price{v < 0 ? -v : v}; }

    // Predicates
    constexpr bool is_zero(double eps = 1e-12) const noexcept { return v > -eps && v < eps; }
    constexpr bool is_positive() const noexcept { return v > 0.0; }
    constexpr bool is_negative() const noexcept { return v < 0.0; }
    bool is_finite() const noexcept { return std::isfinite(v); }
};

// ---- Qty --------------------------------------------------------------------
// Fractional quantities (crypto: 0.00001 BTC). Representation: double.

struct Qty {
    double v{};

    constexpr Qty() noexcept = default;
    constexpr explicit Qty(double x) noexcept : v(x) {}

    constexpr double raw() const noexcept { return v; }

    friend constexpr Qty operator+(Qty a, Qty b) noexcept { return Qty{a.v + b.v}; }
    friend constexpr Qty operator-(Qty a, Qty b) noexcept { return Qty{a.v - b.v}; }
    friend constexpr Qty& operator+=(Qty& a, Qty b) noexcept { a.v += b.v; return a; }
    friend constexpr Qty& operator-=(Qty& a, Qty b) noexcept { a.v -= b.v; return a; }

    friend constexpr Qty operator*(Qty q, double s) noexcept { return Qty{q.v * s}; }
    friend constexpr Qty operator*(double s, Qty q) noexcept { return Qty{s * q.v}; }
    friend constexpr Qty operator/(Qty q, double s) noexcept { return Qty{q.v / s}; }
    friend constexpr Qty& operator*=(Qty& q, double s) noexcept { q.v *= s; return q; }
    friend constexpr Qty& operator/=(Qty& q, double s) noexcept { q.v /= s; return q; }

    friend constexpr double operator/(Qty a, Qty b) noexcept { return a.v / b.v; }

    friend constexpr bool operator==(Qty a, Qty b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(Qty a, Qty b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(Qty a, Qty b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(Qty a, Qty b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(Qty a, Qty b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(Qty a, Qty b) noexcept { return a.v >= b.v; }

    constexpr Qty operator-() const noexcept { return Qty{-v}; }
    constexpr Qty abs() const noexcept { return Qty{v < 0 ? -v : v}; }
    constexpr bool is_zero(double eps = 1e-12) const noexcept { return v > -eps && v < eps; }
    constexpr bool is_positive() const noexcept { return v > 0.0; }
    constexpr bool is_negative() const noexcept { return v < 0.0; }
    bool is_finite() const noexcept { return std::isfinite(v); }
};

// ---- Notional ---------------------------------------------------------------
// Result of Price * Qty. Not constructible from raw arithmetic.

struct Notional {
    double v{};

    constexpr Notional() noexcept = default;
    constexpr explicit Notional(double x) noexcept : v(x) {}

    constexpr double raw() const noexcept { return v; }

    friend constexpr Notional operator+(Notional a, Notional b) noexcept { return Notional{a.v + b.v}; }
    friend constexpr Notional operator-(Notional a, Notional b) noexcept { return Notional{a.v - b.v}; }
    friend constexpr Notional& operator+=(Notional& a, Notional b) noexcept { a.v += b.v; return a; }
    friend constexpr Notional& operator-=(Notional& a, Notional b) noexcept { a.v -= b.v; return a; }
    friend constexpr Notional operator*(Notional n, double s) noexcept { return Notional{n.v * s}; }
    friend constexpr Notional operator*(double s, Notional n) noexcept { return Notional{s * n.v}; }
    friend constexpr Notional operator/(Notional n, double s) noexcept { return Notional{n.v / s}; }

    friend constexpr bool operator==(Notional a, Notional b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(Notional a, Notional b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(Notional a, Notional b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(Notional a, Notional b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(Notional a, Notional b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(Notional a, Notional b) noexcept { return a.v >= b.v; }

    constexpr Notional abs() const noexcept { return Notional{v < 0 ? -v : v}; }
    constexpr bool is_zero(double eps = 1e-12) const noexcept { return v > -eps && v < eps; }
    bool is_finite() const noexcept { return std::isfinite(v); }
};

// ---- BasisPoints ------------------------------------------------------------
// 1 bps = 0.01% = 0.0001 as fraction.

struct BasisPoints {
    double v{};

    constexpr BasisPoints() noexcept = default;
    constexpr explicit BasisPoints(double x) noexcept : v(x) {}

    constexpr double raw() const noexcept { return v; }

    friend constexpr BasisPoints operator+(BasisPoints a, BasisPoints b) noexcept { return BasisPoints{a.v + b.v}; }
    friend constexpr BasisPoints operator-(BasisPoints a, BasisPoints b) noexcept { return BasisPoints{a.v - b.v}; }
    friend constexpr BasisPoints& operator+=(BasisPoints& a, BasisPoints b) noexcept { a.v += b.v; return a; }
    friend constexpr BasisPoints& operator-=(BasisPoints& a, BasisPoints b) noexcept { a.v -= b.v; return a; }
    friend constexpr BasisPoints operator*(BasisPoints b, double s) noexcept { return BasisPoints{b.v * s}; }
    friend constexpr BasisPoints operator*(double s, BasisPoints b) noexcept { return BasisPoints{s * b.v}; }
    friend constexpr BasisPoints operator/(BasisPoints b, double s) noexcept { return BasisPoints{b.v / s}; }
    friend constexpr double operator/(BasisPoints a, BasisPoints b) noexcept { return a.v / b.v; }

    friend constexpr bool operator==(BasisPoints a, BasisPoints b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(BasisPoints a, BasisPoints b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(BasisPoints a, BasisPoints b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(BasisPoints a, BasisPoints b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(BasisPoints a, BasisPoints b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(BasisPoints a, BasisPoints b) noexcept { return a.v >= b.v; }

    constexpr BasisPoints operator-() const noexcept { return BasisPoints{-v}; }
    constexpr BasisPoints abs() const noexcept { return BasisPoints{v < 0 ? -v : v}; }
    constexpr bool is_zero(double eps = 1e-6) const noexcept { return v > -eps && v < eps; }
};

// ---- TickSize ---------------------------------------------------------------
// Instrument-specific minimum price increment. Used in OB fixed-point conversion.

struct TickSize {
    double v{};

    constexpr TickSize() noexcept = default;
    constexpr explicit TickSize(double x) noexcept : v(x) {}

    constexpr double raw() const noexcept { return v; }

    friend constexpr bool operator==(TickSize a, TickSize b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(TickSize a, TickSize b) noexcept { return a.v != b.v; }

    constexpr bool is_zero(double eps = 1e-15) const noexcept { return v > -eps && v < eps; }
    // No arithmetic. TickSize is a configuration constant, not a computed value.
};

// ---- TimestampNs ------------------------------------------------------------
// Monotonic nanosecond timestamp. UNSIGNED. Always positive.
// TimestampNs + TimestampNs is NOT defined (compile error).
// TimestampNs - TimestampNs -> DurationNs (defined below, after DurationNs).

struct DurationNs; // forward declaration for cross-type ops

struct TimestampNs {
    uint64_t v{};

    constexpr TimestampNs() noexcept = default;
    constexpr explicit TimestampNs(uint64_t x) noexcept : v(x) {}

    constexpr uint64_t raw() const noexcept { return v; }

    // TimestampNs + DurationNs -> TimestampNs  (defined after DurationNs)
    // TimestampNs - DurationNs -> TimestampNs  (defined after DurationNs)
    // TimestampNs - TimestampNs -> DurationNs  (defined after DurationNs)

    // Comparison
    friend constexpr bool operator==(TimestampNs a, TimestampNs b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(TimestampNs a, TimestampNs b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(TimestampNs a, TimestampNs b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(TimestampNs a, TimestampNs b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(TimestampNs a, TimestampNs b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(TimestampNs a, TimestampNs b) noexcept { return a.v >= b.v; }

    constexpr bool is_zero() const noexcept { return v == 0; }

    // NO operator+, operator-, operator* with TimestampNs.
    // This is intentional. TimestampNs + TimestampNs is a compile error.
};

// ---- DurationNs -------------------------------------------------------------
// SIGNED nanosecond duration. Negative = deadline overrun.
// int64_t because (actual - deadline) can be negative.

struct DurationNs {
    int64_t v{};

    constexpr DurationNs() noexcept = default;
    constexpr explicit DurationNs(int64_t x) noexcept : v(x) {}

    constexpr int64_t raw() const noexcept { return v; }

    // Duration + Duration -> Duration
    friend constexpr DurationNs operator+(DurationNs a, DurationNs b) noexcept { return DurationNs{a.v + b.v}; }
    friend constexpr DurationNs operator-(DurationNs a, DurationNs b) noexcept { return DurationNs{a.v - b.v}; }
    friend constexpr DurationNs& operator+=(DurationNs& a, DurationNs b) noexcept { a.v += b.v; return a; }
    friend constexpr DurationNs& operator-=(DurationNs& a, DurationNs b) noexcept { a.v -= b.v; return a; }

    // Duration * scalar -> Duration
    friend constexpr DurationNs operator*(DurationNs d, int64_t s) noexcept { return DurationNs{d.v * s}; }
    friend constexpr DurationNs operator*(int64_t s, DurationNs d) noexcept { return DurationNs{s * d.v}; }
    friend constexpr DurationNs operator/(DurationNs d, int64_t s) noexcept { return DurationNs{d.v / s}; }

    // Duration / Duration -> ratio
    friend constexpr int64_t operator/(DurationNs a, DurationNs b) noexcept { return a.v / b.v; }

    // Comparison
    friend constexpr bool operator==(DurationNs a, DurationNs b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(DurationNs a, DurationNs b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(DurationNs a, DurationNs b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(DurationNs a, DurationNs b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(DurationNs a, DurationNs b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(DurationNs a, DurationNs b) noexcept { return a.v >= b.v; }

    constexpr DurationNs operator-() const noexcept { return DurationNs{-v}; }
    constexpr DurationNs abs() const noexcept { return DurationNs{v < 0 ? -v : v}; }
    constexpr bool is_zero() const noexcept { return v == 0; }
    constexpr bool is_positive() const noexcept { return v > 0; }
    constexpr bool is_negative() const noexcept { return v < 0; }
};

// ---- TimestampNs cross-type operators (require DurationNs to be complete) ----

inline constexpr TimestampNs operator+(TimestampNs t, DurationNs d) noexcept {
    return TimestampNs{static_cast<uint64_t>(static_cast<int64_t>(t.v) + d.v)};
}
inline constexpr TimestampNs operator-(TimestampNs t, DurationNs d) noexcept {
    return TimestampNs{static_cast<uint64_t>(static_cast<int64_t>(t.v) - d.v)};
}
inline constexpr TimestampNs& operator+=(TimestampNs& t, DurationNs d) noexcept {
    t.v = static_cast<uint64_t>(static_cast<int64_t>(t.v) + d.v);
    return t;
}
inline constexpr TimestampNs& operator-=(TimestampNs& t, DurationNs d) noexcept {
    t.v = static_cast<uint64_t>(static_cast<int64_t>(t.v) - d.v);
    return t;
}
inline constexpr DurationNs operator-(TimestampNs a, TimestampNs b) noexcept {
    return DurationNs{static_cast<int64_t>(a.v) - static_cast<int64_t>(b.v)};
}
// TimestampNs + TimestampNs is intentionally NOT defined. Compile error.

// ---- TscTicks ---------------------------------------------------------------
// Raw TSC counter. Opaque. NOT convertible to nanoseconds without calibration.
// Only operation: interval measurement (TscTicks - TscTicks -> TscTicks).

struct TscTicks {
    uint64_t v{};

    constexpr TscTicks() noexcept = default;
    constexpr explicit TscTicks(uint64_t x) noexcept : v(x) {}

    constexpr uint64_t raw() const noexcept { return v; }

    // Interval: TscTicks - TscTicks -> TscTicks (represents elapsed ticks)
    friend constexpr TscTicks operator-(TscTicks a, TscTicks b) noexcept { return TscTicks{a.v - b.v}; }

    // Comparison (for ordering)
    friend constexpr bool operator==(TscTicks a, TscTicks b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(TscTicks a, TscTicks b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(TscTicks a, TscTicks b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(TscTicks a, TscTicks b) noexcept { return a.v > b.v; }

    // NO addition, multiplication. TscTicks + TscTicks is meaningless.
};

// ---- SequenceNumber ---------------------------------------------------------
// Monotonic exchange sequence number. Gap = data loss.

struct SequenceNumber {
    uint64_t v{};

    constexpr SequenceNumber() noexcept = default;
    constexpr explicit SequenceNumber(uint64_t x) noexcept : v(x) {}

    constexpr uint64_t raw() const noexcept { return v; }

    // Increment: SequenceNumber + uint64_t -> SequenceNumber
    friend constexpr SequenceNumber operator+(SequenceNumber s, uint64_t n) noexcept { return SequenceNumber{s.v + n}; }
    friend constexpr SequenceNumber& operator+=(SequenceNumber& s, uint64_t n) noexcept { s.v += n; return s; }

    // Gap detection: SequenceNumber - SequenceNumber -> uint64_t
    friend constexpr uint64_t operator-(SequenceNumber a, SequenceNumber b) noexcept { return a.v - b.v; }

    // Comparison
    friend constexpr bool operator==(SequenceNumber a, SequenceNumber b) noexcept { return a.v == b.v; }
    friend constexpr bool operator!=(SequenceNumber a, SequenceNumber b) noexcept { return a.v != b.v; }
    friend constexpr bool operator<(SequenceNumber a, SequenceNumber b) noexcept { return a.v < b.v; }
    friend constexpr bool operator>(SequenceNumber a, SequenceNumber b) noexcept { return a.v > b.v; }
    friend constexpr bool operator<=(SequenceNumber a, SequenceNumber b) noexcept { return a.v <= b.v; }
    friend constexpr bool operator>=(SequenceNumber a, SequenceNumber b) noexcept { return a.v >= b.v; }

    constexpr bool is_zero() const noexcept { return v == 0; }

    // NO multiplication, NO SequenceNumber + SequenceNumber. Both are meaningless.
};

// ---- OrderId ----------------------------------------------------------------
// Fixed-size, stack-allocated. No heap. 48 bytes.

struct OrderId {
    char data[48] = {};

    OrderId() noexcept = default;

    explicit OrderId(const char* s) noexcept {
        if (s) {
            std::strncpy(data, s, sizeof(data) - 1);
            data[sizeof(data) - 1] = '\0';
        }
    }

    bool operator==(const OrderId& o) const noexcept { return std::strcmp(data, o.data) == 0; }
    bool operator!=(const OrderId& o) const noexcept { return std::strcmp(data, o.data) != 0; }

    bool empty() const noexcept { return data[0] == '\0'; }
    const char* c_str() const noexcept { return data; }
    void clear() noexcept { data[0] = '\0'; }

    void set(const char* s) noexcept {
        if (s) { std::strncpy(data, s, sizeof(data) - 1); data[sizeof(data) - 1] = '\0'; }
        else { data[0] = '\0'; }
    }
};

// ---- InstrumentId -----------------------------------------------------------
// Fixed-size, stack-allocated. No heap. 16 bytes. Distinct from OrderId.

struct InstrumentId {
    char data[16] = {};

    InstrumentId() noexcept = default;

    explicit InstrumentId(const char* s) noexcept {
        if (s) {
            std::strncpy(data, s, sizeof(data) - 1);
            data[sizeof(data) - 1] = '\0';
        }
    }

    bool operator==(const InstrumentId& o) const noexcept { return std::strcmp(data, o.data) == 0; }
    bool operator!=(const InstrumentId& o) const noexcept { return std::strcmp(data, o.data) != 0; }

    bool empty() const noexcept { return data[0] == '\0'; }
    const char* c_str() const noexcept { return data; }
    void clear() noexcept { data[0] = '\0'; }

    void set(const char* s) noexcept {
        if (s) { std::strncpy(data, s, sizeof(data) - 1); data[sizeof(data) - 1] = '\0'; }
        else { data[0] = '\0'; }
    }
};

// ---- Cross-Type Free Functions ----------------------------------------------
// These are the ONLY legal way to combine different semantic types.

// Price * Qty -> Notional (the fundamental cross-type product)
inline constexpr Notional notional(Price p, Qty q) noexcept {
    return Notional{p.v * q.v};
}

// Qty * Price -> Notional (convenience overload, same semantics)
inline constexpr Notional notional(Qty q, Price p) noexcept {
    return Notional{q.v * p.v};
}

// Slippage: |actual - expected| / expected in bps
inline constexpr BasisPoints slippage_bps(Price actual, Price expected) noexcept {
    if (expected.v < 1e-15 && expected.v > -1e-15) return BasisPoints{0.0};
    return BasisPoints{std::abs(actual.v - expected.v) / std::abs(expected.v) * 10000.0};
}

// Price difference in basis points: (a - b) / b * 10000
inline constexpr BasisPoints price_diff_bps(Price a, Price b) noexcept {
    if (b.v < 1e-15 && b.v > -1e-15) return BasisPoints{0.0};
    return BasisPoints{(a.v - b.v) / b.v * 10000.0};
}

// Apply basis point offset to price: p * (1 + bps/10000)
inline constexpr Price price_plus_bps(Price p, BasisPoints bps) noexcept {
    return Price{p.v * (1.0 + bps.v / 10000.0)};
}

// Spread in basis points relative to mid
inline constexpr BasisPoints spread_bps(Price bid, Price ask) noexcept {
    double mid = (bid.v + ask.v) * 0.5;
    if (mid < 1e-15 && mid > -1e-15) return BasisPoints{0.0};
    return BasisPoints{(ask.v - bid.v) / mid * 10000.0};
}

// Mid price
inline constexpr Price mid_price(Price bid, Price ask) noexcept {
    return Price{(bid.v + ask.v) * 0.5};
}

// Microprice (volume-weighted mid)
inline constexpr Price microprice(Price bid, Price ask, Qty bq, Qty aq) noexcept {
    double total = bq.v + aq.v;
    if (total < 1e-15) return mid_price(bid, ask);
    return Price{(bid.v * aq.v + ask.v * bq.v) / total};
}

// BasisPoints <-> fraction conversion
inline constexpr BasisPoints fraction_to_bps(double fraction) noexcept {
    return BasisPoints{fraction * 10000.0};
}
inline constexpr double bps_to_fraction(BasisPoints bps) noexcept {
    return bps.v / 10000.0;
}

// ---- Duration convenience constructors --------------------------------------

inline constexpr DurationNs ns_from_us(double us) noexcept {
    return DurationNs{static_cast<int64_t>(us * 1000.0)};
}
inline constexpr DurationNs ns_from_ms(double ms) noexcept {
    return DurationNs{static_cast<int64_t>(ms * 1000000.0)};
}
inline constexpr double to_us(DurationNs d) noexcept {
    return static_cast<double>(d.v) / 1000.0;
}
inline constexpr double to_ms(DurationNs d) noexcept {
    return static_cast<double>(d.v) / 1000000.0;
}
inline constexpr uint64_t to_ms_u64(TimestampNs t) noexcept {
    return t.v / 1000000ULL;
}

// ---- OB Domain Boundary: FixedPrice <-> Price conversion --------------------
// Rounding policy: to_fixed rounds to nearest tick. ALWAYS.
// Floor/ceil variants are explicit.

inline constexpr Price from_fixed(int64_t fixed_price, TickSize tick) noexcept {
    return Price{static_cast<double>(fixed_price) * tick.v};
}

inline constexpr int64_t to_fixed(Price price, TickSize tick) noexcept {
    // Round to nearest
    return static_cast<int64_t>(price.v / tick.v + (price.v >= 0.0 ? 0.5 : -0.5));
}

inline constexpr int64_t to_fixed_floor(Price price, TickSize tick) noexcept {
    double raw = price.v / tick.v;
    auto t = static_cast<int64_t>(raw);
    return (raw < 0.0 && static_cast<double>(t) != raw) ? t - 1 : t;
}

inline constexpr int64_t to_fixed_ceil(Price price, TickSize tick) noexcept {
    double raw = price.v / tick.v;
    auto t = static_cast<int64_t>(raw);
    return (raw > 0.0 && static_cast<double>(t) != raw) ? t + 1 : t;
}

// ---- Compile-Time Layout Verification ---------------------------------------

static_assert(sizeof(Price) == sizeof(double), "Price must be zero-cost");
static_assert(sizeof(Qty) == sizeof(double), "Qty must be zero-cost");
static_assert(sizeof(Notional) == sizeof(double), "Notional must be zero-cost");
static_assert(sizeof(BasisPoints) == sizeof(double), "BasisPoints must be zero-cost");
static_assert(sizeof(TickSize) == sizeof(double), "TickSize must be zero-cost");
static_assert(sizeof(TimestampNs) == sizeof(uint64_t), "TimestampNs must be zero-cost");
static_assert(sizeof(DurationNs) == sizeof(int64_t), "DurationNs must be zero-cost");
static_assert(sizeof(TscTicks) == sizeof(uint64_t), "TscTicks must be zero-cost");
static_assert(sizeof(SequenceNumber) == sizeof(uint64_t), "SequenceNumber must be zero-cost");
static_assert(sizeof(OrderId) == 48, "OrderId must be exactly 48 bytes");
static_assert(sizeof(InstrumentId) == 16, "InstrumentId must be exactly 16 bytes");

static_assert(alignof(Price) == alignof(double));
static_assert(alignof(TimestampNs) == alignof(uint64_t));
static_assert(alignof(DurationNs) == alignof(int64_t));

static_assert(std::is_trivially_copyable_v<Price>);
static_assert(std::is_trivially_copyable_v<Qty>);
static_assert(std::is_trivially_copyable_v<Notional>);
static_assert(std::is_trivially_copyable_v<BasisPoints>);
static_assert(std::is_trivially_copyable_v<TimestampNs>);
static_assert(std::is_trivially_copyable_v<DurationNs>);
static_assert(std::is_trivially_copyable_v<TscTicks>);
static_assert(std::is_trivially_copyable_v<SequenceNumber>);
static_assert(std::is_trivially_copyable_v<OrderId>);
static_assert(std::is_trivially_copyable_v<InstrumentId>);

// Verify TimestampNs and DurationNs are NOT the same type
static_assert(!std::is_same_v<TimestampNs, DurationNs>,
    "TimestampNs and DurationNs must be distinct types");

} // namespace bybit
