#include "host_board.h"

#include <stdio.h>
#include <stdlib.h>
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

namespace {

bool inList(const std::string &item, std::initializer_list<const char *> list) {
  for (const char *s : list)
    if (item == s) return true;
  return false;
}

// Every comma-separated name must be one the (fake) API knows.
std::string checkVariables(const std::string &param, const std::string &value, std::initializer_list<const char *> known) {
  size_t start = 0;
  while (start <= value.size()) {
    size_t end = value.find(',', start);
    if (end == std::string::npos) end = value.size();
    const std::string name = value.substr(start, end - start);
    if (!inList(name, known)) return param + " has an unknown variable '" + name + "'";
    start = end + 1;
  }
  return "";
}

bool isNumber(const std::string &s, double lo, double hi) {
  if (s.empty()) return false;
  char *end = nullptr;
  const double v = strtod(s.c_str(), &end);
  return *end == '\0' && v >= lo && v <= hi;
}

}  // namespace

std::string SimHttp::validateUrl(const char *url) {
  const std::string u(url);
  if (u.compare(0, 8, "https://") != 0) return "not an https:// address";
  for (unsigned char ch : u)
    if (ch <= 0x20 || ch >= 0x7F) return "a space or control character in the address";
  const size_t slash = u.find('/', 8);
  const std::string host = u.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
  if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:") != std::string::npos)
    return "a bad host name";
  const size_t q = u.find('?');
  if (q == std::string::npos) return "";
  // The query: name=value pairs, none empty.
  std::vector<std::pair<std::string, std::string>> params;
  size_t start = q + 1;
  while (start <= u.size()) {
    size_t end = u.find('&', start);
    if (end == std::string::npos) end = u.size();
    const std::string pair = u.substr(start, end - start);
    const size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 == pair.size()) return "an empty or broken parameter '" + pair + "'";
    params.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
    start = end + 1;
  }
  if (host == "api.open-meteo.com") {
    bool lat = false, lon = false;
    for (const auto &p : params) {
      const std::string &k = p.first, &v = p.second;
      std::string why;
      if (k == "latitude") lat = true, why = isNumber(v, -90, 90) ? "" : "latitude out of range";
      else if (k == "longitude") lon = true, why = isNumber(v, -180, 180) ? "" : "longitude out of range";
      else if (k == "current")
        why = checkVariables(k, v, {"temperature_2m", "apparent_temperature", "relative_humidity_2m", "weather_code", "is_day", "wind_speed_10m"});
      else if (k == "daily")
        why = checkVariables(k, v, {"weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_probability_max"});
      else if (k == "hourly") why = checkVariables(k, v, {"temperature_2m", "precipitation_probability", "weather_code"});
      else if (k == "timeformat") why = inList(v, {"iso8601", "unixtime"}) ? "" : "timeformat '" + v + "' is not iso8601 or unixtime";
      else if (k == "timezone") why = v == "auto" || v.find('/') != std::string::npos ? "" : "timezone '" + v + "' is not auto or a zone name";
      else if (k == "forecast_days") why = isNumber(v, 1, 16) ? "" : "forecast_days must be 1..16";
      else if (k == "forecast_hours") why = isNumber(v, 1, 384) ? "" : "forecast_hours must be 1..384";
      else why = "unknown parameter '" + k + "'";
      if (!why.empty()) return why;
    }
    if (!lat || !lon) return "latitude and longitude are required";
  }
  return "";
}

int SimHttp::get(const char *url, uint32_t, size_t maxBodyBytes, HttpSink &sink) {
  ++requests;
  lastRejection = validateUrl(url);
  if (!lastRejection.empty()) {
    lastUrl = url;
    return 400;
  }
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
