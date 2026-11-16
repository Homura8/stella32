#include "record.h"

#include <string.h>

namespace stella {
namespace {

constexpr uint32_t kRecordMagic = 0x53544c41u;  // "STLA"

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t crc;
};

}  // namespace

uint32_t crc32(const void *data, size_t size, uint32_t seed) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = ~seed;
  while (size--) {
    crc ^= *p++;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool validStorageName(const char *name) {
  if (!name || !name[0]) return false;
  const size_t n = strnlen(name, 16);
  return n <= 15;
}

RecordStatus loadRecord(KeyValueStore *store, const char *ns, const char *key, uint16_t version, void *data,
                        size_t size, uint16_t *foundVersion) {
  if (!store || !validStorageName(ns) || !validStorageName(key) || size > kMaxRecordSize) {
    return RecordStatus::Unavailable;
  }
  uint8_t buffer[sizeof(Header) + kMaxRecordSize];
  const int stored = store->read(ns, key, buffer, sizeof(buffer));
  if (stored < 0) return RecordStatus::Missing;
  if ((size_t)stored < sizeof(Header) || (size_t)stored > sizeof(buffer)) return RecordStatus::Corrupt;
  Header h;
  memcpy(&h, buffer, sizeof(h));
  const size_t payload = (size_t)stored - sizeof(Header);
  if (h.magic != kRecordMagic || h.size != payload || h.crc != crc32(buffer + sizeof(Header), payload)) {
    return RecordStatus::Corrupt;
  }
  if (foundVersion) *foundVersion = h.version;
  if (h.version != version) {
    if (payload <= size) memcpy(data, buffer + sizeof(Header), payload);
    return RecordStatus::OldVersion;
  }
  if (payload != size) return RecordStatus::Corrupt;  // Same version, different layout: a bug.
  memcpy(data, buffer + sizeof(Header), size);
  return RecordStatus::Ok;
}

bool saveRecord(KeyValueStore *store, const char *ns, const char *key, uint16_t version, const void *data,
                size_t size) {
  if (!store || !validStorageName(ns) || !validStorageName(key) || size > kMaxRecordSize) return false;
  uint8_t buffer[sizeof(Header) + kMaxRecordSize];
  const Header h{kRecordMagic, version, (uint16_t)size, crc32(data, size)};
  memcpy(buffer, &h, sizeof(h));
  memcpy(buffer + sizeof(h), data, size);
  return store->write(ns, key, buffer, sizeof(h) + size);
}

const char *recordStatusName(RecordStatus s) {
  switch (s) {
    case RecordStatus::Ok: return "ok";
    case RecordStatus::Missing: return "missing";
    case RecordStatus::Corrupt: return "corrupt";
    case RecordStatus::OldVersion: return "old version";
    case RecordStatus::Unavailable: return "unavailable";
  }
  return "?";
}

}  // namespace stella
