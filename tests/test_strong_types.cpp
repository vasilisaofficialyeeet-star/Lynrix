#include "../src/core/strong_types.h"
#include "../src/core/hot_path.h"
#include "../src/core/clock_source.h"
#include "../src/core/memory_policy.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <type_traits>

// ---- Test Harness -----------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) \
    static void test_##name(); \
    struct Reg_##name { Reg_##name() { test_##name(); } } g_reg_##name; \
    static void test_##name()

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; return; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    if (!((a) == (b))) { \
        std::printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
        ++g_fail; return; \
    } \
} while(0)

#define CHECK_NEAR(a, b, eps) do { \
    if (std::abs(static_cast<double>(a) - static_cast<double>(b)) > (eps)) { \
        std::printf("  FAIL %s:%d: |%s - %s| > %s (got %g vs %g)\n", \
                    __FILE__, __LINE__, #a, #b, #eps, \
                    static_cast<double>(a), static_cast<double>(b)); \
        ++g_fail; return; \
    } \
} while(0)

#define PASS() do { ++g_pass; } while(0)

using namespace bybit;

// =============================================================================
// Section 1: Compile-Time Layout Verification
// =============================================================================

TEST(sizeof_price) { static_assert(sizeof(Price) == sizeof(double)); PASS(); }
TEST(sizeof_qty) { static_assert(sizeof(Qty) == sizeof(double)); PASS(); }
TEST(sizeof_notional) { static_assert(sizeof(Notional) == sizeof(double)); PASS(); }
TEST(sizeof_bps) { static_assert(sizeof(BasisPoints) == sizeof(double)); PASS(); }
TEST(sizeof_ticksize) { static_assert(sizeof(TickSize) == sizeof(double)); PASS(); }
TEST(sizeof_timestamp_ns) { static_assert(sizeof(TimestampNs) == sizeof(uint64_t)); PASS(); }
TEST(sizeof_duration_ns) { static_assert(sizeof(DurationNs) == sizeof(int64_t)); PASS(); }
TEST(sizeof_tsc_ticks) { static_assert(sizeof(TscTicks) == sizeof(uint64_t)); PASS(); }
TEST(sizeof_sequence_number) { static_assert(sizeof(SequenceNumber) == sizeof(uint64_t)); PASS(); }
TEST(sizeof_order_id) { static_assert(sizeof(OrderId) == 48); PASS(); }
TEST(sizeof_instrument_id) { static_assert(sizeof(InstrumentId) == 16); PASS(); }

// Critical: TimestampNs and DurationNs are NOT the same type
TEST(timestamp_duration_distinct) {
    static_assert(!std::is_same_v<TimestampNs, DurationNs>);
    PASS();
}

// DurationNs is signed
TEST(duration_is_signed) {
    static_assert(std::is_signed_v<decltype(DurationNs{}.v)>);
    PASS();
}

// TimestampNs is unsigned
TEST(timestamp_is_unsigned) {
    static_assert(std::is_unsigned_v<decltype(TimestampNs{}.v)>);
    PASS();
}

// All types are trivially copyable
TEST(trivially_copyable) {
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
    PASS();
}

// =============================================================================
// Section 2: Price Arithmetic
// =============================================================================

TEST(price_default_zero) {
    Price p;
    CHECK_NEAR(p.raw(), 0.0, 1e-15);
    PASS();
}

TEST(price_explicit_ctor) {
    Price p{100.5};
    CHECK_NEAR(p.raw(), 100.5, 1e-15);
    PASS();
}

TEST(price_addition) {
    Price a{100.0}, b{0.5};
    CHECK_NEAR((a + b).raw(), 100.5, 1e-15);
    PASS();
}

TEST(price_subtraction) {
    Price a{100.5}, b{0.5};
    CHECK_NEAR((a - b).raw(), 100.0, 1e-15);
    PASS();
}

