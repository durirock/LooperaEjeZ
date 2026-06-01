#pragma once
#include <atomic>
#include <vector>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// SpscRing<T> — Single-Producer Single-Consumer ring buffer lock-free.
// REGLA: el callback de audio es el único productor. El hilo consumidor
//        (disco o UI) es el único consumidor. Nunca se invierten roles.
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
class SpscRing {
public:
    explicit SpscRing(size_t capacity)
        : mBuf(capacity + 1), mCap(capacity + 1) {}

    // Llamado desde el CALLBACK de audio (productor).
    // Devuelve false y descarta si el ring está lleno.
    // REGLA: nunca bloquear — si lleno, silencio en red/disco, no en audio.
    bool push(const T& item) noexcept {
        const size_t head = mHead.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % mCap;
        if (next == mTail.load(std::memory_order_acquire))
            return false; // lleno — descarta
        mBuf[head] = item;
        mHead.store(next, std::memory_order_release);
        return true;
    }

    // Llamado desde el hilo consumidor (disco / red).
    bool pop(T& out) noexcept {
        const size_t tail = mTail.load(std::memory_order_relaxed);
        if (tail == mHead.load(std::memory_order_acquire))
            return false; // vacío
        out = mBuf[tail];
        mTail.store((tail + 1) % mCap, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        size_t h = mHead.load(std::memory_order_acquire);
        size_t t = mTail.load(std::memory_order_acquire);
        return (h >= t) ? (h - t) : (mCap - t + h);
    }

    void reset() noexcept {
        mHead.store(0, std::memory_order_relaxed);
        mTail.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<T>       mBuf;
    const size_t         mCap;
    std::atomic<size_t>  mHead{0};
    // Separación de cache lines para evitar false sharing
    char _pad[64 - sizeof(std::atomic<size_t>)]{};
    std::atomic<size_t>  mTail{0};
};
