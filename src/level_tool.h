#pragma once

// Bubble level (spirit level) using the IMU accelerometer.
// Works on any surface orientation: it picks the gravity-dominant axis
// and shows the tilt of the other two as a bubble in a ring. Centers,
// turns green and beeps when level.
namespace LevelTool {

enum Result { kStay, kExit };

void begin();    // enter: turn the speaker on
Result frame();  // run one frame (read IMU + render); kExit = leave
void end();      // exit: turn the speaker off (IR needs the amp off)

}  // namespace LevelTool
