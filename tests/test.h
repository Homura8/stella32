#pragma once
// Minimal self-registering test framework (no external dependencies).
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
#include <stdio.h>

namespace stella_test {

struct Case {
  const char *name;
  void (*fn)();
  Case *next;
};

inline Case *&head() {
  static Case *h = nullptr;
  return h;
}
inline int &failures() {
  static int f = 0;
  return f;
}
inline const char *&current() {
  static const char *c = "";
  return c;
}

struct Registrar {
  Registrar(Case *c) {
    c->next = head();
    head() = c;
  }
};

inline void fail(const char *file, int line, const char *expr) {
  ++failures();
  fprintf(stderr, "FAIL %s (%s:%d): %s\n", current(), file, line, expr);
}

}  // namespace stella_test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                                 \
  static void test_##name();                                                       \
  static stella_test::Case TEST_CAT(case_, name){#name, test_##name, nullptr};     \
  static stella_test::Registrar TEST_CAT(reg_, name){&TEST_CAT(case_, name)};      \
  static void test_##name()

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) stella_test::fail(__FILE__, __LINE__, #cond);         \
  } while (0)
#define CHECK_EQ(a, b)                                                                              \
  do {                                                                                              \
    const auto va_ = (a);                                                                           \
    const auto vb_ = (b);                                                                           \
    if (!(va_ == vb_)) {                                                                            \
      stella_test::fail(__FILE__, __LINE__, #a " == " #b);                                          \
      fprintf(stderr, "     got %lld vs %lld\n", (long long)va_, (long long)vb_);                   \
    }                                                                                               \
  } while (0)
