// MIASMORA — Poltergeist's screens (phase 8, spec v1.24 2 / 3): the settings
// page (MAIN G), the countdown, the reveal (Watcher's eyes, "You found me.")
// and the report. While it lurks the display stays dark and the keys do
// nothing but Esc held 2 s (ui.cpp).
#include "ui_internal.h"
#include "../poltergeist.h"
#include "../party.h"

static bool g_page = false;
static int g_cur = 0, g_scroll = 0;

enum PgRow : uint8_t { R_EV1 = 0, R_MODE1, R_EV2, R_MODE2, R_EV3, R_MODE3, R_TIMING, R_FIX, R_MIN, R_MAX, R_ESC,
                       R_DELAY, R_STOP, R_VOL, R_ROOM, R_SHY, R_FOUND, R_TEST, R_START, R_N };

bool pgPageOpen() { return g_page; }
void pgPageOpenSet(bool on) { g_page = on; }

static int slotOf(int r) { return (r == R_EV1 || r == R_MODE1) ? 0 : ((r == R_EV2 || r == R_MODE2) ? 1 : 2); }
static bool visibleRow(int r) {
  if (r == R_MODE1 || r == R_MODE2 || r == R_MODE3) return pgModeParam(pgLayerAt(gSet.pgEv[slotOf(r)])) >= 0;
  if (r == R_FIX) return gSet.pgTiming == 0;
  if (r == R_MIN || r == R_MAX) return gSet.pgTiming == 1;
  return true;
}
static void fmtInterval(char* v, int n, int idx) {
  const int s = PG_INTERVALS[idx % PG_NUM_INTERVALS];
  if (s < 60) snprintf(v, n, "%d s", s);
  else if (s % 60) snprintf(v, n, "%d.%d min", s / 60, (s % 60) / 6);
  else snprintf(v, n, "%d min", s / 60);
}
static const char* label(int r) {
  static const char* const L[R_N] = {"Event 1", "  Mode", "Event 2", "  Mode", "Event 3", "  Mode", "Timing", "Interval",
                                     "Interval Min", "Interval Max", "Escalate", "Start Delay", "Auto Stop", "Volume", "Room",
                                     "Shy", "When Found", "", ""};
  return L[r];
}
static const char* desc(int r) {
  switch (r) {
    case R_EV1: case R_EV2: case R_EV3: return "A sound it makes now and then";
    case R_MODE1: case R_MODE2: case R_MODE3: return "Which kind of it";
    case R_TIMING: return "Fixed gaps, or never the same";
    case R_FIX: return "The gap (varies a little)";
    case R_MIN: return "The shortest gap";
    case R_MAX: return "The longest gap";
    case R_ESC: return "The gaps shrink to 40 % by the end";
    case R_DELAY: return "Time to hide it after Start";
    case R_STOP: return "It stops by itself after this";
    case R_VOL: return "Within the device's Volume";
    case R_ROOM: return "Reverb on its sounds";
    case R_SHY: return gSet.pgShy ? "Quiet for a while when it hears people" : "Plays whatever happens around it";
    // v0.991: the choices are listed here, so the locked one is seen (it is
    // never shown as the value: it cannot be chosen)
    case R_FOUND: return !gImuReady ? "Needs the IMU" : (partyUnlocked() ? "Reveal / Silent / Party" : "Reveal / Silent / ???");
    case R_TEST: return "Play one of them now";
    case R_START: return "Hide it and let it haunt";
  }
  return "";
}
static void value(int r, char* v, int n) {
  v[0] = 0;
  switch (r) {
    case R_EV1: case R_EV2: case R_EV3: {
      const int id = pgLayerAt(gSet.pgEv[slotOf(r)]);
      snprintf(v, n, "%s", id < 0 ? "None" : LAYER_DEFS[id].name);
      break;
    }
    case R_MODE1: case R_MODE2: case R_MODE3: {
      const int k = slotOf(r), id = pgLayerAt(gSet.pgEv[k]), mk = pgModeParam(id);
      if (mk >= 0) snprintf(v, n, "%s", LAYER_DEFS[id].p[mk].opts[gSet.pgMode[k] % LAYER_DEFS[id].p[mk].nOpts]);
      break;
    }
    case R_TIMING: snprintf(v, n, "%s", gSet.pgTiming ? "Random" : "Fixed"); break;
    case R_FIX: fmtInterval(v, n, gSet.pgFixIdx); break;
    case R_MIN: fmtInterval(v, n, gSet.pgMinIdx); break;
    case R_MAX: fmtInterval(v, n, gSet.pgMaxIdx); break;
    case R_ESC: snprintf(v, n, "%s", gSet.pgEscalate ? "On" : "Off"); break;
    case R_DELAY: snprintf(v, n, "%d min", PG_DELAY_MIN[gSet.pgDelayIdx % PG_NUM_DELAYS]); break;
    case R_STOP: {
      const int m = PG_STOP_MIN[gSet.pgStopIdx % PG_NUM_STOPS];
      if (m < 60) snprintf(v, n, "%d min", m); else snprintf(v, n, "%d h", m / 60);
      break;
    }
    case R_VOL: snprintf(v, n, "%d", gSet.pgVolume); break;
    case R_ROOM: { static const char* const RM[3] = {"Dry", "Small", "Large"}; snprintf(v, n, "%s", RM[gSet.pgRoom % 3]); break; }
    case R_SHY: snprintf(v, n, "%s", gSet.pgShy ? "On" : "Off"); break;
    case R_FOUND: {
      // v0.99: Party stays "???" until a party has happened once
      static const char* const F[3] = {"Party", "Reveal", "Silent"};
      const int f = gSet.pgFound % 3;
      snprintf(v, n, "%s", !gImuReady ? "n/a" : (f == 0 && !partyUnlocked()) ? "???" : F[f]);
      break;
    }
    case R_TEST: snprintf(v, n, "[ Test ]"); break;
    case R_START: snprintf(v, n, "[ Start ]"); break;
  }
}

