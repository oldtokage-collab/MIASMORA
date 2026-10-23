// MIASMORA — the on-board RGB LED. See led.h.
#include "led.h"
#include <Adafruit_NeoPixel.h>
#include "app.h"

static constexpr int LED_PIN = 21;   // the Cardputer ADV's WS2812 (as in Ensō)
static Adafruit_NeoPixel g_strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t g_last = 0xFFFFFFFFu;

void ledInit() {
  g_strip.begin();
  g_strip.setBrightness(255);   // the dimming is done in ledShow()
  g_strip.clear();
  g_strip.show();
  g_last = 0;
}

void ledShow(uint8_t r, uint8_t g, uint8_t b, float level) {
  if (!gSet.led) level = 0.f;
  level = level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
  float k = level * gSet.ledBright * 0.01f;
  k *= k;   // perceptual: an LED looks far brighter at the low end than linear
  const uint32_t c = g_strip.Color((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
  if (c == g_last) return;
  g_last = c;
  g_strip.setPixelColor(0, c);
  g_strip.show();
}

void ledOff() { ledShow(0, 0, 0, 0.f); }
