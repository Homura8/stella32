#pragma once
// What background jobs use to fetch data. It wraps the board's HttpClient
// with the shared TLS lease (a PSRAM-less ESP32 affords one TLS session at a
// time), size limits, and streaming JSON parsing, and turns every outcome
// into a FetchResult that maps directly onto JobResult and DataStatus.
#include <stddef.h>
#include <stdint.h>

#include "../hal/network.h"
#include "../service/job_scheduler.h"
#include "../service/resource_lease.h"
#include "json.h"

namespace stella {

struct FetchResult {
  enum Kind : uint8_t { Ok, Busy, Unavailable, Transport, Http, TooLarge, Invalid };
  Kind kind = Unavailable;
  int status = 0;  // HTTP status or negative HttpError
  char message[40] = {0};

  bool ok() const { return kind == Ok; }
  // Busy (lease contention) defers; everything else that is not Ok failed.
  JobResult jobResult() const {
    return kind == Ok ? JobResult::Ok : kind == Busy ? JobResult::Deferred : JobResult::Failed;
  }
};

struct FetchLimits {
  uint32_t timeoutMs = 15000;
  size_t maxBytes = 32 * 1024;
  uint32_t leaseWaitMs = 20000;
};

class HttpService {
 public:
  void configure(HttpClient *client, SharedResource *lease) {
    client_ = client;
    lease_ = lease;
  }
  bool available() const { return client_ != nullptr && lease_ != nullptr; }

  FetchResult get(const char *url, HttpSink &sink, const FetchLimits &limits = {});
  // Streams the body through a JsonParser; Invalid if the JSON is malformed,
  // truncated or exceeds the parser's limits.
  FetchResult getJson(const char *url, JsonHandler &handler, const FetchLimits &limits = {});

 private:
  HttpClient *client_ = nullptr;
  SharedResource *lease_ = nullptr;
};

}  // namespace stella
