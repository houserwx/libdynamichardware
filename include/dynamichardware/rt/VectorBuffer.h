#pragma once

// ============================================================================
// VectorBuffer.h — Lock-free SPSC (Single-Producer / Single-Consumer) ring
// buffer with pre-allocated storage.
//
// Ownership / threading invariant:
//   - Exactly ONE producer thread calls tryPush().
//   - Exactly ONE consumer thread calls drain().
//   - Any other usage pattern (MPSC, MPMC) is UNDEFINED BEHAVIOUR.
//
// Real-time safety:
//   - tryPush()  is noexcept, lock-free, and allocation-free after construction.
//   - drain()    is called from the non-RT consumer thread.
//   - Construction allocates once.  Never call construction again after
//     the RT thread has started.
//
// Capacity must be a power of two — enforced by static_assert at construction.
// ============================================================================

#include <atomic>
#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

namespace common::rt {

template<typename T>
class VectorBuffer {
public:
    explicit VectorBuffer(std::size_t capacity)
        : capacity_(capacity)
        , mask_(capacity - 1)
        , storage_(capacity)
        , writeIdx_(0)
        , readIdx_(0)
    {
        assert(capacity > 0 && (capacity & mask_) == 0
               && "VectorBuffer: capacity must be a power of two");
    }

    VectorBuffer(const VectorBuffer&)            = delete;
    VectorBuffer& operator=(const VectorBuffer&) = delete;
    VectorBuffer(VectorBuffer&&)                 = delete;
    VectorBuffer& operator=(VectorBuffer&&)      = delete;

    /// Producer side (RT-safe: noexcept, lock-free, no allocation).
    [[nodiscard]] bool tryPush(const T& item) noexcept
    {
        const std::size_t writePos = writeIdx_.load(std::memory_order_relaxed);
        const std::size_t nextPos  = (writePos + 1) & mask_;

        if (nextPos == readIdx_.load(std::memory_order_acquire)) {
            return false;  // buffer full — drop
        }

        storage_[writePos] = item;
        writeIdx_.store(nextPos, std::memory_order_release);
        return true;
    }

    /// Consumer side, single item (RT-safe: noexcept, lock-free).
    [[nodiscard]] bool tryPop(T& out) noexcept
    {
        const std::size_t readPos  = readIdx_.load(std::memory_order_relaxed);
        const std::size_t writePos = writeIdx_.load(std::memory_order_acquire);
        if (readPos == writePos) return false;
        out = storage_[readPos];
        readIdx_.store((readPos + 1) & mask_, std::memory_order_release);
        return true;
    }

    /// Consumer side (non-RT; may only be called by ONE thread).
    template<typename Consumer>
    void drain(Consumer consumer) noexcept
    {
        const std::size_t readPos  = readIdx_.load(std::memory_order_relaxed);
        const std::size_t writePos = writeIdx_.load(std::memory_order_acquire);

        if (readPos == writePos) return;  // empty

        const std::size_t available = (writePos - readPos) & mask_;
        if (available == 0) return;

        consumer(std::span<const T>(storage_.data() + readPos, available));
        readIdx_.store(writePos, std::memory_order_release);
    }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<T>    storage_;
    std::atomic<std::size_t> writeIdx_;
    std::atomic<std::size_t> readIdx_;
};

} // namespace common::rt
