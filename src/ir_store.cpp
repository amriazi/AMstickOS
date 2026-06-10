#include "ir_store.h"

#include <LittleFS.h>

namespace IrStore {

static constexpr uint32_t kMagic = 0x32435249;    // "IRC2" - 2us ticks
static constexpr uint32_t kMagicV1 = 0x31435249;  // "IRC1" - legacy 1us ticks
static constexpr const char* kDir = "/ir";

static String pathFor(const char* name) {
  return String(kDir) + "/" + name + ".ir";
}

bool begin() {
  if (!LittleFS.begin(true)) return false;  // true = format on first use
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  return true;
}

bool save(const char* name, const rmt_symbol_word_t* syms, size_t count) {
  if (!count) return false;
  File f = LittleFS.open(pathFor(name), "w");
  if (!f) return false;

  uint16_t cnt = (uint16_t)count;
  bool ok = f.write((const uint8_t*)&kMagic, sizeof(kMagic)) == sizeof(kMagic) &&
            f.write((const uint8_t*)&cnt, sizeof(cnt)) == sizeof(cnt) &&
            f.write((const uint8_t*)syms, count * sizeof(rmt_symbol_word_t)) ==
                count * sizeof(rmt_symbol_word_t);
  f.close();
  return ok;
}

size_t load(const char* name, rmt_symbol_word_t* buf, size_t maxCount) {
  File f = LittleFS.open(pathFor(name), "r");
  if (!f) return 0;

  uint32_t magic = 0;
  uint16_t cnt = 0;
  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) ||
      (magic != kMagic && magic != kMagicV1) ||
      f.read((uint8_t*)&cnt, sizeof(cnt)) != sizeof(cnt) ||
      cnt == 0 || cnt > maxCount) {
    f.close();
    return 0;
  }

  size_t want = cnt * sizeof(rmt_symbol_word_t);
  size_t got = f.read((uint8_t*)buf, want);
  f.close();
  if (got != want) return 0;

  // Legacy files were captured at 1us/tick; current engine runs 2us/tick.
  if (magic == kMagicV1) {
    for (size_t i = 0; i < cnt; i++) {
      buf[i].duration0 /= 2;
      buf[i].duration1 /= 2;
    }
  }
  return cnt;
}

bool remove(const char* name) { return LittleFS.remove(pathFor(name)); }

bool exists(const char* name) { return LittleFS.exists(pathFor(name)); }

int list(String* names, int maxNames) {
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return 0;

  int n = 0;
  for (File f = dir.openNextFile(); f && n < maxNames; f = dir.openNextFile()) {
    String nm = f.name();  // basename
    if (nm.endsWith(".ir")) names[n++] = nm.substring(0, nm.length() - 3);
  }
  return n;
}

}  // namespace IrStore
