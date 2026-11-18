#include "calendar_source.h"

#include <string.h>

#include "../core/text.h"

namespace stella {

namespace {
constexpr const char *kNamespace = "cal";
constexpr const char *kKey = "source";

void wipe(void *p, size_t n) {
  volatile char *v = static_cast<volatile char *>(p);
  while (n--) *v++ = 0;
}
}  // namespace

bool CalendarSource::validate(const char *url, const char **why) {
  auto no = [&](const char *reason) {
    if (why) *why = reason;
    return false;
  };
  if (!url || !*url) return no("Enter an address");
  if (strlen(url) >= kCalendarUrlBytes) return no("That address is too long");
  if (strncmp(url, "https://", 8) != 0) return no("It must start with https://");
  for (const char *p = url; *p; ++p)
    if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7F) return no("Spaces and odd characters are not allowed");
  const char *host = url + 8;
  if (!*host || *host == '/' || *host == '?' || *host == '#') return no("The address has no host");
  return true;
}

void CalendarSource::configure(KeyValueStore *kv, JobScheduler *jobs) {
  kv_ = kv;
  jobs_ = jobs;
  Record r;
  memset(&r, 0, sizeof(r));
  if (kv_ && loadRecord(kv_, kNamespace, kKey, Record::kVersion, &r, sizeof(r)) == RecordStatus::Ok) {
    r.url[sizeof(r.url) - 1] = '\0';
    if (validate(r.url)) copyText(url_, sizeof(url_), r.url);
  }
  wipe(&r, sizeof(r));
}

bool CalendarSource::hasAddress() const {
  osal::LockGuard<osal::Mutex> guard(lock_);
  return url_[0] != '\0';
}

void CalendarSource::host(char *out, size_t capacity) const {
  if (capacity == 0) return;
  out[0] = '\0';
  osal::LockGuard<osal::Mutex> guard(lock_);
  if (!url_[0]) return;
  const char *start = url_ + 8;  // after "https://"
  size_t n = 0;
  while (start[n] && start[n] != '/' && start[n] != '?' && start[n] != '#' && n + 1 < capacity) {
    out[n] = start[n];
    ++n;
  }
  out[n] = '\0';
}

bool CalendarSource::setAddress(const char *url, const char **why) {
  if (!validate(url, why)) return false;
  {
    osal::LockGuard<osal::Mutex> guard(lock_);
    copyText(url_, sizeof(url_), url);
    save();
  }
  generation_.fetch_add(1);
  refreshNow();
  return true;
}

void CalendarSource::clearAddress() {
  {
    osal::LockGuard<osal::Mutex> guard(lock_);
    wipe(url_, sizeof(url_));
    save();
  }
  generation_.fetch_add(1);
  refreshNow();
}

void CalendarSource::refreshNow() {
  if (jobs_ && job_) jobs_->requestRun(*job_);
}

bool CalendarSource::copyAddress(char out[kCalendarUrlBytes]) const {
  osal::LockGuard<osal::Mutex> guard(lock_);
  copyText(out, kCalendarUrlBytes, url_);
  return url_[0] != '\0';
}

// Called with the lock held.
void CalendarSource::save() {
  if (!kv_) return;
  Record r;
  memset(&r, 0, sizeof(r));
  copyText(r.url, sizeof(r.url), url_);
  saveRecord(kv_, kNamespace, kKey, Record::kVersion, &r, sizeof(r));
  wipe(&r, sizeof(r));
}

}  // namespace stella
