// MIASMORA — Party Mode's screen and LED (hidden; spec v1.23 3.6 / 3.7)
//
// For once the reds are broken: a LET'S PARTY! window in rainbow colours that
// bounce to the beat, then the list of the party's settings under a small
// banner, rainbow bars below, a faint flash of the whole screen on each beat.
// The LED flashes once per beat (2.3 times a second at most, 140 BPM) in a
// new colour each beat, after one white burst at the explosion. Everything
// goes dark at the cut.
#include "ui_internal.h"
#include <math.h>
#include "../audio.h"
#include "../audio_io.h"
#include "../party.h"
#include "../led.h"

bool uiDisplayIsOn();

static constexpr float WINDOW_SEC = 2.2f;   // the big LET'S PARTY! window, then the settings
static int g_cur = 0, g_scroll = 0;

// hue 0..1 -> 565, full saturation
static uint16_t hue565(float h, float v = 1.f) {
  h -= floorf(h);
  const float x = h * 6.f;
  const int i = (int)x;
  const float f = x - i;
  float r, g, b;
  switch (i % 6) {
    case 0: r = 1; g = f; b = 0; break;
    case 1: r = 1 - f; g = 1; b = 0; break;
    case 2: r = 0; g = 1; b = f; break;
    case 3: r = 0; g = 1 - f; b = 1; break;
    case 4: r = f; g = 0; b = 1; break;
    default: r = 1; g = 0; b = 1 - f; break;
  }
  return rgb((uint8_t)(r * 255 * v), (uint8_t)(g * 255 * v), (uint8_t)(b * 255 * v));
}

bool partyOwnsScreen() {
  const PartyStage s = partyStage();
  return s == PS_PARTY || s == PS_CUT || s == PS_DARK;
}

// LET'S PARTY!, each letter its own colour, hopping on the beat
static void drawTitle(int cx, int y, int size, float hueBase, float hop) {
  static const char T[] = "LET'S PARTY!";
  const int n = sizeof(T) - 1, w = 6 * size;
  int x = cx - n * w / 2;
  for (int i = 0; i < n; i++) {
    const char s[2] = {T[i], 0};
    const int dy = (i & 1) ? (int)(-hop * size * 1.5f) : (int)(hop * size * 0.5f);
    drawText(s, x, y + dy, hue565(hueBase + i * 0.08f), size);
    x += w;
  }
}

static void drawBars(int y0, int h, float hueBase) {
  const int N = 32, bw = (SCREEN_W - 8) / N;
  for (int k = 0; k < N; k++) {
    const int idx = (gVizIdx - N + k + VIZ_N * 2) % VIZ_N;
    float v = gVizHist[idx] * 3.5f;
    if (v > 1.f) v = 1.f;
    const int bh = (int)(v * h);
    if (bh > 0) canvas.fillRect(4 + k * bw, y0 + h - bh, bw - 1, bh, hue565(hueBase + k / (float)N));
  }
}

void drawParty() {
  const PartyStage st = partyStage();
  if (st != PS_PARTY) { canvas.fillScreen(TFT_BLACK); return; }   // the cut and the dark: nothing at all
  const uint32_t beats = partyBeats();
  const float ph = partyBeatPhase();
  const float hop = expf(-ph * 7.f);
  // a faint flash on each beat (kept weak: no hard flashing)
  const uint8_t fl = (uint8_t)(22.f * hop);
  canvas.fillScreen(rgb(fl, fl, (uint8_t)(fl + 6)));
  const float hueBase = (beats % 16) / 16.f + ph * 0.06f;
  const float t = partyStageTime();

  if (t < WINDOW_SEC || !partyManual()) {
    // the explosion: a big window with a rainbow frame
    const int x = 14, y = 30, w = SCREEN_W - 28, h = 66;
    for (int k = 0; k < 4; k++) canvas.drawRect(x + k, y + k, w - 2 * k, h - 2 * k, hue565(hueBase + k * 0.12f));
    canvas.fillRect(x + 4, y + 4, w - 8, h - 8, TFT_BLACK);
    drawTitle(SCREEN_W / 2, y + 22, 2, hueBase, hop);
    drawBars(104, 26, hueBase);
    return;
  }
  // the party: a small banner, the settings, the bars
  drawTitle(48, 2, 1, hueBase, hop);
  char load[32];
  snprintf(load, sizeof(load), "VOL%3d dsp%2d%% dr%u", gSet.volume, (int)(gAudioLoadMean * 100.f + 0.5f), (unsigned)audioIoDropouts());
  drawText(load, SCREEN_W - 3, 2, rgb(150, 150, 150), 1, textdatum_t::top_right);
  const int VIS = 6, TOP = 14, ROW = 12;
  if (g_cur < g_scroll) g_scroll = g_cur;
  if (g_cur >= g_scroll + VIS) g_scroll = g_cur - VIS + 1;
  for (int v = 0; v < VIS && g_scroll + v < NUM_PARTY_ITEMS; v++) {
    const int i = g_scroll + v, y = TOP + v * ROW;
    const bool sel = i == g_cur;
    if (sel) canvas.fillRect(0, y - 1, SCREEN_W, ROW, hue565(hueBase + 0.5f, 0.35f));
    drawText(partyItemName(i), 10, y + 1, sel ? TFT_WHITE : rgb(200, 200, 200));
    char v2[24], b[32];
    partyItemValue(i, v2, sizeof(v2));
    snprintf(b, sizeof(b), sel ? "< %s >" : "%s", v2);
    drawText(b, 110, y + 1, sel ? hue565(hueBase + 0.15f) : TFT_WHITE);
  }
  if (g_scroll > 0) drawText("^", SCREEN_W - 8, TOP, rgb(160, 160, 160));
  if (g_scroll + VIS < NUM_PARTY_ITEMS) drawText("v", SCREEN_W - 8, TOP + (VIS - 1) * ROW, rgb(160, 160, 160));
  drawBars(TOP + VIS * ROW + 2, 122 - (TOP + VIS * ROW + 2), hueBase);
  drawNav("[;.]Sel [,/]Chg [Sh;.]Vol [Spc]Fill [Esc]");
}

