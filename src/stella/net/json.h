#pragma once
// Bounded streaming (SAX-style) JSON parser.
//
// Bytes are pushed in as they arrive from the network; scalar values are
// reported with their path ("daily.temperature_2m_max[2]") and nothing is
// buffered beyond one key and one value. Memory is fixed (about 0.5 KiB), so a
// large or hostile response can never exhaust the heap; limits are errors.
#include <stddef.h>
#include <stdint.h>

namespace stella {

enum class JsonType : uint8_t { String, Number, Bool, Null };

struct JsonValue {
  JsonType type;
  const char *text;  // string contents or the number's text (NUL-terminated)
  double number;     // for Number
  bool boolean;      // for Bool
  bool truncated;    // string longer than the value buffer
};

class JsonPath {
 public:
  static constexpr int kMaxDepth = 10;
  static constexpr int kMaxKey = 40;  // Open-Meteo has keys like precipitation_probability_max (29)

  int depth() const { return depth_; }
  // Pattern match: dot-separated keys, "[]" matches any array index, e.g.
  // "current.temperature_2m" or "daily.time[]".
  bool is(const char *pattern) const;
  // Index of the innermost array at or above this value (-1 if none).
  int index() const;

 private:
  friend class JsonParser;
  struct Level {
    bool array;
    bool keyOverflow;
    int16_t index;
    char key[kMaxKey];
  };
  Level levels_[kMaxDepth];
  int depth_ = 0;
};

class JsonHandler {
 public:
  virtual ~JsonHandler() = default;
  virtual void onValue(const JsonPath &path, const JsonValue &value) = 0;
};

class JsonParser {
 public:
  explicit JsonParser(JsonHandler &handler) : handler_(handler) {}
  void reset();
  // Returns false on a syntax error or exceeded limit (see error()).
  bool feed(const char *data, size_t size);
  // True if exactly one complete top-level value was parsed.
  bool finish();
  const char *error() const { return error_; }

 private:
  enum class State : uint8_t {
    Value, ObjectKeyOrEnd, ObjectKey, Colon, CommaOrEnd, ArrayValueOrEnd,
    String, Escape, Unicode, Number, Literal, Done, Error
  };
  bool step(char c);
  bool fail(const char *why);
  bool beginValue(char c);
  void valueDone();
  void emitString();
  bool emitNumber();
  bool emitLiteral();
  void appendUtf8(uint32_t cp);
  void appendChar(char c);

  JsonHandler &handler_;
  JsonPath path_;
  State state_ = State::Value;
  bool inKey_ = false;
  char buffer_[96];
  size_t length_ = 0;
  bool truncated_ = false;
  uint32_t unicode_ = 0;
  uint8_t unicodeDigits_ = 0;
  const char *error_ = nullptr;
};

}  // namespace stella
