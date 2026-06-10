// IR Copy for M5StickS3 (K150)
// - Watch face with auto screen-off (10s)
// - Nemo-style menu (orange on black)
// - Capture any IR remote signal (raw, via RMT), name it, save to flash
// - Replay saved signals through the built-in IR LED
//
// Buttons: Blue = front (G11, BtnA): select / confirm, hold = back
//          Low  = side  (G12, BtnB): next / cycle,     hold = alt action

#include <M5Unified.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "dino_game.h"
#include "ir_engine.h"
#include "ir_store.h"
#include "ui.h"

// ---------------------------------------------------------------- state

enum State {
  ST_WATCH,
  ST_SLEEP,
  ST_MENU,
  ST_REC_WAIT,
  ST_REC_DONE,
  ST_NAME_EDIT,
  ST_LIST,
  ST_ACTIONS,
  ST_DEL_CONFIRM,
  ST_CLOCK_SET,
  ST_ALARM_SET,
  ST_TIMER_SET,
  ST_TIMER_RUN,
  ST_STOPWATCH,
  ST_DINO,
  ST_ALERT,
};

static State s_state = ST_WATCH;
static bool s_dirty = true;
static bool s_swallow = false;  // ignore the button press that woke the screen
static uint32_t s_lastActivity = 0;

static int s_menuSel = 0;
static int s_recSel = 0;
static int s_actSel = 0;
static int s_delSel = 0;

// captured-frame info shown on the result screen
static String s_info1, s_info2;

// name editor
static const char kCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
static char s_name[MAX_NAME_LEN + 1];
static int s_nameLen = 0;
static bool s_pendActive = false;
static int s_pendIdx = 0;

// saved-signal list
static String s_names[IrStore::kMaxSignals];
static int s_nameCount = 0;
static int s_listSel = 0;
static rmt_symbol_word_t s_sendBuf[IrEngine::kMaxSymbols];

// clock editor: year, month, day, hour, minute
static int s_clk[5];
static int s_clkField = 0;

// alarm (persisted to NVS): fires once per matching minute
static bool s_alarmOn = false;
static int s_alarmH = 7, s_alarmM = 0;
static int s_almEdit[3];  // on/off, hour, minute
static int s_almField = 0;
static int s_lastAlarmFire = -1;

// countdown timer (keeps running while you use other screens)
static bool s_timerActive = false;
static bool s_timerPaused = false;
static uint32_t s_timerRemainMs = 0;
static uint32_t s_timerLastTick = 0;
static int s_tmrEdit[2] = {5, 0};  // minutes, seconds
static int s_tmrField = 0;
static uint32_t s_tmrShownSec = UINT32_MAX;

// stopwatch (keeps running while you use other screens)
static bool s_swRun = false;
static uint32_t s_swAccumMs = 0;
static uint32_t s_swStartMs = 0;
static uint32_t s_swLastDraw = 0;

// alarm/timer alert
static const char* s_alertText = "";
static uint32_t s_alertStart = 0;
static bool s_alertPhase = false;

static const String kMainItems[] = {"Copy IR signal", "Saved signals",
                                    "Alarm",          "Timer",
                                    "Stopwatch",      "Dino game",
                                    "Set clock",      "Back to watch"};
static const int kMainCount = 8;
static const String kRecItems[] = {"Save", "Retry", "Discard"};
static const String kActItems[] = {"Send", "Delete", "Back"};
static const String kYesNo[] = {"No", "Yes"};

static void enter(State st) {
  s_state = st;
  s_dirty = true;
}

// ------------------------------------------------------------- green LED
// The green power LED is driven by the M5PM1 PMIC: LED_EN, register
// 0x06 bit 4 (bit 3 of the same register is the 5V ext rail). We keep
// it off and light it only while a button is held or IR is active.
static constexpr uint8_t kPm1Addr = 0x6E;
static bool s_ledOn = true;  // assume on at boot so the first ledSet writes

static void ledSet(bool on) {
  if (on == s_ledOn) return;
  s_ledOn = on;
  if (on) {
    M5.In_I2C.bitOn(kPm1Addr, 0x06, 1 << 4, 100000);
  } else {
    M5.In_I2C.bitOff(kPm1Addr, 0x06, 1 << 4, 100000);
  }
}

// ---------------------------------------------------------------- clock

static Preferences s_prefs;

static time_t buildEpoch() {
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  const char* p = strstr(months, mon);
  struct tm tmv = {};
  tmv.tm_mon = p ? (int)(p - months) / 3 : 0;
  tmv.tm_mday = atoi(__DATE__ + 4);
  tmv.tm_year = atoi(__DATE__ + 7) - 1900;
  tmv.tm_hour = atoi(__TIME__);
  tmv.tm_min = atoi(__TIME__ + 3);
  tmv.tm_sec = atoi(__TIME__ + 6);
  return mktime(&tmv);
}

