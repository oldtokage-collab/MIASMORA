#include "listen.h"
#include "party.h"
#include "planchette.h"
#include "poltergeist.h"
#include <Arduino.h>
#include <algorithm>
#include <string.h>
#include "app.h"
#include "audio.h"
#include "audio_io.h"
#include "export.h"
#include "power.h"
#include "storage.h"

using namespace dsp;

const char* const LISTEN_SENS_NAMES[NUM_LISTEN_SENS] = {"Low", "Mid", "High"};

namespace {

// --- tuning (first values; to be adjusted on the device) ----------------------
constexpr float WIN_SEC = (float)BUF / SR;   // one measurement window, ~23 ms
constexpr int wins(float sec) { return (int)(sec / WIN_SEC + 0.5f); }

// A "rise" is a window's level minus the median of the ~0.3 s before it.
constexpr int   BASE_N = 13;
// Rise needed for a Hush, per sensitivity (spec v1.21 3.2)
const float RISE_DB[NUM_LISTEN_SENS] = {12.f, 9.f, 6.f};   // Low / Mid / High
constexpr float CONFIRM_DB = 3.f;          // this far over: Hush at once, else two windows in a row
// v0.35: close to the room floor a single window needs more. In a very quiet
// mood (Inside at High, v0.34 log) two 23 ms ticks only 10..12 dB over the
// floor, rising 9.5 / 10.6 dB, made it Hush at once.
constexpr float QUIET_LEVEL_DB = 20.f;     // mic less than this over the floor ... (v0.36: 15 -> 20)
constexpr float CONFIRM_QUIET_DB = 6.f;    // ... Hush at once only this far over the bar
constexpr float NEAR_DB = 3.f;             // a room rise this close to the bar: Attention pauses ...
constexpr float NEAR_MIN_DB = 5.f;         // ... but at least this big (High: the mood's own wobble reaches 3 dB)
constexpr uint32_t ATT_PAUSE_MS = 5000;    // ... for this long
// The mic must also be clearly above the room floor, or a quiet room's tiny
// noises (rising 6 dB from nothing) would count. v0.36: per sensitivity (was
// 10 dB for all). In Inside / Abyss the mic sits at the floor, and in the
// v0.35 log the clicks of keys and of touching the device, 18..27 dB over it,
// made it Hush (real sounds there: 20..42 dB over it).
constexpr float FLOOR_INIT = -88.f;        // room + mic noise, measured -90 when stopped
const float LEVEL_MARGIN_DB[NUM_LISTEN_SENS] = {25.f, 20.f, 15.f};   // Low / Mid / High
// v0.36: a Hush waits this long before it starts, and is called off if a key
// press or a movement of the device is noted meanwhile (or just before): the
// UI loop (drawing included, 30..40 ms) can read a key only after the mic
// window with its click has been judged.
constexpr uint32_t DEFER_MS = 120;
constexpr uint32_t LATE_KEY_MS = 60;       // a key noted this long before the Hush counts too
constexpr float OWN_SILENT_DB = -60.f;     // own output below this: the mic hears the room floor
// Our own sound (spec v1.21 3.3 / 3.4). What is written to the speaker now
// is heard at the mic LAT_WIN windows later: 8 x 256 frames of DMA queue
// (93 ms) plus the codec, the air and the input DMA. Fixed; the lag measured
// on the device is shown in HELP and logged, to set this value from.
constexpr int   LAT_WIN = 3;               // ~70 ms (v0.33 measured 3 on the device; 5 was the estimate)
constexpr int   OUT_TOL = 2;               // +- windows around it for the output's own rise
constexpr float OUT_RISE_DB = 6.f;         // the output rose this much there (over its minimum): the rise is ours ...
// ... unless the mic rose this much more than the output did (v0.34). In
// quiet moods the output's rise over its minimum wobbles by 4..6 dB anyway;
// v0.33 left out a clap of +41 dB at the mic on +7.6 dB of output. Our own
// sounds stayed within it in the v0.33 log (DRIP +9..18 at the mic on
// +15..23, BUMP +20 on +9..12, DRILL +15 on +11.5): 15 leaves them a margin.
constexpr float OUT_EXCESS_DB = 15.f;
// An onset the engine reported (an event, a beat, a burst) covers the mic
// from 20 ms before it is heard to 250 ms after (its ring, the room).
constexpr int   OWN_BEFORE = 1;            // windows
constexpr int   OWN_AFTER = 11;
// ... but only an onset the mic actually heard covers its tail (a rise of
// this much right where it was due), and only sounds that are not louder
// than it was by the bar again. On the tiny speaker HEART's beats, say, are
// barely there; they must not blank 70 % of the time between them, and a
// clap during an event's ring is still a clap. While the onset is due, it
// covers a rise up to its own output rise + OUT_EXCESS_DB (v0.34).
constexpr float OWN_HEARD_DB = 3.f;
constexpr int   OWN_HEARD_SPAN = 3;        // windows from when it is due
// The lag measurement: a strong own onset with nothing of ours for 20
// windows before it, and a clear rise at the mic within 15 windows after
constexpr float LAT_OUT_DB = 6.f, LAT_IN_DB = 6.f;
constexpr int   LAT_ISOLATED = 20, LAT_SEARCH = 15;
constexpr float RECOVER_MARGIN_DB = 6.f;   // a Hush again while the sound comes back
constexpr uint32_t KEY_HOLD_MS = 1000;     // key pressed, and this long after: no Hush (the mic hears the keys)
// v0.25 / v0.27: a loud sound may Hush again while the sound comes back;
// continuous noise (a TV) must not keep everything silent: after 4 Hushes
// within 60 s, rest 20 s.
constexpr int   BURST_N = 4;
constexpr uint32_t BURST_MS = 60000, REST_MS = 20000;
// v0.26: dead-mic detection and recovery
constexpr float DEAD_DB = -95.f;               // a working mic reads about -90 even in silence
constexpr int   DEAD_N = wins(10.f);           // the last 10 s ...
constexpr int   DEAD_LIMIT = DEAD_N * 9 / 10;  // ... of which this many dead windows
constexpr int   RECOVER_WAIT_WINDOWS = wins(3.f);   // for a recovery stage to take effect
constexpr int   HEALTHY_WINDOWS = wins(2.f);        // of signal: recovered
constexpr float ATT_RISE_SEC = 240.f;    // 0 -> 1 in four minutes without a sound in the room
constexpr float DREAD_MAX = 20.f;
// Hush Depth (SETTINGS > Listen, v0.4): how far the sound drops; layers stay awake
const float HUSH_FLOOR[NUM_HUSH_DEPTHS] = {0.1f, 0.0447f, 0.02f};   // Light -20 / Mid -27 / Deep -34 dB
constexpr float DROP_SEC = 0.3f;
constexpr float NOTICED_SEC = 2.5f;      // the lone event before the sound returns
constexpr float NOTICED_POKE_SEC = 0.15f;   // layer back at full level before it fires
constexpr float COOLDOWN_SEC = 10.f;     // shown only; since v0.25 a Hush may start again in it
constexpr float NOTICED_CHANCE = 0.25f;  // spec: about one Hush in four
constexpr float SETTLE_SEC = 1.f;        // after a change of volume / mood / play state

enum State { ST_IDLE = 0, ST_DROP, ST_HOLD, ST_NOTICED, ST_RECOVER, ST_COOLDOWN };

// The last N values and a percentile of them
template <int N> struct Pct {
  float v[N];
  int n = 0, i = 0;
  void clear() { n = 0; i = 0; }
  void push(float x) { v[i] = x; i = (i + 1) % N; if (n < N) n++; }
  float pct(float f) const {
    static float tmp[N];
    std::copy(v, v + n, tmp);
    int idx = (int)(f * (n - 1));
    std::nth_element(tmp, tmp + idx, tmp + n);
    return tmp[idx];
  }
};

// --- recent windows -------------------------------------------------------------
struct HistWin {
  float inF, inB, outF, outB;
  float outRise;   // own output's rise in this window (the larger of full band / band)
  float inRise;    // the mic's rise in this window (filled in once judged)
  uint32_t own;    // engine onsets in the buffers written in this window
};
constexpr int HIST_N = 64;   // ~1.5 s: covers the baseline, the lag and the own-onset span
HistWin g_hist[HIST_N];
uint32_t g_m = 0;            // windows pushed since the last reset; the newest is g_m - 1
int g_histN = 0;             // valid windows (since the last reset)

float median(float* v, int n) {
  std::nth_element(v, v + n / 2, v + n);
  return v[n / 2];
}
// Median of the BASE_N windows before window `m` (field selected by `get`)
template <class F> float baseOf(uint32_t m, F get) {
  float t[BASE_N];
  for (int j = 0; j < BASE_N; j++) t[j] = get(g_hist[(m - 1 - j) % HIST_N]);
  return median(t, BASE_N);
}
// ... and their minimum
template <class F> float minOf(uint32_t m, F get) {
  float v = 999.f;
  for (int j = 0; j < BASE_N; j++) v = std::min(v, get(g_hist[(m - 1 - j) % HIST_N]));
  return v;
}

Pct<300> g_floorR;            // ~7 s of mic level while our own output is near silent
float g_floor = FLOOR_INIT;
Pct<31> g_lags;               // measured lags (windows)
uint32_t g_lastOwnStrong = 0; // window of the last isolated strong own onset (+1; 0 = none)
bool g_latTaken = true;

float g_att = 0.f;
State g_state = ST_IDLE;
float g_t = 0.f, g_dur = 0.f, g_mul = 1.f, g_mulFrom = 1.f;
bool  g_notice = false, g_poked = false;
int   g_noticeLayer = -1;
bool  g_shakeHush = false;   // the Hush running was started by a shake (it runs with Listen off too)
uint32_t g_responses = 0;    // Watcher: the "noticed" events so far
float g_shyLoud = 0.f;       // Poltergeist's Shy: how long the room has been loud
constexpr float SHY_LOUD_DB = 15.f, SHY_LOUD_SEC = 1.5f;
int   g_hushes = 0;
float g_settle = 0.f, g_refreshT = 0.f;
bool  g_gated = true, g_effects = false;
bool  g_avail = true, g_help = false, g_micWas = false;
int   g_lastVol = -1, g_lastScene = -2;
bool  g_lastPlaying = false;
char  g_lastName[24] = "";
Rng   g_r;
bool  g_seeded = false;

// the last window as judged (HELP)
float g_inF = -96.f, g_inB = -96.f, g_outF = -96.f, g_outB = -96.f;
float g_riseF = 0.f, g_riseB = 0.f, g_outRiseNear = 0.f;
int   g_ownDist = 99;

void startRecover(float sec) {
  g_mulFrom = g_mul;
  g_state = ST_RECOVER; g_t = 0.f; g_dur = sec;
}

uint32_t g_hushAt[BURST_N] = {0};   // start times of the last Hushes (ms)
uint32_t g_restUntil = 0;           // no new Hush before this (after a burst)
uint32_t g_keyUntil = 0;            // no Hush before this (keys pressed, device moved)
uint32_t g_moveUntil = 0;           // of which: the device moved (log label only)
uint32_t g_lastKeyMs = 0, g_lastMoveMs = 0;   // when a key / a movement was last noted
// A Hush decided but not started yet (DEFER_MS)
struct Pending { bool on; uint32_t ms, seq; float riseF, riseB; uint32_t recent; };
Pending g_pend = {false, 0, 0, 0.f, 0.f, 0};
uint32_t g_attPauseUntil = 0;       // Attention does not rise before this (a sound in the room)
bool g_overPrev = false;            // the previous window was over the bar
bool g_deadHist[DEAD_N] = {false};  // dead-mic windows, the last 10 s
int  g_deadIdx = 0, g_deadSum = 0;
int  g_recStage = 0, g_recWait = 0, g_healthy = 0, g_recoveries = 0;
int  g_lastSlot = 0;

bool timeBefore(uint32_t now, uint32_t until) { return (int32_t)(until - now) > 0; }

void startHush() {
  const uint32_t now = millis();
  g_shakeHush = false;
  g_state = ST_DROP; g_t = 0.f;
  g_mulFrom = g_mul;   // also from part-way back up, when it hears something again
  g_att *= 0.5f;
  g_notice = g_r.chance(NOTICED_CHANCE) && gSet.response;   // Response (SETTINGS > Listen)
  g_noticeLayer = -1;
  g_hushAt[g_hushes % BURST_N] = now;
  g_hushes++;
  // the oldest of the last BURST_N within BURST_MS: continuous noise, rest
  if (g_hushes >= BURST_N && now - g_hushAt[g_hushes % BURST_N] < BURST_MS) g_restUntil = now + REST_MS;
}

// An event layer that is part of the scene, for the lone "noticed" event
int pickNoticeLayer() {
  static const int IDS[] = {L_KNOCK, L_STEPS, L_BUMP, L_CREAK, L_DRAG, L_CLANG, L_PULSE, L_DRIP, L_SCRAPE};
  int cand[sizeof(IDS) / sizeof(IDS[0])], n = 0;
  for (int id : IDS) {
    const LayerState& L = gRt.live.L[id];
    if (L.on && L.level > 0) cand[n++] = id;
  }
  return n ? cand[g_r.irange(0, n - 1)] : -1;
}

void stepHush(float dt) {
  g_t += dt;
  switch (g_state) {
    case ST_DROP:
      g_mul = g_mulFrom + (HUSH_FLOOR[gSet.hushDepth < NUM_HUSH_DEPTHS ? gSet.hushDepth : HUSH_DEEP] - g_mulFrom) * std::min(g_t / DROP_SEC, 1.f);
      if (g_t >= DROP_SEC) { g_state = ST_HOLD; g_t = 0.f; g_dur = g_r.range(3.f, 8.f); }
      break;
    case ST_HOLD:
      if (g_t >= g_dur) {
        g_noticeLayer = g_notice ? pickNoticeLayer() : -1;
        if (g_noticeLayer >= 0) { g_state = ST_NOTICED; g_t = 0.f; g_dur = NOTICED_SEC; g_poked = false; }
        else startRecover(g_r.range(10.f, 20.f));
      }
      break;
    case ST_NOTICED:
      if (!g_poked && g_t >= NOTICED_POKE_SEC) { appRequestSummon(g_noticeLayer, g_r.next()); g_poked = true; g_responses++; }
      if (g_t >= g_dur) startRecover(g_r.range(10.f, 20.f));
      break;
    case ST_RECOVER: {
      float x = std::min(g_t / g_dur, 1.f);
      g_mul = g_mulFrom + (1.f - g_mulFrom) * x * x;
      if (x >= 1.f) { g_mul = 1.f; g_noticeLayer = -1; g_state = ST_COOLDOWN; g_t = 0.f; g_dur = COOLDOWN_SEC; }
      break;
    }
    case ST_COOLDOWN:
      if (g_t >= g_dur) g_state = ST_IDLE;
      break;
    default: break;
  }
}

bool hushActive() { return g_state == ST_DROP || g_state == ST_HOLD || g_state == ST_NOTICED; }

// The baseline has to be refilled (the room floor is kept)
void resetHistory() {
  g_histN = 0;
  g_settle = 0.f;
  g_overPrev = false;
}

// ===========================================================================
// Diagnostic log (v0.23; columns for method A since v0.33): numbers only,
// never sound. /MIASMORA/listen_log.csv gets the last seconds of measurement
// windows whenever a Hush starts, a sound comes close to the bar or is left
// out (ours, the keys...), or the user marks a false Hush / a missed
// reaction. Time is "since power-on" (there is no clock).
// ===========================================================================
constexpr const char* LOG_PATH = DIR_ROOT "/listen_log.csv";
constexpr uint32_t LOG_MAX_BYTES = 4u * 1024u * 1024u;
constexpr int REC_N = 200;               // ~4.6 s of windows kept for a dump
constexpr int DUMP_HUSH = 40, DUMP_NEAR = 12, DUMP_MARK = REC_N;
constexpr uint32_t NEAR_GAP_MS = 2000;   // at most one NEAR / OWN / ... row per 2 s
constexpr uint32_t FLUSH_MS = 15000;

// dB values x10 in 16 bits: 32 bytes a window
struct WinRec {
  uint32_t seq, ms;
  int16_t inF, inB, outF, outB, riseF, riseB, outRise, other;
  uint32_t own;      // engine onsets matched to this window (lag applied)
  int8_t ownDist;    // windows from the nearest own onset (lag applied), 99 = none
  uint8_t state;
  uint8_t flags;     // bit 0 gated, bit 1 slot R
};
inline int16_t q10(float db) { return (int16_t)lroundf(std::max(-3000.f, std::min(3000.f, db * 10.f))); }

WinRec g_rec[REC_N];
uint32_t g_seq = 0, g_dumpedTo = 0;   // windows [.., g_dumpedTo) are in the log already
char g_log[12288];
int  g_logLen = 0;
uint32_t g_lastFlush = 0, g_lastNear = 0, g_lastStat = 0;
bool g_logFull = false, g_logHeader = false;

struct Trigger { bool valid; uint32_t ms; float riseF, riseB; uint32_t ownRecent; };
Trigger g_lastTrig = {false, 0, 0, 0, 0};

void fmtTime(char* b, size_t n, uint32_t ms) {
  uint32_t ds = ms / 100;
  snprintf(b, n, "%lu:%02lu:%02lu.%lu", (unsigned long)(ds / 36000), (unsigned long)(ds / 600 % 60),
           (unsigned long)(ds / 10 % 60), (unsigned long)(ds % 10));
}

void logFlush() {
  if (!gSet.listenLog) { g_logLen = 0; return; }   // SETTINGS > System > Debug > Listen Log
  if (g_logLen == 0 || !gSdReady || exportBusy()) return;
  {
    SdLock lk;
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (f) {
      if (f.size() > LOG_MAX_BYTES) g_logFull = true;
      else f.write((const uint8_t*)g_log, g_logLen);
      f.close();
    }
  }
  g_logLen = 0;
  g_lastFlush = millis();
  if (g_logFull) uiToast("Listen log full (4 MB)");
}

void logLine(const char* s) {
  if (g_logFull || !gSet.listenLog) return;
  int n = (int)strlen(s);
  if (g_logLen + n + 1 > (int)sizeof(g_log)) logFlush();
  if (g_logLen + n + 1 > (int)sizeof(g_log)) return;   // could not flush (export running)
  memcpy(g_log + g_logLen, s, n);
  g_logLen += n;
  g_log[g_logLen++] = '\n';
}

void logHeaderOnce() {
  if (g_logHeader || !gSet.listenLog) return;
  g_logHeader = true;
  logLine("time,type,mood,vol,sens,state,gated,F_in,B_in,F_out,B_out,F_rise,B_rise,out_rise,own_dist,floor,attention,lag,slot,other_slot,own_onsets");
  char b[96], t[16];
  fmtTime(t, sizeof(t), millis());
  snprintf(b, sizeof(b), "%s,START,%s %s,,,,,,,,,,,,,,,,,,", t, FW_NAME, FW_VERSION);   // first entry since power-on
  logLine(b);
}

float latMeasured() { return g_lags.n >= 5 ? g_lags.pct(0.5f) : -1.f; }

// One CSV row. type: W (a window before an event), HUSH (started one), NEAR
// (a room rise close to the bar), ONCE (over it for a single window), REST
// (after a burst of Hushes), OWN / OUT (over it, but our own onset / our own
// output rose), QUIET (over it, but too near the room floor), KEY / MOVE,
// FALSE / MISS (user marks), STAT (every 10 s)
void logRow(const char* type, const WinRec& r) {
  static const char* const ST[] = {"idle", "drop", "hold", "noticed", "recover", "cooldown"};
  char names[160] = "";
  int nl = 0;
  for (int i = 0; i < NUM_LAYERS && nl < (int)sizeof(names) - 12; i++) {
    if (!(r.own & (1u << i))) continue;
    nl += snprintf(names + nl, sizeof(names) - nl, "%s%s", nl ? "|" : "", LAYER_DEFS[i].name);
  }
  if ((r.own & Engine::ONSET_ENGINE) && nl < (int)sizeof(names) - 8) snprintf(names + nl, sizeof(names) - nl, "%sCLICK", nl ? "|" : "");
  char t[16], b[400], od[8] = "";
  fmtTime(t, sizeof(t), r.ms);
  if (r.ownDist != 99) snprintf(od, sizeof(od), "%d", r.ownDist);
  snprintf(b, sizeof(b), "%s,%s,\"%s\",%d,%s,%s,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%.1f,%.2f,%.1f,%c,%.1f,%s",
           t, type, appMoodName(), gSet.volume, LISTEN_SENS_NAMES[gSet.listenSens % NUM_LISTEN_SENS],
           ST[r.state % 6], r.flags & 1, r.inF * 0.1f, r.inB * 0.1f, r.outF * 0.1f, r.outB * 0.1f,
           r.riseF * 0.1f, r.riseB * 0.1f, r.outRise * 0.1f, od, g_floor, g_att, latMeasured(),
           (r.flags & 2) ? 'R' : 'L', r.other * 0.1f, names);
  logLine(b);
}

// The last `count` windows up to window `at` that are not in the log yet,
// `at` itself as the event row, then the windows after it (v0.36: a deferred
// Hush is logged on the window that caused it)
void logEventAt(const char* type, int count, uint32_t at) {
  if (!gSdReady || g_logFull || g_seq == 0) return;
  logHeaderOnce();
  if (at >= g_seq) at = g_seq - 1;
  uint32_t from = at + 1 > (uint32_t)count ? at + 1 - count : 0;
  if (from < g_dumpedTo) from = g_dumpedTo;
  if (g_seq - from > (uint32_t)REC_N) from = g_seq - REC_N;
  if (at < from) logRow(type, g_rec[at % REC_N]);   // its windows are in the log already
  for (uint32_t s = from; s < g_seq; s++) logRow(s == at ? type : "W", g_rec[s % REC_N]);
  g_dumpedTo = g_seq;
}
// ... the event row being the newest window
void logEvent(const char* type, int count) { if (g_seq) logEventAt(type, count, g_seq - 1); }

WinRec& newRec(const MicWindow& w) {
  WinRec& r = g_rec[g_seq % REC_N];
  r = WinRec{};
  r.seq = g_seq; r.ms = millis();
  r.inF = q10(w.inDb); r.inB = q10(w.inBandDb); r.outF = q10(w.outDb); r.outB = q10(w.outBandDb);
  r.other = q10(w.otherDb);
  r.ownDist = 99;
  r.state = (uint8_t)g_state;
  r.flags = (uint8_t)((w.slot ? 2 : 0) | 1);
  g_seq++;
  return r;
}

// --- one window ------------------------------------------------------------------
// Push the window into the history; returns the output's own rise in it
void pushHistory(const MicWindow& w) {
  HistWin& h = g_hist[g_m % HIST_N];
  h.inF = w.inDb; h.inB = w.inBandDb; h.outF = w.outDb; h.outB = w.outBandDb;
  h.own = w.own;
  h.outRise = 0.f;
  h.inRise = 0.f;
  // Our output's rise is taken against the minimum, not the median: the
  // mic's view of it is smeared (room, timing), and coming back after a short
  // dip (STATIC's dropouts) has to count as ours as well.
  if (g_histN >= BASE_N) {
    float bF = minOf(g_m, [](const HistWin& x) { return x.outF; });
    float bB = minOf(g_m, [](const HistWin& x) { return x.outB; });
    h.outRise = std::max(w.outDb - bF, w.outBandDb - bB);
  }
  g_m++;
  if (g_histN < HIST_N) g_histN++;
}

}  // namespace

