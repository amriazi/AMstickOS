# IR Copy — M5StickS3 (K150)

A universal IR remote cloner for the M5StickS3, with a Nemo-style orange-on-black UI.

- **Watch face** on startup: time, date, battery. Screen turns off after **10 s** of inactivity (any button wakes it). While the screen is off the chip drops into **light sleep** (~2 mA on battery), so it survives a night on the nightstand; the alarm/timer still fire. On USB power it stays fully awake instead.
- **Green LED**: off by default — lights only while a button is held or IR capture/send is running.
- **Copy IR signal**: point any remote (TV, AC, stereo…) at the top of the stick, press a button on the remote — the raw signal is captured via the ESP32-S3 RMT peripheral.
- **Name & save**: give the capture a name with the on-device editor (a default like `IR_01` is pre-filled), stored on LittleFS flash — survives reboots and battery drain.
- **Replay**: pick a saved signal and send it through the built-in IR LED.
- **Alarm**: set HH:MM (persisted to flash) — flashes the screen and beeps, even waking from screen-off. Shown as `AL 07:30` on the watch face.
- **Timer**: countdown that keeps running in the background (`T-04:32` on the watch face); flash+beep when done.
- **Stopwatch**: start/stop/reset with tenths, keeps counting in the background.
- **Dino game**: Chrome-dinosaur-style jumper — Blue to jump, speeds up over time, high score saved to flash.

## Controls

| Button | Short press | Hold |
|---|---|---|
| **Blue** (front, G11, BtnA) | select / confirm / add char | back / save name / **repeat-send** |
| **Low** (side, G12, BtnB) | next item / cycle char / +1 | delete char / −1 |

- Watch face → press **Blue** to open the menu.
- Name editor: **Low** cycles the character, **Blue** adds it, **hold Low** deletes, **hold Blue** saves.
- Sending: on a saved signal's **Send**, **hold Blue** to keep transmitting — the frame repeats every ~40 ms until you release (minimum 3 repeats even on a quick press).

## Build & flash (PlatformIO)

```sh
pio run -t upload
```

Uses the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform fork (Arduino core 3.x / ESP-IDF 5) because the StickS3 IR hardware requires the new RMT driver (`driver/rmt_tx.h` / `rmt_rx.h`). The first build downloads the toolchain (~1 GB).

If the upload fails, put the StickS3 into download mode (see M5Stack docs) and retry.

## Hardware notes (verified against official M5 docs)

- IR TX = **G46**, IR RX = **G42**; capture only works through RMT, not GPIO polling.
- The speaker amplifier interferes with IR reception — the firmware calls `M5.Speaker.end()` at boot, so **there is no sound** in this app.
- Capturing: hold the remote roughly **10–50 cm** from the top of the stick.
- Replay always sends the **raw captured waveform** at a 38 kHz carrier, so any protocol works (NEC frames are additionally decoded and shown for info).
- Capture records the **entire burst** the remote sends — all frames *and* the real gaps between them (up to 60 ms). This matters for Sharp TVs, whose remotes send every command as a true frame + an inverted frame ~40 ms apart, and which reject single repeated frames. Press the remote button **briefly** when capturing for the cleanest recording.
- Capture buffer is 128 RMT symbols (256 marks/spaces) — plenty for TV/audio remotes; very long air-conditioner frames may be truncated.

## TV doesn't react to a sent signal?

1. **Hold Blue on Send** — many TVs ignore a single frame; Sony needs at least 3. The firmware always sends ≥3 repeats and keeps repeating while Blue is held.
2. **Aim and distance** — the IR LED is in the top of the stick; point it straight at the TV's IR window from within ~1–3 m.
3. **Re-capture from close range** (~10–30 cm, in a room without bright sunlight) — a noisy capture replays as garbage. The pulse count shown after capture should be roughly the same every time you capture the same button.
4. **Carrier frequency** — replay uses 38 kHz; a few brands use 36/40/56 kHz, which mostly still works but with reduced range.

## Known limitations

- **No RTC chip on the StickS3**: the clock runs on the ESP32 and is persisted to flash every minute, so after a power-button reset it restores to within a minute. Adjust via *Set clock*. (WiFi/NTP sync would fix it completely — not implemented.)
- Carrier is fixed at 38 kHz (covers the vast majority of remotes; a few use 36/40/56 kHz and may have reduced range).
- If buttons A/B feel swapped on your unit, swap the handling or pins in [config.h](include/config.h).

## Project layout

| File | Purpose |
|---|---|
| [src/main.cpp](src/main.cpp) | App state machine: watch, sleep, menus, alarm/timer/stopwatch, editors |
| [src/ir_engine.cpp](src/ir_engine.cpp) | RMT-based raw IR burst capture + 38 kHz replay, NEC decode |
| [src/ir_store.cpp](src/ir_store.cpp) | LittleFS persistence (`/ir/<name>.ir`) |
| [src/dino_game.cpp](src/dino_game.cpp) | Chrome-dino-style jumping game |
| [src/ui.cpp](src/ui.cpp) | Nemo-style canvas UI: header bar, list menus, toasts |
| [include/config.h](include/config.h) | Pins, timeouts, theme colors |

The speaker is normally disabled (it interferes with the StickS3 IR receiver) and is only switched on while an alarm/timer alert is ringing.
