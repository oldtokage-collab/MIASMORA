// MIASMORA — Dark Ambient Drone Generator for M5Stack Cardputer ADV
//
// Fully synthesised (no samples), 22050 Hz mono. The audio task runs on
// PRO_CPU; this file's loop() (UI, input, saving) runs on APP_CPU.
//
// Source map
//   config.h            constants, FW_VERSION
//   dsp/                sine table, filters, resonators, PRNG, dither, reverb
//   layers/             the 14 sound layers (+ shared LFO / phasor / scheduler)
//   audio.*             engine, mixer, master stage, live audio task
//   audio_io.*          own full-duplex I2S driver for the ES8311 codec
//   power.*             deep sleep after the sleep timer
//   scene.*             presets, random scenes, Drift, crossfades, DREAD, scene files
//   app.*               play / seed / scene registry, sleep timer, auto-save
//   export.*            offline WAV loop export (Crossfade / Strict)
//   rec.*               live recording
//   storage.*           SD card, lock, WAV helpers
//   ui/                 screens and input
#include <M5Cardputer.h>
#include <esp_system.h>
#include "config.h"
#include "dsp/dsp.h"
#include "audio.h"
#include "audio_io.h"
#include "app.h"
#include "storage.h"
#include "ui/ui.h"
#include "memstat.h"
#include "led.h"

static uint32_t g_lastFrameMs = 0;

// Why the device last restarted, for HELP (v0.32: v0.31 rebooted itself when
// all 25 layers were on; this tells a watchdog from a crash or a brownout)
static const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "PWR";     // power on
    case ESP_RST_EXT:       return "EXT";     // reset pin
    case ESP_RST_SW:        return "SW";      // esp_restart(), e.g. the Launcher
    case ESP_RST_PANIC:     return "PANIC";   // exception / abort
    case ESP_RST_INT_WDT:   return "IWDT";    // interrupt watchdog
    case ESP_RST_TASK_WDT:  return "TWDT";    // task watchdog (a starved idle task)
    case ESP_RST_WDT:       return "WDT";     // other watchdogs
    case ESP_RST_DEEPSLEEP: return "SLEEP";   // wake from deep sleep
    case ESP_RST_BROWNOUT:  return "BROWN";   // supply voltage dip
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "?";
  }
}
void setup() {
  gResetReason = resetReasonName();
  memMark("boot");                 // Debug > Memory: the heap after each step
  auto cfg = M5.config();
  cfg.internal_spk = true;
  cfg.internal_imu = true;
  M5Cardputer.begin(cfg, true);
  // The IMU lives on M5Unified's global M5 object (not aliased on M5Cardputer)
  gImuReady = (M5.Imu.getType() != m5::imu_none);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(180);
  ledInit();              // dark until something lights it (v0.6)
  memMark("M5 / display / IMU");

  // Audio output: our own full-duplex I2S driver (v0.2, for Listen mode).
  // M5Unified only brings the codec up; M5Cardputer.Speaker / .Mic must not
  // be used after this. Loudness matches the former setVolume(180) path.
  audioIoBegin();
  memMark("I2S driver");

  dsp::init();
  uiInit();               // a dark screen until the start screen
  memMark("screen canvas");

  storageInit();
  memMark("SD card");
  appInit();
  memMark("settings / scenes");
  gEngine.begin();
  memMark("engine / layers");
  appUpdate(0.f);         // publish the first parameter set before audio starts
  audioStart();
  memMark("audio task");

  // the start screen (v0.98): the logo out of the fog, four heartbeats (~6.7 s;
  // any key or G0 skips it)
  M5Cardputer.Display.setBrightness((uint8_t)(gSet.brightness * 255 / 100));
  uiSplashRun();
  g_lastFrameMs = millis();
}

void loop() {
  M5Cardputer.update();
  uint32_t now = millis();
  float dt = (now - g_lastFrameMs) * 0.001f;
  if (dt > 0.1f) dt = 0.1f;
  g_lastFrameMs = now;

  uiLoop(dt);
  appUpdate(dt);
  delay(4);
}
