#include "host_board.h"

#include <stdio.h>
#include <string.h>

namespace stella {
namespace host {

void SimDisplay::writePixels(const Rect &area, const Color *pixels) {
  for (int y = 0; y < area.h; ++y) {
    for (int x = 0; x < area.w; ++x) {
      const int sx = area.x + x, sy = area.y + y;
      if (sx >= 0 && sx < w_ && sy >= 0 && sy < h_) pixels_[(size_t)sy * w_ + sx] = pixels[y * area.w + x];
    }
  }
  written_ += (uint64_t)area.area();
  ++writes_;
  if (onWrite) onWrite(area);
}

bool SimDisplay::writePpm(const std::string &path) const {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", w_, h_);
  for (Color c : pixels_) {
    const unsigned char rgb[3] = {red8(c), green8(c), blue8(c)};
    fwrite(rgb, 1, 3, f);
  }
  return fclose(f) == 0;
}

static std::string keyOf(const char *ns, const char *key) { return std::string(ns) + "/" + key; }

int MemoryStore::read(const char *ns, const char *key, void *out, size_t capacity) {
  auto it = values_.find(keyOf(ns, key));
  if (it == values_.end()) return -1;
  if (it->second.size() <= capacity) memcpy(out, it->second.data(), it->second.size());
  return (int)it->second.size();
}

bool MemoryStore::write(const char *ns, const char *key, const void *data, size_t size) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  values_[keyOf(ns, key)].assign(p, p + size);
  ++writes_;
  return true;
}

bool MemoryStore::erase(const char *ns, const char *key) { return values_.erase(keyOf(ns, key)) > 0; }

std::vector<uint8_t> *MemoryStore::raw(const char *ns, const char *key) {
  auto it = values_.find(keyOf(ns, key));
  return it == values_.end() ? nullptr : &it->second;
}

bool SimWifi::connect(const char *ssid, const char *password) {
  ++connects;
  willSucceed_ = false;
  for (const Ap &ap : aps)
    if (ap.ssid == ssid && ap.password == (password ? password : "")) willSucceed_ = true;
  link_ = WifiLink::Connecting;
  pollsLeft_ = connectPolls;
  return true;
}

WifiLink SimWifi::link() {
  if (link_ == WifiLink::Connecting && --pollsLeft_ <= 0) link_ = willSucceed_ ? WifiLink::Connected : WifiLink::Failed;
  return link_;
}

void SimWifi::address(char *out, size_t capacity) { snprintf(out, capacity, "192.168.1.42"); }

int SimWifi::scanResults(WifiNetwork *out, int capacity) {
  if (scanLeft_ < 0) return -2;
  if (scanLeft_-- > 0) return -1;
  int n = 0;
  for (const Ap &ap : aps) {
    if (n >= capacity) break;
    snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ap.ssid.c_str());
    out[n].rssi = ap.rssi;
    out[n].secured = !ap.password.empty();
    ++n;
  }
  scanLeft_ = -1;
  return n;
}

int SimHttp::get(const char *url, uint32_t, size_t maxBodyBytes, HttpSink &sink) {
  ++requests;
  if (strstr(url, "manifest.json")) ++manifestRequests;
  lastUrl = url;
  if (failWith) return failWith;
  for (const Route &r : routes) {
    if (!strstr(url, r.match.c_str())) continue;
    if (r.status < 200 || r.status >= 300) return r.status;
    if (r.body.size() > maxBodyBytes) return kHttpErrTooLarge;
    // Deliver in small pieces, as a network would.
    for (size_t i = 0; i < r.body.size(); i += 37) {
      if (i >= r.cutAfter) return kHttpErrTimeout;
      const size_t n = r.body.size() - i < 37 ? r.body.size() - i : 37;
      if (!sink.onData(reinterpret_cast<const uint8_t *>(r.body.data() + i), n)) return kHttpErrAborted;
    }
    return r.status;
  }
  return 404;
}

std::string SimSetupServer::request(bool post, const char *path, const std::string &body, int *status) {
  if (!up || !handler_) return "";
  char page[3584];
  SetupRequest req;
  req.post = post;
  req.path = path;
  req.body = body.c_str();
  req.bodyLen = body.size();
  SetupResponse resp;
  resp.body = page;
  resp.capacity = sizeof(page);
  handler_->handleSetup(req, resp);
  if (status) *status = resp.status;
  return std::string(page, resp.length);
}

}  // namespace host
}  // namespace stella