TEST(price_compound_add) {
    Price a{100.0};
    a += Price{0.5};
    CHECK_NEAR(a.raw(), 100.5, 1e-15);
    PASS();
}

TEST(price_scalar_mul) {
    Price p{100.0};
    CHECK_NEAR((p * 1.5).raw(), 150.0, 1e-15);
    CHECK_NEAR((2.0 * p).raw(), 200.0, 1e-15);
    PASS();
}

TEST(price_scalar_div) {
    Price p{100.0};
    CHECK_NEAR((p / 2.0).raw(), 50.0, 1e-15);
    PASS();
}

TEST(price_ratio) {
    Price a{150.0}, b{100.0};
    double ratio = a / b;
    CHECK_NEAR(ratio, 1.5, 1e-15);
    PASS();
}

TEST(price_comparison) {
    Price a{100.0}, b{100.5};
    CHECK(a < b);
    CHECK(b > a);
    CHECK(a != b);
    CHECK(a <= b);
    CHECK(b >= a);
    Price c{100.0};
    CHECK(a == c);
    PASS();
}

TEST(price_negation) {
    Price p{100.0};
    CHECK_NEAR((-p).raw(), -100.0, 1e-15);
    PASS();
}

TEST(price_abs) {
    CHECK_NEAR(Price{-100.0}.abs().raw(), 100.0, 1e-15);
    CHECK_NEAR(Price{100.0}.abs().raw(), 100.0, 1e-15);
    PASS();
}

TEST(price_predicates) {
    CHECK(Price{0.0}.is_zero());
    CHECK(Price{1e-13}.is_zero());
    CHECK(!Price{0.01}.is_zero());
    CHECK(Price{1.0}.is_positive());
    CHECK(Price{-1.0}.is_negative());
    CHECK(Price{100.0}.is_finite());
    PASS();
}

// =============================================================================
// Section 3: Qty Arithmetic
// =============================================================================

TEST(qty_arithmetic) {
    Qty a{1.0}, b{0.5};
    CHECK_NEAR((a + b).raw(), 1.5, 1e-15);
    CHECK_NEAR((a - b).raw(), 0.5, 1e-15);
    CHECK_NEAR((a * 3.0).raw(), 3.0, 1e-15);
    CHECK_NEAR((a / 2.0).raw(), 0.5, 1e-15);
    CHECK_NEAR(a / b, 2.0, 1e-15);
    PASS();
}

// =============================================================================
// Section 4: Notional
// =============================================================================

TEST(notional_from_price_qty) {
    Price p{100.0};
    Qty q{0.5};
    Notional n = notional(p, q);
    CHECK_NEAR(n.raw(), 50.0, 1e-15);
    PASS();
}

TEST(notional_arithmetic) {
    Notional a{50.0}, b{30.0};
    CHECK_NEAR((a + b).raw(), 80.0, 1e-15);
    CHECK_NEAR((a - b).raw(), 20.0, 1e-15);
    CHECK_NEAR((a * 2.0).raw(), 100.0, 1e-15);
    PASS();
}

// =============================================================================
// Section 5: BasisPoints
// =============================================================================

TEST(bps_arithmetic) {
    BasisPoints a{100.0}, b{50.0};
    CHECK_NEAR((a + b).raw(), 150.0, 1e-15);
    CHECK_NEAR((a - b).raw(), 50.0, 1e-15);
    CHECK_NEAR((a * 2.0).raw(), 200.0, 1e-15);
    CHECK_NEAR(a / b, 2.0, 1e-15);
    PASS();
}

TEST(bps_negation) {
    BasisPoints a{100.0};
    CHECK_NEAR((-a).raw(), -100.0, 1e-15);
    PASS();
}

// =============================================================================
// Section 6: TimestampNs / DurationNs (the critical section)
// =============================================================================

TEST(timestamp_default_zero) {
    TimestampNs t;
    CHECK_EQ(t.raw(), 0ULL);
    CHECK(t.is_zero());
    PASS();
}

