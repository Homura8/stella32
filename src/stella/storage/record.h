#pragma once
// Versioned, checksummed persistent records on top of KeyValueStore.
//
// Each record is stored as [header | payload]: a magic number, the schema
// version, the payload size and a CRC-32. A torn write, a corrupt flash page,
// a different struct layout or an older firmware's record is detected rather
// than silently loaded into a struct.
//
// Changing a persisted struct: bump its version and handle
// RecordStatus::OldVersion (migrate from the old layout, or fall back to
// defaults). Never reorder fields of a persisted struct without a bump.
#include <stddef.h>
#include <stdint.h>

#include "../hal/hal.h"

namespace stella {

enum class RecordStatus : uint8_t { Ok, Missing, Corrupt, OldVersion, Unavailable };

constexpr size_t kMaxRecordSize = 1024;

uint32_t crc32(const void *data, size_t size, uint32_t seed = 0);

// NVS namespaces and keys are at most 15 characters.
bool validStorageName(const char *name);

// Loads into `data` only when the status is Ok. On OldVersion, `foundVersion`
// reports the stored version and the raw payload is copied to `data` if it
// fits, so the caller can migrate it.
RecordStatus loadRecord(KeyValueStore *store, const char *ns, const char *key, uint16_t version, void *data,
                        size_t size, uint16_t *foundVersion = nullptr);
bool saveRecord(KeyValueStore *store, const char *ns, const char *key, uint16_t version, const void *data,
                size_t size);

const char *recordStatusName(RecordStatus s);

}  // namespace stella
