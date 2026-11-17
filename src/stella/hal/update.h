#pragma once
// The other app slot of a device with two (OTA layout): where a new firmware
// image is written and how it is checked. Optional part of a Board; called
// from the service thread only.
#include <stddef.h>
#include <stdint.h>

namespace stella {

class FirmwareSlot {
 public:
  virtual ~FirmwareSlot() = default;
  // True when an update can be installed: a second app slot exists.
  virtual bool available() = 0;
  // True when a signing key is built in: the update's signature is then
  // required and verified (the default is to require it). Without a key an
  // update is protected by HTTPS and its SHA-256 only.
  virtual bool requiresSignature() { return true; }
  virtual size_t capacity() = 0;  // bytes the other slot can hold
  virtual bool begin(size_t size) = 0;
  virtual bool write(const uint8_t *data, size_t size) = 0;
  // The image is complete and was verified by the caller: make it the next
  // boot image, pending confirmation (the bootloader rolls back to the old
  // image if it is not confirmed). False if the image is not a valid app.
  virtual bool finish() = 0;
  virtual void abort() = 0;
  // ECDSA P-256 over a 32-byte digest, r and s big-endian 32 bytes each,
  // against the public key built into this firmware.
  virtual bool verify(const uint8_t digest[32], const uint8_t r[32], const uint8_t s[32]) = 0;
};

}  // namespace stella
