#pragma once
// Network hardware interfaces: a Wi-Fi station driver and an HTTP(S) client.
// Both are optional parts of a Board. Implementations must never log
// credentials, and every call is made from the service thread only.
#include <stddef.h>
#include <stdint.h>

namespace stella {

struct WifiNetwork {
  char ssid[33] = {0};
  int8_t rssi = -127;
  bool secured = true;
};

enum class WifiLink : uint8_t {
  Idle,        // not trying to connect
  Connecting,  // association / DHCP in progress
  Connected,   // has an IP address
  Failed,      // gave up: wrong password, network not found, ...
};

class WifiDriver {
 public:
  virtual ~WifiDriver() = default;
  virtual bool begin() = 0;  // station mode, radio on
  // Non-blocking: progress is reported through link().
  virtual bool connect(const char *ssid, const char *password) = 0;
  virtual void disconnect() = 0;
  virtual WifiLink link() = 0;
  virtual int8_t rssi() = 0;
  virtual void address(char *out, size_t capacity) = 0;  // dotted IPv4
  // Non-blocking scan. scanResults(): -1 while running, -2 on failure,
  // otherwise the number of networks written (and the scan is released).
  virtual bool startScan() = 0;
  virtual int scanResults(WifiNetwork *out, int capacity) = 0;
  // Start periodic SNTP; must not change the process time zone.
  virtual void startTimeSync() = 0;
};

// Receives a response body as it arrives. Return false to abort the transfer
// (for example when a size limit is exceeded or parsing has failed).
class HttpSink {
 public:
  virtual ~HttpSink() = default;
  virtual bool onData(const uint8_t *data, size_t size) = 0;
};

// Transport errors (negative). Positive values are HTTP status codes.
enum HttpError : int {
  kHttpErrConnect = -1,  // DNS / TCP / TLS handshake failed
  kHttpErrTimeout = -2,
  kHttpErrAborted = -3,  // the sink returned false
  kHttpErrTooLarge = -4,
  kHttpErrNoNetwork = -5,
};

class HttpClient {
 public:
  virtual ~HttpClient() = default;
  // Blocking GET over HTTPS with certificate verification. Streams the body of
  // a 2xx response into `sink` (chunked transfer decoded). Returns the HTTP
  // status or a negative HttpError. Never reads more than maxBodyBytes.
  virtual int get(const char *url, uint32_t timeoutMs, size_t maxBodyBytes, HttpSink &sink) = 0;
};

}  // namespace stella
