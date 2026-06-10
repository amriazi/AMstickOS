#pragma once

// ---- M5StickS3 (K150) hardware ----
#define IR_TX_PIN 46  // built-in IR LED
#define IR_RX_PIN 42  // built-in IR receiver

// ---- Behaviour ----
#define SCREEN_TIMEOUT_MS 10000UL  // watch face -> screen off
#define MENU_TIMEOUT_MS   60000UL  // menus -> back to watch face
#define DEFAULT_BRIGHTNESS 70      // 0..255
#define MAX_NAME_LEN 10

// ---- Nemo-style theme: orange on black ----
#define COL_BG     TFT_BLACK
#define COL_FG     0xFD20  // orange
#define COL_SEL_BG 0xFD20
#define COL_SEL_FG TFT_BLACK
#define COL_DIM    0x8B61  // dim orange/brown for hints
