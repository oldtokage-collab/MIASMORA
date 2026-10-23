// MIASMORA — the on-board RGB LED (v0.6, spec v1.23 3.8)
//
// One WS2812 on GPIO21, driven as in Ensō (Adafruit NeoPixel). SETTINGS >
// Display > LED (Off / On) and LED Brightness apply to everything that lights
// it (Watcher's red pulse, the party).
#pragma once
#include <stdint.h>

void ledInit();
// colour 0..255 per channel, level 0..1 (before LED Brightness and the
// perceptual curve); sent only when it changes
void ledShow(uint8_t r, uint8_t g, uint8_t b, float level);
void ledOff();
