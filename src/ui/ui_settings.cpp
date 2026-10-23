// MIASMORA — SETTINGS (v0.4, spec v1.2 4 / v1.22 2-3)
//
// A list of categories; Enter opens one, Esc goes back. Values change with
// "," / "/" (held: in steps of 10 for numbers) or Enter; actions (Recalibrate,
// Reset Settings) ask first. The selected item is explained in one line at the
// bottom. System > Debug has its own pages: Cursed Trigger, Memory and
// Performance (and System > Storage shows the card).
#include "ui_internal.h"
#include <SD.h>
#include "../audio.h"
#include "../audio_io.h"
#include "../cursed.h"
#include "../listen.h"
#include "../arc.h"
#include "../memstat.h"
#include "../rec.h"
#include "../storage.h"

// --- items ---------------------------------------------------------------------
enum Item : uint8_t {
  I_VOLUME, I_SEEDLOCK, I_STARTUP, I_FADEIN, I_STARTSND,
  I_DRIFTSPEED, I_DWAITMIN, I_DWAITMAX, I_DXF, I_DCHMIN, I_DCHMAX, I_DLAYMIN, I_DLAYMAX, I_DPOOL, I_DMODE,
  I_SLEEPON, I_SLEEPTIME, I_SLEEPFADE, I_SLEEPFADELEN, I_SLEEPAFTER,
  I_BRIGHT, I_DIM, I_DIMLEVEL, I_AUTOOFF, I_WAKEKEY, I_VIZ, I_LED, I_LEDBR,
  I_SHAKE, I_SHAKESENS, I_PLMODE, I_PLRANGE, I_PLSMOOTH, I_PLCA, I_PLCB, I_PLCC, I_PLCD,
  I_LISTEN, I_LSENS, I_HUSHDEPTH, I_RESPONSE, I_WATCHER, I_RECAL,
  I_ARC, I_ARCLEN, I_ARCCYCLE, I_ARCSLEEP,
  I_CURSED, I_CFREQ,
  I_DEBUG, I_STORAGE, I_RESET, I_ABOUT,
  I_LOG, I_MARK, I_CTRIG, I_MEMORY, I_PERF,
};

static const Item IT_SOUND[]   = {I_VOLUME, I_SEEDLOCK, I_STARTUP, I_FADEIN, I_STARTSND};
static const Item IT_DRIFT[]   = {I_DRIFTSPEED, I_DWAITMIN, I_DWAITMAX, I_DXF, I_DCHMIN, I_DCHMAX,
                                  I_DLAYMIN, I_DLAYMAX, I_DPOOL, I_DMODE};
static const Item IT_SLEEP[]   = {I_SLEEPON, I_SLEEPTIME, I_SLEEPFADE, I_SLEEPFADELEN, I_SLEEPAFTER};
static const Item IT_DISPLAY[] = {I_BRIGHT, I_DIM, I_DIMLEVEL, I_AUTOOFF, I_WAKEKEY, I_VIZ, I_LED, I_LEDBR};
static const Item IT_MOTION[]  = {I_SHAKE, I_SHAKESENS, I_PLMODE, I_PLRANGE, I_PLSMOOTH, I_PLCA, I_PLCB, I_PLCC, I_PLCD};
static const Item IT_LISTEN[]  = {I_LISTEN, I_LSENS, I_HUSHDEPTH, I_RESPONSE, I_WATCHER, I_RECAL};
static const Item IT_ARC[]     = {I_ARC, I_ARCLEN, I_ARCCYCLE, I_ARCSLEEP};
static const Item IT_CURSED[]  = {I_CURSED, I_CFREQ};
static const Item IT_SYSTEM[]  = {I_DEBUG, I_STORAGE, I_RESET, I_ABOUT};
static const Item IT_DEBUG[]   = {I_LOG, I_MARK, I_CTRIG, I_MEMORY, I_PERF};

struct Cat { const char* name; const Item* items; int n; };
#define CAT(name, arr) {name, arr, (int)(sizeof(arr) / sizeof(arr[0]))}
static const Cat CATS[] = {
  CAT("Sound", IT_SOUND), CAT("Drift", IT_DRIFT), CAT("Sleep", IT_SLEEP), CAT("Display", IT_DISPLAY),
  CAT("Motion", IT_MOTION), CAT("Listen", IT_LISTEN), CAT("Arc", IT_ARC), CAT("Cursed", IT_CURSED),
  CAT("System", IT_SYSTEM),
};
static constexpr int NUM_CATS = sizeof(CATS) / sizeof(CATS[0]);
static const Cat DEBUG_CAT = CAT("Debug", IT_DEBUG);

static const char* const STARTUP_NAMES[2] = {"Stop", "Resume"};
static const char* const AFTER_NAMES[2] = {"Deep Sleep", "Display Off"};
static const char* const HUSH_NAMES[NUM_HUSH_DEPTHS] = {"Light", "Mid", "Deep"};
static const char* const SENS3_NAMES[3] = {"Low", "Mid", "High"};

