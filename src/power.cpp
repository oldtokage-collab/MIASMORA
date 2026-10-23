#include "power.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "audio.h"
#include "audio_io.h"
#include "config.h"
#include "storage.h"
#include "led.h"

static volatile bool g_sleeping = false;
bool powerSleeping() { return g_sleeping; }

// Note: M5.Power.powerOff() is not a real power cut on the Cardputer ADV (it
// has no power-management IC): it ends in a deep sleep with no wake source, so
// only the power switch would bring it back. Arming G0 ourselves is better.
void powerDeepSleep() {
  g_sleeping = true;

  // 1) audio: fade out (already silent after the sleep fade), stop feeding,
  //    let the DMA queue (~93 ms) run out into silence, then remove the I2S
  //    driver so it does not pop
  audioPause(600);
  delay(120);
  audioIoEnd();

  // 2) display and backlight
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.sleep();

  // 3) other peripherals
  ledOff();
  if (M5.Imu.getType() != m5::imu_none) M5.Imu.sleep();
  {
    SdLock lk;
    SD.end();
  }

  // 4) wake source: G0 (active low). Wait for it to be released first, or
  //    the chip would wake straight back up.
  uint32_t t0 = millis();
  while (digitalRead(GPIO_NUM_0) == LOW && millis() - t0 < 5000) delay(10);
  delay(50);
  // v0.96: G0 only -- nothing armed elsewhere may stay (Poltergeist's light
  // sleep left its 0.2 s timer armed, and the deep sleep woke straight up)
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

  esp_deep_sleep_start();
}
