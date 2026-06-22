// ============================================================================
// spsc_ring.h — Lock-free single-producer / single-consumer fixed-size ring buffer.
// Stores trivially-copyable POD values only (no allocations). Power-of-two capacity 
// for bitwise mask ops instead of modulo division. Producer NEVER blocks or allocates.
// ============================================================================

#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

template <typename T, size_t N = 8192>
class SpscRing final {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert((N & (N - 1)) == 0 && N >= 2, "Capacity must be power of two ≥ 2");

    struct alignas(64) Slot final { std::atomic<size_t> seq{0}; T value{}; };
    alignas(64) Slot slots_[N];

private:
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};

public:
    constexpr SpscRing() noexcept = default;
    ~SpscRing() = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    /// ── Producer side (RT loop — MUST NOT BLOCK / ALLOCATE) ──────────────
    bool push(const T& item) noexcept {
        const size_t w      = write_pos_.load(std::memory_order_relaxed);
        const size_t next_w = (w + 1) & (N - 1);
        if (next_w == read_pos_.load(std::memory_order_acquire)) return false;
        auto& slot = slots_[w];
        slot.value = item;
        slot.seq.store(w + 1, std::memory_order_release);
        write_pos_.store(next_w, std::memory_order_release);
        return true;
    }

    /// ── Consumer side (stats thread — safe to block / sleep) ─────────────
    bool pop(T& out) noexcept {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t seq = slots_[r].seq.load(std::memory_order_acquire);
        if ((seq & (N - 1)) != ((r + 1) & (N - 1))) return false;
        out = slots_[r].value;
        read_pos_.store((r + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    template <typename F> void drain_all(F&& cb) noexcept { T t{}; while (pop(t)) cb(t); }
};
