#include "dino_game.h"

#include <M5Unified.h>
#include <Preferences.h>

#include "config.h"
#include "ui.h"

namespace DinoGame {

// world (landscape 240x135)
static constexpr int kGroundY = 110;
static constexpr int kDinoX = 26;
static constexpr int kDinoW = 14;
static constexpr int kDinoH = 18;
static constexpr float kGravity = 1250.0f;  // px/s^2
static constexpr float kJumpV = -330.0f;    // px/s

struct Obstacle {
  float x;
  uint8_t w, h;
};

static Obstacle s_obs[4];
static float s_dinoY;
static float s_dinoVy;
static bool s_onGround;
static float s_speed;
static float s_dist;
static float s_sinceSpawn;
static float s_nextGap;
static bool s_gameOver;
static uint32_t s_lastMs;
static int s_score;
static int s_hiScore = -1;
static Preferences s_prefs;

static void scheduleNextGap() {
  s_sinceSpawn = 0;
  s_nextGap = 90.0f + (float)random(110) + s_speed * 0.5f;
}

void reset() {
  if (s_hiScore < 0) {  // first run: load high score
    s_prefs.begin("dino", false);
    s_hiScore = s_prefs.getInt("hi", 0);
  }
  for (auto& o : s_obs) o.x = -100;
  s_dinoY = kGroundY - kDinoH;
  s_dinoVy = 0;
  s_onGround = true;
  s_speed = 100;
  s_dist = 0;
  s_score = 0;
  s_gameOver = false;
  s_lastMs = millis();
  scheduleNextGap();
  s_nextGap += 120;  // a little breathing room at the start
}

static void spawnObstacle() {
  for (auto& o : s_obs) {
    if (o.x > -50) continue;
    o.x = 244;
    switch (random(3)) {
      case 0: o.w = 10; o.h = 20; break;  // small cactus
      case 1: o.w = 12; o.h = 28; break;  // tall cactus
      default: o.w = 20; o.h = 20; break; // double cactus
    }
    return;
  }
}

static void drawDino(M5Canvas& cv) {
  const int x = kDinoX;
  const int y = (int)s_dinoY;
  cv.fillRect(x + 6, y, 9, 7, COL_FG);        // head
  cv.drawPixel(x + 10, y + 2, COL_BG);        // eye
  cv.fillRect(x, y + 5, 12, 9, COL_FG);       // body
  cv.fillRect(x - 3, y + 7, 4, 3, COL_FG);    // tail
  // running legs (alternate while on the ground)
  const bool step = ((int)(s_dist / 12)) & 1;
  if (s_onGround) {
    cv.fillRect(x + (step ? 2 : 7), y + 14, 3, 4, COL_FG);
    cv.fillRect(x + (step ? 7 : 2), y + 14, 3, 2, COL_FG);
  } else {
    cv.fillRect(x + 2, y + 14, 3, 3, COL_FG);
    cv.fillRect(x + 7, y + 14, 3, 3, COL_FG);
  }
}

static void drawObstacle(M5Canvas& cv, const Obstacle& o) {
  const int x = (int)o.x;
  const int top = kGroundY - o.h;
  if (o.w >= 18) {  // double cactus
    cv.fillRect(x + 1, top, 6, o.h, COL_FG);
    cv.fillRect(x + 11, top + 4, 6, o.h - 4, COL_FG);
    cv.fillRect(x - 2, top + 6, 4, 3, COL_FG);
  } else {
    cv.fillRect(x + o.w / 2 - 2, top, 5, o.h, COL_FG);
    cv.fillRect(x, top + 5, 4, 3, COL_FG);            // left arm
    cv.fillRect(x + o.w - 3, top + 8, 4, 3, COL_FG);  // right arm
  }
}

static void drawScene(M5Canvas& cv) {
  cv.fillSprite(COL_BG);

  // clouds
  const int c1 = 230 - ((int)(s_dist / 4) % 300);
  const int c2 = 230 - ((int)(s_dist / 4 + 170) % 300);
  cv.fillRoundRect(c1, 28, 24, 7, 3, COL_DIM);
  cv.fillRoundRect(c1 + 6, 23, 14, 6, 3, COL_DIM);
  cv.fillRoundRect(c2, 48, 20, 6, 3, COL_DIM);

  // ground with scrolling dashes
  cv.drawFastHLine(0, kGroundY, 240, COL_FG);
  const int off = (int)s_dist % 24;
  for (int x = -off; x < 240; x += 24) {
    cv.drawFastHLine(x, kGroundY + 4, 8, COL_DIM);
  }

  for (const auto& o : s_obs) {
    if (o.x > -50) drawObstacle(cv, o);
  }
  drawDino(cv);

  // score
  char buf[24];
  snprintf(buf, sizeof(buf), "%05d  HI %05d", s_score, s_hiScore);
  cv.setFont(&fonts::Font2);
  cv.setTextColor(COL_FG, COL_BG);
  cv.setTextDatum(top_right);
  cv.drawString(buf, 236, 2);

  cv.setFont(&fonts::Font0);
  cv.setTextColor(COL_DIM, COL_BG);
  cv.setTextDatum(bottom_left);
  cv.drawString("Blue: jump   hold Low: quit", 4, 133);
}

Result frame() {
  M5Canvas& cv = UI::cv();

  if (s_gameOver) {
    if (M5.BtnA.wasClicked()) reset();      // retry
    else if (M5.BtnB.wasClicked()) return kExit;
    return kPlaying;
  }

  if (M5.BtnB.wasHold()) return kExit;
  if (M5.BtnA.wasPressed() && s_onGround) {
    s_dinoVy = kJumpV;
    s_onGround = false;
  }

  const uint32_t now = millis();
  float dt = (now - s_lastMs) / 1000.0f;
  s_lastMs = now;
  if (dt > 0.05f) dt = 0.05f;

  // dino physics
  if (!s_onGround) {
    s_dinoVy += kGravity * dt;
    s_dinoY += s_dinoVy * dt;
    if (s_dinoY >= kGroundY - kDinoH) {
      s_dinoY = kGroundY - kDinoH;
      s_dinoVy = 0;
      s_onGround = true;
    }
  }

  // world
  const float step = s_speed * dt;
  s_dist += step;
  s_sinceSpawn += step;
  if (s_speed < 230.0f) s_speed += 4.0f * dt;
  s_score = (int)(s_dist / 8);
  if (s_sinceSpawn >= s_nextGap) {
    spawnObstacle();
    scheduleNextGap();
  }
  for (auto& o : s_obs) {
    if (o.x > -50) o.x -= step;
  }

  // collision (with a small grace margin)
  bool hit = false;
  const int dx0 = kDinoX + 2, dx1 = kDinoX + kDinoW - 2;
  const int dy0 = (int)s_dinoY + 2;
  for (const auto& o : s_obs) {
    if (o.x <= -50) continue;
    const int ox0 = (int)o.x + 1, ox1 = (int)o.x + o.w - 1;
    const int oy0 = kGroundY - o.h + 2;
    if (dx1 > ox0 && dx0 < ox1 && (int)s_dinoY + kDinoH > oy0 && dy0 < kGroundY) {
      hit = true;
      break;
    }
  }

  drawScene(cv);

  if (hit) {
    s_gameOver = true;
    if (s_score > s_hiScore) {
      s_hiScore = s_score;
      s_prefs.putInt("hi", s_hiScore);
    }
    cv.fillRoundRect(40, 38, 160, 62, 6, COL_BG);
    cv.drawRoundRect(40, 38, 160, 62, 6, COL_FG);
    cv.setTextDatum(middle_center);
    cv.setFont(&fonts::Font4);
    cv.setTextColor(COL_FG, COL_BG);
    cv.drawString("GAME OVER", 120, 56);
    char buf[20];
    snprintf(buf, sizeof(buf), "score %d", s_score);
    cv.setFont(&fonts::Font2);
    cv.drawString(buf, 120, 76);
    cv.setFont(&fonts::Font0);
    cv.setTextColor(COL_DIM, COL_BG);
    cv.drawString("Blue: retry    Low: exit", 120, 92);
  }

  UI::push();
  return kPlaying;
}

}  // namespace DinoGame
