#pragma once
// A small, strict HTTP/1.x request reader for the phone-setup web page. It is
// fed bytes as they arrive (any split, even one byte at a time), keeps only
// what the page needs (method, path, a bounded form body) and never grows:
// header lines are inspected and dropped, so a phone's long User-Agent or
// cookies cost nothing. Nothing here waits for the network.
#include <stddef.h>
#include <stdint.h>

namespace stella {

class HttpRequestParser {
 public:
  static constexpr size_t kMaxBody = 512;   // a form with a 63-byte password fits easily
  static constexpr size_t kMaxPath = 96;
  static constexpr size_t kMaxHeaderBytes = 8192;  // all header lines together

  enum class State : uint8_t { Request, Headers, Body, Complete, Failed };

  HttpRequestParser() { reset(); }

  // Forget the previous request (and wipe its body: it may hold a password).
  void reset();
  // Consumes `size` bytes and returns the new state. Bytes after the end of a
  // complete request are ignored.
  State feed(const uint8_t *data, size_t size);

  State state() const { return state_; }
  bool done() const { return state_ == State::Complete || state_ == State::Failed; }
  bool post() const { return post_; }
  const char *path() const { return path_; }
  const char *body() const { return body_; }  // NUL-terminated
  size_t bodyLen() const { return bodyLen_; }
  // HTTP status to answer a Failed request with (400, 405, 413 or 501).
  int errorStatus() const { return errorStatus_; }

 private:
  static constexpr size_t kLine = 96;

  void fail(int status);
  void line();  // one complete line is in line_
  void headersDone();

  State state_ = State::Request;
  bool post_ = false, lineLong_ = false;
  int errorStatus_ = 0;
  long contentLength_ = -1;
  size_t lineLen_ = 0, headerBytes_ = 0, bodyLen_ = 0;
  char line_[kLine];
  char path_[kMaxPath];
  char body_[kMaxBody + 1];
};

}  // namespace stella
