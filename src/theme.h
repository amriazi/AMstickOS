#pragma once

// Runtime color themes. Selecting a theme updates the COL_* globals
// declared in config.h, which every screen draws with.
namespace Theme {

int count();
const char* name(int idx);
int current();
void apply(int idx);  // set the COL_* globals (no persistence)
void load();          // restore saved theme from NVS (call once at boot)
void save(int idx);   // apply + persist to NVS

}  // namespace Theme