static const char* label(Item it) {
  switch (it) {
    case I_VOLUME: return "Volume";        case I_SEEDLOCK: return "Seed Lock";
    case I_STARTUP: return "Startup";      case I_FADEIN: return "Fade In";
    case I_STARTSND: return "Startup Sound";
    case I_DRIFTSPEED: return "Speed";
    case I_DWAITMIN: return "Wait Min";    case I_DWAITMAX: return "Wait Max";
    case I_DXF: return "Transition";
    case I_DCHMIN: return "Changes Min";   case I_DCHMAX: return "Changes Max";
    case I_DLAYMIN: return "Layers Min";   case I_DLAYMAX: return "Layers Max";
    case I_DPOOL: return "Drift Pool";     case I_DMODE: return "Mode Change";
    case I_SLEEPON: return "Sleep Timer";  case I_SLEEPTIME: return "Time";
    case I_SLEEPFADE: return "Fade";       case I_SLEEPFADELEN: return "Fade Length";
    case I_SLEEPAFTER: return "After";
    case I_BRIGHT: return "Brightness";    case I_DIM: return "Dim After";
    case I_DIMLEVEL: return "Dim Level";   case I_AUTOOFF: return "Auto Off";
    case I_WAKEKEY: return "Wake Key";     case I_VIZ: return "Visualizer";
    case I_LED: return "LED";              case I_LEDBR: return "LED Brightness";
    case I_WATCHER: return "Watcher";
    case I_SHAKE: return "Shake Action";   case I_SHAKESENS: return "Shake Sens";
    case I_PLRANGE: return "Tilt Range";   case I_PLSMOOTH: return "Tilt Smoothing";
    case I_PLMODE: return "Tilt Mode";
    case I_PLCA: return "Corner A";        case I_PLCB: return "Corner B";
    case I_PLCC: return "Corner C";        case I_PLCD: return "Corner D";
    case I_LISTEN: return "Listen";        case I_LSENS: return "Sensitivity";
    case I_HUSHDEPTH: return "Hush Depth"; case I_RESPONSE: return "Response";
    case I_RECAL: return "Recalibrate";
    case I_ARC: return "Arc";              case I_ARCLEN: return "Arc Length";
    case I_ARCCYCLE: return "Arc Cycle";   case I_ARCSLEEP: return "Follows Sleep";
    case I_CURSED: return "Cursed Mode";   case I_CFREQ: return "Frequency";
    case I_DEBUG: return "Debug";          case I_STORAGE: return "Storage";
    case I_RESET: return "Reset Settings"; case I_ABOUT: return "About";
    case I_LOG: return "Listen Log";       case I_MARK: return "Mark Keys";
    case I_CTRIG: return "Cursed Trigger"; case I_MEMORY: return "Memory";
    case I_PERF: return "Performance";
  }
  return "";
}

static const char* desc(Item it) {
  switch (it) {
    case I_VOLUME: return "Output volume";
    case I_SEEDLOCK: return "Keep the seed when play starts";
    case I_STARTUP: return "At power-on: stopped, or play";
    case I_FADEIN: return "Fade-in when playback starts";
    case I_STARTSND: return "Heartbeat on the start screen";
    case I_DRIFTSPEED: return "How fast Drift changes the scene";
    case I_DWAITMIN: return "Shortest wait between changes";
    case I_DWAITMAX: return "Longest wait between changes";
    case I_DXF: return "How long one change takes";
    case I_DCHMIN: return "Fewest changes at a time";
    case I_DCHMAX: return "Most changes at a time";
    case I_DLAYMIN: return "Fewest layers sounding";
    case I_DLAYMAX: return "Most layers sounding";
    case I_DPOOL: return "Layers Drift may touch";
    case I_DMODE: return "Drift may change a mode";
    case I_SLEEPON: return "Stop playing after Time";
    case I_SLEEPTIME: return "Minutes (hold: steps of 10)";
    case I_SLEEPFADE: return "Fade out at the end";
    case I_SLEEPFADELEN: return "How long the fade-out takes";
    case I_SLEEPAFTER: return "Then: deep sleep, or display off";
    case I_BRIGHT: return "Screen brightness";
    case I_DIM: return "Dim the screen when idle";
    case I_DIMLEVEL: return "Dimmed: % of Brightness";
    case I_AUTOOFF: return "Screen off when idle";
    case I_WAKEKEY: return "First key: only wake, or also act";
    case I_VIZ: return "MAIN's visualiser (V on MAIN)";
    case I_LED: return "The RGB LED may light";
    case I_LEDBR: return "How bright the LED gets";
    case I_WATCHER: return "An eye opens on a Hush";
    case I_SHAKE: return gImuReady ? "What a shake (Fn+S) does" : "No IMU: use Fn+S";
    case I_SHAKESENS: return "How hard you have to shake";
    case I_PLRANGE: return "PLANCHETTE: tilt to the edge";
    case I_PLSMOOTH: return "PLANCHETTE: how slowly it follows";
    case I_PLMODE: return "PLANCHETTE: axes, or 4 scenes";
    case I_PLCA: return "Vector: forward-left";
    case I_PLCB: return "Vector: forward-right";
    case I_PLCC: return "Vector: back-left";
    case I_PLCD: return "Vector: back-right";
    case I_LISTEN: return "React to sounds in the room";
    case I_LSENS: return "How sudden a sound must be";
    case I_HUSHDEPTH: return "How quiet a Hush gets";
    case I_RESPONSE: return "One lone sound before it returns";
    case I_RECAL: return "Learn the room's quiet again";
    case I_ARC: return "Calm, build, peak, release";
    case I_ARCLEN: return "How long one Arc takes";
    case I_ARCCYCLE: return "Once (then calm) or again";
    case I_ARCSLEEP: return "Once: end with the sleep timer";
    case I_CURSED: return "Strange things happen";
    case I_CFREQ: return "How often they happen";
    case I_DEBUG: return "Logs and test tools";
    case I_STORAGE: return "SD card space, recordings";
    case I_RESET: return "Defaults (scenes, WAVs stay)";
    case I_ABOUT: return "Version and keys";
    case I_LOG: return "Write listen_log.csv";
    case I_MARK: return "Shift+L / Shift+K marks";
    case I_CTRIG: return "Make one effect happen now";
    case I_MEMORY: return "Heap; what start-up took";
    case I_PERF: return "DSP load, dropouts, resets";
  }
  return "";
}

