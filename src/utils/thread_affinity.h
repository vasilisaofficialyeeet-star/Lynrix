#pragma once

// ─── Thread Affinity & QoS for Apple Silicon ────────────────────────────────
// Provides thread-to-core pinning via Darwin's thread_policy and QoS classes.
//
// Apple Silicon does NOT support traditional CPU affinity (pthread_setaffinity).
// Instead we use:
//   1. thread_affinity_policy — affinity tags (threads with same tag share a core group)
//   2. thread_latency_qos_policy — latency QoS tier
//   3. pthread_set_qos_class_self_np — workgroup QoS
//
// For HFT hot-path threads, we set:
//   - QOS_CLASS_USER_INTERACTIVE (highest non-RT priority)
//   - Thread affinity tag to isolate pipeline stages
//   - Real-time scheduling for critical threads (requires root)

#include <cstdint>
#include <cstring>
#include <thread>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <sys/qos.h>
#include <spdlog/spdlog.h>

namespace bybit {

// ─── Affinity Tags ──────────────────────────────────────────────────────────
// Threads with the same tag are scheduled on the same core group.
// Different tags → different core groups (P-cores vs E-cores).

enum class AffinityGroup : int {
    WebSocket   = 1,   // I/O thread — can use E-core
    Parser      = 2,   // JSON parsing — P-core
    OrderBook   = 3,   // OB updates — P-core (critical)
    Features    = 4,   // Feature computation — P-core (SIMD)
    Model       = 5,   // ML inference — P-core (ANE)
    Execution   = 6,   // Order placement — P-core (critical)
    Risk        = 7,   // Risk checks — P-core
    Monitoring  = 8,   // Watchdog/metrics — E-core
};

// ─── Set Thread Affinity Tag ────────────────────────────────────────────────
// Groups threads with the same tag onto the same core complex.
// On M-series: P-cores (performance) vs E-cores (efficiency).

inline bool set_thread_affinity(AffinityGroup group) noexcept {
    thread_affinity_policy_data_t policy;
    policy.affinity_tag = static_cast<integer_t>(group);

    kern_return_t kr = thread_policy_set(
        mach_thread_self(),
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT
    );

    if (kr != KERN_SUCCESS) {
        spdlog::warn("[AFFINITY] Failed to set affinity tag {} (kern_return={})",
                     static_cast<int>(group), kr);
        return false;
    }
    return true;
}

// ─── Set Thread QoS ─────────────────────────────────────────────────────────
// Sets the QoS class for the calling thread.
// USER_INTERACTIVE = highest non-RT priority, minimal scheduler latency.

enum class ThreadQoS : uint8_t {
    UserInteractive = 0,  // Hot-path threads (OB, Features, Execution)
    UserInitiated   = 1,  // Important but not critical (Model, Risk)
    Default         = 2,  // Normal priority
    Utility         = 3,  // Background work (Monitoring, Logging)
    Background      = 4,  // Lowest priority (Persistence)
};

inline bool set_thread_qos(ThreadQoS qos) noexcept {
    qos_class_t cls;
    switch (qos) {
        case ThreadQoS::UserInteractive: cls = QOS_CLASS_USER_INTERACTIVE; break;
        case ThreadQoS::UserInitiated:   cls = QOS_CLASS_USER_INITIATED; break;
        case ThreadQoS::Default:         cls = QOS_CLASS_DEFAULT; break;
        case ThreadQoS::Utility:         cls = QOS_CLASS_UTILITY; break;
        case ThreadQoS::Background:      cls = QOS_CLASS_BACKGROUND; break;
    }

    int ret = pthread_set_qos_class_self_np(cls, 0);
    if (ret != 0) {
        spdlog::warn("[QOS] Failed to set QoS class {} (errno={})",
                     static_cast<int>(qos), ret);
        return false;
    }
    return true;
}

// ─── Set Thread Latency QoS ─────────────────────────────────────────────────
// Controls the scheduler's latency sensitivity for this thread.
// Tier 0 = most latency-sensitive (minimal preemption).

inline bool set_thread_latency_qos(int tier = 0) noexcept {
    thread_latency_qos_policy_data_t policy;
    policy.thread_latency_qos_tier = tier;

    kern_return_t kr = thread_policy_set(
        mach_thread_self(),
        THREAD_LATENCY_QOS_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_LATENCY_QOS_POLICY_COUNT
    );

    if (kr != KERN_SUCCESS) {
        spdlog::warn("[LATENCY_QOS] Failed to set latency tier {} (kern_return={})",
                     tier, kr);
        return false;
    }
    return true;
}

// ─── Set Thread Throughput QoS ──────────────────────────────────────────────
// Controls the scheduler's throughput preference for this thread.
// Tier 0 = maximum throughput (keeps on P-core).

inline bool set_thread_throughput_qos(int tier = 0) noexcept {
    thread_throughput_qos_policy_data_t policy;
    policy.thread_throughput_qos_tier = tier;

    kern_return_t kr = thread_policy_set(
        mach_thread_self(),
        THREAD_THROUGHPUT_QOS_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_THROUGHPUT_QOS_POLICY_COUNT
    );

    if (kr != KERN_SUCCESS) {
        spdlog::warn("[THROUGHPUT_QOS] Failed to set throughput tier {} (kern_return={})",
                     tier, kr);
        return false;
    }
    return true;
}

// ─── Set Thread Name ────────────────────────────────────────────────────────
// Sets a name visible in Instruments, Activity Monitor, and debugger.

inline void set_thread_name(const char* name) noexcept {
    pthread_setname_np(name);
}

// ─── Configure Hot-Path Thread ──────────────────────────────────────────────
// One-call setup for critical trading threads: affinity + QoS + latency tier.

inline void configure_hot_thread(AffinityGroup group, const char* name) noexcept {
    set_thread_name(name);
    set_thread_affinity(group);
    set_thread_qos(ThreadQoS::UserInteractive);
    set_thread_latency_qos(0);    // most latency-sensitive
    set_thread_throughput_qos(0); // keep on P-core

    spdlog::info("[THREAD] Configured '{}' — affinity={} qos=UserInteractive latency=0",
                 name, static_cast<int>(group));
}

// ─── Configure Background Thread ────────────────────────────────────────────
// Setup for monitoring/logging threads: low priority, E-core friendly.

inline void configure_background_thread(AffinityGroup group, const char* name) noexcept {
    set_thread_name(name);
    set_thread_affinity(group);
    set_thread_qos(ThreadQoS::Utility);
    set_thread_latency_qos(3);    // latency-tolerant
    set_thread_throughput_qos(3); // can migrate to E-core

    spdlog::info("[THREAD] Configured '{}' — affinity={} qos=Utility latency=3",
                 name, static_cast<int>(group));
}

// ─── Real-Time Thread Policy (requires root) ────────────────────────────────
// Sets fixed-priority real-time scheduling. Use with caution.
// period/computation/constraint in Mach absolute time units.

struct RealTimeParams {
    uint32_t period_ns       = 1'000'000;   // 1ms period
    uint32_t computation_ns  = 100'000;     // 100µs computation budget
    uint32_t constraint_ns   = 200'000;     // 200µs hard constraint
    bool     preemptible     = false;
};

inline bool set_realtime_policy(const RealTimeParams& params = {}) noexcept {
    // Convert ns to Mach time units
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    double ns_to_mach = static_cast<double>(tb.denom) / static_cast<double>(tb.numer);

    thread_time_constraint_policy_data_t policy;
    policy.period      = static_cast<uint32_t>(params.period_ns * ns_to_mach);
    policy.computation = static_cast<uint32_t>(params.computation_ns * ns_to_mach);
    policy.constraint  = static_cast<uint32_t>(params.constraint_ns * ns_to_mach);
    policy.preemptible = params.preemptible ? TRUE : FALSE;

    kern_return_t kr = thread_policy_set(
        mach_thread_self(),
        THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_TIME_CONSTRAINT_POLICY_COUNT
    );

    if (kr != KERN_SUCCESS) {
        spdlog::warn("[RT] Failed to set real-time policy (kern_return={}, may need root)", kr);
        return false;
    }
    spdlog::info("[RT] Real-time policy set: period={}ns comp={}ns constraint={}ns",
                 params.period_ns, params.computation_ns, params.constraint_ns);
    return true;
}

} // namespace bybit
