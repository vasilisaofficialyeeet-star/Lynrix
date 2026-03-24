#pragma once

#include "../utils/clock.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/host_info.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace bybit {

// ─── System Monitor ─────────────────────────────────────────────────────────
// Real-time monitoring of CPU, memory, latency, and throughput.
// All metrics are lock-free and safe to read from UI thread.

struct SystemMonitorSnapshot {
    // CPU
    double cpu_usage_pct        = 0.0;   // process CPU usage %
    double system_cpu_pct       = 0.0;   // system-wide CPU %
    int    cpu_cores            = 0;

    // Memory
    uint64_t memory_used_bytes  = 0;     // process RSS
    uint64_t memory_peak_bytes  = 0;
    uint64_t system_memory_total = 0;
    uint64_t system_memory_free = 0;
    double   memory_used_mb     = 0.0;

    // Latency metrics (microseconds)
    double ws_latency_p50_us    = 0.0;
    double ws_latency_p99_us    = 0.0;
    double ob_latency_p50_us    = 0.0;
    double ob_latency_p99_us    = 0.0;
    double feat_latency_p50_us  = 0.0;
    double feat_latency_p99_us  = 0.0;
    double model_latency_p50_us = 0.0;
    double model_latency_p99_us = 0.0;
    double risk_latency_p50_us  = 0.0;
    double risk_latency_p99_us  = 0.0;
    double order_latency_p50_us = 0.0;
    double order_latency_p99_us = 0.0;
    double e2e_latency_p50_us   = 0.0;
    double e2e_latency_p99_us   = 0.0;

    // Network
    double exchange_latency_ms  = 0.0;   // round-trip to exchange
    double network_latency_ms   = 0.0;   // pure network latency
    uint64_t bytes_received     = 0;
    uint64_t bytes_sent         = 0;
    uint64_t messages_received  = 0;
    uint64_t messages_sent      = 0;

    // Throughput
    double ticks_per_sec        = 0.0;
    double signals_per_sec      = 0.0;
    double orders_per_sec       = 0.0;
    double fills_per_sec        = 0.0;

    // GPU (placeholder for future CUDA/Metal integration)
    bool   gpu_available        = false;
    double gpu_usage_pct        = 0.0;
    double gpu_memory_used_mb   = 0.0;
    const char* gpu_name        = "N/A";

    // Uptime
    uint64_t uptime_ns          = 0;
    double   uptime_hours       = 0.0;

    uint64_t last_update_ns     = 0;
};

class SystemMonitor {
public:
    SystemMonitor() noexcept {
        start_ns_ = Clock::now_ns();
        last_sample_ns_ = start_ns_;
        prev_ticks_ = 0;
        prev_signals_ = 0;
        prev_orders_ = 0;
        prev_fills_ = 0;

#ifdef __APPLE__
        snapshot_.cpu_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
        size_t mem_size = sizeof(uint64_t);
        uint64_t total_mem = 0;
        sysctlbyname("hw.memsize", &total_mem, &mem_size, nullptr, 0);
        snapshot_.system_memory_total = total_mem;
#endif
    }

    // Update system metrics (call every 1-2 seconds from timer)
    void update(uint64_t ticks, uint64_t signals,
                uint64_t orders, uint64_t fills) noexcept {
        uint64_t now = Clock::now_ns();
        double dt = static_cast<double>(now - last_sample_ns_) / 1e9;

        if (dt > 0.1) {
            // Throughput
            snapshot_.ticks_per_sec = (ticks - prev_ticks_) / dt;
            snapshot_.signals_per_sec = (signals - prev_signals_) / dt;
            snapshot_.orders_per_sec = (orders - prev_orders_) / dt;
            snapshot_.fills_per_sec = (fills - prev_fills_) / dt;

            prev_ticks_ = ticks;
            prev_signals_ = signals;
            prev_orders_ = orders;
            prev_fills_ = fills;
            last_sample_ns_ = now;
        }

        // CPU & Memory
        update_cpu_memory();

        // Uptime
        snapshot_.uptime_ns = now - start_ns_;
        snapshot_.uptime_hours = snapshot_.uptime_ns / 3.6e12;

        snapshot_.last_update_ns = now;
    }

