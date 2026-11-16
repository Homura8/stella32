#pragma once
// Freshness bookkeeping for any fetched data ("last-good data with explicit
// stale states", from Celestia Nightstand). Keep the last good value, record
// every attempt, and let the UI say *why* data might be old.
#include <stdint.h>

#include "../core/deadline.h"
#include "../core/text.h"

namespace stella {

enum class Freshness : uint8_t {
  Empty,    // never succeeded
  Fresh,    // last success within maxAge and the last attempt succeeded
  Stale,    // last success older than maxAge
  Failing,  // data is recent enough but the latest attempt failed
};

struct DataStatus {
  uint32_t updatedMs = 0;    // last success
  uint32_t attemptedMs = 0;  // last attempt (success or failure)
  uint16_t failures = 0;     // consecutive failures
  bool hasData = false;
  bool lastOk = false;
  char error[48] = {0};

  void succeeded(uint32_t now) {
    updatedMs = attemptedMs = now;
    failures = 0;
    hasData = lastOk = true;
    error[0] = '\0';
  }
  void failed(uint32_t now, const char *reason) {
    attemptedMs = now;
    if (failures < 0xFFFF) ++failures;
    lastOk = false;
    copyText(error, sizeof(error), reason);
  }
  Freshness freshness(uint32_t now, uint32_t maxAgeMs) const {
    if (!hasData) return Freshness::Empty;
    if (elapsedMs(now, updatedMs) > maxAgeMs) return Freshness::Stale;
    return lastOk ? Freshness::Fresh : Freshness::Failing;
  }
};

}  // namespace stella
