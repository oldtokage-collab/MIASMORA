// MIASMORA — Poltergeist mode (phase 8). See poltergeist.h.
#include "poltergeist.h"
#include <M5Unified.h>
#include <SD.h>
#include <math.h>
#include "app.h"
#include "audio.h"
#include "config.h"
#include "export.h"
#include "party.h"
#include "rec.h"
#include "storage.h"
#include "audio_io.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

// UI hooks (ui.cpp)
void uiDisplayOff();
void uiWakeDisplay();

const uint16_t PG_INTERVALS[PG_NUM_INTERVALS] = {10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 300, 360, 480, 600, 720, 900, 1200, 1500, 1800};
const uint8_t PG_DELAY_MIN[PG_NUM_DELAYS] = {1, 2, 3, 5, 10};
const uint16_t PG_STOP_MIN[PG_NUM_STOPS] = {30, 60, 120, 180, 240};

static constexpr float SETTLE_SEC = 5.f;          // lying still this long before it starts
static constexpr float STILL_G = 0.03f;           // "still": the acceleration moves less than this
static constexpr float LIFT_DEG = 15.f;           // found: tilted this much from where it lay ...
static constexpr float LIFT_G = 0.12f;            // ... or shaken this much ...
static constexpr float LIFT_SEC = 0.5f;           // ... for this long
static constexpr float IMU_EVERY = 0.2f;
static constexpr float BATT_EVERY = 30.f;         // the battery is read this often
static constexpr int   BATT_LOW = 10;             // %: it ends below this (read three times in a row)
static constexpr float REVEAL_SEC = 5.f;
static constexpr float MIN_GAP = 10.f;            // never closer than 10 s (Escalate)

static PgStage g_stage = PG_OFF;
static float g_t = 0.f;                // time in the stage
static float g_lurkT = 0.f;            // time lurking (for Escalate, the report)
static float g_next = 0.f;             // the next event, in lurk time
static int g_burstLeft = 0;            // Random: more in a row to come
static float g_heartUntil = -1.f;      // HEART beats until this lurk time
static int g_played = 0, g_hushed = 0;
static float g_foundAt = -1.f;
static PgEnd g_end = PGE_MANUAL;
static float g_imuT = 0.f, g_battT = 0.f, g_liftT = 0.f, g_stillT = 0.f;
static float g_g0x = 0.f, g_g0y = 0.f, g_g0z = 1.f;   // the gravity it lay with
static float g_lastAx = 0.f, g_lastAy = 0.f, g_lastAz = 1.f;
static int g_battLowCount = 0;
static int g_bat0 = -1, g_mv0 = 0;
static bool g_testHeart = false;
static int g_testSlot = -1;          // Test: the slot to play once the engine has restarted
static dsp::Rng g_r;
static bool g_seeded = false;
static char g_toast[64] = "";
static bool g_toastShown = false;
// Shy and power (v0.93)
static float g_quietUntil = -1.f;      // Shy: no events before this lurk time
static float g_activeUntil = -1.f;     // an event is sounding until this lurk time
static bool g_idle = false;            // between events: no synthesis, codec output off, 80 MHz
static bool g_suspended = false;       // ... and with Shy off the I2S stopped (light sleep)
static constexpr float WAKE_AHEAD = 0.8f;   // wake this long before the next event
static constexpr float EVENT_SEC = 12.f;    // what an event (a gesture) may take, with its tail
static constexpr int FULL_MHZ = 240, IDLE_MHZ = 80;

PgStage poltergeistStage() { return g_stage; }
bool poltergeistActive() { return g_stage == PG_COUNTDOWN || g_stage == PG_SETTLE || g_stage == PG_LURK; }
bool poltergeistOwnsAudio() { return g_stage == PG_TEST || poltergeistActive(); }
bool poltergeistLurking() { return g_stage == PG_LURK || g_stage == PG_SETTLE ||
                                   (g_stage == PG_COUNTDOWN && poltergeistCountdownLeft() <= 10); }
float poltergeistStageTime() { return g_t; }
bool poltergeistRevealing() { return g_stage == PG_REVEAL; }

