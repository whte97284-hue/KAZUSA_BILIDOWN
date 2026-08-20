#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>

/**
 * @brief 全局速率限制抽象接口 (限速钩子)
 * 
 * 多线程分片引擎 (MultiThreadDownloader) 与单线程下载器 (BiliDownloader)
 * 共用同一个限速钩子: 每读取一块数据后调用 acquire(bytes) 进行限速。
 * 由上层 (BiliController) 创建具体实现并注入。
 */
class RateLimiter {
public:
    virtual ~RateLimiter() = default;

    /**
     * @brief 申请通过 bytes 字节的下载配额
     * 内部按令牌桶算法睡眠到配额许可, 线程安全, 可被多线程并发调用。
     * @param bytes 本次已读取的字节数
     */
    virtual void acquire(size_t bytes) = 0;
};

/**
 * @brief 令牌桶限速器 (Token Bucket)
 * 
 * - 0 = 不限速 (acquire 立即返回, 零开销)
 * - 按字节/秒速率补充令牌, 桶容量 = 每秒速率 (允许突发一个整秒)
 * - setRate 支持运行中动态调速 (例如设置页拖动滑块)
 * - 线程安全, 多下载线程共享同一实例即聚合限速 (总吞吐 ≤ 设定值)
 */
class TokenBucketLimiter : public RateLimiter {
public:
    explicit TokenBucketLimiter(size_t bytesPerSec = 0);
    ~TokenBucketLimiter() override = default;

    // 动态调整限速 (字节/秒; 0 = 不限速)
    void setRate(size_t bytesPerSec);
    size_t rate() const { return m_bytesPerSec.load(std::memory_order_relaxed); }

    void acquire(size_t bytes) override;

private:
    std::atomic<size_t> m_bytesPerSec;   // 限速目标 (字节/秒)
    std::mutex m_mutex;                  // 保护令牌桶状态
    double m_tokens = 0.0;               // 当前可用令牌
    std::chrono::steady_clock::time_point m_lastRefill; // 上次补充时刻

    // 按流逝时间补充令牌 (须持有 m_mutex)
    void refillLocked();
};
