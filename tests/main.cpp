#include <string.h>

#include "stella/core/log.h"
#include "test.h"

int main(int argc, char **argv) {
  // Keep test output readable; failures print their own diagnostics.
  stella::log::setLevel(stella::LogLevel::Error);
  const char *filter = argc > 1 ? argv[1] : nullptr;
  int run = 0;
  for (stella_test::Case *c = stella_test::head(); c; c = c->next) {
    if (filter && !strstr(c->name, filter)) continue;
    stella_test::current() = c->name;
    c->fn();
    ++run;
  }
  if (stella_test::failures()) {
    fprintf(stderr, "%d check(s) failed across %d test(s)\n", stella_test::failures(), run);
    return 1;
  }
  printf("%d tests passed\n", run);
  return 0;
}
