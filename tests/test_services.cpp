// Topics, job scheduling, resource leases, supervision and persistent records.
#include <string.h>

#include <atomic>
#include <thread>

#include "host_board.h"
#include "stella/app/system_settings.h"
#include "stella/service/data_status.h"
#include "stella/service/job_scheduler.h"
#include "stella/service/resource_lease.h"
#include "stella/service/supervisor.h"
#include "stella/service/topic.h"
#include "stella/storage/record.h"
#include "test.h"

using namespace stella;

namespace {

struct CountingNotifier : TopicNotifier {
  int calls = 0;
  void topicChanged(uint16_t) override { ++calls; }
};

struct FakeJob : Job {
  JobPolicy p;
  JobResult next = JobResult::Ok;
  int runs = 0;
  uint32_t duration = 0;
  uint32_t *clock = nullptr;
  const char *name() const override { return "fake"; }
  JobPolicy policy() const override { return p; }
  JobResult run(uint32_t) override {
    ++runs;
    *clock += duration;
    return next;
  }
};

uint32_t readClock(void *c) { return *static_cast<uint32_t *>(c); }

}  // namespace

TEST(topic_coalesces_notifications) {
  CountingNotifier n;
  Topic<int> t(20);
  t.attach(&n);
  CHECK_EQ(t.version(), 0u);
  t.publish(1);
  t.publish(2);
  t.publish(3);
  CHECK_EQ(n.calls, 1);  // one pending notification until acknowledged
  int v = 0;
  CHECK_EQ(t.read(v), 3u);
  CHECK_EQ(v, 3);
  t.acknowledge();
  t.publish(4);
  CHECK_EQ(n.calls, 2);
}

TEST(data_status_freshness) {
  DataStatus s;
  CHECK(s.freshness(1000, 500) == Freshness::Empty);
  s.succeeded(1000);
  CHECK(s.freshness(1200, 500) == Freshness::Fresh);
  s.failed(1300, "HTTP 503");
  CHECK(s.freshness(1400, 500) == Freshness::Failing && s.hasData && s.failures == 1);
  CHECK(s.freshness(1600, 500) == Freshness::Stale);
  CHECK(strcmp(s.error, "HTTP 503") == 0);
}

TEST(job_scheduler_backoff_and_deferral) {
  uint32_t clock = 0xFFFFF000u;  // run across the 32-bit wrap
  JobScheduler js;
  FakeJob job;
  job.clock = &clock;
  job.p.intervalMs = 60000;
  job.p.retryMs = 1000;
  job.p.maxBackoffMs = 4000;
  job.p.deferMs = 200;
  js.add(job, clock);
  js.runDue(readClock, &clock);
  CHECK_EQ(job.runs, 1);
  CHECK_EQ(js.entry(0).dueMs, clock + 60000);

  job.next = JobResult::Failed;
  const uint32_t waits[] = {1000, 2000, 4000, 4000};
  for (uint32_t w : waits) {
    clock = js.entry(0).dueMs;
    js.runDue(readClock, &clock);
    CHECK_EQ(js.entry(0).dueMs - clock, w);
  }
  // Deferred keeps the failure count (no backoff growth, no reset).
  job.next = JobResult::Deferred;
  clock = js.entry(0).dueMs;
  js.runDue(readClock, &clock);
  CHECK_EQ(js.entry(0).dueMs - clock, 200u);
  CHECK_EQ(js.entry(0).failures, 4);
  // A manual refresh clears backoff and runs everything now.
  js.requestRefresh();
  job.next = JobResult::Ok;
  const int before = job.runs;
  js.runDue(readClock, &clock);
  CHECK_EQ(job.runs, before + 1);
  CHECK_EQ(js.entry(0).failures, 0);
}

TEST(job_scheduler_network_gating) {
  uint32_t clock = 1000;
  JobScheduler js;
  FakeJob job;
  job.clock = &clock;
  job.p.needsNetwork = true;
  job.p.retryMs = 1000;
  job.next = JobResult::Failed;
  js.add(job, clock);
  js.setNetworkAvailable(false);
  js.runDue(readClock, &clock);
  CHECK_EQ(job.runs, 0);  // skipped, not failed
  CHECK_EQ(js.entry(0).failures, 0);
  js.setNetworkAvailable(true);
  js.runDue(readClock, &clock);
  CHECK_EQ(job.runs, 1);
  clock += 50;
  js.setNetworkAvailable(false);
  js.runDue(readClock, &clock);
  js.setNetworkAvailable(true);  // reconnect: run immediately, backoff cleared
  js.runDue(readClock, &clock);
  CHECK_EQ(job.runs, 2);
}

TEST(job_scheduler_reports_next_wake) {
  uint32_t clock = 0;
  JobScheduler js;
  FakeJob a, b;
  a.clock = b.clock = &clock;
  a.p.intervalMs = 300;
  b.p.intervalMs = 700;
  js.add(a, clock);
  js.add(b, clock);
  CHECK_EQ(js.runDue(readClock, &clock), 300u);
  a.duration = 50;  // slow jobs move later deadlines correctly
  clock = 300;
  CHECK_EQ(js.runDue(readClock, &clock), 300u);
}