// Hidden items (Fade Length without Fade, the Custom Drift values without Custom)
static bool visible(Item it) {
  if (it == I_SLEEPFADELEN) return gSet.sleepFade;
  if (it == I_PLCA || it == I_PLCB || it == I_PLCC || it == I_PLCD) return gSet.plMode == 1;
  if (it == I_LEDBR) return gSet.led;
  if (it == I_ARCSLEEP) return gSet.arcCycle == ARC_ONCE;
  if (it == I_DWAITMIN || it == I_DWAITMAX || it == I_DXF || it == I_DCHMIN || it == I_DCHMAX)
    return gSet.driftSpeed == DRIFT_CUSTOM;
  return true;
}
static bool isPage(Item it) {
  return it == I_DEBUG || it == I_STORAGE || it == I_CTRIG || it == I_MEMORY || it == I_PERF || it == I_DPOOL;
}
static bool isAction(Item it) { return it == I_RECAL || it == I_RESET || it == I_ABOUT; }

static const char* onOff(bool b) { return b ? "On" : "Off"; }
static void fmtSec(char* v, size_t n, int s) {
  if (s == 0) snprintf(v, n, "Off");
  else if (s < 60) snprintf(v, n, "%d s", s);
  else snprintf(v, n, "%d min", s / 60);
}

// The value as text; frac >= 0 draws a bar
static void value(Item it, char* v, size_t n, float& frac) {
  frac = -1.f;
  switch (it) {
    case I_VOLUME: snprintf(v, n, "%d", gSet.volume); frac = gSet.volume / 100.f; break;
    case I_SEEDLOCK: snprintf(v, n, "%s", onOff(gSet.seedLock)); break;
    case I_STARTUP: snprintf(v, n, "%s", STARTUP_NAMES[gSet.startup & 1]); break;
    case I_FADEIN: fmtSec(v, n, FADE_IN_SEC[gSet.fadeInIdx]); break;
    case I_STARTSND: snprintf(v, n, "%s", onOff(gSet.startupSound)); break;
    case I_DRIFTSPEED: snprintf(v, n, "%s", DRIFT_SPEED_NAMES[gSet.driftSpeed]); break;
    case I_DWAITMIN: snprintf(v, n, "%d s", gSet.dWaitMin); break;
    case I_DWAITMAX: snprintf(v, n, "%d s", gSet.dWaitMax); break;
    case I_DXF: snprintf(v, n, "%d s", gSet.dXf); break;
    case I_DCHMIN: snprintf(v, n, "%d", gSet.dChMin); break;
    case I_DCHMAX: snprintf(v, n, "%d", gSet.dChMax); break;
    case I_DLAYMIN: snprintf(v, n, "%d", gSet.dLayMin); break;
    case I_DLAYMAX: snprintf(v, n, "%d", gSet.dLayMax); break;
    case I_DMODE: snprintf(v, n, "%s", onOff(gSet.dModeChange)); break;
    case I_SLEEPON: snprintf(v, n, "%s", onOff(gSet.sleepOn)); break;
    case I_SLEEPTIME: snprintf(v, n, "%d min", gSet.sleepMin); break;
    case I_SLEEPFADE: snprintf(v, n, "%s", onOff(gSet.sleepFade)); break;
    case I_SLEEPFADELEN: snprintf(v, n, "%d s", gSet.sleepFadeSec); break;
    case I_SLEEPAFTER: snprintf(v, n, "%s", AFTER_NAMES[gSet.sleepAfter & 1]); break;
    case I_BRIGHT: snprintf(v, n, "%d", gSet.brightness); frac = gSet.brightness / 100.f; break;
    case I_DIM: fmtSec(v, n, DIM_AFTER_SEC[gSet.dimIdx]); break;
    case I_DIMLEVEL: snprintf(v, n, "%d%%", gSet.dimLevel); frac = gSet.dimLevel / 50.f; break;
    case I_AUTOOFF: fmtSec(v, n, AUTO_OFF_SEC[gSet.autoOffIdx]); break;
    case I_WAKEKEY: snprintf(v, n, "%s", gSet.wakeAct ? "Wake + Act" : "Wake Only"); break;
    case I_VIZ: snprintf(v, n, "%s", VIZ_NAMES[gSet.viz % NUM_VIZ]); break;
    case I_LED: snprintf(v, n, "%s", onOff(gSet.led)); break;
    case I_LEDBR: snprintf(v, n, "%d%%", gSet.ledBright); frac = gSet.ledBright / 100.f; break;
    case I_WATCHER: snprintf(v, n, "%s", onOff(gSet.watcher)); break;
    case I_SHAKE: snprintf(v, n, "%s", SHAKE_ACTION_NAMES[gSet.shakeAction]); break;
    case I_SHAKESENS: snprintf(v, n, "%s", SENS3_NAMES[appShakeSensIdx()]); break;
    case I_PLRANGE: snprintf(v, n, "%d deg", gSet.plRange); break;
    case I_PLSMOOTH: snprintf(v, n, "%s", SENS3_NAMES[gSet.plSmooth % 3]); break;
    case I_PLMODE: snprintf(v, n, "%s", gSet.plMode ? "Vector" : "Axis"); break;
    case I_PLCA: case I_PLCB: case I_PLCC: case I_PLCD: snprintf(v, n, "%s", gSet.plCorner[it - I_PLCA]); break;
    case I_LISTEN: snprintf(v, n, "%s", !listenAvailable() ? "n/a (no mic)" : onOff(gSet.listen)); break;
    case I_LSENS: snprintf(v, n, "%s", LISTEN_SENS_NAMES[gSet.listenSens]); break;
    case I_HUSHDEPTH: snprintf(v, n, "%s", HUSH_NAMES[gSet.hushDepth % NUM_HUSH_DEPTHS]); break;
    case I_RESPONSE: snprintf(v, n, "%s", onOff(gSet.response)); break;
    case I_ARC: snprintf(v, n, "%s", onOff(gSet.arc)); break;
    case I_ARCLEN: snprintf(v, n, "%d min", ARC_LEN_MIN[gSet.arcLenIdx % NUM_ARC_LENS]); break;
    case I_ARCCYCLE: snprintf(v, n, "%s", gSet.arcCycle == ARC_LOOP ? "Loop" : "Once"); break;
    case I_ARCSLEEP: snprintf(v, n, "%s", onOff(gSet.arcFollowSleep)); break;
    case I_CURSED: snprintf(v, n, "%s", onOff(gSet.cursed != 0)); break;
    case I_CFREQ: snprintf(v, n, "%s", CURSED_LEVEL_NAMES[gSet.cursedFreq]); break;
    case I_LOG: snprintf(v, n, "%s", onOff(gSet.listenLog)); break;
    case I_MARK: snprintf(v, n, "%s", onOff(gSet.markKeys)); break;
    default: v[0] = 0; break;   // pages and actions
  }
}