TEST(timestamp_plus_duration) {
    TimestampNs t{1000};
    DurationNs d{500};
    TimestampNs result = t + d;
    CHECK_EQ(result.raw(), 1500ULL);
    PASS();
}

TEST(timestamp_minus_duration) {
    TimestampNs t{1000};
    DurationNs d{500};
    TimestampNs result = t - d;
    CHECK_EQ(result.raw(), 500ULL);
    PASS();
}

TEST(timestamp_minus_timestamp_gives_duration) {
    TimestampNs a{1500};
    TimestampNs b{1000};
    DurationNs d = a - b;
    CHECK_EQ(d.raw(), 500LL);
    PASS();
}

TEST(timestamp_minus_timestamp_negative_duration) {
    TimestampNs a{1000};
    TimestampNs b{1500};
    DurationNs d = a - b;
    CHECK_EQ(d.raw(), -500LL);
    CHECK(d.is_negative());
    PASS();
}

TEST(timestamp_compound_plus_duration) {
    TimestampNs t{1000};
    t += DurationNs{500};
    CHECK_EQ(t.raw(), 1500ULL);
    PASS();
}

TEST(timestamp_compound_minus_duration) {
    TimestampNs t{1500};
    t -= DurationNs{500};
    CHECK_EQ(t.raw(), 1000ULL);
    PASS();
}

TEST(timestamp_comparison) {
    TimestampNs a{100}, b{200};
    CHECK(a < b);
    CHECK(b > a);
    CHECK(a != b);
    TimestampNs c{100};
    CHECK(a == c);
    PASS();
}

// TimestampNs + TimestampNs is a COMPILE ERROR.
// We verify this by checking the type system forbids it.
// (Cannot test compile errors at runtime, but the static_assert on distinct types
// plus the absence of operator+(TimestampNs, TimestampNs) ensures this.)

TEST(duration_arithmetic) {
    DurationNs a{1000}, b{500};
    CHECK_EQ((a + b).raw(), 1500LL);
    CHECK_EQ((a - b).raw(), 500LL);
    CHECK_EQ((a * 3LL).raw(), 3000LL);
    CHECK_EQ((2LL * a).raw(), 2000LL);
    CHECK_EQ((a / 2LL).raw(), 500LL);
    CHECK_EQ(a / b, 2LL);
    PASS();
}

TEST(duration_negative) {
    DurationNs d{-500};
    CHECK(d.is_negative());
    CHECK(!d.is_positive());
    CHECK_EQ(d.abs().raw(), 500LL);
    CHECK_EQ((-d).raw(), 500LL);
    PASS();
}

TEST(duration_zero) {
    DurationNs d{0};
    CHECK(d.is_zero());
    CHECK(!d.is_positive());
    CHECK(!d.is_negative());
    PASS();
}

// =============================================================================
// Section 7: TscTicks
// =============================================================================

TEST(tsc_interval) {
    TscTicks a{1000}, b{500};
    TscTicks diff = a - b;
    CHECK_EQ(diff.raw(), 500ULL);
    PASS();
}

TEST(tsc_comparison) {
    TscTicks a{100}, b{200};
    CHECK(a < b);
    CHECK(b > a);
    CHECK(a != b);
    PASS();
}

// =============================================================================
// Section 8: SequenceNumber
// =============================================================================

TEST(seq_increment) {
    SequenceNumber s{100};
    SequenceNumber next = s + 1;
    CHECK_EQ(next.raw(), 101ULL);
    s += 5;
    CHECK_EQ(s.raw(), 105ULL);
    PASS();
}

TEST(seq_gap_detection) {
    SequenceNumber expected{100};
    SequenceNumber received{103};
    uint64_t gap = received - expected;
    CHECK_EQ(gap, 3ULL); // 3 messages missing
    PASS();
}

