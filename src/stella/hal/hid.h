#pragma once
// A Bluetooth Low Energy keyboard and media-key device: the touchscreen as a
// macro pad for a computer (the CYD's USB port is serial only, so BLE is the way
// to act as a keyboard). The board supplies the radio; the Deck app uses it.
//
// The radio costs memory, so it runs only while someone wants it: `start()` when
// the Deck screen opens, `stop()` when it closes (and the runtime stops it for
// the length of an over-the-air update). State changes happen on the stack's
// own task, so `state()` must be a cheap, thread-safe read.
#include <stddef.h>
#include <stdint.h>

namespace stella {

enum class HidState : uint8_t {
  Off,          // the radio is not running
  Advertising,  // waiting for a computer to pair or reconnect
  Connected,    // a computer is connected (and encrypted)
  Failed,       // it could not start (see error())
};

// USB HID usages used by the Deck (HID usage tables 1.3).
namespace hid {
// Keyboard modifier bits (the modifier byte of a boot keyboard report).
constexpr uint8_t kCtrl = 0x01, kShift = 0x02, kAlt = 0x04, kGui = 0x08;
// Consumer-control usages (page 0x0C), sent as 16-bit values.
constexpr uint16_t kPlayPause = 0x00CD, kNext = 0x00B5, kPrevious = 0x00B6, kStop = 0x00B7;
constexpr uint16_t kVolumeUp = 0x00E9, kVolumeDown = 0x00EA, kMute = 0x00E2;
}  // namespace hid

class HidDevice {
 public:
  virtual ~HidDevice() = default;
  // Whether this build has a radio and the stack for it.
  virtual bool available() const = 0;
  // Start advertising under `name` (the name the computer shows). Returns false
  // if it could not start (then state() is Failed and error() says why).
  virtual bool start(const char *name) = 0;
  virtual void stop() = 0;
  virtual HidState state() const = 0;
  virtual const char *error() const { return ""; }
  // Press and release one key (a keyboard usage, with modifier bits) or one
  // media key. False when nothing is connected.
  virtual bool sendKey(uint8_t modifiers, uint8_t keycode) = 0;
  virtual bool sendConsumer(uint16_t usage) = 0;
  // Computers this device has paired with (kept across restarts), and forgetting them.
  virtual int bondCount() const = 0;
  virtual void forgetBonds() = 0;
};

}  // namespace stella