int poltergeistCountdownLeft() {
  const float total = PG_DELAY_MIN[gSet.pgDelayIdx % PG_NUM_DELAYS] * 60.f;
  return g_stage == PG_COUNTDOWN ? (int)ceilf(fmaxf(0.f, total - g_t)) : 0;
}

// --- the event slots ---------------------------------------------------------------
int pgLayerCount() {
  int n = 1;   // None
  for (int i = 0; i < NUM_LAYERS; i++) if (layerIsEvent(i)) n++;
  return n + 1;   // + HEART
}
int pgLayerAt(int idx) {
  if (idx <= 0) return -1;
  int n = 1;
  for (int i = 0; i < NUM_LAYERS; i++) if (layerIsEvent(i)) { if (n == idx) return i; n++; }
  return idx == n ? (int)L_HEART : -1;
}
int pgModeParam(int layer) {
  if (layer < 0) return -1;
  const LayerDef& d = LAYER_DEFS[layer];
  for (int k = 0; k < d.nParams; k++) if (d.p[k].nOpts > 1) return k;
  return -1;
}

static int chosenSlots(int* layers) {
  int n = 0;
  for (int k = 0; k < 3; k++) { const int id = pgLayerAt(gSet.pgEv[k]); if (id >= 0) layers[n++] = k; }
  return n;
}

// the scene it plays: the chosen layers at their defaults (the mode chosen),
// silent by themselves (Density 0); HEART only while it beats; the Room
static void buildScene(SceneData& s, bool heartOn) {
  sceneDefaults(s, "Poltergeist");
  for (int k = 0; k < 3; k++) {
    const int id = pgLayerAt(gSet.pgEv[k]);
    if (id < 0) continue;
    LayerState& L = s.L[id];
    L.on = id != L_HEART || heartOn;
    L.level = 70;
    if (layerIsEvent(id)) L.p[0] = 0;
    const int mk = pgModeParam(id);
    if (mk >= 0) L.p[mk] = gSet.pgMode[k] < LAYER_DEFS[id].p[mk].nOpts ? gSet.pgMode[k] : 0;
  }
  LayerState& R = s.L[L_REVERB];
  static const uint8_t MIX[3] = {0, 25, 45}, SIZE[3] = {0, 30, 80}, DAMP[3] = {0, 50, 40};
  const int room = gSet.pgRoom % 3;
  R.on = room > 0; R.level = MIX[room]; R.p[0] = SIZE[room]; R.p[1] = DAMP[room];
}

// --- power (spec v1.24 5.1) ----------------------------------------------------
// Between events nothing is synthesised (the engine skips all DSP at master 0),
// the codec's DAC and output stage are off and the CPU runs at 80 MHz. With
// Shy off the audio task is paused and the I2S stopped too, so the CPU can
// light-sleep (woken every 0.2 s for the IMU, and by a key or G0). Before an
// event all of it comes back, in the reverse order; the codec's volume is
// ramped both ways so it never clicks.
// v0.986: switching the codec's output on or off clicks, and the mic next to
// the speaker heard it as someone in the room (+14..20 dB, within 0.1 s of the
// switch): Shy waited for the quiet, woke for the next event, clicked again...
// and played nothing at all. Shy now ignores the mic this long after a switch.
static constexpr uint32_t DAC_DEAF_MS = 1200;
static uint32_t g_dacMs = 0;   // the last switch (0: none yet)

static void powerIdle() {
  if (g_idle) return;
  audioIoDacPower(false);
  g_dacMs = millis();   // its click: Shy is deaf for a moment (v0.986)
  if (!gSet.pgShy) {   // the mic is not needed: everything audio can stop
    if (audioPause(400)) { audioIoSuspend(true); g_suspended = true; }
  }
  setCpuFrequencyMhz(IDLE_MHZ);
  g_idle = true;
}
static void powerWake() {
  if (!g_idle) return;
  setCpuFrequencyMhz(FULL_MHZ);
  if (g_suspended) { audioIoSuspend(false); audioResume(); g_suspended = false; }
  audioIoDacPower(true);
  g_idle = false;
  g_dacMs = millis();   // its click: Shy is deaf for a moment (v0.986)
}