// --- pages ---------------------------------------------------------------------
enum Page : uint8_t { P_CATS = 0, P_ITEMS, P_DEBUG, P_CTRIG, P_MEMORY, P_PERF, P_STORAGE, P_POOL };
static Page g_page = P_CATS;
static int g_cat = 0;
static int g_cur[5] = {0, 0, 0, 0, 0}, g_scroll[5] = {0, 0, 0, 0, 0};   // cats / items / debug / trigger / pool
static Page g_storageBack = P_ITEMS;

void settingsResetView() { g_page = P_CATS; }

static void keepInView(int curPos, int& scroll, int vis) {
  if (curPos < scroll) scroll = curPos;
  if (curPos >= scroll + vis) scroll = curPos - vis + 1;
  if (scroll < 0) scroll = 0;
}
static void drawScrollMarks(int scroll, int vis, int n, int yTop, int yBottom) {
  if (scroll > 0) drawText("^", SCREEN_W - 4, yTop, C_DIM, 1, textdatum_t::top_right);
  if (scroll + vis < n) drawText("v", SCREEN_W - 4, yBottom, C_DIM, 1, textdatum_t::top_right);
}

// The items of a category that are shown now
static int visibleItems(const Cat& cat, Item* out) {
  int n = 0;
  for (int i = 0; i < cat.n; i++) if (visible(cat.items[i])) out[n++] = cat.items[i];
  return n;
}

static constexpr int VIS = 6, ROW_Y = 16, ROW_H = 14, DESC_Y = 104;

static void drawItemList(const char* title, const Cat& cat, int slot) {
  drawHeader(title);
  Item items[16];
  int n = visibleItems(cat, items);
  if (g_cur[slot] >= n) g_cur[slot] = n - 1;
  keepInView(g_cur[slot], g_scroll[slot], VIS);
  char v[32];
  for (int k = 0; k < VIS && g_scroll[slot] + k < n; k++) {
    const int r = g_scroll[slot] + k;
    const Item it = items[r];
    const int y = ROW_Y + k * ROW_H;
    const bool sel = r == g_cur[slot];
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, 13, C_SEL);
    drawText(label(it), 8, y, sel ? C_ACC2 : C_INK);
    float frac;
    value(it, v, sizeof(v), frac);
    if (isPage(it)) drawText(">", SCREEN_W - 8, y, C_DIM, 1, textdatum_t::top_right);
    else if (isAction(it)) drawText(sel ? "[Enter]" : "", 104, y, C_DIM);
    else if (frac >= 0.f) {
      drawBar(104, y, 92, 8, frac, C_ACC, C_FAINT);
      drawText(v, SCREEN_W - 6, y, C_INK, 1, textdatum_t::top_right);
    } else {
      char vv[40];
      snprintf(vv, sizeof(vv), sel ? "< %s >" : "%s", v);
      drawText(vv, 104, y, C_INK);
    }
  }
  drawScrollMarks(g_scroll[slot], VIS, n, ROW_Y, ROW_Y + (VIS - 1) * ROW_H);
  if (n > 0) drawText(desc(items[g_cur[slot]]), 8, DESC_Y, C_DIM);
  drawNav("[;.]Sel [,/]Change [Enter] [Esc]Back");
}

