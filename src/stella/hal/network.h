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
  // A random number for the temporary setup password. Boards with a hardware
  // random generator must override this; the default is NOT secure.
  virtual uint32_t random32() {
    static uint32_t x = 2463534242u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }
};

// Phone-based Wi-Fi setup: the device opens its own protected access point and
// a tiny web server on it (with a captive DNS so a phone opens the page by
// itself). The server's job is only transport: the portable SetupHandler
// decides what every request means.
struct SetupRequest {
  bool post = false;
  const char *path = "/";
  const char *body = "";  // application/x-www-form-urlencoded, NUL-terminated
  size_t bodyLen = 0;
};

struct SetupResponse {
  int status = 200;
  const char *contentType = "text/html; charset=utf-8";
  char *body = nullptr;  // supplied by the server, filled by the handler
  size_t capacity = 0;
  size_t length = 0;
};

class SetupHandler {
 public:
  virtual ~SetupHandler() = default;
  virtual void handleSetup(const SetupRequest &request, SetupResponse &response) = 0;
};

class SetupServer {
 public:
  virtual ~SetupServer() = default;
  // Starts the WPA2 access point and the server; requests go to `handler`.
  virtual bool begin(const char *apSsid, const char *apPassword, SetupHandler &handler) = 0;
  virtual void poll() = 0;  // serve pending requests (service thread, ~every 25 ms)
  virtual void end() = 0;   // stop the server and the access point
  virtual void address(char *out, size_t capacity) = 0;  // the access point's IPv4
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
