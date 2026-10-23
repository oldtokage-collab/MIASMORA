// MIASMORA — how a one-shot is played (v0.72, spec v1.1 8.3 / v1.2 5.3).
// Shared by the exporter (export.cpp) and the PC test bench.
//
// The chosen layer plays alone and dry, its own scheduling off (Density 0):
// every sound is a Summon at a planned time, on a random stream of its own
// per file. HEART's regular beats are held off (its Cursed heart stop) and its
// beats summoned; its blood-flow noise never falls silent, so it ends by time,
// with a fade.
//   STEPS KNOCK BUMP CLANG -- one event is already a gesture (a walk, a
//       group of knocks, a cluster, a hit and its re-hits): Gesture = that
//       event; Single = its first hit, cut where the next one begins (one
//       buffer is held back so the cut lands just before it)
//   the other event layers -- Single = one event; Gesture = 2..6 of them
//   HEART                  -- Single = 1 beat, Gesture = 8 beats
// A file starts with 0.1 s of silence and ends 0.3 s after the last buffer
// above -70 dBFS -- found in a first pass, with room for the gaps inside a
// gesture (KNOCK's taps are seconds apart) -- or at the layer's longest, faded.
#pragma once
#include <string.h>
#include "audio.h"
#include "scene.h"

namespace oneshot {

constexpr float LEAD = 0.1f, FLOOR = 0.000316f, TAIL = 0.3f;   // -70 dBFS
constexpr float GAIN = 0.72f;   // the files' fixed master (EXPORT_GAIN in export.h)
constexpr int MAX_EV = 8;

inline bool groupLayer(int id) { return id == L_STEPS || id == L_KNOCK || id == L_BUMP || id == L_CLANG; }

struct Gesture { uint8_t id, nMin, nMax; float gMin, gMax; };
static const Gesture GEST[] = {
  {L_PULSE, 3, 6, 0.4f, 1.2f}, {L_CREAK, 3, 5, 0.6f, 1.6f}, {L_SWELL, 2, 3, 2.0f, 4.0f}, {L_DRIP, 3, 6, 0.3f, 1.0f},
  {L_SCRAPE, 2, 4, 0.8f, 2.5f}, {L_DRAG, 2, 2, 4.0f, 6.0f}, {L_SKITTER, 2, 3, 1.5f, 3.0f}, {L_INSECT, 2, 2, 3.0f, 5.0f},
  {L_DRILL, 2, 2, 6.0f, 8.0f},
};

struct Plan {
  float t[MAX_EV];   // event times (s)
  int n;
  float maxLen;      // the longest a file may be (s); reached: faded
  float gapTol;      // silence allowed inside the sound before it counts as over (s)
  float endAt;       // > 0: ends here, faded (HEART)
};

// gesture: Unit = Gesture; st: the layer's parameters
inline Plan plan(int id, bool gesture, const LayerState& st, uint32_t fileSeed, bool wet) {
  dsp::Rng r; r.seed(dsp::deriveSeed(fileSeed, 0x05A7u));
  Plan P;
  P.n = 1; P.t[0] = LEAD; P.endAt = 0.f; P.gapTol = 0.5f;
  P.maxLen = gesture ? 15.f : 6.f;
  if (id == L_HEART) {
    const float period = 60.f / (40.f + st.p[0]), irr = st.p[1] * 0.01f;   // BPM 40..140
    if (gesture) { P.n = 8; for (int k = 1; k < P.n; k++) P.t[k] = P.t[k - 1] + period * (1.f + r.range(-0.04f, 0.04f) * irr); }
    P.endAt = P.t[P.n - 1] + 0.9f;
    P.maxLen = P.endAt + 0.1f;
  } else if (groupLayer(id)) {
    P.gapTol = id == L_KNOCK ? 4.f : (id == L_BUMP ? 1.f : 1.5f);
  } else {
    if (id == L_DRILL) P.maxLen = gesture ? 15.f : 12.f;
    if (id == L_DRAG) P.maxLen = gesture ? 15.f : 10.f;   // a drag lasts 2..8 s
    if (id == L_SKITTER || id == L_INSECT || id == L_DRILL) P.gapTol = 1.f;
    if (gesture) {
      for (const Gesture& g : GEST) if (g.id == id) {
        P.n = r.irange(g.nMin, g.nMax);
        for (int k = 1; k < P.n; k++) P.t[k] = P.t[k - 1] + r.range(g.gMin, g.gMax);
      }
    }
  }
  if (wet && P.endAt <= 0.f) P.maxLen += 4.f;   // room for the reverb's tail
  return P;
}

// Plays one file through emit(float* buf, int n) -> peak (the caller adds
// its reverb, measures, dithers, writes). lastLoud < 0: the first pass --
// finds and returns the last emitted buffer above -70 dBFS; else the second
// pass -- stops TAIL after that one (fading the very end if it had to stop
// early). Returns lastLoud; *faded = it ended by time, not by silence.
// p, cur, held: working memory (the exporter allocates it per export).
template <class Emit>
int play(int id, bool gesture, const LayerState& st, uint32_t fileSeed, bool wet, int lastLoud,
         EngineParams& p, float* cur, float* held, Emit emit, bool* faded = nullptr, volatile bool* cancel = nullptr) {
  const Plan P = plan(id, gesture, st, fileSeed, wet);
  const bool cutSingle = !gesture && groupLayer(id);
  SceneData s;
  sceneDefaults(s, "");
  s.L[id] = st;
  s.L[id].on = true;
  s.L[id].level = 70;
  if (layerIsEvent(id)) s.L[id].p[0] = 0;   // Density 0: only the summoned events
  buildEngineParams(s, GAIN, fileSeed, 0, p);
  p.L[L_REVERB].level = 0.f;
  gEngine.reset(fileSeed, 0, p, true);
  const int maxBufs = (int)(P.maxLen * SR / BUF) + 1;
  const int endBuf = P.endAt > 0.f ? (int)(P.endAt * SR / BUF) : maxBufs;
  const int tailBufs = (int)(TAIL * SR / BUF) + 1;
  const int gapBufs = (int)(P.gapTol * SR / BUF) + 1;
  const int stopAt = lastLoud >= 0 ? lastLoud + tailBufs : 1 << 30;   // second pass: in emitted buffers
  int next = 0, firstOnset = -1, emitted = 0, loud = -1, quiet = 0;
  bool cut = false, haveHeld = false, started = false, byTime = false;
  for (int k = 0; k < maxBufs; k++) {
    if (cancel && *cancel) break;
    const float t1 = (float)(k + 1) * BUF / SR;
    while (next < P.n && P.t[next] < t1) {
      p.summonSeq++;
      p.summonLayer = (int8_t)id;
      p.summonSeed = dsp::deriveSeed(fileSeed, 0x0E00u + (uint32_t)next);
      next++;
    }
    if (id == L_HEART && k == 1) { p.heartStopSeq = 1; p.heartStopBeats = 127; }   // no beats of its own
    gEngine.setParams(p);
    if (!cut) {
      gEngine.render(cur, BUF);
      const bool onset = (gEngine.onsets() & (1u << id)) != 0;
      if (onset && firstOnset < 0) firstOnset = k;
      else if (cutSingle && onset && k - firstOnset >= 7) {   // the next hit begins: fade out just before it
        cut = true;
        const int F = (int)(0.005f * SR);
        for (int i = 0; i < F; i++) held[BUF - 1 - i] *= (float)i / F;
        memset(cur, 0, sizeof(float) * BUF);
      }
      if (id == L_HEART) {   // its flow noise is there from the start: the lead stays silent
        for (int i = 0; i < BUF; i++) {
          const float t = (float)(k * BUF + i) / SR;
          if (t < LEAD - 0.01f) cur[i] = 0.f;
          else if (t < LEAD) cur[i] *= (t - (LEAD - 0.01f)) / 0.01f;
        }
      }
    } else {
      memset(cur, 0, sizeof(float) * BUF);
    }
    // the end by time (HEART) or at the longest: fade the held buffer out
    const bool lastBuf = k + 1 >= endBuf || k + 1 >= maxBufs;
    if (haveHeld) {
      if (lastLoud >= 0 && emitted >= stopAt) break;
      const float pk = emit(held, BUF);
      if (pk >= FLOOR) { loud = emitted; started = true; quiet = 0; } else quiet++;
      emitted++;
    }
    memcpy(held, cur, sizeof(float) * BUF);
    haveHeld = true;
    if (lastBuf) {
      // the last one, faded to nothing
      for (int i = 0; i < BUF; i++) held[i] *= 1.f - (float)i / BUF;
      if (!(lastLoud >= 0 && emitted >= stopAt)) {
        const float pk = emit(held, BUF);
        if (pk >= FLOOR) loud = emitted;
        emitted++;
      }
      haveHeld = false;
      byTime = true;
      break;
    }
    // over: every event fired, and quiet for longer than the gaps inside it
    if (lastLoud < 0 && started && next >= P.n && (float)k * BUF / SR > P.t[P.n - 1] + 0.2f && quiet >= gapBufs) break;
  }
  if (faded) *faded = byTime;
  return loud;
}

}  // namespace oneshot