// The power button fully resets the chip and there is no RTC, so the
// system time would fall back to the firmware build time. We persist
// the clock to NVS flash every minute and restore the newest value.
static void persistClock() {
  s_prefs.putULong("epoch", (uint32_t)time(nullptr));
}

static void seedClock() {
  time_t seed = buildEpoch();
  time_t stored = (time_t)s_prefs.getULong("epoch", 0);
  if (stored > seed) seed = stored;
  if (time(nullptr) < seed) {
    struct timeval tv = {.tv_sec = seed, .tv_usec = 0};
    settimeofday(&tv, nullptr);
  }
}

static int daysInMonth(int year, int month) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    return 29;
  return d[month - 1];
}

// ---------------------------------------------------------------- sleep

static void goSleep() {
  persistClock();
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  setCpuFrequencyMhz(80);
  s_state = ST_SLEEP;
}

static void wakeDisplay() {
  setCpuFrequencyMhz(240);
  M5.Display.wakeup();
  M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
}

static void wakeUp() {
  wakeDisplay();
  s_swallow = true;
  s_lastActivity = millis();
  enter(ST_WATCH);
}

// ------------------------------------------------------- alarm & timer

// The speaker amp interferes with IR receive, so it stays off except
// while an alert is actually ringing.
static void fireAlert(const char* text) {
  s_alertText = text;
  s_alertStart = millis();
  s_alertPhase = false;
  if (s_state == ST_SLEEP) wakeDisplay();
  M5.Speaker.begin();
  M5.Speaker.setVolume(190);
  s_lastActivity = millis();
  enter(ST_ALERT);
}

static void serviceTimer() {
  const uint32_t now = millis();
  if (!s_timerActive || s_timerPaused) {
    s_timerLastTick = now;
    return;
  }
  const uint32_t el = now - s_timerLastTick;
  s_timerLastTick = now;
  if (s_timerRemainMs > el) {
    s_timerRemainMs -= el;
  } else {
    s_timerRemainMs = 0;
    s_timerActive = false;
    fireAlert("TIME'S UP!");
  }
}

static void serviceAlarm() {
  if (!s_alarmOn) return;
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 500) return;
  lastCheck = millis();

  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  const int key = lt.tm_yday * 10000 + lt.tm_hour * 100 + lt.tm_min;
  if (lt.tm_hour == s_alarmH && lt.tm_min == s_alarmM &&
      key != s_lastAlarmFire) {
    s_lastAlarmFire = key;
    fireAlert("ALARM!");
  }
}

static uint32_t swElapsed() {
  return s_swAccumMs + (s_swRun ? millis() - s_swStartMs : 0);
}

// ---------------------------------------------------------------- screens