TEST(resource_lease_optional_yields_to_primary) {
  SharedResource r;
  {
    Lease held(r, LeasePriority::Optional);
    CHECK((bool)held);
    Lease nested(r, LeasePriority::Primary, 0);  // recursive on the owner
    CHECK((bool)nested);
  }
  std::atomic<bool> holding{false}, release{false}, primaryGot{false};
  std::thread sampler([&] {
    Lease l(r, LeasePriority::Optional);
    holding = true;
    while (!release) std::this_thread::yield();
  });
  while (!holding) std::this_thread::yield();
  std::thread scheduler([&] {
    Lease l(r, LeasePriority::Primary, 5000);
    primaryGot = (bool)l;
  });
  while (!r.primaryWaiting()) std::this_thread::yield();
  bool optionalDuringWait = true;
  std::thread late([&] { optionalDuringWait = (bool)Lease(r, LeasePriority::Optional); });
  late.join();
  CHECK(!optionalDuringWait);  // never starts while a Primary client waits
  release = true;
  sampler.join();
  scheduler.join();
  CHECK(primaryGot.load());
  CHECK(r.optionalDeferred() >= 1);
  // A Primary wait that times out is counted and clears the waiting flag.
  std::atomic<bool> hold2{false}, done2{false};
  std::thread holder([&] {
    Lease l(r, LeasePriority::Primary);
    hold2 = true;
    while (!done2) std::this_thread::yield();
  });
  while (!hold2) std::this_thread::yield();
  CHECK(!(bool)Lease(r, LeasePriority::Primary, 20));
  CHECK(r.primaryTimeouts() == 1 && !r.primaryWaiting());
  done2 = true;
  holder.join();
}

TEST(supervisor_limits_and_wrap) {
  Supervisor s;
  CHECK_EQ(s.watch("bad", 0, 0), -1);
  const uint32_t t0 = 0xFFFFF000u;
  const int ui = s.watch("ui", 1000, t0), svc = s.watch("svc", 5000, t0);
  uint32_t age = 0;
  CHECK_EQ(s.firstStalled(t0 + 1000, age), -1);
  CHECK_EQ(s.firstStalled(t0 + 1001, age), ui);
  s.beat(ui, t0 + 4500);
  CHECK_EQ(s.firstStalled(t0 + 5001, age), svc);
  CHECK(strcmp(s.name(svc), "svc") == 0);
  s.beat(svc, t0 + 6000);
  CHECK_EQ(s.age(svc, t0 + 5990), 0u);  // beat newer than the sample
  s.retire(svc);
  s.beat(ui, t0 + 90000);
  CHECK_EQ(s.firstStalled(t0 + 90000, age), -1);
}

TEST(restart_record_reports_once) {
  RestartRecord r;
  memset(&r, 0x5a, sizeof(r));  // power-on garbage
  CHECK(!restart_record::takePending(r));
  CHECK(restart_record::valid(r) && r.restarts == 0);
  restart_record::note(r, "a-thread-name-that-is-long", 700000);
  CHECK(restart_record::takePending(r) && !restart_record::takePending(r));
  CHECK_EQ(r.restarts, 1u);
  CHECK_EQ(strlen(r.thread), sizeof(r.thread) - 1);
  r.stalledMs ^= 1;
  CHECK(!restart_record::valid(r));
}

TEST(records_detect_corruption_and_versions) {
  host::MemoryStore store;
  struct V1 {
    int a;
    char b[8];
  } v{42, "hi"}, out{};
  CHECK(loadRecord(&store, "app", "k", 1, &out, sizeof(out)) == RecordStatus::Missing);
  CHECK(saveRecord(&store, "app", "k", 1, &v, sizeof(v)));
  CHECK(loadRecord(&store, "app", "k", 1, &out, sizeof(out)) == RecordStatus::Ok && out.a == 42);
  uint16_t found = 0;
  CHECK(loadRecord(&store, "app", "k", 2, &out, sizeof(out), &found) == RecordStatus::OldVersion && found == 1);
  store.raw("app", "k")->back() ^= 0xFF;  // flip a payload byte
  CHECK(loadRecord(&store, "app", "k", 1, &out, sizeof(out)) == RecordStatus::Corrupt);
  store.raw("app", "k")->resize(3);  // torn write
  CHECK(loadRecord(&store, "app", "k", 1, &out, sizeof(out)) == RecordStatus::Corrupt);
  CHECK(!saveRecord(&store, "a-namespace-too-long", "k", 1, &v, sizeof(v)));
  CHECK(loadRecord(nullptr, "app", "k", 1, &out, sizeof(out)) == RecordStatus::Unavailable);
}

TEST(settings_round_trip_and_sanitize) {
  host::MemoryStore store;
  SystemSettings s;
  s.appearance = Appearance::Dark;
  s.brightness = 3;  // below the floor
  s.clock24h = false;
  strcpy(s.timezone, "JST-9");
  CHECK(saveSettings(&store, s));
  SystemSettings loaded;
  CHECK(loadSettings(&store, loaded) == RecordStatus::Ok);
  CHECK(loaded.appearance == Appearance::Dark && !loaded.clock24h && strcmp(loaded.timezone, "JST-9") == 0);
  CHECK_EQ(loaded.brightness, 16);  // sanitized on load
  int count = 0;
  CHECK(timezonePresets(count) && count >= 5);
}
