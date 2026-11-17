#pragma once
// What changed in each version, for people (not commit messages), newest first.
// Shown by Settings > About > What's new, and once by itself after an update.
// Keep lines short: they are one line each on a 320 px screen (about 34 characters).
#include <stddef.h>

namespace stella {
namespace apps {

struct ChangelogEntry {
  const char *version;
  const char *lines[6];  // up to six, then nullptr
};

const ChangelogEntry *changelog(int &count);

}  // namespace apps
}  // namespace stella