static void drawWatch(bool force) {
  static int lastSec = -1;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  if (!force && lt.tm_sec == lastSec) return;
  lastSec = lt.tm_sec;

  char hhmm[8], secs[4], date[32];
  strftime(hhmm, sizeof(hhmm), "%H:%M", &lt);
  strftime(secs, sizeof(secs), "%S", &lt);
  strftime(date, sizeof(date), "%a %d %b %Y", &lt);

  M5Canvas& cv = UI::cv();
  UI::clear();

  // battery, top-right (+ flash icon when charging)
  char bat[8];
  snprintf(bat, sizeof(bat), "%d%%", M5.Power.getBatteryLevel());
  cv.setFont(&fonts::Font0);
  cv.setTextColor(COL_DIM, COL_BG);
  cv.setTextDatum(top_right);
  cv.drawString(bat, cv.width() - 4, 4);
  if (M5.Power.isCharging() == m5::Power_Class::is_charging) {
    const int bx = cv.width() - 4 - cv.textWidth(bat) - 11;
    const int by = 2;
    cv.fillTriangle(bx + 6, by, bx + 1, by + 6, bx + 4, by + 6, TFT_YELLOW);
    cv.fillTriangle(bx + 2, by + 11, bx + 7, by + 5, bx + 4, by + 5, TFT_YELLOW);
  }

  // alarm / running-timer status, top-left
  char status[28] = "";
  if (s_alarmOn) {
    snprintf(status, sizeof(status), "AL %02d:%02d", s_alarmH, s_alarmM);
  }
  if (s_timerActive) {
    const uint32_t s = (s_timerRemainMs + 999) / 1000;
    const size_t len = strlen(status);
    snprintf(status + len, sizeof(status) - len, "%sT-%02u:%02u",
             len ? "  " : "", (unsigned)(s / 60), (unsigned)(s % 60));
  }
  if (status[0]) {
    cv.setTextDatum(top_left);
    cv.drawString(status, 4, 4);
  }

  // time, big 7-segment style
  cv.setFont(&fonts::Font7);
  cv.setTextColor(COL_FG, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString(hhmm, cv.width() / 2 - 12, 58);

  // seconds, small, to the right of the time
  cv.setFont(&fonts::Font2);
  cv.setTextColor(COL_DIM, COL_BG);
  cv.setTextDatum(middle_left);
  cv.drawString(secs, cv.width() / 2 + 72, 74);

  // date
  cv.setFont(&fonts::Font2);
  cv.setTextColor(COL_FG, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString(date, cv.width() / 2, 108);

  UI::drawFooter("Blue: menu");
  UI::push();
}

static void drawRecDone() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("CAPTURED");

  cv.setFont(&fonts::Font2);
  cv.setTextColor(COL_FG, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString(s_info1, cv.width() / 2, 32);
  cv.setTextColor(COL_DIM, COL_BG);
  cv.drawString(s_info2, cv.width() / 2, 50);

  // three horizontal buttons
  const int bw = 70, bh = 22, y = 70;
  for (int i = 0; i < 3; i++) {
    const int x = 8 + i * (bw + 6);
    if (i == s_recSel) {
      cv.fillRoundRect(x, y, bw, bh, 4, COL_SEL_BG);
      cv.setTextColor(COL_SEL_FG, COL_SEL_BG);
    } else {
      cv.drawRoundRect(x, y, bw, bh, 4, COL_DIM);
      cv.setTextColor(COL_FG, COL_BG);
    }
    cv.setTextDatum(middle_center);
    cv.drawString(kRecItems[i], x + bw / 2, y + bh / 2);
  }

  UI::drawFooter("Low: next   Blue: select");
  UI::push();
}

static void drawNameEdit() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("SAVE AS");

  cv.setFont(&fonts::Font4);
  cv.setTextDatum(middle_left);
  int x = 12;
  const int y = 56;
  for (int i = 0; i < s_nameLen; i++) {
    char c[2] = {s_name[i], 0};
    cv.setTextColor(COL_FG, COL_BG);
    cv.drawString(c, x, y);
    x += cv.textWidth(c) + 2;
  }
  if (s_pendActive) {
    char c[2] = {kCharset[s_pendIdx], 0};
    const int w = cv.textWidth(c);
    cv.fillRect(x - 1, y - 14, w + 4, 28, COL_SEL_BG);
    cv.setTextColor(COL_SEL_FG, COL_SEL_BG);
    cv.drawString(c, x + 1, y);
    x += w + 4;
  }
  // cursor underline
  cv.fillRect(x + 1, y + 14, 12, 2, COL_DIM);

  cv.setFont(&fonts::Font0);
  cv.setTextColor(COL_DIM, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString("Low: change char   Blue: add char", cv.width() / 2, 95);
  cv.drawString("hold Low: delete   hold Blue: SAVE", cv.width() / 2, 108);
  UI::push();
}

static void drawClockSet() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("SET CLOCK");

  char tok[5][8];
  snprintf(tok[0], 8, "%04d", s_clk[0]);
  snprintf(tok[1], 8, "%02d", s_clk[1]);
  snprintf(tok[2], 8, "%02d", s_clk[2]);
  snprintf(tok[3], 8, "%02d", s_clk[3]);
  snprintf(tok[4], 8, "%02d", s_clk[4]);

  cv.setFont(&fonts::Font4);
  cv.setTextDatum(middle_left);

  // line 1: YYYY-MM-DD   line 2: HH:MM
  struct {
    const char* text;
    int field;  // -1 = separator
  } parts1[] = {{tok[0], 0}, {"-", -1}, {tok[1], 1}, {"-", -1}, {tok[2], 2}};
  struct {
    const char* text;
    int field;
  } parts2[] = {{tok[3], 3}, {":", -1}, {tok[4], 4}};

  int x = 24;
  const int y1 = 48, y2 = 84;
  for (auto& p : parts1) {
    const int w = cv.textWidth(p.text);
    if (p.field == s_clkField) {
      cv.fillRect(x - 2, y1 - 14, w + 4, 28, COL_SEL_BG);
      cv.setTextColor(COL_SEL_FG, COL_SEL_BG);
    } else {
      cv.setTextColor(COL_FG, COL_BG);
    }
    cv.drawString(p.text, x, y1);
    x += w + 6;
  }
  x = 70;
  for (auto& p : parts2) {
    const int w = cv.textWidth(p.text);
    if (p.field == s_clkField) {
      cv.fillRect(x - 2, y2 - 14, w + 4, 28, COL_SEL_BG);
      cv.setTextColor(COL_SEL_FG, COL_SEL_BG);
    } else {
      cv.setTextColor(COL_FG, COL_BG);
    }
    cv.drawString(p.text, x, y2);
    x += w + 6;
  }

  UI::drawFooter("Low:+1  holdLow:-1  Blue:next  holdBlue:cancel");
  UI::push();
}

static void drawAlarmSet() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("ALARM");

  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), "%02d", s_almEdit[1]);
  snprintf(mm, sizeof(mm), "%02d", s_almEdit[2]);
  struct {
    const char* text;
    int field;
  } parts[] = {{s_almEdit[0] ? "ON " : "OFF", 0}, {hh, 1}, {":", -1}, {mm, 2}};

  cv.setFont(&fonts::Font4);
  cv.setTextDatum(middle_left);
  int x = 36;
  const int y = 64;
  for (auto& p : parts) {
    const int w = cv.textWidth(p.text);
    if (p.field == s_almField) {
      cv.fillRect(x - 2, y - 14, w + 4, 28, COL_SEL_BG);
      cv.setTextColor(COL_SEL_FG, COL_SEL_BG);
    } else {
      cv.setTextColor(COL_FG, COL_BG);
    }
    cv.drawString(p.text, x, y);
    x += w + (p.field == 0 ? 24 : 5);
  }

  UI::drawFooter("Low: change  Blue: next  holdBlue: cancel");
  UI::push();
}

