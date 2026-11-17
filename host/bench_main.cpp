// Rendering benchmark (host, optimised build): full-frame cost per screen and
// the cost of typical partial updates. Not a device measurement; use it to
// compare changes. Build: cmake -S . -B build-bench -DSTELLA_SANITIZE=OFF
//   -DCMAKE_BUILD_TYPE=Release && cmake --build build-bench --target stella_bench
#include <stdio.h>

#include <chrono>

#include "sim_session.h"

using namespace stella;
using namespace stella::host;

namespace {

double frameMs(SimSession &s, int frames) {
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < frames; ++i) {
    s.os.invalidateAll();
    s.os.step();
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
}

}  // namespace

int main() {
  SimSession s(320, 240);
  const char *apps[] = {"home", "clock", "weather", "settings", "touch"};
  double total = 0;
  for (const char *app : apps) {
    s.os.launch(app);
    s.run(300);
    const double ms = frameMs(s, 60);
    total += ms;
    printf("%-9s full frame: %6.3f ms (host)\n", app, ms);
  }
  printf("average   full frame: %6.3f ms (host)\n", total / 5);
  return 0;
}