bool poltergeistShyListening() { return g_stage == PG_LURK && gSet.pgShy; }
bool poltergeistShyDeaf() { return g_dacMs != 0 && millis() - g_dacMs < DAC_DEAF_MS; }

void poltergeistHeard() {
  if (g_stage != PG_LURK) return;
  const float until = g_lurkT + g_r.range(30.f, 120.f);
  if (g_quietUntil < g_lurkT) g_hushed++;   // a new quiet spell (one that goes on is longer, not another)
  if (until > g_quietUntil) g_quietUntil = until;
}

void poltergeistIdleSleep() {
  if (g_stage != PG_LURK || !g_suspended) return;
  // a key or G0 still down / not yet read: no sleep this time
  if (digitalRead(11) == LOW || digitalRead(0) == LOW) return;
  // the keyboard's own interrupt (CHANGE, M5Cardputer's TCA8418 reader) is
  // held off while the pin is a low-level wake source, then given back
  gpio_intr_disable(GPIO_NUM_11);
  esp_sleep_enable_timer_wakeup((uint64_t)(IMU_EVERY * 1e6f));
  gpio_wakeup_enable(GPIO_NUM_11, GPIO_INTR_LOW_LEVEL);   // the keyboard (TCA8418 INT)
  gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);    // G0
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  // v0.96: the wake sources are taken back at once (they would stay armed for
  // the next sleep of any kind -- the deep sleep after Auto Stop woke up)
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  gpio_wakeup_disable(GPIO_NUM_11);
  gpio_wakeup_disable(GPIO_NUM_0);
  gpio_set_intr_type(GPIO_NUM_11, GPIO_INTR_ANYEDGE);
  gpio_intr_enable(GPIO_NUM_11);
}

// one event: a summon (a gesture layer plays its gesture), HEART 4..8 beats
static void fire(int slot, float now, bool test) {
  const int id = pgLayerAt(gSet.pgEv[slot]);
  if (id < 0) return;
  if (id == L_HEART) {
    const float period = 60.f / (40.f + LAYER_DEFS[L_HEART].p[0].def);
    const int beats = g_r.irange(4, 8);
    g_heartUntil = now + 0.3f + beats * period;
    if (test) g_testHeart = true;
    g_activeUntil = g_heartUntil + 1.f;
  } else {
    appRequestSummon(id, g_r.next());
    g_activeUntil = now + EVENT_SEC;
  }
  if (!test) g_played++;
}

// the next gap (s): Fixed +-5 %, Random log-uniform; Escalate down to 40 %
static float nextGap() {
  float g;
  if (gSet.pgTiming == 0) {
    g = PG_INTERVALS[gSet.pgFixIdx % PG_NUM_INTERVALS] * g_r.range(0.95f, 1.05f);
  } else {
    const float lo = PG_INTERVALS[gSet.pgMinIdx % PG_NUM_INTERVALS], hi = PG_INTERVALS[gSet.pgMaxIdx % PG_NUM_INTERVALS];
    g = expf(g_r.range(logf(fminf(lo, hi)), logf(fmaxf(lo, hi))));
  }
  if (gSet.pgEscalate) {
    const float total = PG_STOP_MIN[gSet.pgStopIdx % PG_NUM_STOPS] * 60.f;
    const float x = fminf(1.f, g_lurkT / total);
    g *= 1.f - 0.6f * powf(x, 1.5f);   // gentle at first, 40 % at the end
  }
  return fmaxf(g, MIN_GAP);
}

const char* poltergeistTest() {
  if (g_stage != PG_OFF) return "";
  int slots[3];
  const int n = chosenSlots(slots);
  if (!n) return "No event chosen";
  if (exportBusy()) return "Not while exporting";
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; }
  g_stage = PG_TEST; g_t = 0.f; g_lurkT = 0.f; g_heartUntil = -1.f; g_testHeart = false;
  appRestartScene();
  // v0.91: not now -- the engine's restart (asked for in the same breath) would
  // take the summon as already done; 0.3 s on, it plays
  g_testSlot = slots[g_r.irange(0, n - 1)];
  return nullptr;
}

