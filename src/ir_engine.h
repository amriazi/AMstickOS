#pragma once

#include <Arduino.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>

// Raw IR capture & replay using the ESP32-S3 RMT peripheral.
// The StickS3 IR receiver only works through RMT (not GPIO polling),
// per the official M5Stack documentation.
namespace IrEngine {

// 128 symbols = 256 marks/spaces. Plenty for TV/audio remotes
// (NEC is 34 symbols). Very long A/C frames may get truncated.
constexpr size_t kMaxSymbols = 128;

// 2 us per RMT tick. Coarser than the 1 us default on purpose: it lets a
// single RMT symbol hold inter-frame gaps up to 65 ms, so we capture the
// WHOLE burst a remote sends (all frames + the real gaps between them)
// and replay it verbatim. Critical for e.g. Sharp TVs, which send each
// command as a true frame + an inverted frame ~40 ms apart and reject
// anything else.
constexpr uint32_t kResolutionHz = 500000;
constexpr uint32_t kTickUs = 2;

bool begin();

// --- Capture ---
bool startCapture();   // arm the receiver
void cancelCapture();  // abort a pending capture
bool pollCaptured();   // true once when a complete frame arrived
const rmt_symbol_word_t* captured();
size_t capturedCount();

// --- Replay ---
bool send(const rmt_symbol_word_t* syms, size_t count);

// Try to decode a frame as NEC (for display only - replay is always raw).
bool decodeNEC(const rmt_symbol_word_t* syms, size_t count,
               uint16_t& addr, uint16_t& cmd);

}  // namespace IrEngine
