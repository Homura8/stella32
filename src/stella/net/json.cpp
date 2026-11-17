#include "json.h"

#include <stdlib.h>
#include <string.h>

namespace stella {

bool JsonPath::is(const char *pattern) const {
  const char *p = pattern;
  for (int i = 0; i < depth_; ++i) {
    const Level &l = levels_[i];
    if (l.array) {
      if (p[0] != '[' || p[1] != ']') return false;
      p += 2;
      continue;
    }
    if (*p == '.') ++p;
    if (l.keyOverflow) return false;
    const size_t n = strlen(l.key);
    if (strncmp(p, l.key, n) != 0) return false;
    p += n;
    if (*p && *p != '.' && *p != '[') return false;
  }
  return *p == '\0';
}

int JsonPath::index() const {
  for (int i = depth_ - 1; i >= 0; --i)
    if (levels_[i].array) return levels_[i].index;
  return -1;
}

void JsonParser::reset() {
  path_.depth_ = 0;
  state_ = State::Value;
  inKey_ = false;
  length_ = 0;
  truncated_ = false;
  error_ = nullptr;
}

bool JsonParser::fail(const char *why) {
  if (state_ != State::Error) error_ = why;
  state_ = State::Error;
  return false;
}

bool JsonParser::feed(const char *data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (!step(data[i])) return false;
  }
  return state_ != State::Error;
}

bool JsonParser::finish() {
  if (state_ == State::Number) {
    if (!emitNumber()) return false;
    valueDone();
  } else if (state_ == State::Literal) {
    if (!emitLiteral()) return false;
    valueDone();
  }
  if (state_ == State::Done) return true;
  if (state_ != State::Error) fail("unexpected end of input");
  return false;
}

void JsonParser::appendChar(char c) {
  if (length_ + 1 < sizeof(buffer_)) {
    buffer_[length_++] = c;
  } else {
    truncated_ = true;
  }
}

void JsonParser::appendUtf8(uint32_t cp) {
  if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';  // surrogate halves are not decoded
  if (cp < 0x80) {
    appendChar((char)cp);
  } else if (cp < 0x800) {
    appendChar((char)(0xC0 | (cp >> 6)));
    appendChar((char)(0x80 | (cp & 0x3F)));
  } else {
    appendChar((char)(0xE0 | (cp >> 12)));
    appendChar((char)(0x80 | ((cp >> 6) & 0x3F)));
    appendChar((char)(0x80 | (cp & 0x3F)));
  }
}

void JsonParser::valueDone() {
  if (path_.depth_ == 0) {
    state_ = State::Done;
  } else {
    state_ = State::CommaOrEnd;
  }
}

void JsonParser::emitString() {
  buffer_[length_] = '\0';
  JsonValue v{JsonType::String, buffer_, 0, false, truncated_};
  handler_.onValue(path_, v);
}

bool JsonParser::emitNumber() {
  buffer_[length_] = '\0';
  if (truncated_ || length_ == 0) return fail("number too long");
  char *end = nullptr;
  const double d = strtod(buffer_, &end);
  if (!end || *end != '\0' || buffer_[0] == '+') return fail("invalid number");
  JsonValue v{JsonType::Number, buffer_, d, false, false};
  handler_.onValue(path_, v);
  return true;
}

bool JsonParser::emitLiteral() {
  buffer_[length_] = '\0';
  JsonValue v{JsonType::Null, buffer_, 0, false, false};
  if (strcmp(buffer_, "true") == 0) {
    v.type = JsonType::Bool;
    v.boolean = true;
  } else if (strcmp(buffer_, "false") == 0) {
    v.type = JsonType::Bool;
  } else if (strcmp(buffer_, "null") != 0) {
    return fail("invalid literal");
  }
  handler_.onValue(path_, v);
  return true;
}

