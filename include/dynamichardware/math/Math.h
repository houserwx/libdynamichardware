#pragma once

// ============================================================================
// Math.h — Common math types and helpers.
//
// Fixed-point helpers for sub-cycle precision in simulated encoder physics.
// ============================================================================

#include <cmath>
#include <array>
#include <cstdint>

namespace common::math {

// ---------------------------------------------------------------------------
// Fixed-point helpers (20-bit fractional).
// Used for sub-cycle precision in simulated encoder physics.
// ---------------------------------------------------------------------------
inline constexpr int FixedShift = 20;
inline constexpr int64_t FixedOne = 1LL << FixedShift;

[[nodiscard]] inline int64_t toFixed(float v) noexcept {
    return static_cast<int64_t>(v * static_cast<float>(FixedOne));
}

[[nodiscard]] inline float fromFixed(int64_t v) noexcept {
    return static_cast<float>(v) / static_cast<float>(FixedOne);
}

} // namespace common::math