void listenLogFlush() { logFlush(); }

void listenNoteKey() { g_lastKeyMs = millis(); g_keyUntil = g_lastKeyMs + KEY_HOLD_MS; }

void listenNoteMove() {
  g_lastKeyMs = g_lastMoveMs = millis();
  g_keyUntil = g_lastKeyMs + KEY_HOLD_MS;   // same hold as for keys
  g_moveUntil = g_keyUntil;                 // ... logged as MOVE
}

bool listenResting() { return timeBefore(millis(), g_restUntil); }

bool listenMoving() { return timeBefore(millis(), g_moveUntil); }

void listenMark(bool falseHush) {
  if (!gSet.markKeys) return;   // SETTINGS > System > Debug > Mark Keys
  if (!gSet.listenLog) { uiToast("Listen Log is off (Debug)"); return; }
  if (!gSet.listen) { uiToast("Listen is off"); return; }
  if (!gSdReady) { uiToast("No SD card"); return; }
  logEvent(falseHush ? "FALSE" : "MISS", DUMP_MARK);
  logFlush();   // at once, so the card can be taken out
  uiToast(falseHush ? "Marked: false Hush" : "Marked: no reaction");
}

bool listenLastTrigger(uint32_t& ms, float& riseF, float& riseB, int& layer) {
  if (!g_lastTrig.valid) return false;
  ms = g_lastTrig.ms; riseF = g_lastTrig.riseF; riseB = g_lastTrig.riseB;
  layer = -1;
  for (int i = 0; i < NUM_LAYERS; i++)   // the first layer with an onset of ours just before
    if (g_lastTrig.ownRecent & (1u << i)) { layer = i; break; }
  return true;
}