TEST(seq_no_gap) {
    SequenceNumber expected{100};
    SequenceNumber received{100};
    uint64_t gap = received - expected;
    CHECK_EQ(gap, 0ULL);
    PASS();
}

TEST(seq_comparison) {
    SequenceNumber a{100}, b{200};
    CHECK(a < b);
    CHECK(b > a);
    CHECK(a != b);
    PASS();
}

// =============================================================================
// Section 9: Cross-Type Free Functions
// =============================================================================

TEST(price_diff_bps_positive) {
    Price a{101.0}, b{100.0};
    BasisPoints diff = price_diff_bps(a, b);
    CHECK_NEAR(diff.raw(), 100.0, 1e-10); // 1% = 100 bps
    PASS();
}

TEST(price_diff_bps_negative) {
    Price a{99.0}, b{100.0};
    BasisPoints diff = price_diff_bps(a, b);
    CHECK_NEAR(diff.raw(), -100.0, 1e-10);
    PASS();
}

TEST(price_diff_bps_zero_base) {
    Price a{100.0}, b{0.0};
    BasisPoints diff = price_diff_bps(a, b);
    CHECK_NEAR(diff.raw(), 0.0, 1e-15); // safe division
    PASS();
}

TEST(price_plus_bps_fn) {
    Price p{100.0};
    BasisPoints bps{100.0}; // +1%
    Price result = price_plus_bps(p, bps);
    CHECK_NEAR(result.raw(), 101.0, 1e-10);
    PASS();
}

TEST(spread_bps_fn) {
    Price bid{99.95}, ask{100.05};
    BasisPoints s = spread_bps(bid, ask);
    CHECK_NEAR(s.raw(), 10.0, 0.1); // ~10 bps
    PASS();
}

TEST(mid_price_fn) {
    Price bid{99.0}, ask{101.0};
    CHECK_NEAR(mid_price(bid, ask).raw(), 100.0, 1e-15);
    PASS();
}

TEST(microprice_equal_qty) {
    Price bid{99.0}, ask{101.0};
    Price mp = microprice(bid, ask, Qty{1.0}, Qty{1.0});
    CHECK_NEAR(mp.raw(), 100.0, 1e-15);
    PASS();
}

TEST(microprice_skewed) {
    Price bid{99.0}, ask{101.0};
    Price mp = microprice(bid, ask, Qty{3.0}, Qty{1.0});
    // (99*1 + 101*3) / 4 = 402/4 = 100.5
    CHECK_NEAR(mp.raw(), 100.5, 1e-15);
    PASS();
}

TEST(fraction_bps_roundtrip) {
    BasisPoints b{100.0};
    double f = bps_to_fraction(b);
    CHECK_NEAR(f, 0.01, 1e-15);
    BasisPoints b2 = fraction_to_bps(f);
    CHECK_NEAR(b2.raw(), 100.0, 1e-10);
    PASS();
}

// =============================================================================
// Section 10: Duration Convenience
// =============================================================================

TEST(ns_from_us_fn) {
    DurationNs d = ns_from_us(5.0);
    CHECK_EQ(d.raw(), 5000LL);
    PASS();
}

TEST(ns_from_ms_fn) {
    DurationNs d = ns_from_ms(1.5);
    CHECK_EQ(d.raw(), 1500000LL);
    PASS();
}

TEST(to_us_fn) {
    CHECK_NEAR(to_us(DurationNs{5000}), 5.0, 1e-10);
    PASS();
}

TEST(to_ms_fn) {
    CHECK_NEAR(to_ms(DurationNs{1500000}), 1.5, 1e-10);
    PASS();
}

TEST(to_ms_u64_fn) {
    CHECK_EQ(to_ms_u64(TimestampNs{1500000000ULL}), 1500ULL);
    PASS();
}

// =============================================================================
// Section 11: OB Domain Boundary (FixedPrice conversion)
// =============================================================================

