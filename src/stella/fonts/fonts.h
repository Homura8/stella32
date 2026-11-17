#pragma once
// Built-in typefaces (Celestia Glow): Quicksand for text, Baloo 2 for headings
// and the clock. One FontSet per density class; the theme picks the set that
// matches the display. Regenerate with tools/fontgen.py.
#include "../gfx/font.h"

namespace stella {
namespace fonts {

struct FontSet {
  const Font *caption;   // small labels, status bar
  const Font *body;      // default text
  const Font *title;     // screen and card titles
  const Font *headline;  // large greetings / values
  const Font *display;   // big numerals only: " 0123456789:-APM°"
  const Font *bedside;   // the largest numerals, " 0123456789:", for the bedside clock
  const Font *hometime;  // the time in Home's header: " 0123456789:-APM°"
};

extern const FontSet kCompact;  // ~320x240 panels
extern const FontSet kRegular;  // ~480x320 and larger

}  // namespace fonts
}  // namespace stella
