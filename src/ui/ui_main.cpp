// MIASMORA — MAIN screen: mood, DREAD, visualiser, seed, battery, play / REC state
#include "ui_internal.h"
#include "../audio.h"
#include "../rec.h"
#include "../cursed.h"
#include "../listen.h"
#include "../arc.h"
#include "../arc_curve.h"

static char g_seedBuf[7];
static int g_seedLen = 0;
static uint32_t g_moodShowUntil = 0;

bool seedEntryActive() { return g_seedLen > 0; }

static int nextScene(int dir) {
  int n = (int)gScenes.size();
  if (n == 0) return -1;
  int cur = gCurScene < 0 ? (dir > 0 ? -1 : 0) : gCurScene;
  return ((cur + dir) % n + n) % n;
}

void drawMain() {
  char b[40];
  // --- top row: play state, volume | REC, sleep, battery -------------------------
  uint16_t pc = gPlaying ? C_INK : C_DIM;
  if (gPlaying) canvas.fillTriangle(4, 2, 4, 10, 10, 6, pc);
  else { canvas.fillRect(3, 2, 3, 8, pc); canvas.fillRect(8, 2, 3, 8, pc); }
  snprintf(b, sizeof(b), "VOL %d", gSet.volume);
  drawText(b, 16, 2, C_DIM);

  int rx = SCREEN_W - 3;
  int bat = M5Cardputer.Power.getBatteryLevel();
  if (bat >= 0) {
    snprintf(b, sizeof(b), "%d%%", bat);
    drawText(b, rx, 2, bat <= 15 ? C_WARN : C_DIM, 1, textdatum_t::top_right);
    rx -= 28;
  }
  int rem = appSleepRemainingSec();
  if (rem >= 0) {
    if (appSleepHeld()) snprintf(b, sizeof(b), "zz hold");
    else if (appSleepFading()) snprintf(b, sizeof(b), "zz fade");
    else snprintf(b, sizeof(b), "zz %d:%02d", rem / 60, rem % 60);
    drawText(b, rx, 2, C_DIM, 1, textdatum_t::top_right);
    rx -= (int)strlen(b) * 6 + 6;
  }
  if (recActive()) {
    uint32_t s = recElapsedMs() / 1000;
    snprintf(b, sizeof(b), "REC %lu:%02lu%s", (unsigned long)(s / 60), (unsigned long)(s % 60), recDropped() ? "!" : "");
    bool blink = (millis() / 500) % 2;
    drawText(b, rx, 2, recCapturing() ? C_ACC2 : C_DIM, 1, textdatum_t::top_right);
    int w = (int)strlen(b) * 6;
    if (blink && recCapturing()) canvas.fillCircle(rx - w - 6, 5, 3, C_ACC2);
  }
  canvas.drawFastHLine(0, 12, SCREEN_W, C_LINE);

  // --- mood name ------------------------------------------------------------------
  const char* name = appMoodName();
  static char glitched[24];
  if (cursedNameGlitch()) {   // Cursed Mode: the name breaks up for a moment
    static const char NOISE[] = "#%&?!/\\|*@$";
    uint32_t s = millis() / 70;
    strncpy(glitched, name, sizeof(glitched) - 1);
    glitched[sizeof(glitched) - 1] = 0;
    for (int k = 0; glitched[k]; k++) {
      s = s * 1103515245u + 12345u;
      if ((s >> 16) % 3 == 0) glitched[k] = NOISE[(s >> 8) % (sizeof(NOISE) - 1)];
    }
    name = glitched;
  }
  int len = strlen(name);
  int sz = len <= 18 ? 2 : 1;
  bool fresh = millis() < g_moodShowUntil;
  drawText(name, SCREEN_W / 2, sz == 2 ? 17 : 21, fresh ? C_ACC2 : C_INK, sz, textdatum_t::top_center);
  if (gEdited) drawText("*", SCREEN_W - 6, 17, C_DIM, 1, textdatum_t::top_right);
  if (appIsDrift()) drawText("~", 6, 17, C_DIM);

  // --- DREAD ----------------------------------------------------------------------
  int dread = constrain((int)gRt.live.dread + appDreadBoost(), 0, 100);
  drawText("DREAD", 4, 39, C_DIM);
  drawBar(40, 38, 160, 9, dread / 100.f, C_ACC, C_FAINT);
  snprintf(b, sizeof(b), "%d", dread);
  drawText(b, SCREEN_W - 4, 39, C_INK, 1, textdatum_t::top_right);

  // Watcher (v0.6): on a Hush the eye takes the visualiser's place (any kind, Off too)
  if (watcherVisible()) { vizIdle(); watcherDraw(); }
  else drawVisualizer();   // SETTINGS > Display > Visualizer; V on MAIN

  // --- Arc's progress (v0.5): a thin bar in the gap between the visualiser
  // (y 53..99) and the seed row, ticks where Build, Peak and Release begin;
  // it flickers during the climax, and dims once a Once has finished
  if (arcShown()) {
    const int ax = 4, aw = SCREEN_W - 8, ay = 101;
    const bool flick = arcClimaxRunning() && (millis() / 120) % 2;
    canvas.fillRect(ax, ay, aw, 3, C_FAINT);
    const int fw = (int)(arcProgress() * aw + 0.5f);
    canvas.fillRect(ax, ay, fw, 3, arcFinished() ? C_DIM : flick ? C_ACC2 : C_ACC);
    const float ticks[3] = {arc::BUILD_AT, arc::PEAK_AT, arc::RELEASE_AT};
    for (float t : ticks) canvas.drawFastVLine(ax + (int)(t * aw), ay - 1, 5, C_DIM);
  }

  // --- seed -----------------------------------------------------------------------
  if (g_seedLen > 0) {
    char d[7];
    for (int i = 0; i < 6; i++) d[i] = i < g_seedLen ? g_seedBuf[i] : '_';
    d[6] = 0;
    snprintf(b, sizeof(b), "SEED %s", d);
    drawText(b, 4, 107, C_ACC2);
    drawText("[Del] [Esc]", SCREEN_W - 4, 107, C_DIM, 1, textdatum_t::top_right);
  } else {
    snprintf(b, sizeof(b), "SEED %06lu", (unsigned long)gSeed);
    drawText(b, 4, 107, C_INK);
    if (gSet.seedLock) drawText("LOCK", 76, 107, C_ACC2);
    if (gSet.listen) {   // Listen: an ear mark and how much Attention has built up
      bool hush = listenHushing();
      drawText(hush ? "HUSH" : "LSN", 108, 107, hush ? C_ACC2 : C_DIM);
      drawBar(136, 108, 40, 6, listenAttention(), C_ACC, C_FAINT);
    }
    if (!gPlaying) drawText("STOPPED", SCREEN_W - 4, 107, C_DIM, 1, textdatum_t::top_right);
  }
  drawNav("[,/]Mood [;.]Vol [Sh;.]Dread [V]iz [Q]Save");
}

