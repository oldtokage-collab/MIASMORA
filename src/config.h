// MIASMORA — global constants
#pragma once
#include <stdint.h>

// The one and only place the version number lives. Splash and Help/About read it from here.
#define FW_NAME    "MIASMORA"
#define FW_VERSION "v1.0"

static constexpr int   SR          = 22050;   // sample rate (Hz), mono
static constexpr int   BUF         = 512;     // samples per audio buffer (~23 ms)
static constexpr int   CTL         = 32;      // control-rate block inside a buffer (~1.45 ms)
static constexpr float INV_SR      = 1.0f / SR;
static constexpr float PI_F        = 3.14159265f;
static constexpr float TWO_PI_F    = 6.28318531f;

static constexpr int   SCREEN_W    = 240;
static constexpr int   SCREEN_H    = 135;

// SD card (SPI) pins on Cardputer / Cardputer ADV
static constexpr int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;

#define DIR_ROOT      "/MIASMORA"
#define DIR_SCENE     "/MIASMORA/scene"
#define DIR_REC       "/MIASMORA/rec"
#define DIR_ONESHOT   "/MIASMORA/oneshot"
#define SETTINGS_PATH "/MIASMORA/settings.json"

static constexpr uint32_t SAVE_DEBOUNCE_MS    = 1000;
static constexpr uint32_t KEY_REPEAT_DELAY_MS = 420;
static constexpr uint32_t KEY_REPEAT_RATE_MS  = 60;
