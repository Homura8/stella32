#pragma once
// Bounded C-string helpers. Fixed buffers everywhere; never truncate in the
// middle of a UTF-8 sequence.
#include <stddef.h>
#include <stdint.h>

namespace stella {

// Copy with truncation at a UTF-8 character boundary; always NUL-terminates
// when capacity > 0. Returns true if the whole source fitted.
bool copyText(char *dst, size_t capacity, const char *src);

// Decode one UTF-8 code point and advance `p`. Malformed input yields U+FFFD
// and consumes at least one byte; the terminating NUL is never consumed.
uint32_t nextCodepoint(const char *&p);

// FNV-1a 32-bit, for stable small identifiers (not for security).
uint32_t hash32(const void *data, size_t size);
uint32_t hash32(const char *text);

}  // namespace stella
