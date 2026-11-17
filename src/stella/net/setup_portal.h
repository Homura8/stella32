#pragma once
// The web page behind phone-based Wi-Fi setup. Portable and host-tested: it
// turns a SetupRequest into a SetupResponse and, for a valid form, asks the
// Wi-Fi service to join. Everything from the phone is untrusted: field sizes
// are bounded, text is HTML-escaped, and the password is wiped after use and
// never logged.
//
//   GET  (any path)  the page: nearby networks, a form, the last result
//   POST /join       ssid=...&pw=...  -> join, then a "watch the device" page
#include <stddef.h>

#include "../hal/network.h"
#include "network_status.h"

namespace stella {

// What the portal needs from the Wi-Fi service (service thread only).
class SetupControl {
 public:
  virtual ~SetupControl() = default;
  virtual WifiScan setupScan() const = 0;
  virtual NetworkStatus setupStatus() const = 0;
  virtual void setupJoin(const char *ssid, const char *password) = 0;
};

class SetupPortal : public SetupHandler {
 public:
  static constexpr size_t kMaxSsid = 32;      // 802.11
  static constexpr size_t kMaxPassword = 63;  // WPA2 passphrase
  static constexpr size_t kPageBytes = 3584;  // the response buffer a server should provide

  explicit SetupPortal(SetupControl &control) : control_(control) {}
  void handleSetup(const SetupRequest &request, SetupResponse &response) override;

  // Helpers, public for tests.
  // Decodes %XX and '+'; returns the decoded length, or -1 if it does not fit
  // (a NUL is always written when cap > 0).
  static int urlDecode(char *dst, size_t cap, const char *src, size_t len);
  // Finds `name` in a urlencoded body and decodes its value. False if absent
  // or too long.
  static bool formField(const char *body, size_t len, const char *name, char *out, size_t cap);
  // Escapes & < > " ' ; returns false (and truncates) if it does not fit.
  static bool htmlEscape(char *dst, size_t cap, const char *src);

 private:
  void page(SetupResponse &response, const char *notice, bool problem);
  SetupControl &control_;
};

}  // namespace stella