static void drawTimerSet() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("TIMER");

  char mm[4], ss[4];
  snprintf(mm, sizeof(mm), "%02d", s_tmrEdit[0]);
  snprintf(ss, sizeof(ss), "%02d", s_tmrEdit[1]);
  struct {
    const char* text;
    int field;
  } parts[] = {{mm, 0}, {":", -1}, {ss, 1}};

  cv.setFont(&fonts::Font4);
  cv.setTextDatum(middle_left);
  int x = 86;
  const int y = 60;
  for (auto& p : parts) {
    const int w = cv.textWidth(p.text);
    if (p.field == s_tmrField) {
      cv.fillRect(x - 2, y - 14, w + 4, 28, COL_SEL_BG);
      cv.setTextColor(COL_SEL_FG, COL_SEL_BG);
    } else {
      cv.setTextColor(COL_FG, COL_BG);
    }
    cv.drawString(p.text, x, y);
    x += w + 5;
  }

  cv.setFont(&fonts::Font2);
  cv.setTextColor(COL_DIM, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString("minutes : seconds", cv.width() / 2, 90);

  UI::drawFooter("Low:+1  holdLow:-1  Blue: next/START");
  UI::push();
}

static void drawTimerRun() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("TIMER");

  const uint32_t s = (s_timerRemainMs + 999) / 1000;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(s / 60),
           (unsigned)(s % 60));
  cv.setFont(&fonts::Font7);
  cv.setTextColor(COL_FG, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString(buf, cv.width() / 2, 62);

  if (s_timerPaused) {
    cv.setFont(&fonts::Font2);
    cv.setTextColor(COL_DIM, COL_BG);
    cv.drawString("PAUSED", cv.width() / 2, 96);
  }

  UI::drawFooter("Blue: pause  Low: menu  holdBlue: cancel");
  UI::push();
}

static void drawStopwatch() {
  M5Canvas& cv = UI::cv();
  UI::clear();
  UI::drawHeader("STOPWATCH");

  const uint32_t el = swElapsed();
  char buf[8], tenth[4];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(el / 60000),
           (unsigned)((el / 1000) % 60));
  snprintf(tenth, sizeof(tenth), ".%u", (unsigned)((el / 100) % 10));
  cv.setFont(&fonts::Font7);
  cv.setTextColor(COL_FG, COL_BG);
  cv.setTextDatum(middle_center);
  cv.drawString(buf, cv.width() / 2 - 14, 62);
  cv.setFont(&fonts::Font4);
  cv.setTextDatum(middle_left);
  cv.drawString(tenth, cv.width() / 2 + 68, 74);

  UI::drawFooter(s_swRun ? "Blue: stop   holdBlue: back"
                         : "Blue: start  Low: reset  holdBlue: back");
  UI::push();
}

static void drawAlert() {
  M5Canvas& cv = UI::cv();
  const uint16_t bg = s_alertPhase ? COL_FG : COL_BG;
  const uint16_t fg = s_alertPhase ? COL_BG : COL_FG;
  cv.fillSprite(bg);

  cv.setFont(&fonts::Font4);
  cv.setTextColor(fg, bg);
  cv.setTextDatum(middle_center);
  cv.drawString(s_alertText, cv.width() / 2, 50);

  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  char t[8];
  strftime(t, sizeof(t), "%H:%M", &lt);
  cv.setFont(&fonts::Font2);
  cv.drawString(t, cv.width() / 2, 82);

  cv.setFont(&fonts::Font0);
  cv.drawString("press any button", cv.width() / 2, 116);
  UI::push();
}

// ---------------------------------------------------------------- helpers

static void suggestName() {
  for (int i = 1; i < 100; i++) {
    snprintf(s_name, sizeof(s_name), "IR_%02d", i);
    if (!IrStore::exists(s_name)) break;
  }
  s_nameLen = strlen(s_name);
  s_pendActive = false;
  s_pendIdx = 0;
}

static void reloadList() {
  s_nameCount = IrStore::list(s_names, IrStore::kMaxSignals);
  if (s_listSel >= s_nameCount) s_listSel = s_nameCount ? s_nameCount - 1 : 0;
}