bool JsonParser::beginValue(char c) {
  length_ = 0;
  truncated_ = false;
  if (c == '{' || c == '[') {
    if (path_.depth_ >= JsonPath::kMaxDepth) return fail("nesting too deep");
    JsonPath::Level &l = path_.levels_[path_.depth_++];
    l.array = c == '[';
    l.keyOverflow = false;
    l.index = 0;
    l.key[0] = '\0';
    state_ = l.array ? State::ArrayValueOrEnd : State::ObjectKeyOrEnd;
    return true;
  }
  if (c == '"') {
    inKey_ = false;
    state_ = State::String;
    return true;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    appendChar(c);
    state_ = State::Number;
    return true;
  }
  if (c == 't' || c == 'f' || c == 'n') {
    appendChar(c);
    state_ = State::Literal;
    return true;
  }
  return fail("unexpected character");
}

static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool JsonParser::step(char c) {
  switch (state_) {
    case State::Error:
      return false;
    case State::Done:
      return isSpace(c) ? true : fail("data after the top-level value");
    case State::Value:
      return isSpace(c) ? true : beginValue(c);
    case State::ArrayValueOrEnd:
      if (isSpace(c)) return true;
      if (c == ']') {
        --path_.depth_;
        valueDone();
        return true;
      }
      return beginValue(c);
    case State::ObjectKeyOrEnd:
      if (isSpace(c)) return true;
      if (c == '}') {
        --path_.depth_;
        valueDone();
        return true;
      }
      // fallthrough
    case State::ObjectKey:
      if (isSpace(c)) return true;
      if (c != '"') return fail("expected a key");
      inKey_ = true;
      length_ = 0;
      truncated_ = false;
      state_ = State::String;
      return true;
    case State::Colon:
      if (isSpace(c)) return true;
      if (c != ':') return fail("expected ':'");
      state_ = State::Value;
      return true;
    case State::CommaOrEnd: {
      if (isSpace(c)) return true;
      JsonPath::Level &l = path_.levels_[path_.depth_ - 1];
      if (c == ',') {
        if (l.array) {
          if (l.index < 32767) ++l.index;
          state_ = State::Value;
        } else {
          state_ = State::ObjectKey;
        }
        return true;
      }
      if ((c == ']' && l.array) || (c == '}' && !l.array)) {
        --path_.depth_;
        valueDone();
        return true;
      }
      return fail("expected ',' or the end of a container");
    }
    case State::String:
      if (c == '"') {
        if (inKey_) {
          JsonPath::Level &l = path_.levels_[path_.depth_ - 1];
          l.keyOverflow = truncated_ || length_ >= sizeof(l.key);
          const size_t n = length_ < sizeof(l.key) - 1 ? length_ : sizeof(l.key) - 1;
          memcpy(l.key, buffer_, n);
          l.key[n] = '\0';
          state_ = State::Colon;
        } else {
          emitString();
          valueDone();
        }
        return true;
      }
      if (c == '\\') {
        state_ = State::Escape;
        return true;
      }
      if ((unsigned char)c < 0x20) return fail("control character in string");
      appendChar(c);
      return true;
    case State::Escape:
      state_ = State::String;
      switch (c) {
        case '"': appendChar('"'); return true;
        case '\\': appendChar('\\'); return true;
        case '/': appendChar('/'); return true;
        case 'b': appendChar('\b'); return true;
        case 'f': appendChar('\f'); return true;
        case 'n': appendChar('\n'); return true;
        case 'r': appendChar('\r'); return true;
        case 't': appendChar('\t'); return true;
        case 'u':
          unicode_ = 0;
          unicodeDigits_ = 0;
          state_ = State::Unicode;
          return true;
        default: return fail("invalid escape");
      }
    case State::Unicode: {
      int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (v < 0) return fail("invalid \\u escape");
      unicode_ = (unicode_ << 4) | (uint32_t)v;
      if (++unicodeDigits_ == 4) {
        appendUtf8(unicode_);
        state_ = State::String;
      }
      return true;
    }
    case State::Number:
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
        if (length_ >= 32) return fail("number too long");
        appendChar(c);
        return true;
      }
      if (!emitNumber()) return false;
      valueDone();
      return step(c);
    case State::Literal:
      if (c >= 'a' && c <= 'z') {
        if (length_ >= 5) return fail("invalid literal");
        appendChar(c);
        return true;
      }
      if (!emitLiteral()) return false;
      valueDone();
      return step(c);
  }
  return fail("internal state");
}

}  // namespace stella