TEST(from_fixed_basic) {
    TickSize tick{0.01};
    Price p = from_fixed(10050, tick);
    CHECK_NEAR(p.raw(), 100.5, 1e-10);
    PASS();
}

TEST(to_fixed_round_nearest) {
    TickSize tick{0.01};
    int64_t f = to_fixed(Price{100.505}, tick);
    CHECK_EQ(f, 10051LL); // rounds 100.505/0.01 = 10050.5 -> 10051
    PASS();
}

TEST(to_fixed_floor_fn) {
    TickSize tick{0.01};
    int64_t f = to_fixed_floor(Price{100.505}, tick);
    CHECK_EQ(f, 10050LL);
    PASS();
}

TEST(to_fixed_ceil_fn) {
    TickSize tick{0.01};
    int64_t f = to_fixed_ceil(Price{100.505}, tick);
    CHECK_EQ(f, 10051LL);
    PASS();
}

TEST(fixed_roundtrip) {
    TickSize tick{0.01};
    Price original{100.50};
    int64_t fixed = to_fixed(original, tick);
    Price restored = from_fixed(fixed, tick);
    CHECK_NEAR(restored.raw(), original.raw(), 1e-10);
    PASS();
}

// =============================================================================
// Section 12: OrderId / InstrumentId
// =============================================================================

TEST(orderid_default_empty) {
    OrderId id;
    CHECK(id.empty());
    PASS();
}

TEST(orderid_ctor) {
    OrderId id{"ABC123"};
    CHECK(!id.empty());
    CHECK_EQ(std::strcmp(id.c_str(), "ABC123"), 0);
    PASS();
}

TEST(orderid_equality) {
    OrderId a{"ABC123"}, b{"ABC123"}, c{"DEF456"};
    CHECK(a == b);
    CHECK(a != c);
    PASS();
}

TEST(orderid_set_clear) {
    OrderId id;
    id.set("XYZ789");
    CHECK_EQ(std::strcmp(id.c_str(), "XYZ789"), 0);
    id.clear();
    CHECK(id.empty());
    PASS();
}

TEST(orderid_truncation) {
    char long_str[100];
    std::memset(long_str, 'A', sizeof(long_str));
    long_str[99] = '\0';
    OrderId id{long_str};
    CHECK_EQ(std::strlen(id.c_str()), 47u); // 48 - 1 null
    PASS();
}

TEST(orderid_null_safe) {
    OrderId id{nullptr};
    CHECK(id.empty());
    id.set(nullptr);
    CHECK(id.empty());
    PASS();
}

TEST(instrumentid_basic) {
    InstrumentId sym{"BTCUSDT"};
    CHECK(!sym.empty());
    CHECK_EQ(std::strcmp(sym.c_str(), "BTCUSDT"), 0);
    PASS();
}

TEST(instrumentid_truncation) {
    InstrumentId sym{"VERYLONGSYMBOLNAME"};
    CHECK_EQ(std::strlen(sym.c_str()), 15u); // 16 - 1 null
    PASS();
}

// OrderId and InstrumentId are distinct types (cannot mix)
TEST(orderid_instrumentid_distinct) {
    static_assert(!std::is_same_v<OrderId, InstrumentId>);
    PASS();
}

// =============================================================================
// Section 13: HotResult
// =============================================================================

TEST(hot_result_success) {
    auto r = HotResult::success();
    CHECK(r.ok);
    CHECK(r.error == nullptr);
    CHECK(static_cast<bool>(r));
    PASS();
}

TEST(hot_result_fail) {
    auto r = HotResult::fail("test_error");
    CHECK(!r.ok);
    CHECK_EQ(std::strcmp(r.error, "test_error"), 0);
    CHECK(!static_cast<bool>(r));
    PASS();
}

// =============================================================================
// Section 14: Pipeline Stage Budgets
// =============================================================================