// ---------------------------------------------------------------------------
bool listenAvailable() { return g_avail && audioIoReady(); }

bool listenEnable(bool on) {
  if (!on) { gSet.listen = false; return true; }
  if (!listenAvailable() || !audioIoMicEnable(true)) { g_avail = false; gSet.listen = false; return false; }
  gSet.listen = true;
  resetHistory();
  return true;
}

void listenSetHelpTest(bool on) { g_help = on; }

// SETTINGS > Listen > Recalibrate (spec v1.21 3.8): the room floor and the
// measured output -> mic lag are learned afresh (the lag used stays fixed)
// Shake's Hush (spec v1.22 1.5): the same Hush as Listen's, with Listen off
// too; Hush Depth and Response apply. Not while it is down already (coming
// back, it may Hush again). It does not count towards Listen's rest after a
// burst of Hushes. Returns false when nothing happened.
bool listenShakeHush() {
  if (g_state == ST_DROP || g_state == ST_HOLD || g_state == ST_NOTICED) return false;
  g_state = ST_DROP; g_t = 0.f;
  g_mulFrom = g_mul;
  if (gSet.listen) g_att *= 0.5f;
  g_notice = g_r.chance(NOTICED_CHANCE) && gSet.response;
  g_noticeLayer = -1;
  g_hushes++;   // HEART skips a beat and races (hushSeq)
  g_shakeHush = true;
  return true;
}