static void drawCats() {
  drawHeader("SETTINGS", FW_VERSION);
  keepInView(g_cur[0], g_scroll[0], VIS);
  for (int k = 0; k < VIS && g_scroll[0] + k < NUM_CATS; k++) {
    const int r = g_scroll[0] + k;
    const int y = ROW_Y + k * ROW_H;
    const bool sel = r == g_cur[0];
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, 13, C_SEL);
    drawText(CATS[r].name, 8, y, sel ? C_ACC2 : C_INK);
    drawText(">", SCREEN_W - 8, y, C_DIM, 1, textdatum_t::top_right);
  }
  drawScrollMarks(g_scroll[0], VIS, NUM_CATS, ROW_Y, ROW_Y + (VIS - 1) * ROW_H);
  drawNav("[;.]Sel [Enter]Open [H]elp [Esc]");
}

// --- Debug pages ----------------------------------------------------------------
static void drawTrigger() {
  drawHeader("DEBUG > Cursed Trigger");
  const int n = cursedFxCount();
  keepInView(g_cur[3], g_scroll[3], VIS);
  for (int k = 0; k < VIS && g_scroll[3] + k < n; k++) {
    const int r = g_scroll[3] + k;
    const int y = ROW_Y + k * ROW_H;
    const bool sel = r == g_cur[3];
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, 13, C_SEL);
    drawText(cursedFxName(r), 8, y, sel ? C_ACC2 : C_INK);
  }
  drawScrollMarks(g_scroll[3], VIS, n, ROW_Y, ROW_Y + (VIS - 1) * ROW_H);
  const char* nm; uint32_t ms; bool dbg;
  char b[48];
  if (cursedLast(nm, ms, dbg)) {
    uint32_t s = ms / 1000;
    snprintf(b, sizeof(b), "Last: %s %lu:%02lu:%02lu%s", nm, (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
             (unsigned long)(s % 60), dbg ? " (dbg)" : "");
  } else {
    snprintf(b, sizeof(b), "Last: none yet");
  }
  drawText(b, 8, DESC_Y, C_DIM);
  drawNav("[;.]Sel [Enter]Trigger now [Esc]Back");
}

static void drawMemory() {
  drawHeader("DEBUG > Memory");
  // refreshed once a second (spec v1.22 2.2)
  static uint32_t lastMs = 0, fr = 0, mn = 0, lg = 0;
  if (millis() - lastMs >= 1000 || lastMs == 0) { lastMs = millis(); memNow(fr, mn, lg); }
  char b[48];
  snprintf(b, sizeof(b), "Heap %luK  min %luK  block %luK", (unsigned long)(fr / 1024), (unsigned long)(mn / 1024),
           (unsigned long)(lg / 1024));
  drawText(b, 4, 15, C_INK);
  drawText("start-up step      took   free", 4, 26, C_DIM);
  const int n = memMarkCount();
  for (int i = 0; i < n && i < 8; i++) {
    long took = i ? (long)memMarkFree(i - 1) - (long)memMarkFree(i) : 0;
    snprintf(b, sizeof(b), "%-18s %5ldK %5luK", memMarkName(i), took / 1024, (unsigned long)(memMarkFree(i) / 1024));
    drawText(b, 4, 36 + i * 10, C_INK);
  }
  if (recActive()) drawText("+ REC ring buffer while recording", 4, 116, C_DIM);
  drawNav("[Esc]Back");
}

static void drawPerf() {
  drawHeader("DEBUG > Performance");
  char b[48];
  snprintf(b, sizeof(b), "DSP avg %d%%  p99 %d%%  peak %d%%", (int)(gAudioLoadMean * 100.f + 0.5f),
           (int)(audioLoadP99() * 100.f + 0.5f), (int)(gAudioLoadPeak * 100.f + 0.5f));
  drawText(b, 4, 18, C_INK);
  snprintf(b, sizeof(b), "Audio task (all) %d%%", (int)(gAudioBusyMean * 100.f + 0.5f));
  drawText(b, 4, 32, C_INK);
  snprintf(b, sizeof(b), "Dropouts (dr) %u", (unsigned)audioIoDropouts());
  drawText(b, 4, 46, audioIoDropouts() ? C_WARN : C_INK);
  snprintf(b, sizeof(b), "Free heap %uK", (unsigned)(ESP.getFreeHeap() / 1024));
  drawText(b, 4, 60, C_INK);
  snprintf(b, sizeof(b), "Last reset: %s", gResetReason);
  drawText(b, 4, 74, C_INK);
  if (audioIoMicOn()) {
    MicStats ms;
    audioIoMicStats(ms);
    snprintf(b, sizeof(b), "Mic work %d us / buffer", (int)(ms.procUs + 0.5f));
    drawText(b, 4, 88, C_DIM);
  }
  if (gVizFftUs > 0.f) {   // the Spectrum view's FFT, on the UI core
    snprintf(b, sizeof(b), "Spectrum FFT %d us / frame", (int)(gVizFftUs + 0.5f));
    drawText(b, 4, 100, C_DIM);
  }
  drawNav("[R]Reset stats [Esc]Back");
}

