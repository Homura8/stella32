#include "http_service.h"

#include <stdio.h>

#include "../core/log.h"
#include "../core/text.h"

namespace stella {
namespace {

void describe(FetchResult &r) {
  switch (r.kind) {
    case FetchResult::Ok: r.message[0] = '\0'; break;
    case FetchResult::Busy: copyText(r.message, sizeof(r.message), "Network busy"); break;
    case FetchResult::Unavailable: copyText(r.message, sizeof(r.message), "No network"); break;
    case FetchResult::TooLarge: copyText(r.message, sizeof(r.message), "Response too large"); break;
    case FetchResult::Invalid: copyText(r.message, sizeof(r.message), "Unexpected response"); break;
    case FetchResult::Http: snprintf(r.message, sizeof(r.message), "Server error (HTTP %d)", r.status); break;
    case FetchResult::Transport:
      copyText(r.message, sizeof(r.message), r.status == kHttpErrTimeout ? "Timed out" : "Can't reach server");
      break;
  }
}

class JsonSink : public HttpSink {
 public:
  explicit JsonSink(JsonParser &parser) : parser_(parser) {}
  bool onData(const uint8_t *data, size_t size) override {
    return parser_.feed(reinterpret_cast<const char *>(data), size);
  }

 private:
  JsonParser &parser_;
};

}  // namespace

FetchResult HttpService::get(const char *url, HttpSink &sink, const FetchLimits &limits) {
  FetchResult r;
  if (!available()) {
    r.kind = FetchResult::Unavailable;
    describe(r);
    return r;
  }
  Lease lease(*lease_, LeasePriority::Primary, limits.leaseWaitMs);
  if (!lease) {
    r.kind = FetchResult::Busy;
    describe(r);
    return r;
  }
  r.status = client_->get(url, limits.timeoutMs, limits.maxBytes, sink);
  if (r.status >= 200 && r.status < 300) r.kind = FetchResult::Ok;
  else if (r.status == kHttpErrTooLarge) r.kind = FetchResult::TooLarge;
  else if (r.status == kHttpErrAborted) r.kind = FetchResult::Invalid;
  else if (r.status == kHttpErrNoNetwork) r.kind = FetchResult::Unavailable;
  else if (r.status < 0) r.kind = FetchResult::Transport;
  else r.kind = FetchResult::Http;
  describe(r);
  if (!r.ok()) LOGW("http", "request failed: %s", r.message);  // never log the URL (may hold keys)
  return r;
}

FetchResult HttpService::getJson(const char *url, JsonHandler &handler, const FetchLimits &limits) {
  JsonParser parser(handler);
  JsonSink sink(parser);
  FetchResult r = get(url, sink, limits);
  if (r.kind == FetchResult::Invalid || (r.ok() && !parser.finish())) {
    r.kind = FetchResult::Invalid;
    describe(r);
    if (parser.error()) LOGW("http", "JSON: %s", parser.error());
  }
  return r;
}

}  // namespace stella