static void startCaptureOrFail() {
  M5.Power.setExtOutput(true, m5::ext_none);  // power the IR rail
  delay(20);                                  // let the rail settle
  if (IrEngine::startCapture()) {
    enter(ST_REC_WAIT);
  } else {
    M5.Power.setExtOutput(false);
    UI::toast("ERROR", "IR receiver busy", 1200);
    enter(ST_MENU);
  }
}

static void prepareCaptureInfo() {
  s_info1 = "Captured " + String(IrEngine::capturedCount() * 2) + " pulses";
  uint16_t addr, cmd;
  if (IrEngine::decodeNEC(IrEngine::captured(), IrEngine::capturedCount(),
                          addr, cmd)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "NEC  A:%04X  C:%02X", addr, cmd & 0xFF);
    s_info2 = buf;
  } else {
    s_info2 = "Protocol: RAW";
  }
}

// Send repeatedly while the Blue button is held (minimum 3 repeats -
// many TVs ignore a single frame; Sony needs at least 3).
static void sendWhileHeld() {
  size_t n = IrStore::load(s_names[s_listSel].c_str(), s_sendBuf,
                           IrEngine::kMaxSymbols);
  if (!n) {
    UI::toast("SEND", "Load failed!", 1000);
    return;
  }

  M5.Power.setExtOutput(true, m5::ext_none);  // power the IR rail
  ledSet(true);
  delay(20);

  int sent = 0;
  uint32_t lastDraw = 0;
  do {
    if (millis() - lastDraw > 150) {
      char line[24];
      snprintf(line, sizeof(line), "Sending...  x%d", sent);
      UI::drawMsg("SEND", line, nullptr, nullptr, "release Blue to stop");
      lastDraw = millis();
    }
    if (!IrEngine::send(s_sendBuf, n)) break;
    sent++;
    delay(40);  // inter-frame gap (~NEC repeat spacing)
    M5.update();
  } while (M5.BtnA.isPressed() || sent < 3);

  M5.Power.setExtOutput(false);

  char line[16];
  snprintf(line, sizeof(line), "Sent x%d", sent);
  UI::toast("SEND", line, 700);
  s_lastActivity = millis();
}

static void initClockEditor() {
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  s_clk[0] = lt.tm_year + 1900;
  s_clk[1] = lt.tm_mon + 1;
  s_clk[2] = lt.tm_mday;
  s_clk[3] = lt.tm_hour;
  s_clk[4] = lt.tm_min;
  s_clkField = 0;
}

static void bumpClockField(int dir) {
  static const int lo[] = {2020, 1, 1, 0, 0};
  static const int hi[] = {2099, 12, 31, 23, 59};
  int& v = s_clk[s_clkField];
  v += dir;
  if (v > hi[s_clkField]) v = lo[s_clkField];
  if (v < lo[s_clkField]) v = hi[s_clkField];
}

static void bumpAlarmField(int dir) {
  static const int lo[] = {0, 0, 0};
  static const int hi[] = {1, 23, 59};
  int& v = s_almEdit[s_almField];
  v += dir;
  if (v > hi[s_almField]) v = lo[s_almField];
  if (v < lo[s_almField]) v = hi[s_almField];
}

static void bumpTimerField(int dir) {
  static const int hi[] = {99, 59};
  int& v = s_tmrEdit[s_tmrField];
  v += dir;
  if (v > hi[s_tmrField]) v = 0;
  if (v < 0) v = hi[s_tmrField];
}

static void applyClock() {
  int dim = daysInMonth(s_clk[0], s_clk[1]);
  if (s_clk[2] > dim) s_clk[2] = dim;
  struct tm tmv = {};
  tmv.tm_year = s_clk[0] - 1900;
  tmv.tm_mon = s_clk[1] - 1;
  tmv.tm_mday = s_clk[2];
  tmv.tm_hour = s_clk[3];
  tmv.tm_min = s_clk[4];
  tmv.tm_sec = 0;
  struct timeval tv = {.tv_sec = mktime(&tmv), .tv_usec = 0};
  settimeofday(&tv, nullptr);
  persistClock();
}

// ---------------------------------------------------------------- setup

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = false;  // unused - saves power
  cfg.internal_mic = false;  // unused - saves power
  M5.begin(cfg);
  M5.Display.setRotation(3);
  M5.Display.setBrightness(DEFAULT_BRIGHTNESS);

  // Required for StickS3 IR receive (per official docs):
  M5.Speaker.end();  // speaker amp interferes with the IR receiver

  // Green LED + 5V ext rail off by default; they are switched on only
  // while IR capture/send is running (battery saving).
  M5.Power.setExtOutput(false);
  ledSet(false);

  s_prefs.begin("ircopy", false);
  seedClock();
  s_alarmOn = s_prefs.getBool("alOn", false);
  s_alarmH = s_prefs.getInt("alH", 7);
  s_alarmM = s_prefs.getInt("alM", 0);
  UI::begin();

  bool irOk = IrEngine::begin();
  bool fsOk = IrStore::begin();
  if (!irOk || !fsOk) {
    UI::drawMsg("ERROR", irOk ? "Storage init failed" : "IR init failed",
                "Check serial log");
    delay(3000);
  }

  s_lastActivity = millis();
  enter(ST_WATCH);
}

