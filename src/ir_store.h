#pragma once

#include <Arduino.h>
#include <driver/rmt_types.h>

// Persistent storage of captured IR frames on LittleFS.
// One file per signal: /ir/<name>.ir  (magic + count + raw RMT symbols)
namespace IrStore {

constexpr int kMaxSignals = 32;

bool begin();
bool save(const char* name, const rmt_symbol_word_t* syms, size_t count);
// Returns number of symbols loaded, 0 on failure.
size_t load(const char* name, rmt_symbol_word_t* buf, size_t maxCount);
bool remove(const char* name);
bool exists(const char* name);
// Fills `names` (up to maxNames), returns how many were found.
int list(String* names, int maxNames);

}  // namespace IrStore
