#include "ir_engine.h"

#include "config.h"

namespace IrEngine {

static rmt_channel_handle_t s_rx = nullptr;
static rmt_channel_handle_t s_tx = nullptr;
static rmt_encoder_handle_t s_copyEnc = nullptr;
static QueueHandle_t s_rxQueue = nullptr;

static rmt_symbol_word_t s_rxBuf[kMaxSymbols];
static rmt_symbol_word_t s_lastFrame[kMaxSymbols];
static size_t s_lastCount = 0;
static bool s_capturing = false;

static bool IRAM_ATTR rxDoneCb(rmt_channel_handle_t,
                               const rmt_rx_done_event_data_t* edata,
                               void* ctx) {
  BaseType_t hpw = pdFALSE;
  xQueueSendFromISR(static_cast<QueueHandle_t>(ctx), edata, &hpw);
  return hpw == pdTRUE;
}

bool begin() {
  // --- TX channel: IR LED on G46, 38kHz carrier ---
  rmt_tx_channel_config_t txc = {};
  txc.gpio_num = (gpio_num_t)IR_TX_PIN;
  txc.clk_src = RMT_CLK_SRC_DEFAULT;
  txc.resolution_hz = kResolutionHz;
  txc.mem_block_symbols = 64;
  txc.trans_queue_depth = 4;
  if (rmt_new_tx_channel(&txc, &s_tx) != ESP_OK) return false;

  rmt_carrier_config_t cc = {};
  cc.frequency_hz = 38000;
  cc.duty_cycle = 0.33f;
  if (rmt_apply_carrier(s_tx, &cc) != ESP_OK) return false;

  rmt_copy_encoder_config_t ec = {};
  if (rmt_new_copy_encoder(&ec, &s_copyEnc) != ESP_OK) return false;
  if (rmt_enable(s_tx) != ESP_OK) return false;

  // --- RX channel: IR receiver on G42 ---
  rmt_rx_channel_config_t rxc = {};
  rxc.gpio_num = (gpio_num_t)IR_RX_PIN;
  rxc.clk_src = RMT_CLK_SRC_DEFAULT;
  rxc.resolution_hz = kResolutionHz;
  rxc.mem_block_symbols = kMaxSymbols;
  if (rmt_new_rx_channel(&rxc, &s_rx) != ESP_OK) return false;

  s_rxQueue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
  if (!s_rxQueue) return false;

  rmt_rx_event_callbacks_t cbs = {};
  cbs.on_recv_done = rxDoneCb;
  if (rmt_rx_register_event_callbacks(s_rx, &cbs, s_rxQueue) != ESP_OK) return false;
  if (rmt_enable(s_rx) != ESP_OK) return false;

  return true;
}

bool startCapture() {
  if (s_capturing) return true;
  xQueueReset(s_rxQueue);

  rmt_receive_config_t rc = {};
  rc.signal_range_min_ns = 2000;       // ignore <2us glitches (1 tick)
  // 60ms idle = end of burst. Gaps SHORTER than this stay inside the
  // capture, so multi-frame transmissions (Sharp true+inverted pairs,
  // NEC repeats, ...) are recorded with their real timing.
  rc.signal_range_max_ns = 60000000;
  if (rmt_receive(s_rx, s_rxBuf, sizeof(s_rxBuf), &rc) != ESP_OK) return false;

  s_capturing = true;
  return true;
}

void cancelCapture() {
  if (!s_capturing) return;
  rmt_disable(s_rx);  // aborts the pending receive
  rmt_enable(s_rx);
  xQueueReset(s_rxQueue);
  s_capturing = false;
}

bool pollCaptured() {
  if (!s_capturing) return false;

  rmt_rx_done_event_data_t ev;
  if (xQueueReceive(s_rxQueue, &ev, 0) != pdTRUE) return false;
  s_capturing = false;

  size_t n = ev.num_symbols;
  if (n > kMaxSymbols) n = kMaxSymbols;

  // Too short to be a real remote command -> noise, re-arm silently.
  if (n < 4) {
    startCapture();
    return false;
  }

  memcpy(s_lastFrame, ev.received_symbols, n * sizeof(rmt_symbol_word_t));
  s_lastCount = n;
  return true;
}

const rmt_symbol_word_t* captured() { return s_lastFrame; }
size_t capturedCount() { return s_lastCount; }

bool send(const rmt_symbol_word_t* syms, size_t count) {
  if (!count || count > kMaxSymbols) return false;

  // The receiver outputs an inverted (active-low) signal. Normalize:
  // duration0 = mark (carrier ON), duration1 = space (carrier OFF).
  static rmt_symbol_word_t buf[kMaxSymbols];
  for (size_t i = 0; i < count; i++) {
    buf[i].duration0 = syms[i].duration0;
    buf[i].level0 = 1;
    buf[i].duration1 = syms[i].duration1;
    buf[i].level1 = 0;
  }

  rmt_transmit_config_t tc = {};
  tc.loop_count = 0;
  if (rmt_transmit(s_tx, s_copyEnc, buf,
                   count * sizeof(rmt_symbol_word_t), &tc) != ESP_OK) {
    return false;
  }
  return rmt_tx_wait_all_done(s_tx, 1000) == ESP_OK;
}

// 25% tolerance match; v is in RMT ticks, target in microseconds
static bool near(uint32_t v, uint32_t target) {
  const uint32_t us = v * kTickUs;
  return us > target - target / 4 && us < target + target / 4;
}

bool decodeNEC(const rmt_symbol_word_t* syms, size_t count,
               uint16_t& addr, uint16_t& cmd) {
  // NEC: 9ms mark + 4.5ms space header, then 32 bits (560us mark,
  // 560us space = 0 / 1690us space = 1), LSB first, trailing mark.
  if (count < 34) return false;
  if (!near(syms[0].duration0, 9000) || !near(syms[0].duration1, 4500)) return false;

  uint32_t data = 0;
  for (int i = 0; i < 32; i++) {
    const rmt_symbol_word_t& s = syms[1 + i];
    if (!near(s.duration0, 560)) return false;
    if (near(s.duration1, 1690)) {
      data |= 1UL << i;
    } else if (!near(s.duration1, 560)) {
      return false;
    }
  }
  addr = data & 0xFFFF;
  cmd = (data >> 16) & 0xFFFF;
  return true;
}

}  // namespace IrEngine