// ---------------------------------------------------------------- loop

void loop() {
  M5.update();

  // persist the clock every minute (survives power-button resets)
  static uint32_t s_lastPersist = 0;
  if (millis() - s_lastPersist > 60000) {
    persistClock();
    s_lastPersist = millis();
  }

  bool aC = M5.BtnA.wasClicked();
  bool aH = M5.BtnA.wasHold();
  bool bC = M5.BtnB.wasClicked();
  bool bH = M5.BtnB.wasHold();
  const bool anyPress = M5.BtnA.wasPressed() || M5.BtnB.wasPressed();
  if (aC || aH || bC || bH || anyPress) s_lastActivity = millis();

  // countdown timer & alarm run globally - they can fire from any
  // state, including sleep (the loop keeps running with the screen off)
  serviceTimer();
  serviceAlarm();

  // -- asleep: wake on any press, swallow that press --
  if (s_state == ST_SLEEP) {
    if (anyPress) {
      wakeUp();
    } else if (M5.Power.isCharging() == m5::Power_Class::is_charging) {
      delay(50);  // on USB power: stay fully responsive
    } else {
      // Light sleep between housekeeping ticks: ~2mA instead of ~30mA,
      // so a night on the nightstand barely dents the battery. Buttons
      // (active low) wake instantly; the timer wake lets the alarm /
      // countdown / clock-persist logic keep running.
      uint32_t chunkMs = 30000;
      if (s_timerActive && !s_timerPaused) chunkMs = 500;
      else if (s_alarmOn) chunkMs = 2000;
      gpio_wakeup_enable(GPIO_NUM_11, GPIO_INTR_LOW_LEVEL);
      gpio_wakeup_enable(GPIO_NUM_12, GPIO_INTR_LOW_LEVEL);
      esp_sleep_enable_gpio_wakeup();
      esp_sleep_enable_timer_wakeup((uint64_t)chunkMs * 1000);
      esp_light_sleep_start();
    }
    return;
  }
  if (s_swallow) {
    if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) s_swallow = false;
    aC = aH = bC = bH = false;
  }

  // green LED: only while a button is held or an operation is running
  ledSet(M5.BtnA.isPressed() || M5.BtnB.isPressed() ||
         s_state == ST_REC_WAIT || s_state == ST_ALERT);

  const uint32_t idle = millis() - s_lastActivity;

  switch (s_state) {
    case ST_WATCH:
      if (aC || bC) {
        s_menuSel = 0;
        enter(ST_MENU);
        break;
      }
      if (idle > SCREEN_TIMEOUT_MS) {
        goSleep();
        break;
      }
      drawWatch(s_dirty);
      s_dirty = false;
      break;

    case ST_MENU:
      if (bC) { s_menuSel = (s_menuSel + 1) % kMainCount; s_dirty = true; }
      if (aC) {
        switch (s_menuSel) {
          case 0: startCaptureOrFail(); break;
          case 1: reloadList(); s_listSel = 0; enter(ST_LIST); break;
          case 2:  // Alarm
            s_almEdit[0] = s_alarmOn;
            s_almEdit[1] = s_alarmH;
            s_almEdit[2] = s_alarmM;
            s_almField = 0;
            enter(ST_ALARM_SET);
            break;
          case 3:  // Timer
            if (s_timerActive) {
              enter(ST_TIMER_RUN);
            } else {
              s_tmrField = 0;
              enter(ST_TIMER_SET);
            }
            break;
          case 4: enter(ST_STOPWATCH); break;
          case 5: DinoGame::reset(); enter(ST_DINO); break;
          case 6: initClockEditor(); enter(ST_CLOCK_SET); break;
          case 7: enter(ST_WATCH); break;
        }
        break;
      }
      if (aH || idle > MENU_TIMEOUT_MS) { enter(ST_WATCH); break; }
      if (s_dirty) {
        UI::drawMenu("IR COPY", kMainItems, kMainCount, s_menuSel,
                     "Low: next   Blue: select   hold Blue: back");
        s_dirty = false;
      }
      break;

    case ST_REC_WAIT:
      if (IrEngine::pollCaptured()) {
        M5.Power.setExtOutput(false);  // IR rail off until next operation
        prepareCaptureInfo();
        s_recSel = 0;
        enter(ST_REC_DONE);
        break;
      }
      if (bC || aH || idle > MENU_TIMEOUT_MS) {
        IrEngine::cancelCapture();
        M5.Power.setExtOutput(false);
        enter(ST_MENU);
        break;
      }
      if (s_dirty) {
        UI::drawMsg("IR COPY", "Point the remote at", "the top of the stick,",
                    "press its button briefly", "Low: cancel");
        s_dirty = false;
      }
      break;

    case ST_REC_DONE:
      if (bC) { s_recSel = (s_recSel + 1) % 3; s_dirty = true; }
      if (aC) {
        if (s_recSel == 0) {        // Save
          suggestName();
          enter(ST_NAME_EDIT);
        } else if (s_recSel == 1) {  // Retry
          startCaptureOrFail();
        } else {                     // Discard
          enter(ST_MENU);
        }
        break;
      }
      if (aH || idle > MENU_TIMEOUT_MS) { enter(ST_MENU); break; }
      if (s_dirty) { drawRecDone(); s_dirty = false; }
      break;

    case ST_NAME_EDIT:
      if (bC) {  // cycle pending char
        if (!s_pendActive) {
          s_pendActive = true;
          s_pendIdx = 0;
        } else {
          s_pendIdx = (s_pendIdx + 1) % (int)(sizeof(kCharset) - 1);
        }
        s_dirty = true;
      }
      if (aC && s_pendActive && s_nameLen < MAX_NAME_LEN) {  // commit char
        s_name[s_nameLen++] = kCharset[s_pendIdx];
        s_name[s_nameLen] = 0;
        s_pendActive = false;
        s_dirty = true;
      }
      if (bH) {  // backspace
        if (s_pendActive) {
          s_pendActive = false;
        } else if (s_nameLen > 0) {
          s_name[--s_nameLen] = 0;
        }
        s_dirty = true;
      }
      if (aH) {  // save
        if (s_pendActive && s_nameLen < MAX_NAME_LEN) {
          s_name[s_nameLen++] = kCharset[s_pendIdx];
          s_name[s_nameLen] = 0;
        }
        if (s_nameLen > 0) {
          bool ok = IrStore::save(s_name, IrEngine::captured(),
                                  IrEngine::capturedCount());
          UI::toast("SAVE", ok ? (String("Saved ") + s_name).c_str()
                               : "Save failed!",
                    900);
          enter(ST_MENU);
        }
        break;
      }
      if (idle > MENU_TIMEOUT_MS) { enter(ST_MENU); break; }
      if (s_dirty) { drawNameEdit(); s_dirty = false; }
      break;

    case ST_LIST:
      if (s_nameCount == 0) {
        if (s_dirty) {
          UI::drawMsg("SAVED SIGNALS", "No saved signals yet", nullptr,
                      nullptr, "any button: back");
          s_dirty = false;
        }
        if (aC || bC || aH) enter(ST_MENU);
        if (idle > MENU_TIMEOUT_MS) enter(ST_WATCH);
        break;
      }
      if (bC) { s_listSel = (s_listSel + 1) % s_nameCount; s_dirty = true; }
      if (aC) { s_actSel = 0; enter(ST_ACTIONS); break; }
      if (aH) { enter(ST_MENU); break; }
      if (idle > MENU_TIMEOUT_MS) { enter(ST_WATCH); break; }
      if (s_dirty) {
        UI::drawMenu("SAVED SIGNALS", s_names, s_nameCount, s_listSel,
                     "Low: next   Blue: open   hold Blue: back");
        s_dirty = false;
      }
      break;

    case ST_ACTIONS:
      if (bC) { s_actSel = (s_actSel + 1) % 3; s_dirty = true; }
      // Send fires on press (not click) so holding Blue keeps sending.
      if (s_actSel == 0 && M5.BtnA.wasPressed() && !s_swallow) {
        sendWhileHeld();
        s_dirty = true;
        break;
      }
      if (aC) {
        if (s_actSel == 1) {  // Delete
          s_delSel = 0;
          enter(ST_DEL_CONFIRM);
          break;
        }
        if (s_actSel == 2) {  // Back
          enter(ST_LIST);
          break;
        }
      }
      if (aH) { enter(ST_LIST); break; }
      if (idle > MENU_TIMEOUT_MS) { enter(ST_WATCH); break; }
      if (s_dirty) {
        UI::drawMenu(s_names[s_listSel].c_str(), kActItems, 3, s_actSel,
                     "Low: next   Blue: hold to send");
        s_dirty = false;
      }
      break;

    case ST_DEL_CONFIRM:
      if (bC) { s_delSel = (s_delSel + 1) % 2; s_dirty = true; }
      if (aC) {
        if (s_delSel == 1) {
          IrStore::remove(s_names[s_listSel].c_str());
          UI::toast("DELETE", "Deleted", 700);
          reloadList();
          enter(ST_LIST);
        } else {
          enter(ST_ACTIONS);
        }
        break;
      }
      if (aH) { enter(ST_ACTIONS); break; }
      if (idle > MENU_TIMEOUT_MS) { enter(ST_WATCH); break; }
      if (s_dirty) {
        String title = "Delete " + s_names[s_listSel] + "?";
        UI::drawMenu(title.c_str(), kYesNo, 2, s_delSel,
                     "Low: next   Blue: select");
        s_dirty = false;
      }
      break;

    case ST_CLOCK_SET:
      if (bC) { bumpClockField(+1); s_dirty = true; }
      if (bH) { bumpClockField(-1); s_dirty = true; }
      if (aC) {
        if (++s_clkField > 4) {
          applyClock();
          UI::toast("CLOCK", "Time set", 700);
          enter(ST_MENU);
          break;
        }
        s_dirty = true;
      }
      if (aH || idle > MENU_TIMEOUT_MS) { enter(ST_MENU); break; }
      if (s_dirty) { drawClockSet(); s_dirty = false; }
      break;

    case ST_ALARM_SET:
      if (bC) { bumpAlarmField(+1); s_dirty = true; }
      if (bH) { bumpAlarmField(-1); s_dirty = true; }
      if (aC) {
        if (++s_almField > 2) {
          s_alarmOn = s_almEdit[0] != 0;
          s_alarmH = s_almEdit[1];
          s_alarmM = s_almEdit[2];
          s_prefs.putBool("alOn", s_alarmOn);
          s_prefs.putInt("alH", s_alarmH);
          s_prefs.putInt("alM", s_alarmM);
          UI::toast("ALARM", s_alarmOn ? "Alarm on" : "Alarm off", 700);
          enter(ST_MENU);
          break;
        }
        s_dirty = true;
      }
      if (aH || idle > MENU_TIMEOUT_MS) { enter(ST_MENU); break; }
      if (s_dirty) { drawAlarmSet(); s_dirty = false; }
      break;

    case ST_TIMER_SET:
      if (bC) { bumpTimerField(+1); s_dirty = true; }
      if (bH) { bumpTimerField(-1); s_dirty = true; }
      if (aC) {
        if (++s_tmrField > 1) {
          const uint32_t total = s_tmrEdit[0] * 60 + s_tmrEdit[1];
          s_tmrField = 0;
          if (total > 0) {
            s_timerRemainMs = total * 1000;
            s_timerActive = true;
            s_timerPaused = false;
            s_timerLastTick = millis();
            enter(ST_TIMER_RUN);
            break;
          }
        }
        s_dirty = true;
      }
      if (aH || idle > MENU_TIMEOUT_MS) { enter(ST_MENU); break; }
      if (s_dirty) { drawTimerSet(); s_dirty = false; }
      break;

    case ST_TIMER_RUN: {
      if (!s_timerActive) { enter(ST_TIMER_SET); break; }  // just fired
      if (aC) { s_timerPaused = !s_timerPaused; s_dirty = true; }
      if (bC) { enter(ST_MENU); break; }  // keeps running in background
      if (aH) {
        s_timerActive = false;
        UI::toast("TIMER", "Cancelled", 600);
        enter(ST_MENU);
        break;
      }
      if (idle > MENU_TIMEOUT_MS) { enter(ST_WATCH); break; }
      const uint32_t s = (s_timerRemainMs + 999) / 1000;
      if (s_dirty || s != s_tmrShownSec) {
        s_tmrShownSec = s;
        drawTimerRun();
        s_dirty = false;
      }
      break;
    }

    case ST_STOPWATCH:
      if (aC) {
        if (s_swRun) {
          s_swAccumMs += millis() - s_swStartMs;
          s_swRun = false;
        } else {
          s_swStartMs = millis();
          s_swRun = true;
        }
        s_dirty = true;
      }
      if (bC && !s_swRun) { s_swAccumMs = 0; s_dirty = true; }
      if (aH) { enter(ST_MENU); break; }  // keeps running in background
      if (idle > MENU_TIMEOUT_MS) { enter(ST_WATCH); break; }
      if (s_dirty || (s_swRun && millis() - s_swLastDraw >= 100)) {
        s_swLastDraw = millis();
        drawStopwatch();
        s_dirty = false;
      }
      break;

    case ST_DINO:
      s_lastActivity = millis();  // no screen timeout mid-game
      if (DinoGame::frame() == DinoGame::kExit) enter(ST_MENU);
      break;

    case ST_ALERT: {
      const bool phase = ((millis() - s_alertStart) / 350) & 1;
      if (s_dirty || phase != s_alertPhase) {
        s_alertPhase = phase;
        drawAlert();
        s_dirty = false;
      }
      if (phase && !M5.Speaker.isPlaying()) M5.Speaker.tone(2200, 180);
      // any button dismisses; auto-dismiss after 60s to save battery
      if (anyPress || millis() - s_alertStart > 60000) {
        M5.Speaker.end();  // keep the amp off for IR receive
        s_lastActivity = millis();
        enter(ST_WATCH);
      }
      break;
    }

    case ST_SLEEP:
      break;  // handled above
  }

  delay(10);
}
