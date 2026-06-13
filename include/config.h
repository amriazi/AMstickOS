#pragma once

#include <stdint.h>

// ---- M5StickS3 (K150) hardware ----
#define IR_TX_PIN 46  // built-in IR LED
#define IR_RX_PIN 42  // built-in IR receiver

// ---- Behaviour ----
#define SCREEN_TIMEOUT_MS 10000UL  // watch face -> screen off
#define MENU_TIMEOUT_MS   60000UL  // menus -> back to watch face
#define DEFAULT_BRIGHTNESS 70      // 0..255
#define MAX_NAME_LEN 10

// ---- Feature switches ----
// Flip to 1 to unlock the Games menu. Kept locked for now; the game
// code stays compiled in so it can be enabled later without changes.
#define GAMES_UNLOCKED 0

// ---- Battery display ----
// Keep a hidden buffer so the device keeps working below the shown 0%.
// Displayed% = (raw - BUFFER) / (100 - BUFFER) * 100, clamped to 0..100.
#define BATT_BUFFER_PCT 30
// At/below this DISPLAYED percentage the battery readout turns red and
// blinks as a warning.
#define BATT_WARN_PCT 15

// ---- Raise-to-wake (wrist gesture) ----
// While the screen is off, a wrist raise can wake it to the watch face.
// Detection watches the accelerometer axis normal to the screen.
// If the gesture fires on lowering instead of raising, flip WRIST_SIGN;
// if it never fires, try a different WRIST_AXIS (0=x, 1=y, 2=z).
#define WRIST_AXIS 2
#define WRIST_SIGN (+1)
#define WRIST_UP_G 0.60f    // axis*sign above this = screen facing user
#define WRIST_DOWN_G 0.30f  // axis*sign below this = re-arm
#define WRIST_POLL_MS 250   // light-sleep tick while raise-to-wake is on

// ---- Theme colors (RGB565) ----
// Runtime-selectable; the active palette lives in these globals (see
// theme.cpp). Default initialised to the orange "Nemo" theme.
extern uint16_t COL_BG;
extern uint16_t COL_FG;
extern uint16_t COL_SEL_BG;
extern uint16_t COL_SEL_FG;
extern uint16_t COL_DIM;