// Drift > Drift Pool: every layer (but the reverb), On = Drift may touch it
static int poolLayers(int* ids) {
  int n = 0;
  for (int i = 0; i < NUM_LAYERS; i++) if (i != L_REVERB) ids[n++] = i;
  return n;
}
static void drawPool() {
  drawHeader("DRIFT > Drift Pool");
  int ids[NUM_LAYERS];
  const int n = poolLayers(ids);
  keepInView(g_cur[4], g_scroll[4], VIS);
  for (int k = 0; k < VIS && g_scroll[4] + k < n; k++) {
    const int r = g_scroll[4] + k;
    const int id = ids[r];
    const int y = ROW_Y + k * ROW_H;
    const bool sel = r == g_cur[4];
    const bool on = gSet.dPool & (1u << id);
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, 13, C_SEL);
    drawText(LAYER_DEFS[id].name, 8, y, sel ? C_ACC2 : (on ? C_INK : C_FAINT));
    drawText(GROUP_NAMES[LAYER_DEFS[id].group], 96, y, C_FAINT);
    drawText(on ? "On" : "Off", 160, y, on ? C_INK : C_DIM);
  }
  drawScrollMarks(g_scroll[4], VIS, n, ROW_Y, ROW_Y + (VIS - 1) * ROW_H);
  drawText("Off: Drift never touches it", 8, DESC_Y, C_DIM);
  drawNav("[;.]Sel [,/ Enter]On/Off [Esc]Back");
}

// System > Storage: counted when the page opens (the card can be slow)
static uint64_t g_sdFree = 0, g_sdTotal = 0;
static int g_recFiles = -1;
static void scanStorage() {
  g_recFiles = -1;
  g_sdFree = g_sdTotal = 0;
  if (!gSdReady) return;
  g_sdFree = sdFreeBytes();
  SdLock lk;
  g_sdTotal = SD.totalBytes();
  File d = SD.open(DIR_REC);
  if (!d) return;
  g_recFiles = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) g_recFiles++;
    f.close();
  }
  d.close();
}
static void drawStorage() {
  drawHeader("SYSTEM > Storage");
  char b[48];
  if (!gSdReady) {
    drawText("No SD card", 8, 24, C_WARN);
  } else {
    snprintf(b, sizeof(b), "Free  %.2f GB of %.2f GB", g_sdFree / 1073741824.0, g_sdTotal / 1073741824.0);
    drawText(b, 8, 24, C_INK);
    if (g_recFiles >= 0) snprintf(b, sizeof(b), "Files in %s: %d", DIR_REC, g_recFiles);
    else snprintf(b, sizeof(b), "%s: not found", DIR_REC);
    drawText(b, 8, 40, C_INK);
  }
  drawNav("[Esc]Back");
}

void drawSettings() {
  switch (g_page) {
    case P_CATS: drawCats(); break;
    case P_ITEMS: {
      char t[32];
      snprintf(t, sizeof(t), "SETTINGS > %s", CATS[g_cat].name);
      drawItemList(t, CATS[g_cat], 1);
      break;
    }
    case P_DEBUG: drawItemList("SYSTEM > Debug", DEBUG_CAT, 2); break;
    case P_CTRIG: drawTrigger(); break;
    case P_MEMORY: drawMemory(); break;
    case P_PERF: drawPerf(); break;
    case P_STORAGE: drawStorage(); break;
    case P_POOL: drawPool(); break;
  }
}

// --- changing values --------------------------------------------------------------
static void doRecalibrate() { listenRecalibrate(); uiToast("Listening to the room again"); }
static void doReset() { appResetSettings(); uiToast("Settings reset"); }

