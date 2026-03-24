#pragma once

#include <atomic>

namespace bybit {

// Lightweight spinlock for ultra-short critical sections outside hot path.
class SpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // spin
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#elif defined(__aarch64__)
            asm volatile("yield");
#endif
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

    bool try_lock() noexcept {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock) noexcept : lock_(lock) { lock_.lock(); }
    ~SpinLockGuard() noexcept { lock_.unlock(); }
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
private:
    SpinLock& lock_;
};

} // namespace bybit