void drawPgPage() {
  drawHeader("POLTERGEIST");
  int rows[R_N], nRows = 0, curPos = 0;
  if (!visibleRow(g_cur)) g_cur = R_START;
  for (int r = 0; r < R_N; r++) if (visibleRow(r)) { if (r == g_cur) curPos = nRows; rows[nRows++] = r; }
  const int VIS = 7, ROW = 13, TOP = 15;
  if (curPos < g_scroll) g_scroll = curPos;
  if (curPos >= g_scroll + VIS) g_scroll = curPos - VIS + 1;
  char v[32];
  for (int k = 0; k < VIS && g_scroll + k < nRows; k++) {
    const int r = rows[g_scroll + k], y = TOP + k * ROW;
    const bool sel = r == g_cur;
    if (sel) canvas.fillRect(0, y - 1, SCREEN_W, ROW - 1, C_SEL);
    value(r, v, sizeof(v));
    if (r == R_TEST || r == R_START) {
      drawText(v, SCREEN_W / 2, y + 1, sel ? C_ACC2 : C_INK, 1, textdatum_t::top_center);
    } else {
      drawText(label(r), 6, y + 1, sel ? C_ACC2 : C_INK);
      char vv[40];
      snprintf(vv, sizeof(vv), sel ? "< %s >" : "%s", v);
      drawText(vv, 112, y + 1, C_INK);
    }
  }
  if (g_scroll > 0) drawText("^", SCREEN_W - 4, TOP, C_DIM, 1, textdatum_t::top_right);
  if (g_scroll + VIS < nRows) drawText("v", SCREEN_W - 4, TOP + (VIS - 1) * ROW, C_DIM, 1, textdatum_t::top_right);
  drawText(desc(g_cur), 4, TOP + VIS * ROW + 1, C_DIM);
  drawNav("[;.]Sel [,/]Change [Enter]Do [Esc]");
}

static void doStart() {
  poltergeistStart();
  g_page = false;
}

