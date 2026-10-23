// MIASMORA — deep sleep after the sleep timer (spec v1.1, 4.2)
#pragma once

// Silence and stop the audio path, switch the display / IMU / SD off and
// enter deep sleep. Wakes on G0 (GPIO0); waking is a normal restart.
// Never returns.
void powerDeepSleep();

// True from the moment the deep-sleep sequence starts (Cursed Mode stays quiet)
bool powerSleeping();
