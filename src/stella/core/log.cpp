#include "log.h"

#include <stdio.h>

namespace stella {
namespace {

void stdoutSink(LogLevel, const char *line) {
  fputs(line, stdout);
  fflush(stdout);
}

LogSink g_sink = stdoutSink;
LogLevel g_level = LogLevel::Info;

}  // namespace

namespace log {

void setSink(LogSink sink) { g_sink = sink ? sink : stdoutSink; }
void setLevel(LogLevel minimum) { g_level = minimum; }

void vwrite(LogLevel level, const char *tag, const char *format, va_list args) {
  if (level < g_level || level == LogLevel::None) return;
  static const char kLetters[] = {'D', 'I', 'W', 'E'};
  char line[192];
  int n = snprintf(line, sizeof(line), "%c [%s] ", kLetters[(int)level], tag ? tag : "-");
  if (n < 0) return;
  if ((size_t)n < sizeof(line) - 2) {
    const int m = vsnprintf(line + n, sizeof(line) - 2 - (size_t)n, format, args);
    if (m > 0) n += m;
  }
  if ((size_t)n > sizeof(line) - 2) n = (int)sizeof(line) - 2;  // Truncated line.
  line[n] = '\n';
  line[n + 1] = '\0';
  g_sink(level, line);
}

void write(LogLevel level, const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vwrite(level, tag, format, args);
  va_end(args);
}

}  // namespace log
}  // namespace stella
