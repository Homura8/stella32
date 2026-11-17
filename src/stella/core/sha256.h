#pragma once
// SHA-256 (FIPS 180-4), small and allocation-free: used to check downloaded
// firmware images while they stream into flash.
#include <stddef.h>
#include <stdint.h>

namespace stella {

class Sha256 {
 public:
  Sha256() { reset(); }
  void reset();
  void update(const void *data, size_t size);
  void finish(uint8_t out[32]);  // the object must be reset() before reuse

  static void hash(const void *data, size_t size, uint8_t out[32]);

 private:
  void block(const uint8_t *p);
  uint32_t h_[8];
  uint8_t buffer_[64];
  size_t used_ = 0;
  uint64_t total_ = 0;
};

// Lowercase hex of `n` bytes into out (2n + 1 bytes, NUL-terminated).
void toHex(const uint8_t *data, size_t n, char *out);
// Parses exactly 2n hex digits (either case) into out; false if malformed.
bool fromHex(const char *hex, uint8_t *out, size_t n);

}  // namespace stella
