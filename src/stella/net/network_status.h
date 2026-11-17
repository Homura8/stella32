#pragma once
// Network state shared between the Wi-Fi service and the UI, and the control
// interface apps use (AppContext::network()). All types are fixed-size and
// trivially copyable so they can travel through Topic<T>.
#include <stdint.h>

#include "../hal/network.h"

namespace stella {

// System topic ids. App topics start at kFirstUser.
namespace topics {
constexpr uint16_t kNetwork = 1;   // Topic<NetworkStatus>
constexpr uint16_t kWifiScan = 2;  // Topic<WifiScan>
constexpr uint16_t kFirstUser = 16;
}  // namespace topics

enum class WifiState : uint8_t {
  Unavailable,    // the board has no Wi-Fi
  NotConfigured,  // no saved network
  Connecting,
  Connected,
  Disconnected,   // saved network, link down; retrying with backoff
};

struct NetworkStatus {
  bool present = false;    // the board has a network interface
  bool connected = false;  // has an IP address
  bool timeSynced = false;
  WifiState state = WifiState::Unavailable;
  int8_t rssi = 0;
  char ssid[33] = {0};
  char ip[16] = {0};
  char error[40] = {0};  // last problem, for the UI ("Couldn't join ...")
};

struct WifiScan {
  static constexpr int kMax = 12;
  bool scanning = false;
  bool failed = false;
  uint8_t count = 0;
  uint16_t sequence = 0;  // increments per completed scan
  WifiNetwork networks[kMax];  // strongest first, one entry per SSID
};

// Signal strength in 0..4 bars.
inline int signalBars(int8_t rssi) {
  return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
}

class NetworkControl {
 public:
  virtual ~NetworkControl() = default;
  virtual NetworkStatus status() const = 0;
  virtual WifiScan scan() const = 0;
  // Commands are queued for the service thread and return immediately.
  virtual void requestScan() = 0;
  virtual void join(const char *ssid, const char *password) = 0;
  virtual void forget() = 0;
};

}  // namespace stella