void listenRecalibrate() {
  g_floorR.clear();
  g_floor = FLOOR_INIT;
  g_lags.clear();
  g_lastOwnStrong = 0;
  g_latTaken = true;
  resetHistory();
}
bool listenHelpTest() { return g_help; }
float listenAttention() { return g_att; }
bool listenHushing() { return g_state >= ST_DROP && g_state <= ST_RECOVER; }

void listenWatch(ListenWatch& w) {
  switch (g_state) {
    case ST_DROP: w.stage = WS_DROP; w.dur = DROP_SEC; break;
    case ST_HOLD: w.stage = WS_HOLD; w.dur = g_dur; break;
    case ST_NOTICED: w.stage = WS_NOTICED; w.dur = g_dur; break;
    case ST_RECOVER: w.stage = WS_RECOVER; w.dur = g_dur; break;
    default: w.stage = WS_NONE; w.dur = 0.f; break;
  }
  w.t = g_t;
  w.hushes = (uint32_t)g_hushes;
  w.responses = g_responses;
}

int listenDreadAdd() { return g_effects ? (int)lroundf(DREAD_MAX * g_att) : 0; }
float listenDensMul() { return g_effects ? 1.f + g_att : 1.f; }

void listenUpdate(float dt) {
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; }

  // --- the microphone: on for Listen or for HELP's mic test -----------------
  // (v0.26: a mic that fails stays failed for this session only; the setting
  // is kept, so Listen is back after the next start)
  const bool shy = poltergeistShyListening();   // Poltergeist's Shy (v0.93): the mic, its own way
  // (lurking without Shy: the mic is off too -- nothing listens then, power)
  const bool pgDeaf = poltergeistLurking() && !shy;
  bool want = ((gSet.listen || g_help) && !pgDeaf || shy) && listenAvailable();
  if (want != audioIoMicOn()) {
    if (!audioIoMicEnable(want) && want) {
      g_avail = false;
      uiToast("Mic not available");
    }
  }
  bool micOn = audioIoMicOn();
  if (micOn && !g_micWas) resetHistory();
  g_micWas = micOn;

  // --- when not to listen (spec 5.6) ------------------------------------------
  const bool hardStop = exportBusy() || appSleepFading() || powerSleeping() || partyActive() || planchetteActive() || poltergeistOwnsAudio();   // a party, PLANCHETTE, Poltergeist: no Hush
  const bool listening = (gSet.listen && micOn && gPlaying && !hardStop) || (shy && micOn);
  g_effects = gSet.listen && micOn && !hardStop;
  if (!gSet.listen) g_att = 0.f;
  // Hush in progress but Listen stopped: come back quickly instead of jumping
  if (((!gSet.listen && !g_shakeHush) || hardStop) && hushActive()) startRecover(2.f);
  if (g_shakeHush && (g_state == ST_IDLE || g_state == ST_COOLDOWN)) g_shakeHush = false;

  // --- changes that alter what we play: the baseline starts afresh -------------
  if (gSet.volume != g_lastVol || gCurScene != g_lastScene || gPlaying != g_lastPlaying ||
      strncmp(g_lastName, appMoodName(), sizeof(g_lastName) - 1) != 0) {
    g_lastVol = gSet.volume; g_lastScene = gCurScene; g_lastPlaying = gPlaying;
    strncpy(g_lastName, appMoodName(), sizeof(g_lastName) - 1);
    g_lastName[sizeof(g_lastName) - 1] = 0;
    resetHistory();
  }

  // --- measurement windows (~23 ms each) -----------------------------------------
  MicWindow w;
  while (audioIoMicPop(w)) {
    if (!g_avail) continue;   // given up on the mic: drop what is still queued
    const float wsec = (float)w.n / SR;
    // --- a mic that has gone silent or frozen (v0.26) ---------------------------
    // A working mic never reads below about -90 dBFS, even in a silent room
    // with nothing playing. Near-digital silence (or a constant) for most of
    // the last 10 s means the ADC is not delivering: try the recovery ladder.
    const bool dead = w.frozen || w.inDb <= DEAD_DB;
    g_deadSum += (int)dead - (int)g_deadHist[g_deadIdx];
    g_deadHist[g_deadIdx] = dead;
    g_deadIdx = (g_deadIdx + 1) % DEAD_N;
    if (w.slot != g_lastSlot) { g_lastSlot = w.slot; logEvent(w.slot ? "SLOT_R" : "SLOT_L", DUMP_NEAR); }
    if (dead) {
      g_healthy = 0;
      g_gated = true;   // a dead ADC must not read as a quiet room
      newRec(w);
      resetHistory();   // its -96 dB must not become a baseline
      if (g_recWait > 0) { g_recWait--; continue; }
      if (g_deadSum >= DEAD_LIMIT) {
        g_deadSum = 0;
        for (auto& h : g_deadHist) h = false;
        if (++g_recStage <= 3) {
          char t[12];
          snprintf(t, sizeof(t), "RECOV%d", g_recStage);
          logEvent(t, DUMP_NEAR);
          g_recoveries++;
          audioIoMicRecover(g_recStage);
          g_recWait = RECOVER_WAIT_WINDOWS;
        } else {
          logEvent("MICDEAD", DUMP_NEAR);
          logFlush();
          g_avail = false;   // for this session; want = false switches the ADC off
          uiToast("Mic not responding");
          break;
        }
      }
      continue;
    }
    if (g_recStage > 0 && ++g_healthy >= HEALTHY_WINDOWS) {
      char t[12];
      snprintf(t, sizeof(t), "MICOK%d", g_recStage);   // the stage that brought it back
      logEvent(t, DUMP_NEAR);
      g_recStage = 0;
    }
    if (g_recWait > 0) g_recWait--;

    const uint32_t now = millis();
    const bool keyGate = timeBefore(now, g_keyUntil);   // the mic hears the keyboard

    // Room floor: only while our own output is practically silent
    // (v0.986: nor while the codec's output has just been switched -- its click)
    const bool shyDeaf = shy && poltergeistShyDeaf();
    if (!keyGate && !shyDeaf && w.outDb < OWN_SILENT_DB) g_floorR.push(w.inDb);

    pushHistory(w);
    const uint32_t m = g_m - 1;   // this window
    g_settle += wsec;
    const bool haveBase = g_histN > BASE_N;
    const bool settled = haveBase && g_settle >= SETTLE_SEC;
    g_gated = !settled || !listening;

    // --- rise at the mic, and what of ours could explain it -----------------
    float riseF = 0.f, riseB = 0.f;
    if (haveBase) {
      riseF = w.inDb - baseOf(m, [](const HistWin& x) { return x.inF; });
      riseB = w.inBandDb - baseOf(m, [](const HistWin& x) { return x.inB; });
    }
    const float rise = std::max(riseF, riseB);
    g_hist[m % HIST_N].inRise = rise;
    // the output's own rise where this window's sound was written (lag applied)
    float outNear = -99.f;
    for (int d = -OUT_TOL; d <= OUT_TOL; d++) {
      int k = (int)m - LAT_WIN + d;
      if (k < 0 || (int)m - k >= g_histN) continue;
      outNear = std::max(outNear, g_hist[(uint32_t)k % HIST_N].outRise);
    }
    const float bar = RISE_DB[gSet.listenSens < NUM_LISTEN_SENS ? gSet.listenSens : 1];
    // an onset the engine reported, heard from 20 ms before to 250 ms after
    uint32_t ownMask = 0;
    int ownDist = 99;
    for (int d = -OWN_BEFORE - 1; d <= OWN_AFTER; d++) {   // d = windows since it was heard
      int k = (int)m - LAT_WIN - d;
      if (k < 0 || (int)m - k >= g_histN) continue;
      const uint32_t o = g_hist[(uint32_t)k % HIST_N].own;
      if (!o) continue;
      // In its tail: did the mic hear it at all, and is this window louder
      // than it was by the bar again? Then it is a new sound, not its ring.
      // While it is due: it can only explain a rise up to what it did to our
      // output plus the same allowance (v0.34; HEART's beats hardly reach
      // the speaker, and a +21 dB sound on one was left out in v0.33).
      if (d <= OWN_HEARD_SPAN) {
        float oRise = g_hist[(uint32_t)k % HIST_N].outRise;
        if (k + 1 <= (int)m) oRise = std::max(oRise, g_hist[(uint32_t)(k + 1) % HIST_N].outRise);
        if (rise >= std::max(oRise, 0.f) + OUT_EXCESS_DB) continue;
      } else {
        bool heard = false;
        float pkF = -96.f, pkB = -96.f;
        for (int e = -OWN_BEFORE; e <= OWN_HEARD_SPAN; e++) {
          const HistWin& h = g_hist[(uint32_t)(k + LAT_WIN + e) % HIST_N];
          heard |= h.inRise >= OWN_HEARD_DB;
          pkF = std::max(pkF, h.inF); pkB = std::max(pkB, h.inB);
        }
        if (!heard) continue;
        if (w.inDb >= pkF + bar || w.inBandDb >= pkB + bar) continue;
      }
      ownMask |= o;
      if (abs(d) < abs(ownDist)) ownDist = d;
    }
    const bool ownNear = ownMask != 0;
    const bool outRose = outNear >= OUT_RISE_DB && rise < outNear + OUT_EXCESS_DB;

    // --- the lag, measured on our own isolated onsets (HELP / log) -------------
    {
      const HistWin& h = g_hist[m % HIST_N];
      if (h.own && h.outRise >= LAT_OUT_DB) {
        bool isolated = g_lastOwnStrong == 0 || m + 1 - g_lastOwnStrong >= (uint32_t)LAT_ISOLATED;
        for (int j = 1; j < LAT_ISOLATED && isolated && j < g_histN; j++)
          if (g_hist[(m - j) % HIST_N].own) isolated = false;
        g_lastOwnStrong = m + 1;
        g_latTaken = !isolated;
      }
      if (!g_latTaken && listening && riseF >= LAT_IN_DB && g_lastOwnStrong) {
        int lag = (int)(m + 1 - g_lastOwnStrong);
        if (lag >= 1 && lag <= LAT_SEARCH) { g_lags.push((float)lag); g_latTaken = true; }
        else if (lag > LAT_SEARCH) g_latTaken = true;
      }
    }

    // keep the window for HELP and the diagnostic log
    g_inF = w.inDb; g_inB = w.inBandDb; g_outF = w.outDb; g_outB = w.outBandDb;
    g_riseF = riseF; g_riseB = riseB; g_outRiseNear = outNear; g_ownDist = ownDist;
    WinRec& r = newRec(w);
    r.riseF = q10(riseF); r.riseB = q10(riseB); r.outRise = q10(outNear < -50.f ? 0.f : outNear);
    r.own = ownMask; r.ownDist = (int8_t)ownDist;
    if (!g_gated) r.flags &= ~1;

    if (g_gated) { g_overPrev = false; continue; }
    // Already silent (dropping / holding / the lone event): nothing to judge.
    // While coming back (RECOVER) a loud sound may Hush again (v0.25).
    if (g_state == ST_DROP || g_state == ST_HOLD || g_state == ST_NOTICED) { g_overPrev = false; continue; }
    if (g_pend.on) { g_overPrev = false; continue; }   // one is about to start

    // --- the judgement (spec v1.21 3.2) ------------------------------------
    const float need = bar + (g_state == ST_RECOVER ? RECOVER_MARGIN_DB : 0.f);
    const int sens = gSet.listenSens < NUM_LISTEN_SENS ? gSet.listenSens : 1;
    const bool loudEnough = w.inDb >= g_floor + LEVEL_MARGIN_DB[sens];
    const bool room = loudEnough && !ownNear && !outRose && !keyGate;
    const bool resting = timeBefore(now, g_restUntil);
    const bool over = room && rise >= need;
    // just over the bar: only if the previous window was over it too
    const float instant = (w.inDb < g_floor + QUIET_LEVEL_DB) ? CONFIRM_QUIET_DB : CONFIRM_DB;
    const bool sure = over && (rise >= need + instant || g_overPrev);
    g_overPrev = over;
    // Shy: a sound in the room only keeps Poltergeist quiet (no Hush, no rest)
    // ... and Shy does not judge then either (v0.986)
    if (shyDeaf) { g_shyLoud = 0.f; continue; }
    if (shy && sure) { poltergeistHeard(); continue; }
    // ... and so does a sound that goes on (a voice): 1.5 s loud in the room
    if (shy) {
      g_shyLoud = (loudEnough && !ownNear && !outRose && w.inDb >= g_floor + SHY_LOUD_DB) ? g_shyLoud + wsec : 0.f;
      if (g_shyLoud >= SHY_LOUD_SEC) { g_shyLoud = 0.f; poltergeistHeard(); }
      continue;
    }
    if (sure && !resting) {
      // our own onsets in the last second, for HELP ("what was playing")
      uint32_t recent = 0;
      for (int j = 0; j < wins(1.f) && j < g_histN; j++) recent |= g_hist[(m - j) % HIST_N].own;
      g_pend = {true, now, r.seq, riseF, riseB, recent};   // starts after DEFER_MS (below)
      continue;
    }
    // a sound in the room (close to the bar, or over it but not confirmed):
    // Attention stops rising for a while (spec v1.21 3.5)
    const float nearBar = std::max(bar - NEAR_DB, NEAR_MIN_DB);
    if (room && rise >= nearBar) g_attPauseUntil = now + ATT_PAUSE_MS;
    // close to / over the bar without a Hush: why (rate-limited)
    if (rise >= nearBar && now - g_lastNear > NEAR_GAP_MS) {
      const char* why;
      if (rise < need)       why = "NEAR";
      else if (keyGate)      why = timeBefore(now, g_moveUntil) ? "MOVE" : "KEY";
      else if (ownNear)      why = "OWN";
      else if (outRose)      why = "OUT";
      else if (!loudEnough)  why = "QUIET";
      else if (resting)      why = "REST";
      else                   why = "ONCE";
      logEvent(why, DUMP_NEAR);
      g_lastNear = now;
    }
  }

  // --- a Hush waiting to start: called off by a key or a movement noted late --
  if (g_pend.on) {
    const uint32_t now = millis();
    const bool late = (int32_t)(g_lastKeyMs - (g_pend.ms - LATE_KEY_MS)) >= 0;
    if (late) {
      g_pend.on = false;
      logEventAt(g_lastMoveMs == g_lastKeyMs ? "LATEMOVE" : "LATEKEY", DUMP_NEAR, g_pend.seq);
    } else if (!listening || g_gated) {
      g_pend.on = false;
    } else if (now - g_pend.ms >= DEFER_MS) {
      g_pend.on = false;
      startHush();
      g_rec[g_pend.seq % REC_N].state = (uint8_t)g_state;
      g_lastTrig = {true, g_pend.ms, g_pend.riseF, g_pend.riseB, g_pend.recent};
      logEventAt("HUSH", DUMP_HUSH, g_pend.seq);
    }
  }

  // Attention: rises while nothing happens in the room (spec v1.21 3.5),
  // not while the sound is down or coming back
  if (listening && !g_gated && (g_state == ST_IDLE || g_state == ST_COOLDOWN) &&
      !timeBefore(millis(), g_attPauseUntil))
    g_att = std::min(1.f, g_att + dt / ATT_RISE_SEC);

  // A summary row every 10 s while Listen runs; the buffer goes to SD every 15 s
  if (gSet.listen && micOn && g_seq > 0) {
    const uint32_t now = millis();
    if (now - g_lastStat >= 10000) { g_lastStat = now; logHeaderOnce(); logRow("STAT", g_rec[(g_seq - 1) % REC_N]); }
    if (g_logLen > 0 && now - g_lastFlush >= FLUSH_MS) logFlush();
  }

  g_refreshT += dt;
  if (g_refreshT >= 1.f) { g_refreshT = 0.f; if (g_floorR.n >= 20) g_floor = g_floorR.pct(0.2f); }

  if (g_state != ST_IDLE) stepHush(dt);
}