TEST(stage_names_valid) {
    CHECK_EQ(std::strcmp(stage_name(PipelineStage::OBValidate), "OBValidate"), 0);
    CHECK_EQ(std::strcmp(stage_name(PipelineStage::MLInference), "MLInference"), 0);
    CHECK_EQ(std::strcmp(stage_name(PipelineStage::UIPublish), "UIPublish"), 0);
    PASS();
}

TEST(stage_budget_values) {
    CHECK_EQ(STAGE_BUDGET_NS[0], 50ULL);       // OBValidate
    CHECK_EQ(STAGE_BUDGET_NS[3], 50'000ULL);   // MLInference
    CHECK_EQ(STAGE_BUDGET_NS[9], 0ULL);        // LogDeferred (cold, unbounded)
    CHECK_EQ(STAGE_BUDGET_NS[10], 0ULL);       // AnalyticsRL (cold, unbounded)
    PASS();
}

TEST(deferred_work_layout) {
    static_assert(sizeof(DeferredWork) == 64);
    static_assert(alignof(DeferredWork) == 64);
    PASS();
}

TEST(tick_latency_hot_path_sum) {
    TickLatency tl;
    tl.stage_ns[0] = 50;
    tl.stage_ns[1] = 5000;
    tl.stage_ns[2] = 500;
    tl.stage_ns[3] = 30000;
    tl.stage_ns[4] = 400;
    tl.stage_ns[5] = 300;
    tl.stage_ns[6] = 2000;
    tl.stage_ns[7] = 0;      // OrderSubmit (cold)
    tl.stage_ns[8] = 100;
    tl.stage_ns[11] = 200;   // UIPublish
    uint64_t hot = tl.hot_path_ns();
    CHECK_EQ(hot, 50ULL + 5000 + 500 + 30000 + 400 + 300 + 2000 + 0 + 100 + 200);
    PASS();
}

TEST(load_shed_state) {
    LoadShedState ls;
    ls.record_tick(false, 1);
    ls.record_tick(false, 2);
    ls.record_tick(true, 3);
    CHECK_EQ(ls.shed_count, 1ULL);
    CHECK_EQ(ls.total_ticks, 3ULL);
    CHECK_NEAR(ls.shed_rate_pct(), 33.333, 0.1);
    CHECK_EQ(ls.last_shed_tick, 3ULL);
    PASS();
}

// =============================================================================
// Section 15: Clock Sources
// =============================================================================

TEST(tsc_clock_monotonic) {
    TscClockSource clock;
    TimestampNs a = clock.now();
    for (int i = 0; i < 1000; ++i) { asm volatile("" ::: "memory"); }
    TimestampNs b = clock.now();
    CHECK(b >= a);
    PASS();
}

TEST(mock_clock_manual) {
    MockClockSource clock{TimestampNs{1000}};
    CHECK_EQ(clock.now().raw(), 1000ULL);
    clock.advance(DurationNs{500});
    CHECK_EQ(clock.now().raw(), 1500ULL);
    clock.set(TimestampNs{9999});
    CHECK_EQ(clock.now().raw(), 9999ULL);
    PASS();
}

TEST(mock_clock_elapsed) {
    MockClockSource clock{TimestampNs{1000}};
    TscTicks start = clock.now_ticks();
    clock.advance(DurationNs{5000});
    DurationNs elapsed = clock.elapsed(start);
    CHECK_EQ(elapsed.raw(), 5000LL);
    PASS();
}

TEST(replay_clock_advance_to) {
    ReplayClockSource clock;
    CHECK_EQ(clock.now().raw(), 0ULL);
    clock.advance_to(TimestampNs{1'000'000'000ULL});
    CHECK_EQ(clock.now().raw(), 1'000'000'000ULL);
    PASS();
}

TEST(replay_clock_advance_by) {
    ReplayClockSource clock{TimestampNs{1000}};
    clock.advance_by(DurationNs{500});
    CHECK_EQ(clock.now().raw(), 1500ULL);
    PASS();
}

TEST(replay_clock_elapsed) {
    ReplayClockSource clock{TimestampNs{1000}};
    TscTicks start = clock.now_ticks();
    clock.advance_to(TimestampNs{6000});
    DurationNs elapsed = clock.elapsed(start);
    CHECK_EQ(elapsed.raw(), 5000LL);
    PASS();
}

TEST(clock_fn_from_tsc) {
    ClockFn fn = ClockFn::from_tsc();
    TimestampNs t = fn.now();
    CHECK(t.raw() > 0);
    PASS();
}

TEST(clock_fn_from_mock) {
    MockClockSource mock{TimestampNs{42}};
    ClockFn fn = ClockFn::from(mock);
    CHECK_EQ(fn.now().raw(), 42ULL);
    mock.set(TimestampNs{999});
    CHECK_EQ(fn.now().raw(), 999ULL);
    PASS();
}

TEST(clock_fn_size) {
    static_assert(sizeof(ClockFn) == 2 * sizeof(void*));
    PASS();
}

// Concept verification (compile-time only)
TEST(clock_concept) {
    static_assert(ClockSourceConcept<TscClockSource>);
    static_assert(ClockSourceConcept<ReplayClockSource>);
    static_assert(ClockSourceConcept<MockClockSource>);
    PASS();
}

TEST(clock_wall_ms) {
    MockClockSource clock{TimestampNs{1'500'000'000ULL}};
    CHECK_EQ(clock.wall_ms(), 1500ULL);
    PASS();
}

// =============================================================================
// Section 16: Memory Policy
// =============================================================================

TEST(cacheline_constant) {
#if defined(__APPLE__) && defined(__aarch64__)
    CHECK_EQ(BYBIT_CACHELINE, 128u);
#else
    CHECK_EQ(BYBIT_CACHELINE, 64u);
#endif
    PASS();
}

TEST(cache_line_padded_layout) {
    static_assert(sizeof(CacheLinePadded<uint64_t>) == BYBIT_CACHELINE);
    static_assert(alignof(CacheLinePadded<uint64_t>) == BYBIT_CACHELINE);
    PASS();
}

TEST(cache_line_padded_usage) {
    CacheLinePadded<uint64_t> x{42};
    CHECK_EQ(*x, 42ULL);
    *x = 99;
    CHECK_EQ(*x, 99ULL);
    PASS();
}

TEST(isolated_counter_layout) {
    static_assert(sizeof(IsolatedCounter) == BYBIT_CACHELINE);
    static_assert(alignof(IsolatedCounter) == BYBIT_CACHELINE);
    PASS();
}

TEST(isolated_counter_usage) {
    IsolatedCounter c;
    CHECK_EQ(c.load(), 0ULL);
    c.increment();
    CHECK_EQ(c.load(), 1ULL);
    c.fetch_add(5);
    CHECK_EQ(c.load(), 6ULL);
    c.store(100);
    CHECK_EQ(c.load(), 100ULL);
    PASS();
}

TEST(isolated_atomic_double_layout) {
    static_assert(sizeof(IsolatedAtomicDouble) == BYBIT_CACHELINE);
    static_assert(alignof(IsolatedAtomicDouble) == BYBIT_CACHELINE);
    PASS();
}

TEST(isolated_atomic_double_usage) {
    IsolatedAtomicDouble d;
    CHECK_NEAR(d.load(), 0.0, 1e-15);
    d.store(3.14);
    CHECK_NEAR(d.load(), 3.14, 1e-10);
    PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::printf("\n=== Stage 1 V2: Strong Types + Hot Path + Clock + Memory ===\n\n");

    // All tests auto-registered via static constructors

    std::printf("\n--- Results ---\n");
    std::printf("  Passed: %d\n", g_pass);
    std::printf("  Failed: %d\n", g_fail);
    std::printf("  Total:  %d\n", g_pass + g_fail);

    return g_fail > 0 ? 1 : 0;
}
