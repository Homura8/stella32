#pragma once
// Structured, bounded logging: one line per call, formatted into a stack buffer
// and handed to a single sink (stdout by default, which is UART0 on ESP32).
// Never log credentials, tokens or private URLs; log lengths/presence instead.
#include <stdarg.h>

namespace stella {

enum class LogLevel : unsigned char { Debug = 0, Info = 1, Warn = 2, Error = 3, None = 4 };

using LogSink = void (*)(LogLevel level, const char *line);

namespace log {
void setSink(LogSink sink);           // nullptr restores the stdout sink
void setLevel(LogLevel minimum);      // runtime filter (compile-time below)
void write(LogLevel level, const char *tag, const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
void vwrite(LogLevel level, const char *tag, const char *format, va_list args);
}  // namespace log

}  // namespace stella

#ifndef STELLA_LOG_MIN_LEVEL
#define STELLA_LOG_MIN_LEVEL 1  // Info
#endif

#define STELLA_LOG_AT(lvl, tag, ...)                                                        \
  do {                                                                                      \
    if ((int)(lvl) >= STELLA_LOG_MIN_LEVEL) ::stella::log::write((lvl), (tag), __VA_ARGS__); \
  } while (0)
#define LOGD(tag, ...) STELLA_LOG_AT(::stella::LogLevel::Debug, tag, __VA_ARGS__)
#define LOGI(tag, ...) STELLA_LOG_AT(::stella::LogLevel::Info, tag, __VA_ARGS__)
#define LOGW(tag, ...) STELLA_LOG_AT(::stella::LogLevel::Warn, tag, __VA_ARGS__)
#define LOGE(tag, ...) STELLA_LOG_AT(::stella::LogLevel::Error, tag, __VA_ARGS__)