// d = -1 / +1 (Enter: +1); held = the key's hold time (numbers: steps of 10)
static void change(Item it, int d, uint32_t heldMs, bool enter) {
  const int big = heldMs > 1000 ? 10 : 1;
  auto cyc = [&](uint8_t& x, int n) { x = (uint8_t)((x + d + n) % n); };
  switch (it) {
    case I_VOLUME: gSet.volume = (uint8_t)constrain((int)gSet.volume + d * big, 0, 100); break;
    case I_SEEDLOCK: gSet.seedLock = !gSet.seedLock; break;
    case I_STARTUP: gSet.startup ^= 1; break;
    case I_FADEIN: cyc(gSet.fadeInIdx, NUM_FADE_IN); break;
    case I_STARTSND: gSet.startupSound = !gSet.startupSound; break;
    case I_DRIFTSPEED: cyc(gSet.driftSpeed, NUM_DRIFT_SPEEDS); break;
    // Min <= Max: moving one past the other takes it along
    case I_DWAITMIN:
      gSet.dWaitMin = (uint8_t)constrain((int)gSet.dWaitMin + d * big, 3, 120);
      if (gSet.dWaitMax < gSet.dWaitMin) gSet.dWaitMax = gSet.dWaitMin;
      break;
    case I_DWAITMAX:
      gSet.dWaitMax = (uint8_t)constrain((int)gSet.dWaitMax + d * big, 3, 120);
      if (gSet.dWaitMin > gSet.dWaitMax) gSet.dWaitMin = gSet.dWaitMax;
      break;
    case I_DXF: gSet.dXf = (uint8_t)constrain((int)gSet.dXf + d * big, 2, 30); break;
    case I_DCHMIN:
      gSet.dChMin = (uint8_t)constrain((int)gSet.dChMin + d, 1, 3);
      if (gSet.dChMax < gSet.dChMin) gSet.dChMax = gSet.dChMin;
      break;
    case I_DCHMAX:
      gSet.dChMax = (uint8_t)constrain((int)gSet.dChMax + d, 1, 3);
      if (gSet.dChMin > gSet.dChMax) gSet.dChMin = gSet.dChMax;
      break;
    case I_DLAYMIN:
      gSet.dLayMin = (uint8_t)constrain((int)gSet.dLayMin + d, 1, 8);
      if (gSet.dLayMax < gSet.dLayMin) gSet.dLayMax = gSet.dLayMin;
      break;
    case I_DLAYMAX:
      gSet.dLayMax = (uint8_t)constrain((int)gSet.dLayMax + d, 1, 8);
      if (gSet.dLayMin > gSet.dLayMax) gSet.dLayMin = gSet.dLayMax;
      break;
    case I_DMODE: gSet.dModeChange = !gSet.dModeChange; break;
    case I_SLEEPON: gSet.sleepOn = !gSet.sleepOn; appSleepArm(); break;
    case I_SLEEPTIME: gSet.sleepMin = (uint8_t)constrain((int)gSet.sleepMin + d * big, 1, 240); appSleepArm(); break;
    case I_SLEEPFADE: gSet.sleepFade = !gSet.sleepFade; break;
    case I_SLEEPFADELEN: gSet.sleepFadeSec = (uint8_t)constrain((int)gSet.sleepFadeSec + d * (big > 1 ? 10 : 5), 10, 120); break;
    case I_SLEEPAFTER: gSet.sleepAfter ^= 1; break;
    case I_BRIGHT: gSet.brightness = (uint8_t)constrain((int)gSet.brightness + d * (big > 1 ? 10 : 2), 10, 100); break;
    case I_DIM: cyc(gSet.dimIdx, NUM_DIM_AFTER); break;
    case I_DIMLEVEL: gSet.dimLevel = (uint8_t)constrain((int)gSet.dimLevel + d * (big > 1 ? 10 : 5), 10, 50); break;
    case I_AUTOOFF: cyc(gSet.autoOffIdx, NUM_AUTO_OFF); break;
    case I_WAKEKEY: gSet.wakeAct = !gSet.wakeAct; break;
    case I_VIZ: cyc(gSet.viz, NUM_VIZ); break;
    case I_LED: gSet.led = !gSet.led; break;
    case I_LEDBR: gSet.ledBright = (uint8_t)constrain((int)gSet.ledBright + d * (big > 1 ? 10 : 5), 10, 100); break;
    case I_WATCHER: gSet.watcher = !gSet.watcher; break;
    case I_SHAKE: cyc(gSet.shakeAction, NUM_SHAKE_ACTIONS); break;
    case I_SHAKESENS: gSet.shakeSens = SHAKE_SENS_VALUES[constrain(appShakeSensIdx() + d, 0, NUM_SHAKE_SENS - 1)]; break;
    case I_PLRANGE: gSet.plRange = (uint8_t)constrain((int)gSet.plRange + d * 5, 20, 45); break;
    case I_PLSMOOTH: gSet.plSmooth = (uint8_t)constrain((int)gSet.plSmooth + d, 0, 2); break;
    case I_PLMODE: gSet.plMode ^= 1; break;
    case I_PLCA: case I_PLCB: case I_PLCC: case I_PLCD: {
      // the presets and the custom scenes, in the MOOD order (not Drift: it never holds still)
      char* cn = gSet.plCorner[it - I_PLCA];
      const int n2 = (int)gScenes.size();
      int cur = -1;
      for (int k = 0; k < n2; k++) if (gScenes[k].name == cn) { cur = k; break; }
      for (int step = 0; step < n2; step++) {
        cur = ((cur < 0 ? 0 : cur) + d + n2) % n2;
        if (gScenes[cur].kind != SK_DRIFT) break;
      }
      strncpy(cn, gScenes[cur].name.c_str(), 23); cn[23] = 0;
      break;
    }
    case I_LISTEN:
      if (!listenAvailable()) { uiToast("Mic not available"); return; }
      if (!gSet.listen) { openListenConsent(); return; }   // consent every time
      listenEnable(false);
      break;
    case I_LSENS: gSet.listenSens = (uint8_t)constrain((int)gSet.listenSens + d, 0, NUM_LISTEN_SENS - 1); break;
    case I_HUSHDEPTH: gSet.hushDepth = (uint8_t)constrain((int)gSet.hushDepth + d, 0, NUM_HUSH_DEPTHS - 1); break;
    case I_RESPONSE: gSet.response = !gSet.response; break;
    case I_ARC: gSet.arc = !gSet.arc; if (gSet.arc) arcRestart(); break;   // from Calm
    case I_ARCLEN: cyc(gSet.arcLenIdx, NUM_ARC_LENS); break;               // the progress stays
    case I_ARCCYCLE: gSet.arcCycle ^= 1; break;
    case I_ARCSLEEP: gSet.arcFollowSleep = !gSet.arcFollowSleep; break;
    case I_RECAL:
      if (!enter) return;
      if (!gSet.listen) { uiToast("Listen is off"); return; }
      openConfirmFn("Recalibrate Listen?", doRecalibrate);
      return;
    case I_CURSED:
      if (!gSet.cursed) { openCursedConsent(gSet.cursedFreq); return; }   // consent to turn it on
      gSet.cursed = 0;
      cursedStopAll();
      break;
    case I_CFREQ:
      gSet.cursedFreq = (uint8_t)constrain((int)gSet.cursedFreq + d, 1, NUM_CURSED_LEVELS - 1);
      if (gSet.cursed) gSet.cursed = gSet.cursedFreq;
      break;
    case I_RESET:
      if (!enter) return;
      openConfirmFn("Reset all settings?", doReset);
      return;
    case I_ABOUT: if (enter) gHelp = true; return;
    case I_LOG: gSet.listenLog = !gSet.listenLog; break;
    case I_MARK: gSet.markKeys = !gSet.markKeys; break;
    default: return;
  }
  appSettingsChanged();
}

