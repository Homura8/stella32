#pragma once
// Values pushed to the device by a script on the home network (the remote
// page's `POST /api/push`): "cpu=37%&temp=61C" or one pair per line. They feed
// the Values app, so the screen can show a PC's CPU and temperatures, a Home
// Assistant sensor, the state of a CI run, anything that fits in a few
// characters. They live in RAM only (live numbers are worthless after a restart)
// and each remembers when it last arrived, so the screen can dim one that has
// gone quiet.
//
// Everything here is untrusted input and bounded: at most eight values, keys of
// letters, digits, space, '_', '-' and '.', control characters removed, and text
// cut at a UTF-8 boundary.
#include <stddef.h>
#include <stdint.h>

namespace stella {

namespace topics {
constexpr uint16_t kValues = 5;  // Topic<PushedValues>
}

constexpr int kMaxPushedValues = 8;
constexpr size_t kPushKeyBytes = 14;    // including the NUL: 13 characters
constexpr size_t kPushValueBytes = 22;  // 21 bytes of UTF-8

struct PushedValue {
  char key[kPushKeyBytes] = {0};
  char value[kPushValueBytes] = {0};
  uint32_t updatedMs = 0;  // when it last arrived (the device's millis)
};

struct PushedValues {
  uint8_t count = 0;
  uint32_t generation = 0;  // changes with every accepted push
  PushedValue items[kMaxPushedValues];
};

struct PushResult {
  int accepted = 0;  // pairs stored, changed or removed
  int rejected = 0;  // pairs that were not acceptable
};

// Read the pairs in `body` (separated by '&' or line breaks; "key=value"; %XX
// escapes decoded only where two hex digits follow) into `state`. A key seen again
// is updated in place; a new key takes a free slot, or replaces the one that has
// been quiet longest; an empty value removes the key. Order is the order the keys
// first arrived in.
PushResult applyPush(PushedValues &state, const char *body, size_t length, uint32_t nowMs);

}  // namespace stella