const char* poltergeistCanStart() {
  int slots[3];
  if (!chosenSlots(slots)) return "No event chosen";
  if (exportBusy()) return "Not while exporting";
  if (recActive()) return "Stop REC first";
  if (partyActive()) return "Not now";
  return nullptr;
}

void poltergeistStart() {
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; }
  g_stage = PG_COUNTDOWN; g_t = 0.f;
  g_quietUntil = -1.f; g_activeUntil = -1.f;
  g_dacMs = 0;
  g_played = g_hushed = 0; g_foundAt = -1.f; g_heartUntil = -1.f;
  g_battLowCount = 0;
  g_bat0 = M5.Power.getBatteryLevel();
  g_mv0 = M5.Power.getBatteryVoltage();
  // the sleep timer stops (Auto Stop takes its place); the scene goes quiet
  if (gSet.sleepOn) { gSet.sleepOn = false; appSleepArm(); }
  appRestartScene();
}

void poltergeistCancel() { if (g_stage == PG_COUNTDOWN) { g_stage = PG_OFF; uiWakeDisplay(); } }

static void fmtDur(char* out, int n, float sec) {
  const int m = (int)(sec / 60.f);
  if (m >= 60) snprintf(out, n, "%dh %02dm", m / 60, m % 60);
  else snprintf(out, n, "%dm", m);
}

void poltergeistReport(char lines[5][40]) {
  char d[16], f[16];
  fmtDur(d, sizeof(d), g_lurkT);
  snprintf(lines[0], 40, "POLTERGEIST REPORT");
  snprintf(lines[1], 40, " Haunted for   %s", d);
  snprintf(lines[2], 40, " Played        %d times", g_played);
  snprintf(lines[3], 40, " Hushed        %d times", g_hushed);
  if (g_end == PGE_FOUND) { fmtDur(f, sizeof(f), g_foundAt); snprintf(lines[4], 40, " Found after   %s", f); }
  else snprintf(lines[4], 40, " %s", g_end == PGE_MANUAL ? "Stopped" : (g_end == PGE_BATTERY ? "Not found (battery low)" : "Not found"));
}

// a line in /MIASMORA/poltergeist_log.txt (no clock: durations, the battery)
static void writeLog() {
  if (!gSdReady) return;
  char found[16] = "-";
  if (g_end == PGE_FOUND) snprintf(found, sizeof(found), "%dm", (int)(g_foundAt / 60.f));
  static const char* const END[4] = {"auto-stop", "found", "stopped", "battery"};
  String ev;
  for (int k = 0; k < 3; k++) {
    const int id = pgLayerAt(gSet.pgEv[k]);
    if (id < 0) continue;
    if (ev.length()) ev += ",";
    ev += LAYER_DEFS[id].name;
    const int mk = pgModeParam(id);
    if (mk >= 0) { ev += ":"; ev += LAYER_DEFS[id].p[mk].opts[gSet.pgMode[k] % LAYER_DEFS[id].p[mk].nOpts]; }
  }
  char line[200];
  snprintf(line, sizeof(line), "%s haunted=%dm played=%d hushed=%d found=%s battery=%d%%/%.2fV->%d%%/%.2fV events=%s timing=%s escalate=%s shy=%s\n",
           END[g_end], (int)(g_lurkT / 60.f), g_played, g_hushed, found, g_bat0, g_mv0 / 1000.f,
           M5.Power.getBatteryLevel(), M5.Power.getBatteryVoltage() / 1000.f, ev.c_str(),
           gSet.pgTiming ? "random" : "fixed", gSet.pgEscalate ? "on" : "off", gSet.pgShy ? "on" : "off");
  SdLock lk;
  File f = SD.open(DIR_ROOT "/poltergeist_log.txt", FILE_APPEND);
  if (f) { f.print(line); f.close(); }
}

