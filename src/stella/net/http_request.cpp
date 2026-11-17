#include "http_request.h"

#include <string.h>

namespace stella {
namespace {

void wipe(char *p, size_t n) {
  volatile char *v = p;
  for (size_t i = 0; i < n; ++i) v[i] = 0;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Case-insensitive "line starts with name" (name is lower case).
bool startsWith(const char *line, size_t len, const char *name) {
  const size_t n = strlen(name);
  if (len < n) return false;
  for (size_t i = 0; i < n; ++i)
    if (lower(line[i]) != name[i]) return false;
  return true;
}

}  // namespace

void HttpRequestParser::reset() {
  wipe(line_, sizeof(line_));
  wipe(body_, sizeof(body_));
  path_[0] = '\0';
  state_ = State::Request;
  post_ = lineLong_ = false;
  errorStatus_ = 0;
  contentLength_ = -1;
  lineLen_ = headerBytes_ = bodyLen_ = 0;
}

void HttpRequestParser::fail(int status) {
  state_ = State::Failed;
  errorStatus_ = status;
  wipe(body_, sizeof(body_));
  bodyLen_ = 0;
}

HttpRequestParser::State HttpRequestParser::feed(const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size && !done(); ++i) {
    const char c = (char)data[i];
    if (state_ == State::Body) {
      body_[bodyLen_++] = c;
      body_[bodyLen_] = '\0';
      if ((long)bodyLen_ == contentLength_) state_ = State::Complete;
      continue;
    }
    if (++headerBytes_ > kMaxHeaderBytes) {
      fail(400);
      break;
    }
    if (c == '\n') {
      if (lineLen_ && line_[lineLen_ - 1] == '\r') --lineLen_;
      line_[lineLen_] = '\0';
      line();
      lineLen_ = 0;
      lineLong_ = false;
    } else if (lineLen_ + 1 < kLine) {
      line_[lineLen_++] = c;
    } else {
      lineLong_ = true;  // the rest of an over-long line is dropped
    }
  }
  return state_;
}

void HttpRequestParser::line() {
  if (state_ == State::Request) {
    if (!lineLen_) return;  // stray blank lines before a request are allowed
    if (lineLong_) return fail(400);
    // METHOD SP target SP HTTP/x.y
    const char *p = line_;
    if (strncmp(p, "GET ", 4) == 0) {
      post_ = false;
      p += 4;
    } else if (strncmp(p, "HEAD ", 5) == 0) {
      post_ = false;
      p += 5;
    } else if (strncmp(p, "POST ", 5) == 0) {
      post_ = true;
      p += 5;
    } else {
      return fail(405);
    }
    size_t n = 0;
    while (*p && *p != ' ' && *p != '?' && *p != '#') {
      if (n + 1 >= kMaxPath) return fail(400);
      path_[n++] = *p++;
    }
    path_[n] = '\0';
    if (!n || path_[0] != '/') return fail(400);  // the page only serves absolute paths
    if (!strstr(line_, " HTTP/1.")) return fail(400);
    state_ = State::Headers;
    return;
  }
  // Headers.
  if (!lineLen_) return headersDone();
  if (startsWith(line_, lineLen_, "content-length:")) {
    if (lineLong_ || contentLength_ >= 0) return fail(400);
    const char *p = line_ + 15;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return fail(400);
    long v = 0;
    for (; *p >= '0' && *p <= '9'; ++p) {
      v = v * 10 + (*p - '0');
      if (v > (long)kMaxBody) return fail(413);
    }
    contentLength_ = v;
  } else if (startsWith(line_, lineLen_, "transfer-encoding:")) {
    return fail(501);  // chunked bodies are not supported
  }
}

void HttpRequestParser::headersDone() {
  if (!post_) {
    state_ = State::Complete;
    return;
  }
  if (contentLength_ < 0) return fail(400);
  state_ = contentLength_ == 0 ? State::Complete : State::Body;
  body_[0] = '\0';
}

}  // namespace stella
