#pragma once
// The words for a gift build: a CYD running StellaOS that opens on a message
// first thing, then explains itself. To make your own:
//   - Arduino IDE: copy this file to "gift.h" next to CYD.ino and change the words
//     there (CYD.ino uses gift.h when it exists; gift.h is not committed, so a name
//     in it stays on your computer), then remove the "//" before
//     "#define STELLA_GIFT 1" at the top of CYD.ino.
//   - arduino-cli: the same gift.h, then
//       arduino-cli compile --profile cyd --build-property "compiler.cpp.extra_flags=-DSTELLA_GIFT=1" examples/CYD
//     (without a gift.h it builds with the words below).
//
// The device saves the words the first time it starts with them (and again if you
// change them), so the message stays through any later update, even to an
// ordinary build that has no words of its own. Limits: name 15 characters,
// headline 63, wish 95, up to 8 pages of 199 characters. Each page is read one at a
// time on the 320x240 screen, so keep them to a few lines;
// tests/test_greeting.cpp checks these words fit at every screen size.
#include <Stella.h>

inline const stella::apps::GreetingContent &giftContent() {
  static const stella::apps::GreetingContent kContent = [] {
    stella::apps::GreetingContent c;
    c.name = "Birthday";
    c.headline = "Happy Birthday!";  // add the name: "Happy Birthday <name>!"
    c.wish = "Wishing you an amazing year ahead.";
    c.pages[0] =
        "This little device is an ESP32 based CYD. All you need to know is that it's basically a tiny computer with a "
        "little resistive touchscreen, wifi/bluetooth and microSD support lol.";
    c.pages[1] = "And best part of it is that it's completely customisable, you can run your own software on it!";
    c.pages[2] =
        "This little OS I built with the help of Claude is an example of what you can make. You can either use it like "
        "this or build your own software for it.";
    c.pages[3] =
        "The source code for this little OS is at https://github.com/Homura8/stella32 if you want to fork it and make "
        "your own version!";
    c.pages[4] = "P.S. I also bundled a regular ESP32-S3 without a screen for you too!";
    c.pageCount = 5;
    return c;
  }();
  return kContent;
}
