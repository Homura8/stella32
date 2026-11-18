#pragma once
// Where the calendar comes from: one https address of an iCalendar feed (a
// Google "secret address in iCal format", a university timetable...). It is a
// private URL, so it is kept like a password: only in this versioned record,
// never logged, never in a topic, and the screens show only its host.
//
// The address is set from the UI thread (Settings > Calendar) or by the remote
// page's handler (another thread) and read by the calendar service on the
// service thread, so every access takes the lock. Setting it asks the calendar
// job to run, so a new address is fetched at once.
#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "../osal/osal.h"
#include "../service/job_scheduler.h"
#include "../storage/record.h"

namespace stella {

constexpr size_t kCalendarUrlBytes = 256;  // including the terminating NUL

// What the UI and the remote page may do with the address.
class CalendarSourceControl {
 public:
  virtual ~CalendarSourceControl() = default;
  virtual bool hasAddress() const = 0;
  // The host part ("calendar.google.com"), for display. Empty when there is no address.
  virtual void host(char *out, size_t capacity) const = 0;
  // Validates (https, printable ASCII, no spaces, fits) and stores. Returns false and keeps the
  // old address if it is not acceptable; `why` (optional) gets a short reason.
  virtual bool setAddress(const char *url, const char **why = nullptr) = 0;
  virtual void clearAddress() = 0;
  // Ask for a fetch now (the same job the timer runs).
  virtual void refreshNow() = 0;
};

class CalendarSource : public CalendarSourceControl {
 public:
  // Before the scheduler starts. `kv` may be null (nothing is remembered then).
  void configure(KeyValueStore *kv, JobScheduler *jobs);
  // The calendar job registers itself so a change can ask it to run.
  void attachJob(const Job &job) { job_ = &job; }

  bool hasAddress() const override;
  void host(char *out, size_t capacity) const override;
  bool setAddress(const char *url, const char **why = nullptr) override;
  void clearAddress() override;
  void refreshNow() override;

  // For the service: a copy of the address (it wipes its own copy after the request) and a counter
  // that changes whenever the address does.
  bool copyAddress(char out[kCalendarUrlBytes]) const;
  uint32_t generation() const { return generation_.load(); }

  static bool validate(const char *url, const char **why = nullptr);

 private:
  struct Record {
    static constexpr uint16_t kVersion = 1;
    char url[kCalendarUrlBytes];
  };
  void save();

  KeyValueStore *kv_ = nullptr;
  JobScheduler *jobs_ = nullptr;
  const Job *job_ = nullptr;
  mutable osal::Mutex lock_;
  char url_[kCalendarUrlBytes] = {0};
  std::atomic<uint32_t> generation_{0};
};

}  // namespace stella
