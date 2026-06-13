#include "level_tool.h"

#include <M5Unified.h>
#include <math.h>

#include "config.h"
#include "ui.h"

namespace LevelTool {

static constexpr float kLevelDeg = 1.5f;  // within this = "level"
static bool s_wasLevel = false;
static uint32_t s_lastBeep = 0;

void begin() {
  M5.Speaker.begin();
  M5.Speaker.setVolume(140);
  s_wasLevel = false;
  s_lastBeep = 0;
}

void end() {
  M5.Speaker.end();  // amp must stay off for IR receive
}

Result frame() {
  if (M5.BtnA.wasHold() || M5.BtnB.wasHold()) return kExit;

  M5.Imu.update();
  float ax = 0, ay = 0, az = 0;
  M5.Imu.getAccel(&ax, &ay, &az);

  // Pick the axis gravity points along; the other two are the tilt plane.
  const float aa[3] = {ax, ay, az};
  int dom = 0;
  for (int i = 1; i < 3; i++)
    if (fabsf(aa[i]) > fabsf(aa[dom])) dom = i;
  const int i1 = (dom + 1) % 3;
  const int i2 = (dom + 2) % 3;

  // In-plane gravity components -> tilt. |g| ~ 1, so component ~ sin(angle).
  float u = aa[i1];
  float v = aa[i2];
  float mag = sqrtf(u * u + v * v);
  if (mag > 1.0f) mag = 1.0f;
  const float tiltDeg = asinf(mag) * 57.2958f;
  const bool level = tiltDeg <= kLevelDeg;

  // --- render ---
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("LEVEL");

  const int cx = cv.width() / 2;
  const int cy = (cv.height() + 16) / 2;
  const int R = 48;            // outer ring radius
  const uint16_t accent = level ? TFT_GREEN : COL_FG;

  // outer ring + crosshair + centered target ring
  cv.drawCircle(cx, cy, R, COL_DIM);
  cv.drawCircle(cx, cy, R - 1, COL_DIM);
  cv.drawFastHLine(cx - R, cy, 2 * R, COL_DIM);
  cv.drawFastVLine(cx, cy - R, 2 * R, COL_DIM);
  cv.drawCircle(cx, cy, 12, accent);

  // bubble: offset proportional to tilt, clamped inside the ring
  const float scale = R / 0.55f;  // ~0.55g (33 deg) maps to the rim
  int bx = (int)(u * scale);
  int by = (int)(v * scale);
  int bd = (int)sqrtf((float)(bx * bx + by * by));
  if (bd > R - 9) {
    bx = bx * (R - 9) / bd;
    by = by * (R - 9) / bd;
  }
  cv.fillCircle(cx + bx, cy - by, 8, accent);

  // numeric tilt
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f deg", tiltDeg);
  cv.setFont(&fonts::Font2);
  cv.setTextColor(level ? TFT_GREEN : COL_FG, COL_BG);
  cv.setTextDatum(top_left);
  cv.drawString(buf, 6, 22);
  if (level) {
    cv.setTextDatum(top_right);
    cv.drawString("LEVEL", cv.width() - 6, 22);
  }

  UI::drawFooter("hold any button: back");
  UI::push();

  // --- audio feedback ---
  if (level) {
    // chirp on becoming level, then a soft tick ~once a second
    if (!s_wasLevel || millis() - s_lastBeep > 1000) {
      M5.Speaker.tone(s_wasLevel ? 1500 : 2200, 60);
      s_lastBeep = millis();
    }
  }
  s_wasLevel = level;

  return kStay;
}

}  // namespace LevelTool
