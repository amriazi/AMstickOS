#pragma once

#include <M5Unified.h>

// Nemo-style UI: orange-on-black, title bar, list menus, footer hints.
// Everything is drawn to an off-screen canvas and pushed at once.
namespace UI {

void begin();
M5Canvas& cv();
void push();

void clear();
void drawHeader(const char* title);
void drawFooter(const char* hint);
// Scrolling list menu. `sel` is highlighted.
void drawMenu(const char* title, const String* items, int n, int sel,
              const char* hint);
// Simple message screen (up to 3 lines).
void drawMsg(const char* title, const char* l1, const char* l2 = nullptr,
             const char* l3 = nullptr, const char* hint = nullptr);
// Blocking toast (short confirmation).
void toast(const char* title, const char* text, uint32_t ms = 800);

}  // namespace UI
