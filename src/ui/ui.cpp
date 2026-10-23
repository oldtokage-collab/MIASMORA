// MIASMORA — UI core: drawing helpers, key input, global keys, display power,
// shake detection, modals, splash.
#include "ui.h"
#include "ui_internal.h"
#include "../rec.h"
#include "../export.h"
#include "../storage.h"
#include "../cursed.h"
#include "../listen.h"
#include "../power.h"
#include "../party.h"
#include "../planchette.h"
#include "../poltergeist.h"

M5Canvas canvas(&M5Cardputer.Display);
Screen gScreen = SCR_MAIN;
bool gHelp = false;

uint16_t C_BG, C_INK, C_DIM, C_FAINT, C_LINE, C_ACC, C_ACC2, C_SEL, C_WARN;

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return canvas.color565(r, g, b); }

void uiInitColors() {
  C_BG    = rgb(6, 6, 8);
  C_INK   = rgb(205, 200, 192);
  C_DIM   = rgb(120, 116, 112);
  C_FAINT = rgb(70, 66, 64);
  C_LINE  = rgb(42, 38, 38);
  C_ACC   = rgb(165, 28, 28);
  C_ACC2  = rgb(225, 70, 55);
  C_SEL   = rgb(40, 14, 14);
  C_WARN  = rgb(235, 90, 80);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
void drawText(const char* s, int x, int y, uint16_t color, int size, textdatum_t datum) {
  canvas.setFont(&fonts::Font0);
  canvas.setTextSize(size);
  canvas.setTextDatum(datum);
  canvas.setTextColor(color);
  canvas.drawString(s, x, y);
  canvas.setTextSize(1);
}

void drawHeader(const char* title, const char* right) {
  drawText(title, 4, 2, C_INK);
  if (right) drawText(right, SCREEN_W - 4, 2, C_DIM, 1, textdatum_t::top_right);
  canvas.drawFastHLine(0, 11, SCREEN_W, C_LINE);
}

void drawNav(const char* text) {
  canvas.drawFastHLine(0, 124, SCREEN_W, C_LINE);
  drawText(text, 2, 127, C_DIM);
}

void drawBar(int x, int y, int w, int h, float frac, uint16_t fill, uint16_t frame) {
  if (frac < 0.f) frac = 0.f;
  if (frac > 1.f) frac = 1.f;
  canvas.drawRect(x, y, w, h, frame);
  int fw = (int)((w - 2) * frac + 0.5f);
  if (fw > 0) canvas.fillRect(x + 1, y + 1, fw, h - 2, fill);
}

int stepFor(uint32_t heldMs) {
  if (heldMs < 1200) return 2;
  if (heldMs < 2500) return 4;
  return 8;
}

// ---------------------------------------------------------------------------
// Toast
// ---------------------------------------------------------------------------
static char g_toast[64];
static uint32_t g_toastUntil = 0;

void uiToast(const char* msg) {
  strncpy(g_toast, msg, sizeof(g_toast) - 1);
  g_toast[sizeof(g_toast) - 1] = 0;
  // two lines ("...\n..."): a longer message, shown a little longer (v0.99)
  g_toastUntil = millis() + (strchr(g_toast, '\n') ? 3000 : 1800);
}

void drawToast() {
  if (millis() >= g_toastUntil) return;
  char l1[64], l2[64] = "";
  strncpy(l1, g_toast, sizeof(l1) - 1);
  l1[sizeof(l1) - 1] = 0;
  if (char* nl = strchr(l1, '\n')) { *nl = 0; strncpy(l2, nl + 1, sizeof(l2) - 1); l2[sizeof(l2) - 1] = 0; }
  const int n = (int)std::max(strlen(l1), strlen(l2));
  int w = n * 6 + 12;
  if (w > SCREEN_W - 8) w = SCREEN_W - 8;
  const int hh = l2[0] ? 25 : 15;
  int x = (SCREEN_W - w) / 2, y = 119 - hh;
  canvas.fillRect(x, y, w, hh, C_SEL);
  canvas.drawRect(x, y, w, hh, C_ACC);
  drawText(l1, SCREEN_W / 2, y + 4, C_INK, 1, textdatum_t::top_center);
  if (l2[0]) drawText(l2, SCREEN_W / 2, y + 14, C_INK, 1, textdatum_t::top_center);
}

// ---------------------------------------------------------------------------
// Text input modal (scene name)
// ---------------------------------------------------------------------------
static TextPurpose g_txtPurpose = TXT_NONE;
static char g_txt[20];
static int g_txtLen = 0;

void openTextInput(TextPurpose p, const char* initial) {
  g_txtPurpose = p;
  g_txtLen = 0;
  if (initial) {
    g_txtLen = strlen(initial);
    if (g_txtLen > 18) g_txtLen = 18;
    memcpy(g_txt, initial, g_txtLen);
  }
  g_txt[g_txtLen] = 0;
}
bool textInputActive() { return g_txtPurpose != TXT_NONE; }

void drawTextInput() {
  canvas.fillRect(16, 40, SCREEN_W - 32, 52, C_BG);
  canvas.drawRect(16, 40, SCREEN_W - 32, 52, C_ACC);
  drawText("Save scene as:", 24, 46, C_DIM);
  char b[24];
  snprintf(b, sizeof(b), "%s%s", g_txt, (millis() / 400) % 2 ? "_" : " ");
  drawText(b, 24, 60, C_INK);
  drawText("[Enter] Save  [Esc] Cancel", 24, 78, C_DIM);
}

void keyTextInput(const KeyEv& ev) {
  if (ev.esc) { g_txtPurpose = TXT_NONE; return; }
  if (ev.del) { if (g_txtLen > 0) g_txt[--g_txtLen] = 0; return; }
  if (ev.enter) {
    if (g_txtPurpose == TXT_SAVE_AS) {
      if (appSaveSceneAs(String(g_txt))) uiToast("Scene saved");
      else uiToast(gSdReady ? "Name not allowed" : "No SD card");
    }
    g_txtPurpose = TXT_NONE;
    return;
  }
  char c = ev.space ? ' ' : ev.c;
  bool okc = isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_';
  if (okc && g_txtLen < 18) { g_txt[g_txtLen++] = c; g_txt[g_txtLen] = 0; }
}

// ---------------------------------------------------------------------------
// Confirm modal (delete custom scene)
// ---------------------------------------------------------------------------
static bool g_confirm = false;
static char g_confirmMsg[40];
static int g_confirmTarget = -1;
static void (*g_confirmFn)() = nullptr;   // v0.4: any action (SETTINGS' Reset, Recalibrate)

void openConfirm(const char* msg, int target) {
  g_confirm = true;
  strncpy(g_confirmMsg, msg, sizeof(g_confirmMsg) - 1);
  g_confirmMsg[sizeof(g_confirmMsg) - 1] = 0;
  g_confirmTarget = target;
  g_confirmFn = nullptr;
}
void openConfirmFn(const char* msg, void (*fn)()) {
  openConfirm(msg, -1);
  g_confirmFn = fn;
}
bool confirmActive() { return g_confirm; }
void drawConfirm() {
  canvas.fillRect(16, 44, SCREEN_W - 32, 44, C_BG);
  canvas.drawRect(16, 44, SCREEN_W - 32, 44, C_ACC);
  drawText(g_confirmMsg, SCREEN_W / 2, 52, C_INK, 1, textdatum_t::top_center);
  drawText("[Enter] Yes   [Esc] No", SCREEN_W / 2, 72, C_DIM, 1, textdatum_t::top_center);
}
void keyConfirm(const KeyEv& ev) {
  if (ev.enter) {
    g_confirm = false;
    if (g_confirmFn) { void (*fn)() = g_confirmFn; g_confirmFn = nullptr; fn(); }
    else if (appDeleteScene(g_confirmTarget)) uiToast("Deleted");
  } else if (ev.esc || ev.k == 'n') {
    g_confirm = false;
  }
}

// ---------------------------------------------------------------------------
// Key input (Enso's reader + Shift normalisation)
// ---------------------------------------------------------------------------
static char baseKey(char c) {
  switch (c) {
    case ':': return ';';
    case '>': return '.';
    case '<': return ',';
    case '?': return '/';
    case '~': return '`';
    default: break;
  }
  if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
  return c;
}

// True when an event should be handled this frame: a fresh press, or an
// auto-repeat of an adjust / move key.
// A key "held" this long is taken to be stuck: on the ADV the keyboard library
// tracks held keys from the TCA8418's press / release events, and if a release
// event is ever lost (event FIFO overflow while the UI was busy, I2C glitch)
// the key stays "down" forever -- which auto-repeated "," / "/" and kept
// switching moods on its own. A stuck key is ignored until it is seen released
// or a different key combination appears. Normal holds (a full volume / DREAD
// sweep) take about 3 s.
static constexpr uint32_t KEY_STUCK_MS = 8000;

static bool readKey(KeyEv& ev) {
  static int lastSig = 0, lastCount = 0, stuckSig = 0;
  static uint32_t heldSince = 0, lastRepeat = 0;
  ev.heldMs = 0;
  if (!M5Cardputer.Keyboard.isPressed()) { lastSig = 0; lastCount = 0; stuckSig = 0; return false; }
  Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
  ev.c = st.word.empty() ? 0 : st.word[0];
  ev.k = baseKey(ev.c);
  ev.enter = st.enter; ev.tab = st.tab; ev.space = st.space; ev.del = st.del;
  ev.shift = st.shift; ev.fn = st.fn;
  ev.esc = ev.k == '`' || ev.c == 27;
  int sig = (unsigned char)ev.c | (ev.enter << 8) | (ev.tab << 9) | (ev.space << 10) | (ev.del << 11)
          | (ev.shift << 12) | (ev.fn << 13);
  int count = (int)st.word.size() + ev.enter + ev.tab + ev.space + ev.del + ev.shift + ev.fn;
  // Releasing one key of a combo (e.g. Shift before S) changes the signature
  // but is not a new press: only a growing key count counts as one.
  if (count < lastCount) { lastSig = sig; lastCount = count; return false; }
  lastCount = count;
  if (ev.c == 0 && !ev.enter && !ev.tab && !ev.space && !ev.del) {   // modifiers alone
    lastSig = sig;
    return false;
  }
  uint32_t now = millis();
  if (sig != lastSig) { lastSig = sig; heldSince = now; lastRepeat = now; stuckSig = 0; return true; }
  ev.heldMs = now - heldSince;
  if (sig == stuckSig) return false;
  if (ev.heldMs > KEY_STUCK_MS) { stuckSig = sig; return false; }
  bool repeatable = ev.k == ',' || ev.k == '/' || ev.k == ';' || ev.k == '.' || ev.del;
  if (repeatable && ev.heldMs > KEY_REPEAT_DELAY_MS && now - lastRepeat > KEY_REPEAT_RATE_MS) {
    lastRepeat = now;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Display power (spec 7.2)
// ---------------------------------------------------------------------------
static bool g_displayOn = true;
// Enso v0.84 bug: a two-key combo (Shift+S) is not released atomically. Each
// key's release edge lands a few ms apart and the reader sees the remaining
// key as a fresh press -- which woke the display straight back up after
// Shift+S had turned it off. With this guard set, a keyboard press can only
// wake the display after the keyboard has been seen completely released
// once. G0 is a separate button and always wakes immediately.
static bool displayWakeGuard = false;
// v0.4 (SETTINGS > Display): dimmed after Dim After, off after Auto Off of no
// key / G0. Both wake with any key or G0 (Wake Key: the key only wakes, or
// also does what it does).
static bool g_dimmed = false;
static uint32_t g_lastActivity = 0;

static int effectiveBrightness() {
  int b = g_dimmed ? gSet.brightness * gSet.dimLevel / 100 : gSet.brightness;
  return b < 1 ? 1 : b;
}
static void applyBrightness() { M5Cardputer.Display.setBrightness((uint8_t)(effectiveBrightness() * 255 / 100)); }

void uiDisplayOff() {
  if (!g_displayOn) return;
  g_displayOn = false;
  g_dimmed = false;
  displayWakeGuard = true;
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.sleep();
}

// Cursed Mode hooks: override the backlight / invert without touching the
// real display-off state (so the wake guard and G0 keep working as usual)
bool uiDisplayIsOn() { return g_displayOn; }
// Party Mode (Cursed): only with MAIN on screen and nothing in the way
bool uiPartyScreenOk() {
  return gScreen == SCR_MAIN && !gHelp && !confirmActive() && !textInputActive() &&
         !cursedConsentActive() && !exportUiActive() && !seedEntryActive();
}

void uiCursedBrightness(int percent) {
  if (!g_displayOn) return;
  int p = percent < 0 ? effectiveBrightness() : percent;
  M5Cardputer.Display.setBrightness((uint8_t)(p * 255 / 100));
}
void uiCursedInvert(bool on) {
  if (!g_displayOn) return;
  M5Cardputer.Display.invertDisplay(on);
}

// ---------------------------------------------------------------------------
// Cursed Mode consent (shown when turning it on from Off)
// ---------------------------------------------------------------------------
static bool g_consent = false;
static uint8_t g_consentLevel = 1;
static bool g_listenConsent = false;   // the same modal, for Listen (spec v1.1, 5.5)

void openCursedConsent(uint8_t level) { g_consent = true; g_consentLevel = level; }
void openListenConsent() { g_consent = true; g_listenConsent = true; }
bool cursedConsentActive() { return g_consent; }

static void drawListenConsent() {
  canvas.fillRect(6, 14, SCREEN_W - 12, 108, C_BG);
  canvas.drawRect(6, 14, SCREEN_W - 12, 108, C_ACC2);
  drawText("LISTEN", SCREEN_W / 2, 20, C_ACC2, 1, textdatum_t::top_center);
  drawText("The microphone will measure how", 14, 36, C_INK);
  drawText("loud the room is.", 14, 46, C_INK);
  drawText("Sound is NOT recorded or saved:", 14, 60, C_DIM);
  drawText("only the level is used. Uses a", 14, 70, C_DIM);
  drawText("little more battery.", 14, 80, C_DIM);
  drawText("[Enter] Turn on   [Esc] Cancel", SCREEN_W / 2, 108, C_INK, 1, textdatum_t::top_center);
}

void drawCursedConsent() {
  if (g_listenConsent) { drawListenConsent(); return; }
  canvas.fillRect(6, 14, SCREEN_W - 12, 108, C_BG);
  canvas.drawRect(6, 14, SCREEN_W - 12, 108, C_ACC2);
  drawText("CURSED MODE", SCREEN_W / 2, 20, C_ACC2, 1, textdatum_t::top_center);
  drawText("While it is on, strange things", 14, 36, C_INK);
  drawText("will happen without warning:", 14, 46, C_INK);
  drawText("moods change by themselves, the", 14, 60, C_DIM);
  drawText("screen or sound may cut out...", 14, 70, C_DIM);
  drawText("Nothing is deleted. It stays on", 14, 84, C_DIM);
  drawText("until you turn it off in SETTINGS.", 14, 94, C_DIM);
  drawText("[Enter] I accept   [Esc] Cancel", SCREEN_W / 2, 108, C_INK, 1, textdatum_t::top_center);
}
void keyCursedConsent(const KeyEv& ev) {
  if (g_listenConsent) {
    if (ev.enter) {
      uiToast(listenEnable(true) ? "Listening..." : "Mic not available");
      appSettingsChanged();
      g_consent = g_listenConsent = false;
    } else if (ev.esc) {
      g_consent = g_listenConsent = false;
    }
    return;
  }
  if (ev.enter) {
    gSet.cursed = g_consentLevel;
    appSettingsChanged();
    uiToast("...so be it.");
    g_consent = false;
  } else if (ev.esc) {
    g_consent = false;
  }
}

static void displayWake() {
  g_displayOn = true;
  g_dimmed = false;
  displayWakeGuard = false;
  g_lastActivity = millis();
  M5Cardputer.Display.wakeup();
  applyBrightness();
}

// Full brightness again (an export has finished: the result should be seen)
void uiWakeDisplay() {
  g_lastActivity = millis();
  if (!g_displayOn) displayWake();
  else if (g_dimmed) { g_dimmed = false; applyBrightness(); }
}

// ---------------------------------------------------------------------------
// Shake (IMU), Enso's detector: leaky energy integrator + one-shot arming
// ---------------------------------------------------------------------------
static float g_shakeEnergy = 0.f;
static bool  g_shakeArmed = true;   // cleared on trigger, re-armed once the device settles
static uint32_t g_lastShake = 0;

// Listen (v0.292): the device being picked up or put down makes the mic hear
// the hands and the table (v0.29 run: every Hush without an event that the
// user marked false came from moving it). Movement is seen by the IMU, against
// a slow baseline so that steady vibration (the speaker at high volume) does
// not count.
static constexpr float MOVE_JOLT_G = 0.04f;    // over the baseline, smoothed over ~0.1 s
static constexpr float MOVE_GYRO_DPS = 15.f;   // rotation, over the baseline
static float g_joltFast = 0.f, g_joltBase = 0.f, g_gyroFast = 0.f, g_gyroBase = 0.f;

static void updateMovement(float dt, float jolt) {
  float gx = 0, gy = 0, gz = 0;
  float gyro = M5.Imu.getGyro(&gx, &gy, &gz) ? sqrtf(gx * gx + gy * gy + gz * gz) : 0.f;
  const float fa = 1.f - expf(-dt / 0.1f);
  g_joltFast += (jolt - g_joltFast) * fa;
  g_gyroFast += (gyro - g_gyroFast) * fa;
  const bool moving = g_joltFast > g_joltBase + MOVE_JOLT_G || g_gyroFast > g_gyroBase + MOVE_GYRO_DPS;
  if (moving) listenNoteMove();
  // The baseline follows in 3 s while the device rests, and in 30 s while it
  // seems to move -- so a vibration that never stops cannot hold Listen off
  const float sa = 1.f - expf(-dt / (moving ? 30.f : 3.f));
  g_joltBase += (g_joltFast - g_joltBase) * sa;
  g_gyroBase += (g_gyroFast - g_gyroBase) * sa;
}

// Shakes (and Fn+S) are not taken while exporting, while falling
// asleep, or while a dialog is open (spec v1.22 1.10). Recording, a dimmed or
// switched-off display: taken (the display is not woken).
static bool shakeBlocked() {
  return exportBusy() || appSleepFading() || powerSleeping() ||
         confirmActive() || textInputActive() || cursedConsentActive();
}
static void runShake(uint8_t action) {
  if (partyActive()) { partyFill(); return; }   // at a party a shake is a fill, whatever the action
  if (shakeBlocked() || action == SHAKE_OFF) return;
  appShakeRun(action);
}

// The double shake of spec v1.22 1.2 was removed in v0.45: two natural shakes
// run into one another (the hand does not stop in between), and neither a
// window from the trigger (v0.43) nor one from coming to rest (v0.44) caught
// them on the device.

static void updateShake(float dt) {
  if (planchetteActive() || poltergeistOwnsAudio()) { g_shakeEnergy = 0.f; return; }   // the IMU is theirs
  const bool shake = gSet.shakeAction != SHAKE_OFF;
  if ((!shake && !gSet.listen) || !gImuReady) { g_shakeEnergy = 0.f; return; }
  float ax = 0, ay = 0, az = 0;
  M5.Imu.update();
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  float jolt = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  if (gSet.listen) updateMovement(dt, jolt);
  if (!shake) { g_shakeEnergy = 0.f; return; }
  g_shakeEnergy += jolt * dt * 12.f;
  g_shakeEnergy -= g_shakeEnergy * dt * 4.f;
  float thresh = 1.6f - 1.2f * (gSet.shakeSens * 0.01f);
  uint32_t now = millis();
  // A time debounce alone fired several times per shake; one shake = one action.
  if (g_shakeArmed && g_shakeEnergy > thresh) {
    g_lastShake = now;
    g_shakeArmed = false;
    g_shakeEnergy = 0.f;
    runShake(gSet.shakeAction);
    return;
  }
  // re-armed once the device settles
  if (!g_shakeArmed && g_shakeEnergy < thresh * 0.2f && now - g_lastShake > 1000) g_shakeArmed = true;
}

// ---------------------------------------------------------------------------
// Key dispatch
// ---------------------------------------------------------------------------
static void toggleRec() {
  if (recActive()) {
    if (recCapturing()) { recStop(); uiToast("REC stopped"); }
    return;
  }
  if (!gSdReady) { uiToast("No SD card"); return; }
  if (recStart(appMoodTag())) uiToast("REC started");
  else uiToast("REC failed");
}

static void handleKey(const KeyEv& ev) {
  if (cursedConsentActive()) { keyCursedConsent(ev); return; }
  if (textInputActive()) { keyTextInput(ev); return; }
  if (confirmActive()) { keyConfirm(ev); return; }
  if (exportUiActive()) { keyExport(ev); return; }
  if (gHelp) { keyHelp(ev); return; }

  // Party Mode owns the keys while it lasts (one's own party: REC still works);
  // the reveal afterwards is closed by any key, which does nothing else
  if (partyRevealActive()) { partyRevealClose(); return; }
  // Poltergeist: its reveal and report close with any key; the countdown takes Esc only
  if (poltergeistStage() == PG_REVEAL || poltergeistStage() == PG_REPORT) { poltergeistCloseScreen(); return; }
  if (poltergeistStage() == PG_COUNTDOWN) { if (ev.esc) { poltergeistCancel(); pgPageOpenSet(true); } return; }
  if (poltergeistOwnsAudio()) return;   // settling, lurking, a test: nothing
  if (pgPageOpen()) { keyPgPage(ev); return; }
  if (partyActive()) {
    if (partyManual() && ev.shift && ev.k == 'r') toggleRec();
    else keyParty(ev);
    return;
  }
  // PLANCHETTE owns the keys too; the sleep timer and REC still work
  if (planchetteActive()) {
    if (ev.shift && ev.k == 's') { if (!appSleepFading()) appShakeRun(SHAKE_SLEEP); }
    else if (ev.shift && ev.k == 'r') toggleRec();
    else keyPlanchette(ev);
    return;
  }
  if (ev.space) { appTogglePlay(); return; }
  if (ev.shift && ev.k == 'r') { toggleRec(); return; }
  if (!ev.fn && !ev.shift && ev.k == 'g' && gScreen == SCR_MAIN) { pgPageOpenSet(true); return; }   // Poltergeist (v0.9)
  if (!ev.fn && !ev.shift && ev.k == 'p' && gScreen == SCR_MAIN) {   // PLANCHETTE (v0.8)
    const char* why = planchetteEnter();
    if (why && *why) uiToast(why);
    return;
  }
  if (ev.fn && ev.k == 'p' && gScreen == SCR_MAIN) {   // (not in HELP / README)
    const char* why = partyStartManual();
    if (why && *why) uiToast(why);
    return;
  }
  // v0.43: Shift+S is the Sleep Timer on/off, whatever the Shake Action;
  // the shake moved to Fn+S
  if (ev.shift && ev.k == 's') { if (!appSleepFading()) appShakeRun(SHAKE_SLEEP); return; }
  if (ev.fn && ev.k == 's') {
    if (gSet.shakeAction == SHAKE_OFF) uiToast("Shake Action is Off (SETTINGS)");
    else runShake(gSet.shakeAction);
    return;
  }
  if (ev.shift && ev.k == 'l') { listenMark(true); return; }    // Listen log: that Hush was wrong
  if (ev.shift && ev.k == 'k') { listenMark(false); return; }   // Listen log: it did not react
  if (ev.tab) { gScreen = (Screen)((gScreen + 1) % NUM_SCREENS); return; }

  // Screen-specific keys first (MAIN's seed entry, EDIT's sub-view, ...)
  bool used = false;
  switch (gScreen) {
    case SCR_MAIN:     used = keyMain(ev); break;
    case SCR_MOOD:     used = keyMood(ev); break;
    case SCR_EDIT:     used = keyEdit(ev); break;
    case SCR_EXPORT:   used = keyExport(ev); break;
    case SCR_SETTINGS: used = keySettings(ev); break;
    default: break;
  }
  if (used) return;

  if (ev.esc) { gScreen = SCR_MAIN; return; }
  if (ev.shift || ev.fn) return;
  switch (ev.k) {
    case 'h': gHelp = true; break;
    case 'm': gScreen = SCR_MOOD; break;
    case 'e': gScreen = SCR_EDIT; break;
    case 'x': gScreen = SCR_EXPORT; break;
    case 's': gScreen = SCR_SETTINGS; break;
    case 'r':
      appRandomScene();
      uiToast(gSet.seedLock ? "Unknown (seed locked)" : "Unknown");
      break;
    default: break;
  }
}

// The start screen is ui_splash.cpp (v0.98); until it starts (the SD card and
// the settings are read first) the screen stays dark
void uiInit() {
  canvas.setColorDepth(16);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  uiInitColors();
  canvas.fillScreen(C_BG);
  canvas.pushSprite(0, 0);
}

static void render() {
  canvas.fillScreen(C_BG);
  if (gHelp || gScreen != SCR_MAIN || partyOwnsScreen() || planchetteActive() || pgPageOpen() ||
      poltergeistStage() != PG_OFF) vizIdle();
  const PgStage pg = poltergeistStage();
  if (pg == PG_REVEAL) {
    drawPgReveal();
  } else if (pg == PG_REPORT) {
    drawPgReport();
  } else if (pg == PG_COUNTDOWN) {
    drawPgCountdown();
  } else if (pgPageOpen() && !partyOwnsScreen()) {
    drawPgPage();
  } else if (partyOwnsScreen()) {
    drawParty();
  } else if (planchetteActive()) {
    if (exportUiActive()) drawExport();   // Shift+X: its progress and result
    else drawPlanchette();
  } else if (gHelp) {
    drawHelp();
  } else {
    switch (gScreen) {
      case SCR_MAIN:     drawMain(); break;
      case SCR_MOOD:     drawMood(); break;
      case SCR_EDIT:     drawEdit(); break;
      case SCR_EXPORT:   drawExport(); break;
      case SCR_SETTINGS: drawSettings(); break;
      default: break;
    }
  }
  if (partyRevealActive()) drawPartyReveal();
  if (textInputActive()) drawTextInput();
  if (confirmActive()) drawConfirm();
  if (cursedConsentActive()) drawCursedConsent();
  drawToast();
  canvas.pushSprite(0, 0);
}

// The start screen was skipped with a key: that key does nothing, and nothing
// is taken until the keyboard has been released (v0.98)
static bool g_keyGuard = false;
void uiKeyGuard() { g_keyGuard = true; }

void uiLoop(float dt) {
  KeyEv ev = {};
  bool got = readKey(ev);
  if (g_keyGuard) {
    if (M5Cardputer.Keyboard.isPressed()) got = false;
    else g_keyGuard = false;
  }
  bool g0 = M5Cardputer.BtnA.wasPressed();
  // Listen: the mic hears the keyboard, so typing must not cause a Hush
  if (got || g0 || M5Cardputer.Keyboard.isPressed()) listenNoteKey();

  const uint32_t now = millis();
  static bool bootToast = false;   // "Last Poltergeist: ..." after an Auto Stop
  if (!bootToast) { bootToast = true; if (const char* t = poltergeistBootToast()) uiToast(t); }
  static uint32_t escSince = 0;   // Poltergeist: Esc held since (0 = not held)
  if (poltergeistLurking()) {
    const bool cd = poltergeistStage() == PG_COUNTDOWN;
    // v0.95: the hold is timed here -- readKey() reports a non-repeating key
    // like Esc only once, on its press, so "Esc held 2 s" never came through
    bool escDown = false;
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
      for (char c : st.word) if (baseKey(c) == '`' || c == 27) escDown = true;
    }
    if (!escDown) escSince = 0;
    else if (!escSince) escSince = now | 1;
    const uint32_t escHeld = escSince ? now - escSince : 0;
    if (g0 || (cd && got && ev.esc) || (!cd && escHeld >= 2000)) {
      if (cd) { poltergeistCancel(); pgPageOpenSet(true); }
      else poltergeistStop(PGE_MANUAL);
      escSince = 0;
    }
    got = false; g0 = false;   // nothing else: no display, no key
  } else {
    escSince = 0;
  }
  if (!g_displayOn) {
    // Only a press that follows a full release may wake the display (guard)
    if (displayWakeGuard && !M5Cardputer.Keyboard.isPressed()) displayWakeGuard = false;
    if (g0 || (got && !displayWakeGuard)) {
      displayWake();
      if (got && gSet.wakeAct) handleKey(ev);   // Wake Key: Wake + Act
    }
  } else if (g_dimmed && (got || g0)) {
    g_dimmed = false;                           // dimmed: back to full brightness
    applyBrightness();
    g_lastActivity = now;
    if (got && gSet.wakeAct) handleKey(ev);     // G0 only wakes
  } else {
    if (got || g0) g_lastActivity = now;
    if (g0) uiDisplayOff();
    else if (got) handleKey(ev);
  }

  // Dim / Auto Off. The timers stand still while a dialog is open.
  // (and not at a party)
  if (confirmActive() || textInputActive() || cursedConsentActive() || partyActive() || partyRevealActive() || planchetteActive() || pgPageOpen() || poltergeistStage() != PG_OFF) g_lastActivity = now;
  if (g_displayOn) {
    const uint32_t idle = now - g_lastActivity;
    const uint32_t offMs = (uint32_t)AUTO_OFF_SEC[gSet.autoOffIdx] * 1000u;
    const uint32_t dimMs = (uint32_t)DIM_AFTER_SEC[gSet.dimIdx] * 1000u;
    if (offMs && idle >= offMs) {
      uiDisplayOff();
      displayWakeGuard = false;   // no key combo is being released: any key wakes
    } else {
      const bool d = dimMs && idle >= dimMs;
      if (d != g_dimmed) { g_dimmed = d; applyBrightness(); }
    }
  }

  // Screen changed (by any key): EDIT and SETTINGS come back on their first page
  static Screen lastScreen = SCR_MAIN;
  if (gScreen != lastScreen) {
    if (lastScreen == SCR_EDIT) editResetView();
    if (lastScreen == SCR_SETTINGS) settingsResetView();
    lastScreen = gScreen;
  }

  updateShake(dt);
  exportUiPoll();
  cursedUpdate(dt);
  if (partyActive()) partyLedUpdate();   // the party has the LED
  else watcherUpdate(dt);                // the eye and its LED (v0.6)

  static int lastBright = -1;
  if (g_displayOn && lastBright != effectiveBrightness()) {
    applyBrightness();
    lastBright = effectiveBrightness();
  }
  if (!g_displayOn) lastBright = -1;

  static uint32_t lastRender = 0;
  // v0.42: a frame every ~66 ms (spec v1.22 4.4: too many pushSprite calls
  // made crackles in C.P.S.)
  if (g_displayOn && now - lastRender >= 66) { lastRender = now; render(); }
  poltergeistIdleSleep();   // Poltergeist, Shy off, between events: 0.2 s of light sleep
}