static void finish(PgEnd why) {
  powerWake();   // full speed, sound and codec back first
  g_end = why;
  writeLog();
  if (gPlaying) appTogglePlay();   // back to MAIN stopped (never playing by itself)
  appRestartScene();
  if (why == PGE_AUTO || why == PGE_BATTERY) {
    // as the sleep timer ends: the report waits for the next start (deep sleep)
    // or on the dark screen (Display Off)
    snprintf(gSet.pgLast, sizeof(gSet.pgLast), "Last Poltergeist: %s, played %d times",
             why == PGE_BATTERY ? "battery low" : "Not found", g_played);
    appSettingsChanged();
    g_stage = PG_REPORT; g_t = 0.f;
    appSleepNow();
    return;
  }
  uiWakeDisplay();
  if (why == PGE_FOUND) {
    switch ((gSet.pgFound % 3 == 0 && !partyUnlocked()) ? 1 : gSet.pgFound % 3) {
      case 0: g_stage = PG_OFF; partyStartManual(); return;   // Party: one's own party
      case 1: g_stage = PG_REVEAL; g_t = 0.f; return;         // Reveal: the eyes, then the report
      default: break;                                         // Silent: the report
    }
  }
  g_stage = PG_REPORT; g_t = 0.f;
}

void poltergeistStop(PgEnd why) { if (poltergeistActive()) finish(why); }

void poltergeistCloseScreen() {
  if (g_stage == PG_REVEAL) { g_stage = PG_REPORT; g_t = 0.f; return; }
  if (g_stage == PG_REPORT) {
    g_stage = PG_OFF;
    if (gSet.pgLast[0]) { gSet.pgLast[0] = 0; appSettingsChanged(); }   // it has been seen
  }
}

const char* poltergeistBootToast() {
  if (g_toastShown || !gSet.pgLast[0]) return nullptr;
  g_toastShown = true;
  strncpy(g_toast, gSet.pgLast, sizeof(g_toast) - 1);
  gSet.pgLast[0] = 0;
  appSettingsChanged();
  return g_toast;
}

// the IMU every 0.2 s: still enough to start? lifted?
static void checkImu(float dt) {
  g_imuT += dt;
  if (g_imuT < IMU_EVERY) return;
  const float step = g_imuT;
  g_imuT = 0.f;
  float ax, ay, az;
  M5.Imu.update();
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  if (g_stage == PG_SETTLE) {
    const float d = fabsf(ax - g_lastAx) + fabsf(ay - g_lastAy) + fabsf(az - g_lastAz);
    g_stillT = d < STILL_G ? g_stillT + step : 0.f;
    if (g_stillT >= SETTLE_SEC) {   // lying still: this is where it lies
      g_g0x = ax; g_g0y = ay; g_g0z = az;
      g_stage = PG_LURK; g_t = 0.f; g_lurkT = 0.f; g_liftT = 0.f;
      g_next = nextGap();
    }
  } else if (g_stage == PG_LURK && gImuReady) {
    const float m = sqrtf(ax * ax + ay * ay + az * az), m0 = sqrtf(g_g0x * g_g0x + g_g0y * g_g0y + g_g0z * g_g0z);
    const float c = (ax * g_g0x + ay * g_g0y + az * g_g0z) / fmaxf(m * m0, 1e-6f);
    const float tilt = acosf(fmaxf(-1.f, fminf(1.f, c))) * 57.29578f;
    const bool moved = tilt > LIFT_DEG || fabsf(m - 1.f) > LIFT_G;
    // a door slamming, someone walking past: brief -- lifted: it lasts
    g_liftT = moved ? g_liftT + step : 0.f;
    if (g_liftT >= LIFT_SEC) { g_foundAt = g_lurkT; finish(PGE_FOUND); }
  }
  g_lastAx = ax; g_lastAy = ay; g_lastAz = az;
}