    // Update latency metrics from Metrics struct
    void update_latencies(double ws_p50, double ws_p99,
                          double ob_p50, double ob_p99,
                          double feat_p50, double feat_p99,
                          double model_p50, double model_p99,
                          double risk_p50, double risk_p99,
                          double order_p50, double order_p99,
                          double e2e_p50, double e2e_p99) noexcept {
        snapshot_.ws_latency_p50_us = ws_p50;
        snapshot_.ws_latency_p99_us = ws_p99;
        snapshot_.ob_latency_p50_us = ob_p50;
        snapshot_.ob_latency_p99_us = ob_p99;
        snapshot_.feat_latency_p50_us = feat_p50;
        snapshot_.feat_latency_p99_us = feat_p99;
        snapshot_.model_latency_p50_us = model_p50;
        snapshot_.model_latency_p99_us = model_p99;
        snapshot_.risk_latency_p50_us = risk_p50;
        snapshot_.risk_latency_p99_us = risk_p99;
        snapshot_.order_latency_p50_us = order_p50;
        snapshot_.order_latency_p99_us = order_p99;
        snapshot_.e2e_latency_p50_us = e2e_p50;
        snapshot_.e2e_latency_p99_us = e2e_p99;
    }

    // Record network stats
    void update_network(double exchange_latency_ms, double network_latency_ms,
                        uint64_t bytes_rx, uint64_t bytes_tx,
                        uint64_t msgs_rx, uint64_t msgs_tx) noexcept {
        snapshot_.exchange_latency_ms = exchange_latency_ms;
        snapshot_.network_latency_ms = network_latency_ms;
        snapshot_.bytes_received = bytes_rx;
        snapshot_.bytes_sent = bytes_tx;
        snapshot_.messages_received = msgs_rx;
        snapshot_.messages_sent = msgs_tx;
    }

    const SystemMonitorSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void update_cpu_memory() noexcept {
#ifdef __APPLE__
        // Process CPU usage via Mach tasks
        task_info_data_t tinfo;
        mach_msg_type_number_t task_info_count = TASK_INFO_MAX;

        if (task_info(mach_task_self(), TASK_BASIC_INFO,
                      (task_info_t)tinfo, &task_info_count) == KERN_SUCCESS) {
            auto* basic = reinterpret_cast<task_basic_info_t>(tinfo);
            snapshot_.memory_used_bytes = basic->resident_size;
            snapshot_.memory_used_mb = basic->resident_size / (1024.0 * 1024.0);
            snapshot_.memory_peak_bytes = std::max(
                snapshot_.memory_peak_bytes, snapshot_.memory_used_bytes);
        }

        // Thread CPU times
        thread_array_t thread_list;
        mach_msg_type_number_t thread_count;
        if (task_threads(mach_task_self(), &thread_list, &thread_count) == KERN_SUCCESS) {
            double total_cpu = 0.0;
            for (mach_msg_type_number_t i = 0; i < thread_count; ++i) {
                thread_basic_info_data_t thinfo;
                mach_msg_type_number_t thinfo_count = THREAD_BASIC_INFO_COUNT;
                if (thread_info(thread_list[i], THREAD_BASIC_INFO,
                                (thread_info_t)&thinfo, &thinfo_count) == KERN_SUCCESS) {
                    if (!(thinfo.flags & TH_FLAGS_IDLE)) {
                        total_cpu += thinfo.cpu_usage / static_cast<double>(TH_USAGE_SCALE) * 100.0;
                    }
                }
                mach_port_deallocate(mach_task_self(), thread_list[i]);
            }
            vm_deallocate(mach_task_self(), (vm_address_t)thread_list,
                          thread_count * sizeof(thread_t));
            snapshot_.cpu_usage_pct = total_cpu;
        }

        // System free memory
        vm_statistics64_data_t vm_stat;
        mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info64_t)&vm_stat, &vm_count) == KERN_SUCCESS) {
            uint64_t page_size = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
            snapshot_.system_memory_free =
                (vm_stat.free_count + vm_stat.inactive_count) * page_size;
        }
#endif
    }

    SystemMonitorSnapshot snapshot_;
    uint64_t start_ns_ = 0;
    uint64_t last_sample_ns_ = 0;
    uint64_t prev_ticks_ = 0;
    uint64_t prev_signals_ = 0;
    uint64_t prev_orders_ = 0;
    uint64_t prev_fills_ = 0;
};

} // namespace bybit
