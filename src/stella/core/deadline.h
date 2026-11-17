#pragma once
// Monotonic millisecond arithmetic that is correct across the 49.7-day wrap of
// a 32-bit millisecond counter. Never compare raw timestamps with < or >.
#include <stdint.h>

namespace stella {

// True once `now` is at or after `deadline`, valid while the two are within
// 2^31 ms (24.8 days) of each other.
constexpr bool reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

// Milliseconds from `since` to `now`; a `since` slightly in the future (sampled
// on another core) counts as zero rather than as ~49 days.
constexpr uint32_t elapsedMs(uint32_t now, uint32_t since) {
  return (int32_t)(now - since) > 0 ? now - since : 0;
}

// Milliseconds until `deadline`, or 0 if already reached.
constexpr uint32_t remainingMs(uint32_t now, uint32_t deadline) {
  return reached(now, deadline) ? 0 : deadline - now;
}

constexpr uint32_t minWait(uint32_t a, uint32_t b) { return a < b ? a : b; }

}  // namespace stella