static void openPage(Item it) {
  switch (it) {
    case I_DEBUG: g_page = P_DEBUG; break;
    case I_STORAGE: scanStorage(); g_storageBack = g_page; g_page = P_STORAGE; break;
    case I_CTRIG: g_page = P_CTRIG; break;
    case I_MEMORY: g_page = P_MEMORY; break;
    case I_PERF: g_page = P_PERF; break;
    case I_DPOOL: g_page = P_POOL; break;
    default: break;
  }
}

static bool keyList(const KeyEv& ev, const Cat& cat, int slot) {
  Item items[16];
  int n = visibleItems(cat, items);
  if (n == 0) return false;
  if (g_cur[slot] >= n) g_cur[slot] = n - 1;
  if (ev.k == ';') { g_cur[slot] = (g_cur[slot] + n - 1) % n; return true; }
  if (ev.k == '.') { g_cur[slot] = (g_cur[slot] + 1) % n; return true; }
  const Item it = items[g_cur[slot]];
  if (isPage(it)) { if (ev.enter || ev.k == '/') { openPage(it); return true; } return false; }
  if (ev.k == ',' || ev.k == '/' || ev.enter) { change(it, ev.k == ',' ? -1 : 1, ev.heldMs, ev.enter); return true; }
  return false;
}

bool keySettings(const KeyEv& ev) {
  if (ev.shift) return false;
  switch (g_page) {
    case P_CATS:
      if (ev.k == ';') { g_cur[0] = (g_cur[0] + NUM_CATS - 1) % NUM_CATS; return true; }
      if (ev.k == '.') { g_cur[0] = (g_cur[0] + 1) % NUM_CATS; return true; }
      if (ev.enter || ev.k == '/') { g_cat = g_cur[0]; g_cur[1] = g_scroll[1] = 0; g_page = P_ITEMS; return true; }
      return false;   // Esc: back to MAIN (global)
    case P_ITEMS:
      if (ev.esc) { g_page = P_CATS; return true; }
      return keyList(ev, CATS[g_cat], 1);
    case P_DEBUG:
      if (ev.esc) { g_page = P_ITEMS; return true; }
      return keyList(ev, DEBUG_CAT, 2);
    case P_CTRIG: {
      if (ev.esc) { g_page = P_DEBUG; return true; }
      const int n = cursedFxCount();
      if (ev.k == ';') { g_cur[3] = (g_cur[3] + n - 1) % n; return true; }
      if (ev.k == '.') { g_cur[3] = (g_cur[3] + 1) % n; return true; }
      if (ev.enter) {
        const char* err = cursedTrigger(g_cur[3]);
        uiToast(err ? err : cursedFxName(g_cur[3]));
        return true;
      }
      return false;
    }
    case P_MEMORY:
      if (ev.esc) { g_page = P_DEBUG; return true; }
      return false;
    case P_PERF:
      if (ev.esc) { g_page = P_DEBUG; return true; }
      if (ev.k == 'r') { gAudioStatsReset = true; uiToast("Load stats reset"); return true; }
      return false;
    case P_STORAGE:
      if (ev.esc) { g_page = g_storageBack; return true; }
      return false;
    case P_POOL: {
      if (ev.esc) { g_page = P_ITEMS; return true; }
      int ids[NUM_LAYERS];
      const int n = poolLayers(ids);
      if (ev.k == ';') { g_cur[4] = (g_cur[4] + n - 1) % n; return true; }
      if (ev.k == '.') { g_cur[4] = (g_cur[4] + 1) % n; return true; }
      if (ev.k == ',' || ev.k == '/' || ev.enter) {
        gSet.dPool ^= 1u << ids[g_cur[4]];
        appSettingsChanged();
        return true;
      }
      return false;
    }
  }
  return false;
}
