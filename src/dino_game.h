#pragma once

// Chrome-dinosaur-style jumping game.
// Blue (BtnA) = jump / retry, hold Low (BtnB) = quit, Low at game over = exit.
namespace DinoGame {

enum Result { kPlaying, kExit };

void reset();    // start a new game (call before entering the state)
Result frame();  // run one frame: input + physics + render

}  // namespace DinoGame
