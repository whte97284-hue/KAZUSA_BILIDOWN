#include "RateLimiter.hpp"
#include <thread>

TokenBucketLimiter::TokenBucketLimiter(size_t bytesPerSec)
    : m_bytesPerSec(bytesPerSec), m_lastRefill(std::chrono::steady_clock::now()) {}

void TokenBucketLimiter::setRate(size_t bytesPerSec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bytesPerSec.store(bytesPerSec, std::memory_order_relaxed);
    // 调速时重置桶, 避免旧速率积累的令牌被瞬间耗尽
    m_tokens = 0.0;
    m_lastRefill = std::chrono::steady_clock::now();
}

void TokenBucketLimiter::refillLocked() {
    if (m_bytesPerSec.load(std::memory_order_relaxed) == 0) return;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastRefill).count();
    m_lastRefill = now;
    if (elapsed <= 0.0) return;
    // 桶容量 = 每秒速率, 允许突发一个整秒
    double cap = static_cast<double>(m_bytesPerSec.load(std::memory_order_relaxed));
    m_tokens = std::min(m_tokens + elapsed * cap, cap);
}

void TokenBucketLimiter::acquire(size_t bytes) {
    const size_t rate = m_bytesPerSec.load(std::memory_order_relaxed);
    if (rate == 0 || bytes == 0) return; // 不限速, 零开销

    // 计算需要的等待时间: 令牌不足 → 补足差额
    double neededSec = static_cast<double>(bytes) / static_cast<double>(rate);
    std::unique_lock<std::mutex> lock(m_mutex);
    refillLocked();
    double deficit = neededSec - m_tokens / static_cast<double>(rate);
    if (deficit > 0.0) {
        // 释放锁睡眠, 避免阻塞其他线程的令牌补充
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::duration<double>(deficit));
        lock.lock();
    }
    // 扣减令牌 (取整到已消费字节)
    double consumed = static_cast<double>(bytes) / static_cast<double>(rate);
    m_tokens -= consumed * static_cast<double>(rate);
    if (m_tokens < 0.0) m_tokens = 0.0;
    m_lastRefill = std::chrono::steady_clock::now();
}