void listenApply(EngineParams& p) {
  p.hushSeq = (uint32_t)g_hushes;   // HEART: a Hush skips a beat (v0.31)
  // HEART's React: Attention adds to the tension DREAD already gives
  if (g_effects) p.tension = 1.f - (1.f - p.tension) * (1.f - 0.6f * g_att);
  if (g_mul >= 1.f) return;
  for (int i = 0; i < NUM_LAYERS; i++) {
    if (i == L_REVERB) continue;   // its input falls silent; the tail dies away naturally
    if (i == g_noticeLayer && (g_state == ST_NOTICED || g_state == ST_RECOVER)) continue;
    p.L[i].level *= g_mul;
  }
}

void listenDebug(ListenDebug& d) {
  d.inDb = g_inF; d.inBandDb = g_inB; d.outDb = g_outF; d.outBandDb = g_outB;
  d.riseF = g_riseF; d.riseB = g_riseB;
  d.outRise = g_outRiseNear < -50.f ? 0.f : g_outRiseNear;
  d.floorDb = g_floor;
  d.ownDist = g_ownDist;
  d.latFixed = LAT_WIN;
  d.latMeasured = latMeasured();
  d.gated = g_gated;
  d.state = (int)g_state;
  d.hushes = g_hushes;
  d.recoveries = g_recoveries;
}