void poltergeistUpdate(float frameDt) {
  // v0.95: its own clock. The loop's dt is capped at 0.1 s (main.cpp), but with
  // Shy off each loop light-sleeps 0.2 s: on the capped dt every timer here ran
  // at about half speed (Auto Stop, the gaps, the IMU). Real elapsed time instead.
  static uint32_t lastMs = 0;
  const uint32_t nowMs = millis();
  float dt = lastMs ? (nowMs - lastMs) * 0.001f : frameDt;
  lastMs = nowMs;
  if (dt > 1.f) dt = 1.f;   // never a jump (the loop is never that slow while it runs)
  g_t += dt;
  switch (g_stage) {
    case PG_TEST:
      g_lurkT += dt;
      if (g_testSlot >= 0 && g_lurkT >= 0.3f) { fire(g_testSlot, g_lurkT, true); g_testSlot = -1; }
      if (g_testSlot < 0 && g_lurkT > (g_testHeart ? g_heartUntil + 1.f : 6.3f)) { g_stage = PG_OFF; appRestartScene(); }
      break;
    case PG_COUNTDOWN: {
      const int left = poltergeistCountdownLeft();
      if (left <= 10) uiDisplayOff();   // the last 10 s: dark
      if (left <= 0) { g_stage = PG_SETTLE; g_t = 0.f; g_stillT = 0.f; }
      break;
    }
    case PG_SETTLE:
      checkImu(dt);
      if (!gImuReady && g_t >= SETTLE_SEC) { g_stage = PG_LURK; g_t = 0.f; g_lurkT = 0.f; g_next = nextGap(); }
      break;
    case PG_LURK: {
      g_lurkT += dt;
      uiDisplayOff();
      checkImu(dt);
      if (g_stage != PG_LURK) break;
      // Shy: an event due while it keeps quiet waits for the quiet to end
      if (g_lurkT >= g_next && g_lurkT < g_quietUntil) g_next = g_quietUntil + g_r.range(5.f, 20.f);
      // power: awake around an event (and a moment before it), idle otherwise
      if (g_lurkT >= g_next - WAKE_AHEAD || g_lurkT < g_activeUntil) powerWake();
      else powerIdle();
      // the events
      if (g_lurkT >= g_next) {
        int slots[3];
        const int n = chosenSlots(slots);
        if (n) fire(slots[g_r.irange(0, n - 1)], g_lurkT, false);
        if (g_burstLeft > 0) { g_burstLeft--; g_next = g_lurkT + g_r.range(2.f, 6.f); }
        else {
          if (gSet.pgTiming == 1 && g_r.chance(0.1f)) {   // now and then: again, a few seconds on
            g_burstLeft = g_r.irange(1, 2) - 1;
            g_next = g_lurkT + g_r.range(2.f, 6.f);
          } else {
            g_next = g_lurkT + nextGap();
          }
        }
      }
      // Auto Stop, the battery
      if (g_lurkT >= PG_STOP_MIN[gSet.pgStopIdx % PG_NUM_STOPS] * 60.f) { finish(PGE_AUTO); break; }
      g_battT += dt;
      if (g_battT >= BATT_EVERY) {
        g_battT = 0.f;
        const int b = M5.Power.getBatteryLevel();
        g_battLowCount = (b >= 0 && b < BATT_LOW) ? g_battLowCount + 1 : 0;
        if (g_battLowCount >= 3) finish(PGE_BATTERY);
      }
      break;
    }
    case PG_REVEAL:
      if (g_t >= REVEAL_SEC) { g_stage = PG_REPORT; g_t = 0.f; }
      break;
    default: break;
  }
}

void poltergeistParams(EngineParams& p, uint32_t seed, uint32_t resetSeq) {
  SceneData s;
  const float now = g_stage == PG_TEST ? g_lurkT : g_lurkT;
  buildScene(s, g_heartUntil > 0.f && now < g_heartUntil);
  // its own volume, within the device's Volume; silent until it lurks
  const float v = gSet.volume * 0.01f * gSet.pgVolume * 0.01f;
  const bool sounding = g_stage == PG_TEST || (g_stage == PG_LURK && !g_idle);   // idle: no DSP at all
  buildEngineParams(s, sounding ? 1.5f * v * sqrtf(v) : 0.f, seed, resetSeq, p);
}