static void commitSeed() {
  uint32_t v = 0;
  for (int i = 0; i < 6; i++) v = v * 10 + (g_seedBuf[i] - '0');
  g_seedLen = 0;
  appChangeSeedSmooth(v);   // v0.43: through a dip, like Shake's New Seed (was a hard restart)
  char b[24];
  snprintf(b, sizeof(b), "Seed %06lu", (unsigned long)v);
  uiToast(b);
}

bool keyMain(const KeyEv& ev) {
  // Seed entry: digits collect until six are in, then the seed is applied
  if (!ev.shift && ev.c >= '0' && ev.c <= '9') {
    g_seedBuf[g_seedLen++] = ev.c;
    if (g_seedLen >= 6) commitSeed();
    return true;
  }
  if (g_seedLen > 0) {
    if (ev.del) { g_seedLen--; return true; }
    if (ev.esc) { g_seedLen = 0; return true; }
  }
  if (ev.shift && (ev.k == ';' || ev.k == '.')) {
    appSettleEdit();
    int d = (ev.k == ';' ? 1 : -1) * stepFor(ev.heldMs);
    int v = constrain((int)gRt.live.dread + d, 0, 100);
    if (v != gRt.live.dread) { gRt.live.dread = (uint8_t)v; appSceneEdited(); }
    return true;
  }
  if (ev.shift) return false;
  if (ev.k == ';' || ev.k == '.') {
    int v = constrain((int)gSet.volume + (ev.k == ';' ? 2 : -2), 0, 100);
    if (v != gSet.volume) { gSet.volume = (uint8_t)v; appSettingsChanged(); }
    return true;
  }
  if (ev.k == 'q') { appShakeRun(SHAKE_QUICKSAVE); return true; }   // v0.5: Quick Save from MAIN
  if (ev.k == 'v') {   // the next visualiser (Off included), saved as Display > Visualizer
    gSet.viz = (uint8_t)((gSet.viz + 1) % NUM_VIZ);
    appSettingsChanged();
    uiToast(VIZ_NAMES[gSet.viz]);
    return true;
  }
  if (ev.k == ',' || ev.k == '/') {
    int idx = nextScene(ev.k == '/' ? 1 : -1);
    if (idx >= 0) { appSelectScene(idx); g_moodShowUntil = millis() + 1500; }
    return true;
  }
  return false;
}