static void change(int r, int d) {
  const int nL = pgLayerCount();
  auto cyc = [&](uint8_t& x, int m) { x = (uint8_t)((x % m + d + m) % m); };
  switch (r) {
    case R_EV1: case R_EV2: case R_EV3: { const int k = slotOf(r); cyc(gSet.pgEv[k], nL); gSet.pgMode[k] = 0; break; }
    case R_MODE1: case R_MODE2: case R_MODE3: {
      const int k = slotOf(r), id = pgLayerAt(gSet.pgEv[k]), mk = pgModeParam(id);
      if (mk >= 0) cyc(gSet.pgMode[k], LAYER_DEFS[id].p[mk].nOpts);
      break;
    }
    case R_TIMING: gSet.pgTiming ^= 1; break;
    case R_FIX: gSet.pgFixIdx = (uint8_t)constrain((int)gSet.pgFixIdx + d, 0, PG_NUM_INTERVALS - 1); break;
    case R_MIN:
      gSet.pgMinIdx = (uint8_t)constrain((int)gSet.pgMinIdx + d, 0, PG_NUM_INTERVALS - 1);
      if (gSet.pgMaxIdx < gSet.pgMinIdx) gSet.pgMaxIdx = gSet.pgMinIdx;
      break;
    case R_MAX:
      gSet.pgMaxIdx = (uint8_t)constrain((int)gSet.pgMaxIdx + d, 0, PG_NUM_INTERVALS - 1);
      if (gSet.pgMinIdx > gSet.pgMaxIdx) gSet.pgMinIdx = gSet.pgMaxIdx;
      break;
    case R_ESC: gSet.pgEscalate = !gSet.pgEscalate; break;
    case R_DELAY: cyc(gSet.pgDelayIdx, PG_NUM_DELAYS); break;
    case R_STOP: cyc(gSet.pgStopIdx, PG_NUM_STOPS); break;   // never off (spec 2.2)
    case R_VOL: gSet.pgVolume = (uint8_t)constrain((int)gSet.pgVolume + d * 5, 0, 100); break;
    case R_ROOM: cyc(gSet.pgRoom, 3); break;
    case R_SHY: gSet.pgShy = !gSet.pgShy; break;
    case R_FOUND: {
      if (!gImuReady) break;
      // shown in the order Reveal, Silent, ??? / Party (stored: 0 Party, 1 Reveal, 2 Silent)
      static const uint8_t ORDER[3] = {1, 2, 0};
      int pos = 0;
      for (int k = 0; k < 3; k++) if (ORDER[k] == gSet.pgFound % 3) pos = k;
      const uint8_t next = ORDER[(pos + d + 3) % 3];
      if (next == 0 && !partyUnlocked()) { uiToast("Something hasn't happened yet..."); return; }
      gSet.pgFound = next;
      break;
    }
    default: return;
  }
  appSettingsChanged();
}

void keyPgPage(const KeyEv& ev) {
  if (ev.esc) { g_page = false; return; }
  auto step = [&](int dir) {
    int r = g_cur;
    for (int k = 0; k < R_N; k++) { r = (r + dir + R_N) % R_N; if (visibleRow(r)) break; }
    g_cur = r;
  };
  if (ev.k == ';') { step(-1); return; }
  if (ev.k == '.') { step(1); return; }
  if (ev.enter && g_cur == R_TEST) { const char* why = poltergeistTest(); if (why && *why) uiToast(why); return; }
  if (ev.enter && g_cur == R_START) {
    if (const char* why = poltergeistCanStart()) { uiToast(why); return; }
    static char m[48];
    snprintf(m, sizeof(m), "Start? Hide it within %d min.", PG_DELAY_MIN[gSet.pgDelayIdx % PG_NUM_DELAYS]);
    openConfirmFn(m, doStart);
    return;
  }
  if (ev.k == ',' || ev.k == '/' || ev.enter) change(g_cur, ev.k == ',' ? -1 : 1);
}

void drawPgCountdown() {
  drawHeader("POLTERGEIST");
  const int left = poltergeistCountdownLeft();
  char b[16];
  snprintf(b, sizeof(b), "%d:%02d", left / 60, left % 60);
  drawText(b, SCREEN_W / 2, 36, C_ACC2, 5, textdatum_t::top_center);
  drawText("Hide the device now.", SCREEN_W / 2, 88, C_INK, 1, textdatum_t::top_center);
  drawText("The screen goes dark at 0:10.", SCREEN_W / 2, 100, C_DIM, 1, textdatum_t::top_center);
  drawNav("[Esc] Cancel");
}

void drawPgReveal() {
  canvas.fillScreen(TFT_BLACK);
  watcherDraw();   // the eyes, forced open (ui_watcher.cpp)
  drawText("You found me.", SCREEN_W / 2, 108, C_ACC2, 2, textdatum_t::top_center);
}

void drawPgReport() {
  char lines[5][40];
  poltergeistReport(lines);
  drawHeader(lines[0]);
  for (int i = 1; i < 5; i++) drawText(lines[i], 8, 22 + (i - 1) * 16, C_INK, 1);
  drawText("Log: /MIASMORA/poltergeist_log.txt", 8, 92, C_DIM);
  drawNav("Any key: back");
}