// after a Cursed party, back in the dark: how to call it again
void drawPartyReveal() {
  const float h = (millis() % 4000) / 4000.f;
  // v0.99: the first party also unlocks Poltergeist's When Found > Party (the
  // line does not fit the window's width: it wraps after "When Found")
  const bool unlock = partyRevealUnlockLine();
  const int x = 16, y = unlock ? 24 : 34, w = SCREEN_W - 32, hh = unlock ? 86 : 62;
  for (int k = 0; k < 3; k++) canvas.drawRect(x + k, y + k, w - 2 * k, hh - 2 * k, hue565(h + k * 0.15f));
  canvas.fillRect(x + 3, y + 3, w - 6, hh - 6, TFT_BLACK);
  drawText("You found the Secret Party!", SCREEN_W / 2, y + 12, TFT_WHITE, 1, textdatum_t::top_center);
  drawText("Press Fn+P to call it again.", SCREEN_W / 2, y + 26, hue565(h + 0.5f), 1, textdatum_t::top_center);
  if (unlock) {
    drawText("Poltergeist: When Found", SCREEN_W / 2, y + 42, TFT_WHITE, 1, textdatum_t::top_center);
    drawText("> Party unlocked", SCREEN_W / 2, y + 52, hue565(h + 0.25f), 1, textdatum_t::top_center);
  }
  drawText("(press any key)", SCREEN_W / 2, y + (unlock ? 68 : 44), rgb(150, 150, 150), 1, textdatum_t::top_center);
}

// keys at a party (the caller has already taken Shift+R for REC)
void keyParty(const KeyEv& ev) {
  const PartyStage st = partyStage();
  if (ev.esc) { partyEnd(); return; }
  if (st != PS_PARTY || !partyManual()) return;     // nothing else until the party is on; a Cursed one: Esc only
  if (ev.space) { partyFill(); return; }
  // the volume (v0.66): Shift+; / . as on MAIN for DREAD -- the device's own Volume
  if (ev.shift && (ev.k == ';' || ev.k == '.')) {
    gSet.volume = (uint8_t)constrain((int)gSet.volume + (ev.k == ';' ? 2 : -2), 0, 100);
    appSettingsChanged();
    return;
  }
  if (partyStageTime() < WINDOW_SEC) return;        // the window first
  if (ev.k == ';') { g_cur = (g_cur + NUM_PARTY_ITEMS - 1) % NUM_PARTY_ITEMS; return; }
  if (ev.k == '.') { g_cur = (g_cur + 1) % NUM_PARTY_ITEMS; return; }
  if (ev.k == ',') { partyItemChange(g_cur, -1); return; }
  if (ev.k == '/' || ev.enter) { partyItemChange(g_cur, 1); return; }
}

// the LED: a white burst at the explosion, then a colour per beat, flashing
// with the kick; dark at the cut; nothing with the display off
static const uint8_t PAL[6][3] = {{255, 0, 60}, {255, 140, 0}, {255, 230, 0}, {0, 230, 90}, {0, 140, 255}, {170, 0, 255}};
void partyLedUpdate() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last < 25) return;
  last = now;
  if (partyStage() != PS_PARTY || !uiDisplayIsOn()) { ledOff(); return; }
  if (partyStageTime() < 0.35f) { ledShow(255, 255, 255, 1.f); return; }
  const uint8_t* c = PAL[partyBeats() % 6];
  ledShow(c[0], c[1], c[2], 0.25f + 0.75f * expf(-partyBeatPhase() * 5.f));
}
