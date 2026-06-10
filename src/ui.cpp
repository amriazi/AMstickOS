#include "ui.h"

#include "config.h"

namespace UI {

static M5Canvas s_canvas(&M5.Display);

static constexpr int kHeaderH = 20;
static constexpr int kFooterH = 14;
static constexpr int kRowH = 18;

void begin() {
  s_canvas.setColorDepth(16);
  s_canvas.createSprite(M5.Display.width(), M5.Display.height());
  s_canvas.setTextWrap(false);
}

M5Canvas& cv() { return s_canvas; }

void push() { s_canvas.pushSprite(0, 0); }

void clear() { s_canvas.fillSprite(COL_BG); }

void drawHeader(const char* title) {
  s_canvas.fillRect(0, 0, s_canvas.width(), kHeaderH, COL_SEL_BG);
  s_canvas.setTextColor(COL_SEL_FG, COL_SEL_BG);
  s_canvas.setFont(&fonts::Font2);
  s_canvas.setTextDatum(middle_center);
  s_canvas.drawString(title, s_canvas.width() / 2, kHeaderH / 2);
}

void drawFooter(const char* hint) {
  if (!hint) return;
  s_canvas.setTextColor(COL_DIM, COL_BG);
  s_canvas.setFont(&fonts::Font0);
  s_canvas.setTextDatum(middle_center);
  s_canvas.drawString(hint, s_canvas.width() / 2,
                      s_canvas.height() - kFooterH / 2);
}

void drawMenu(const char* title, const String* items, int n, int sel,
              const char* hint) {
  clear();
  drawHeader(title);

  const int listY = kHeaderH + 2;
  const int visible = (s_canvas.height() - listY - kFooterH) / kRowH;
  int top = 0;
  if (sel >= visible) top = sel - visible + 1;

  s_canvas.setFont(&fonts::Font2);
  s_canvas.setTextDatum(middle_left);
  for (int i = 0; i < visible && top + i < n; i++) {
    const int idx = top + i;
    const int y = listY + i * kRowH;
    if (idx == sel) {
      s_canvas.fillRoundRect(2, y, s_canvas.width() - 4, kRowH - 1, 3, COL_SEL_BG);
      s_canvas.setTextColor(COL_SEL_FG, COL_SEL_BG);
      s_canvas.drawString("> " + items[idx], 8, y + kRowH / 2);
    } else {
      s_canvas.setTextColor(COL_FG, COL_BG);
      s_canvas.drawString("  " + items[idx], 8, y + kRowH / 2);
    }
  }

  // Scroll indicators
  s_canvas.setTextColor(COL_DIM, COL_BG);
  s_canvas.setTextDatum(middle_right);
  if (top > 0) s_canvas.drawString("^", s_canvas.width() - 4, listY + 6);
  if (top + visible < n) {
    s_canvas.drawString("v", s_canvas.width() - 4,
                        listY + visible * kRowH - 6);
  }

  drawFooter(hint);
  push();
}

void drawMsg(const char* title, const char* l1, const char* l2,
             const char* l3, const char* hint) {
  clear();
  drawHeader(title);
  s_canvas.setFont(&fonts::Font2);
  s_canvas.setTextColor(COL_FG, COL_BG);
  s_canvas.setTextDatum(middle_center);
  const int cx = s_canvas.width() / 2;
  int y = kHeaderH + 18;
  if (l1) { s_canvas.drawString(l1, cx, y); y += 20; }
  if (l2) { s_canvas.drawString(l2, cx, y); y += 20; }
  if (l3) { s_canvas.drawString(l3, cx, y); }
  drawFooter(hint);
  push();
}

void toast(const char* title, const char* text, uint32_t ms) {
  drawMsg(title, text);
  delay(ms);
}

}  // namespace UI
