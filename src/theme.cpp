#include "theme.h"

#include <M5Unified.h>
#include <Preferences.h>

#include "config.h"

// Definitions of the globals declared in config.h (start on orange).
uint16_t COL_BG = 0x0000;
uint16_t COL_FG = 0xFD20;
uint16_t COL_SEL_BG = 0xFD20;
uint16_t COL_SEL_FG = 0x0000;
uint16_t COL_DIM = 0x8B61;

namespace Theme {

struct Palette {
  const char* name;
  uint16_t fg;
  uint16_t dim;
};

// Background is always black; selection bar is the accent (fg) with
// black text, which reads well for all of these.
static const Palette kPalettes[] = {
    {"Orange", 0xFD20, 0x8B61},
    {"Green", 0x07E0, 0x03E0},
    {"Cyan", 0x07FF, 0x03EF},
    {"Amber", 0xFEA0, 0x8400},
    {"White", 0xFFFF, 0x8410},
    {"Red", 0xF800, 0x8000},
};
static constexpr int kCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

static int s_current = 0;

int count() { return kCount; }

const char* name(int idx) {
  if (idx < 0 || idx >= kCount) idx = 0;
  return kPalettes[idx].name;
}

int current() { return s_current; }

void apply(int idx) {
  if (idx < 0 || idx >= kCount) idx = 0;
  s_current = idx;
  COL_BG = 0x0000;
  COL_FG = kPalettes[idx].fg;
  COL_SEL_BG = kPalettes[idx].fg;
  COL_SEL_FG = 0x0000;
  COL_DIM = kPalettes[idx].dim;
}

void load() {
  Preferences p;
  p.begin("ircopy", true);  // read-only
  int idx = p.getInt("theme", 0);
  p.end();
  apply(idx);
}

void save(int idx) {
  apply(idx);
  Preferences p;
  p.begin("ircopy", false);
  p.putInt("theme", idx);
  p.end();
}

}  // namespace Theme
