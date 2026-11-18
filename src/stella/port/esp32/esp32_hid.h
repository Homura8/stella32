#pragma once
// Bluetooth LE keyboard and media keys for the Deck app, on NimBLE.
//
// Optional: it is built only when the NimBLE-Arduino library is present (the
// `cyd-ble` profile in examples/CYD/sketch.yaml adds it), because the stack
// costs flash and RAM. Without the library STELLA_HAS_BLE is 0 and this file
// declares nothing, so the default firmware is unchanged.
//
// The radio runs only between start() and stop() (the Deck screen's lifetime):
// stop() releases the whole stack and its memory. The stack's callbacks run on
// its own task and only set atomics; the UI thread reads state() and calls
// sendKey/sendConsumer. Pairing is Just Works with bonding and LE Secure
// Connections: the computer lists "StellaOS Deck" and bonds with it, and bonds
// stay in NVS until forgetBonds().
#if defined(__has_include)
#if __has_include(<NimBLEDevice.h>)
#define STELLA_HAS_BLE 1
#endif
#endif
#ifndef STELLA_HAS_BLE
#define STELLA_HAS_BLE 0
#endif

#if STELLA_HAS_BLE

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <atomic>
#include <string.h>

#include "../../hal/hid.h"

namespace stella {
namespace esp32 {

class Esp32Hid : public HidDevice, private NimBLEServerCallbacks {
 public:
  bool available() const override { return true; }

  bool start(const char *name) override {
    if (running_) return true;
    error_[0] = '\0';
    if (!NimBLEDevice::init(name ? name : "StellaOS Deck")) return fail("Bluetooth did not start");
    NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, Just Works, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    server_ = NimBLEDevice::createServer();
    if (!server_) return failAndRelease("Bluetooth server failed");
    server_->setCallbacks(this, false);
    hid_ = new NimBLEHIDDevice(server_);
    keyboard_ = hid_->getInputReport(kKeyboardReport);
    consumer_ = hid_->getInputReport(kConsumerReport);
    if (!keyboard_ || !consumer_) return failAndRelease("Bluetooth HID failed");
    hid_->setManufacturer("StellaOS");
    hid_->setPnp(0x02, 0x05AC, 0x820A, 0x0100);
    hid_->setHidInfo(0x00, 0x01);
    hid_->setReportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
    if (!server_->start()) return failAndRelease("Bluetooth server failed");
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid_->getHidService()->getUUID());
    if (!adv->start()) return failAndRelease("Bluetooth did not advertise");
    bonds_.store(NimBLEDevice::getNumBonds());
    state_.store((uint8_t)HidState::Advertising);
    running_ = true;
    return true;
  }

  void stop() override {
    if (!running_ && state_.load() != (uint8_t)HidState::Failed) {
      state_.store((uint8_t)HidState::Off);
      return;
    }
    release();
    state_.store((uint8_t)HidState::Off);
  }

  HidState state() const override { return (HidState)state_.load(); }
  const char *error() const override { return error_; }

  bool sendKey(uint8_t modifiers, uint8_t keycode) override {
    if (!ready() || !keyboard_) return false;
    uint8_t down[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0}, up[8] = {0};
    keyboard_->setValue(down, sizeof(down));
    if (!keyboard_->notify()) return false;
    delay(12);
    keyboard_->setValue(up, sizeof(up));
    keyboard_->notify();
    return true;
  }

  bool sendConsumer(uint16_t usage) override {
    if (!ready() || !consumer_) return false;
    const uint8_t down[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)}, up[2] = {0, 0};
    consumer_->setValue(down, sizeof(down));
    if (!consumer_->notify()) return false;
    delay(12);
    consumer_->setValue(up, sizeof(up));
    consumer_->notify();
    return true;
  }

  int bondCount() const override { return bonds_.load(); }

  void forgetBonds() override {
    if (running_) NimBLEDevice::deleteAllBonds();
    else if (NimBLEDevice::init("StellaOS Deck")) {  // the bonds live in NVS: open the stack just to clear them
      NimBLEDevice::deleteAllBonds();
      NimBLEDevice::deinit(true);
    }
    bonds_.store(0);
  }

 private:
  static constexpr uint8_t kKeyboardReport = 1, kConsumerReport = 2;
  // Keyboard (report 1: modifiers, reserved, six keys) and consumer control (report 2: one 16-bit usage).
  static constexpr uint8_t kReportMap[] = {
      0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, kKeyboardReport,  //
      0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,  //
      0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                                                              //
      0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,  //
      0xC0,                                                                                            //
      0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, kConsumerReport,                                       //
      0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,  //
      0xC0};

  bool ready() const { return running_ && state_.load() == (uint8_t)HidState::Connected; }

  bool fail(const char *why) {
    strncpy(error_, why, sizeof(error_) - 1);
    state_.store((uint8_t)HidState::Failed);
    return false;
  }
  bool failAndRelease(const char *why) {
    release();
    return fail(why);
  }
  void release() {
    running_ = false;
    keyboard_ = consumer_ = nullptr;
    NimBLEDevice::deinit(true);  // frees the stack, the server and its memory
    delete hid_;
    hid_ = nullptr;
    server_ = nullptr;
  }

  // The stack's task: set flags, nothing more.
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override {}
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    if (info.isEncrypted()) {
      bonds_.store(NimBLEDevice::getNumBonds());
      state_.store((uint8_t)HidState::Connected);
    }
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    if (state_.load() == (uint8_t)HidState::Connected) state_.store((uint8_t)HidState::Advertising);
    NimBLEDevice::getAdvertising()->start();  // be found again
  }

  NimBLEServer *server_ = nullptr;
  NimBLEHIDDevice *hid_ = nullptr;
  NimBLECharacteristic *keyboard_ = nullptr, *consumer_ = nullptr;
  bool running_ = false;
  std::atomic<uint8_t> state_{(uint8_t)HidState::Off};
  std::atomic<int> bonds_{0};
  char error_[40] = {0};
};

}  // namespace esp32
}  // namespace stella

#endif  // STELLA_HAS_BLE
