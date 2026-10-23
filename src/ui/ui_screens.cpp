// MIASMORA — MOOD, EXPORT and HELP / ABOUT screens (SETTINGS: ui_settings.cpp)
#include "ui_internal.h"
#include "../audio.h"
#include "../audio_io.h"
#include "../listen.h"
#include "../export.h"
#include "../rec.h"
#include "../storage.h"
#include "../cursed.h"
#include "../arc.h"
#include "../party.h"

// ===========================================================================
// MOOD
// ===========================================================================
static int g_moodCur = 0, g_moodScroll = 0;
static bool g_moodSynced = false;

void drawMood() {
  int n = (int)gScenes.size();
  if (!g_moodSynced) { g_moodCur = gCurScene >= 0 ? gCurScene : 0; g_moodSynced = true; }
  if (g_moodCur >= n) g_moodCur = n - 1;
  if (g_moodCur < 0) g_moodCur = 0;
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "%d/%d", g_moodCur + 1, n);
  drawHeader("MOOD", hdr);
  const int ROW_H = 11, TOP = 14, VIS = 10;
  if (g_moodCur < g_moodScroll) g_moodScroll = g_moodCur;
  if (g_moodCur >= g_moodScroll + VIS) g_moodScroll = g_moodCur - VIS + 1;
  for (int v = 0; v < VIS && g_moodScroll + v < n; v++) {
    int i = g_moodScroll + v;
    const SceneEntry& e = gScenes[i];
    int y = TOP + v * ROW_H;
    bool sel = i == g_moodCur;
    if (sel) canvas.fillRect(0, y, SCREEN_W, ROW_H, C_SEL);
    if (i == gCurScene) canvas.fillTriangle(4, y + 2, 4, y + 8, 8, y + 5, C_ACC2);
    drawText(e.name.c_str(), 14, y + 2, sel ? C_ACC2 : C_INK);
    const char* tag = e.kind == SK_DRIFT ? "drift" : (e.kind == SK_CUSTOM ? "custom" : "");
    drawText(tag, SCREEN_W - 4, y + 2, C_DIM, 1, textdatum_t::top_right);
  }
  drawNav("[;.]Sel [Enter]Play [N]Save as [D]el");
}

bool keyMood(const KeyEv& ev) {
  if (ev.shift) return false;
  int n = (int)gScenes.size();
  if (ev.esc) { g_moodSynced = false; return false; }   // global handler goes back to MAIN
  if (ev.k == ';') { g_moodCur = (g_moodCur + n - 1) % n; return true; }
  if (ev.k == '.') { g_moodCur = (g_moodCur + 1) % n; return true; }
  if (ev.enter) {
    appSelectScene(g_moodCur);
    uiToast(gScenes[g_moodCur].name.c_str());
    return true;
  }
  if (ev.k == 'n') {
    if (!gSdReady) { uiToast("No SD card"); return true; }
    const char* base = appMoodName();
    openTextInput(TXT_SAVE_AS, (gCurScene >= 0 && gScenes[gCurScene].kind == SK_CUSTOM) ? base : "");
    return true;
  }
  if (ev.k == 'd') {
    if (g_moodCur >= 0 && g_moodCur < n && gScenes[g_moodCur].kind == SK_CUSTOM) {
      char m[40];
      snprintf(m, sizeof(m), "Delete \"%s\"?", gScenes[g_moodCur].name.c_str());
      openConfirm(m, g_moodCur);
    } else {
      uiToast("Presets can't be deleted");
    }
    return true;
  }
  return false;
}

// ===========================================================================
// EXPORT
// ===========================================================================
enum XRow { XR_MODE = 0, XR_LEN, XR_XF, XR_SEED, XR_DRIFT, XR_ARC, XR_STEMS, XR_NORM, XR_BATCH, XR_BFOLDER, XR_BSLEEP, XR_START, XR_ONESHOT, XR_N };
// EXPORT > One-shot (v0.72): its own page
enum ORow { OR_LAYER = 0, OR_MODE, OR_UNIT, OR_COUNT, OR_REVERB, OR_NORM, OR_START, OR_N };
static bool g_osPage = false;
static int g_oCur = OR_START;
// the event layers, then HEART
static int osLayers(int* ids) {
  int n = 0;
  for (int i = 0; i < NUM_LAYERS; i++) if (layerIsEvent(i)) ids[n++] = i;
  ids[n++] = L_HEART;
  return n;
}
static int osLayerId() { int ids[NUM_LAYERS]; const int n = osLayers(ids); return ids[gSet.oLayerIdx % n]; }
// the layer's mode: its first option parameter (-1: none)
static int osModeParam(int id) {
  const LayerDef& d = LAYER_DEFS[id];
  for (int k = 0; k < d.nParams; k++) if (d.p[k].nOpts > 1) return k;
  return -1;
}
static bool g_sleepAfterPending = false;   // a batch with Sleep After is running
static const char* const STEM_OPTS[3] = {"Off", "Wet", "Dry + Reverb"};
// v0.52: EXPORT > Arc -- the section mark in the file name with Apply
static const char* const ARC_TAGS[4] = {"_ARCcm", "_ARCbd", "_ARCpk", "_ARCrl"};
static bool arcApplies() { return arcShown() && gSet.xArcApply; }
static const char* arcTagNow() { return arcApplies() ? ARC_TAGS[arcFinished() ? 0 : arcSection() & 3] : ""; }
static int g_xScroll = 0;

// Scrolling list helper shared by EXPORT and SETTINGS: keeps the cursor's
// position among the visible rows inside the window.
static void keepInView(int curPos, int& scroll, int vis) {
  if (curPos < scroll) scroll = curPos;
  if (curPos >= scroll + vis) scroll = curPos - vis + 1;
  if (scroll < 0) scroll = 0;
}
static void drawScrollMarks(int scroll, int vis, int n, int yTop, int yBottom) {
  if (scroll > 0) drawText("^", SCREEN_W - 4, yTop, C_DIM, 1, textdatum_t::top_right);
  if (scroll + vis < n) drawText("v", SCREEN_W - 4, yBottom, C_DIM, 1, textdatum_t::top_right);
}
static int g_xCur = XR_START;
static bool g_xResumed = true;

static bool rowVisible(int r) {
  if (r == XR_XF) return gSet.xMode == EXM_CROSSFADE;
  if (r == XR_DRIFT) return appIsDrift();
  if (r == XR_ARC) return arcShown();   // only with Arc on (off: there is nothing to apply)
  if (r == XR_BFOLDER || r == XR_BSLEEP) return gSet.xBatchIdx != 0;
  return true;
}

bool exportUiActive() { return exportState() != EXS_IDLE; }

void exportUiPoll() {
  // As soon as the export task is done, live playback comes back
  if (!g_xResumed && exportState() != EXS_RUNNING) {
    audioResume();
    g_xResumed = true;
    uiWakeDisplay();   // the screen may have dimmed / gone off meanwhile: show the result
    // a batch with Sleep After: done -> as the sleep timer ends (not after a cancel or a failure)
    if (g_sleepAfterPending && exportState() == EXS_DONE) appSleepNow();
    g_sleepAfterPending = false;
  }
}

static ExportConfig g_xcfg;
static void startExportNow() {
  if (!audioPause(1500)) { audioResume(); uiToast("Audio busy, try again"); return; }
  g_xResumed = false;
  g_sleepAfterPending = g_xcfg.batch && gSet.xBatchSleep;
  if (!exportBegin(g_xcfg)) { g_sleepAfterPending = false; uiToast("Export failed to start"); }
}

static void startOneshot() {
  if (partyActive()) { uiToast("Not now"); return; }
  if (recActive()) { uiToast("Stop REC first"); return; }
  if (!gSdReady) { uiToast("No SD card"); return; }
  if (gSet.xNewSeed) appNewRandomSeed();
  cursedStopAll();
  ExportConfig& cfg = g_xcfg;
  cfg = ExportConfig();
  cfg.oneshot = true;
  const int id = osLayerId();
  cfg.osLayer = (uint8_t)id;
  // its parameters: the scene's if it plays there, else the defaults; the Mode chosen
  if (gRt.live.L[id].on) cfg.osState = gRt.settled().L[id];
  else { SceneData d; sceneDefaults(d, ""); cfg.osState = d.L[id]; }
  const int mk = osModeParam(id);
  String name = LAYER_DEFS[id].name;
  if (mk >= 0) {
    const uint8_t m = gSet.oMode < LAYER_DEFS[id].p[mk].nOpts ? gSet.oMode : 0;
    cfg.osState.p[mk] = m;
    name += "_" + sanitizeName(LAYER_DEFS[id].p[mk].opts[m]);
  }
  if (gSet.oUnit) name += "_G";
  cfg.osName = name;
  cfg.osUnit = gSet.oUnit;
  cfg.osCount = ONESHOT_COUNTS[gSet.oCountIdx % NUM_ONESHOT_COUNTS];
  cfg.osWet = gSet.oWet;
  const LayerState& rv = gRt.live.L[L_REVERB];   // the scene's reverb, or a plain one
  cfg.osRevMix = rv.on ? rv.level * 0.01f : 0.4f;
  cfg.osRevSize = rv.on ? rv.p[0] * 0.01f : 0.6f;
  cfg.osRevDamp = rv.on ? rv.p[1] * 0.01f : 0.5f;
  cfg.normalize = gSet.oNorm;
  cfg.seed = gSeed;
  cfg.batch = 0;
  const uint64_t need = exportEstimateBytes(cfg), freeB = sdFreeBytes();
  if (need > freeB) {
    static char m[48];
    snprintf(m, sizeof(m), "Needs %lu MB, %lu MB free. Start?", (unsigned long)(need >> 20) + 1, (unsigned long)(freeB >> 20));
    openConfirmFn(m, startExportNow);
    return;
  }
  startExportNow();
}

static void drawOneshotPage() {
  drawHeader("EXPORT > One-shot", gSdReady ? "/oneshot" : "NO SD CARD");
  const int id = osLayerId(), mk = osModeParam(id);
  char v[40];
  for (int r = 0; r < OR_N; r++) {
    const int y = 16 + r * 14;
    const bool sel = r == g_oCur;
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, 13, C_SEL);
    const char* label = "";
    bool enabled = true;
    switch (r) {
      case OR_LAYER: label = "Layer"; snprintf(v, sizeof(v), "%s", LAYER_DEFS[id].name); break;
      case OR_MODE:
        label = "Mode";
        if (mk < 0) { snprintf(v, sizeof(v), "-"); enabled = false; }
        else snprintf(v, sizeof(v), "%s", LAYER_DEFS[id].p[mk].opts[gSet.oMode < LAYER_DEFS[id].p[mk].nOpts ? gSet.oMode : 0]);
        break;
      case OR_UNIT: label = "Unit"; snprintf(v, sizeof(v), "%s", gSet.oUnit ? (id == L_HEART ? "Gesture (8 beats)" : "Gesture") : (id == L_HEART ? "Single (1 beat)" : "Single")); break;
      case OR_COUNT: label = "Count"; snprintf(v, sizeof(v), "%d", ONESHOT_COUNTS[gSet.oCountIdx % NUM_ONESHOT_COUNTS]); break;
      case OR_REVERB: label = "Reverb"; snprintf(v, sizeof(v), "%s", gSet.oWet ? "Wet" : "Dry"); break;
      case OR_NORM: label = "Normalize"; snprintf(v, sizeof(v), "%s", gSet.oNorm ? "On (-1 dBFS)" : "Off"); break;
      case OR_START: snprintf(v, sizeof(v), "[ Start one-shots ]"); break;
    }
    if (r == OR_START) {
      drawText(v, SCREEN_W / 2, y, gSdReady ? (sel ? C_ACC2 : C_INK) : C_FAINT, 1, textdatum_t::top_center);
    } else {
      drawText(label, 8, y, sel ? C_ACC2 : C_INK);
      char vv[44];
      snprintf(vv, sizeof(vv), sel && enabled ? "< %s >" : "%s", v);
      drawText(vv, 90, y, enabled ? C_INK : C_FAINT);
    }
  }
  drawNav("[;.]Sel [,/]Change [Enter]Start [Esc]");
}

static bool keyOneshotPage(const KeyEv& ev) {
  if (ev.esc) { g_osPage = false; return true; }
  if (ev.k == ';') { g_oCur = (g_oCur + OR_N - 1) % OR_N; return true; }
  if (ev.k == '.') { g_oCur = (g_oCur + 1) % OR_N; return true; }
  if (ev.enter && g_oCur == OR_START) { startOneshot(); return true; }
  if (ev.k == ',' || ev.k == '/' || ev.enter) {
    const int d = ev.k == ',' ? -1 : 1;
    int ids[NUM_LAYERS];
    const int n = osLayers(ids);
    const int id = osLayerId(), mk = osModeParam(id);
    switch (g_oCur) {
      case OR_LAYER: gSet.oLayerIdx = (uint8_t)((gSet.oLayerIdx % n + d + n) % n); gSet.oMode = 0; break;
      case OR_MODE: if (mk >= 0) { const int m = LAYER_DEFS[id].p[mk].nOpts; gSet.oMode = (uint8_t)((gSet.oMode % m + d + m) % m); } break;
      case OR_UNIT: gSet.oUnit ^= 1; break;
      case OR_COUNT: gSet.oCountIdx = (uint8_t)((gSet.oCountIdx + d + NUM_ONESHOT_COUNTS) % NUM_ONESHOT_COUNTS); break;
      case OR_REVERB: gSet.oWet = !gSet.oWet; break;
      case OR_NORM: gSet.oNorm = !gSet.oNorm; break;
      default: return true;
    }
    appSettingsChanged();
    return true;
  }
  return true;   // the page keeps the keys
}

// PLANCHETTE's Shift+X (spec v1.2 6.4): the scene just saved (Arc's share
// already baked into it, v0.9), with the EXPORT settings -- but one file (no
// batch) and no Arc on top: what was heard, the same as the saved scene
void exportFromPlanchette(const char* sceneName, const SceneData& scene) {
  if (recActive()) { uiToast("Stop REC first"); return; }
  if (!gSdReady) { uiToast("No SD card"); return; }
  ExportConfig& cfg = g_xcfg;
  cfg = ExportConfig();
  cfg.mode = gSet.xMode == 1 ? EXM_STRICT : EXM_CROSSFADE;
  cfg.lenSec = LOOP_LENS[gSet.xLenIdx];
  cfg.xfSec = XF_LENS[gSet.xXfIdx];
  cfg.driftRecord = false;
  cfg.normalize = gSet.xNormalize;
  cfg.stems = gSet.xStems;
  cfg.batch = 0;
  cfg.seed = gSeed;
  cfg.rt = gRt;
  cfg.rt.finishXf();
  cfg.rt.live = scene;
  cfg.rt.drift = false;
  cfg.moodTag = sanitizeName(sceneName);
  cfg.arcDread = 0;
  cfg.arcDens = 1.f;
  cfg.arcTag = "";
  cursedStopAll();
  const uint64_t need = exportEstimateBytes(cfg), freeB = sdFreeBytes();
  if (need > freeB) {
    static char m[48];
    snprintf(m, sizeof(m), "Needs %lu MB, %lu MB free. Start?", (unsigned long)(need >> 20) + 1, (unsigned long)(freeB >> 20));
    openConfirmFn(m, startExportNow);
    return;
  }
  startExportNow();
}

static void startExport() {
  if (partyActive()) { uiToast("Not now"); return; }
  if (recActive()) { uiToast("Stop REC first"); return; }
  if (!gSdReady) { uiToast("No SD card"); return; }
  if (gSet.xNewSeed) appNewRandomSeed();
  cursedStopAll();
  ExportConfig& cfg = g_xcfg;
  cfg = ExportConfig();
  cfg.mode = gSet.xMode == 1 ? EXM_STRICT : EXM_CROSSFADE;
  cfg.lenSec = LOOP_LENS[gSet.xLenIdx];
  cfg.xfSec = XF_LENS[gSet.xXfIdx];
  cfg.driftRecord = gSet.xDriftRecord && cfg.mode == EXM_CROSSFADE;
  cfg.normalize = gSet.xNormalize;
  cfg.stems = gSet.xStems;
  cfg.batch = BATCH_COUNTS[gSet.xBatchIdx % NUM_BATCH];
  cfg.batchFolder = gSet.xBatchFolder;
  cfg.sceneKind = appIsDrift() ? 2 : (gCurScene < 0 && !strcmp(appMoodName(), "Unknown") ? 1 : 0);
  cfg.seed = gSeed;
  cfg.rt = gRt;
  cfg.moodTag = appMoodTag();
  cfg.arcDread = arcApplies() ? (int8_t)arcExportDreadAdd() : 0;
  cfg.arcDens = arcApplies() ? arcExportDensMul() : 1.f;
  cfg.arcTag = arcTagNow();
  // the space needed (a whole batch) against what is free: ask first if short
  const uint64_t need = exportEstimateBytes(cfg), freeB = sdFreeBytes();
  if (need > freeB) {
    static char m[48];
    snprintf(m, sizeof(m), "Needs %lu MB, %lu MB free. Start?", (unsigned long)(need >> 20) + 1, (unsigned long)(freeB >> 20));
    openConfirmFn(m, startExportNow);
    return;
  }
  startExportNow();
}

static void drawExportProgress() {
  ExportState st = exportState();
  drawHeader("EXPORT");
  char b[48];
  if (st == EXS_RUNNING) {
    drawText(exportStage(), 8, 30, C_INK);
    if (exportBatchCount() > 1) {   // a batch: which file
      snprintf(b, sizeof(b), "%d/%d", exportBatchIndex() + 1, exportBatchCount());
      drawText(b, SCREEN_W - 8, 30, C_ACC2, 1, textdatum_t::top_right);
    }
    float p = exportProgress();
    drawBar(8, 50, SCREEN_W - 16, 14, p, C_ACC, C_FAINT);
    snprintf(b, sizeof(b), "%d%%", (int)(p * 100.f));
    drawText(b, SCREEN_W / 2, 72, C_INK, 1, textdatum_t::top_center);
    drawText("Playback paused while rendering", 8, 92, C_DIM);
    drawNav("[Esc] Cancel");
    return;
  }
  if (st == EXS_DONE) {
    drawText("Saved", 8, 26, C_ACC2, 2);
    String r = exportResult();
    int slash = r.lastIndexOf('/');
    String dir = slash > 0 ? r.substring(0, slash + 1) : String();
    String file = slash >= 0 ? r.substring(slash + 1) : r;
    drawText(dir.c_str(), 8, 54, C_DIM);
    // wrap long names over two lines
    if (file.length() > 38) {
      drawText(file.substring(0, 38).c_str(), 8, 68, C_INK);
      drawText(file.substring(38).c_str(), 8, 80, C_INK);
    } else {
      drawText(file.c_str(), 8, 68, C_INK);
    }
  } else if (st == EXS_CANCELLED) {
    drawText("Cancelled", 8, 30, C_INK, 2);
    drawText("The partial file was removed.", 8, 60, C_DIM);
    if (exportResult().length()) drawText(exportResult().c_str(), 8, 74, C_INK);   // a batch: what was kept
  } else {
    drawText("Export failed", 8, 30, C_WARN, 2);
    drawText(exportResult().c_str(), 8, 60, C_INK);
  }
  drawNav("Any key: back");
}

void drawExport() {
  if (exportUiActive()) { drawExportProgress(); return; }
  if (g_osPage) { drawOneshotPage(); return; }
  drawHeader("EXPORT", gSdReady ? "22.05k/16bit/mono" : "NO SD CARD");
  if (!rowVisible(g_xCur)) g_xCur = XR_START;
  // visible rows (Crossfade / Drift rows come and go with the settings)
  int rows[XR_N], nRows = 0, curPos = 0;
  for (int r = 0; r < XR_N; r++) if (rowVisible(r)) { if (r == g_xCur) curPos = nRows; rows[nRows++] = r; }
  const int VIS = 6;
  keepInView(curPos, g_xScroll, VIS);
  char v[40];
  for (int k = 0; k < VIS && g_xScroll + k < nRows; k++) {
    int r = rows[g_xScroll + k];
    int y = 16 + k * 14;
    bool sel = r == g_xCur;
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, 13, C_SEL);
    const char* label = "";
    bool enabled = true;
    switch (r) {
      case XR_MODE:  label = "Loop Mode"; snprintf(v, sizeof(v), "%s", gSet.xMode ? "Strict" : "Crossfade"); break;
      case XR_LEN:   label = "Loop Length"; snprintf(v, sizeof(v), "%d s", LOOP_LENS[gSet.xLenIdx]); break;
      case XR_XF:    label = "Crossfade"; snprintf(v, sizeof(v), "%d s", XF_LENS[gSet.xXfIdx]); break;
      case XR_SEED:
        label = "Seed";
        if (gSet.xNewSeed) snprintf(v, sizeof(v), "New random");
        else snprintf(v, sizeof(v), "Current (%06lu)", (unsigned long)gSeed);
        break;
      case XR_DRIFT:
        label = "Drift";
        if (gSet.xMode) { snprintf(v, sizeof(v), "Freeze (Strict)"); enabled = false; }
        else snprintf(v, sizeof(v), "%s", gSet.xDriftRecord ? "Record" : "Freeze");
        break;
      case XR_ARC:   label = "Arc"; snprintf(v, sizeof(v), "%s", gSet.xArcApply ? "Apply" : "Ignore"); break;
      case XR_STEMS: label = "Stems"; snprintf(v, sizeof(v), "%s", STEM_OPTS[gSet.xStems % 3]); break;
      case XR_BATCH:
        label = "Batch";
        if (gSet.xBatchIdx) snprintf(v, sizeof(v), "%d files", BATCH_COUNTS[gSet.xBatchIdx % NUM_BATCH]);
        else snprintf(v, sizeof(v), "Off");
        break;
      case XR_BFOLDER: label = "Batch Folder"; snprintf(v, sizeof(v), "%s", gSet.xBatchFolder ? "On" : "Off"); break;
      case XR_BSLEEP:  label = "Sleep After"; snprintf(v, sizeof(v), "%s", gSet.xBatchSleep ? "On" : "Off"); break;
      case XR_NORM:  label = "Normalize"; snprintf(v, sizeof(v), "%s", gSet.xNormalize ? "On (-1 dBFS)" : "Off"); break;
      case XR_START: label = ""; snprintf(v, sizeof(v), "[ Start export ]"); break;
      case XR_ONESHOT: label = "One-shot"; snprintf(v, sizeof(v), "events one by one >"); break;
    }
    if (r == XR_START) {
      drawText(v, SCREEN_W / 2, y, gSdReady ? (sel ? C_ACC2 : C_INK) : C_FAINT, 1, textdatum_t::top_center);
    } else {
      drawText(label, 8, y, sel ? C_ACC2 : C_INK);
      char vv[44];
      snprintf(vv, sizeof(vv), sel && enabled ? "< %s >" : "%s", v);
      drawText(vv, 100, y, enabled ? C_INK : C_FAINT);
    }
  }
  drawScrollMarks(g_xScroll, VIS, nRows, 16, 16 + (VIS - 1) * 14);
  int y = 16 + VIS * 14;  // file name preview / notes
  char seedStr[8];
  snprintf(seedStr, sizeof(seedStr), "%s", gSet.xNewSeed ? "######" : "");
  if (!gSet.xNewSeed) snprintf(seedStr, sizeof(seedStr), "%06lu", (unsigned long)gSeed);
  bool rec = appIsDrift() && !gSet.xMode && gSet.xDriftRecord;
  static const char* const DRT[4] = {"_DRs", "_DRm", "_DRf", "_DRc"};
  String fn = appMoodTag() + (gSet.xMode ? "_ST_" : "_XF_") + String(LOOP_LENS[gSet.xLenIdx]) + "s_" + seedStr
            + (rec ? DRT[gSet.driftSpeed] : "") + arcTagNow() + "_NNN" + (gSet.xStems ? "_DRONE.." : "");
  drawText(fn.c_str(), 4, y + 2, C_DIM);
  if (rec)
    drawText("Record: a longer loop hides the seam", 4, y + 13, C_ACC2);
  drawNav("[;.]Sel [,/]Change [Enter]Start");
}

bool keyExport(const KeyEv& ev) {
  if (exportUiActive()) {
    if (exportState() == EXS_RUNNING) { if (ev.esc) exportCancel(); }
    else exportAcknowledge();
    return true;
  }
  if (ev.shift) return false;
  if (g_osPage) return keyOneshotPage(ev);
  auto step = [&](int dir) {
    int r = g_xCur;
    for (int k = 0; k < XR_N; k++) { r = (r + dir + XR_N) % XR_N; if (rowVisible(r)) break; }
    g_xCur = r;
  };
  if (ev.k == ';') { step(-1); return true; }
  if (ev.k == '.') { step(1); return true; }
  if (ev.enter && g_xCur == XR_START) { startExport(); return true; }
  if (ev.enter && g_xCur == XR_ONESHOT) { g_osPage = true; return true; }
  if (ev.k == ',' || ev.k == '/' || ev.enter) {
    int d = ev.k == ',' ? -1 : 1;
    switch (g_xCur) {
      case XR_MODE:  gSet.xMode ^= 1; break;
      case XR_LEN:   gSet.xLenIdx = (gSet.xLenIdx + d + NUM_LOOP_LENS) % NUM_LOOP_LENS; break;
      case XR_XF:    gSet.xXfIdx = (gSet.xXfIdx + d + NUM_XF_LENS) % NUM_XF_LENS; break;
      case XR_SEED:  gSet.xNewSeed = !gSet.xNewSeed; break;
      case XR_DRIFT: if (!gSet.xMode) gSet.xDriftRecord = !gSet.xDriftRecord; break;
      case XR_ARC:   gSet.xArcApply = !gSet.xArcApply; break;
      case XR_STEMS: gSet.xStems = (uint8_t)((gSet.xStems + d + 3) % 3); break;
      case XR_BATCH: gSet.xBatchIdx = (uint8_t)((gSet.xBatchIdx + d + NUM_BATCH) % NUM_BATCH); break;
      case XR_BFOLDER: gSet.xBatchFolder = !gSet.xBatchFolder; break;
      case XR_BSLEEP: gSet.xBatchSleep = !gSet.xBatchSleep; break;
      case XR_ONESHOT: g_osPage = true; return true;
      case XR_NORM:  gSet.xNormalize = !gSet.xNormalize; break;
      default: return true;
    }
    appSettingsChanged();
    return true;
  }
  return false;
}

// ===========================================================================
// HELP / ABOUT
// ===========================================================================
static int g_helpScroll = 0;
static const char* const HELP_LINES[] = {
  "#KEYS (all screens)",
  "Space      Play / stop",
  "M E X S    Mood/Edit/Export/Settings",
  "Tab        Next screen   Esc  Back",
  "; .        Move    , /  Change",
  "R          Random scene (Unknown)",
  "Shift+R    Live REC start / stop",
  "Shift+S    Sleep timer on/off",
  "Fn+S       Shake action",
  "G0         Display off / on",
  "H          This help",
  "#MAIN",
  "; .        Volume",
  ", /        Previous / next mood",
  "Shift+; .  DREAD",
  "0-9        Type a 6-digit seed",
  "V          Visualizer (7 kinds)",
  "Q          Quick Save (Shake_NNN)",
  "P          PLANCHETTE (tilt it)",
  "G          Poltergeist (hide it)",
  "#PLANCHETTE",
  "Space      Hold     C  Centre",
  "Enter / Q  Save (Tilt_NNN)",
  "Shift+X    Save + export WAV",
  "Esc        Leave (sound stays)",
  "#POLTERGEIST",
  "Hidden, it knocks now and then.",
  "Esc held 2 s or G0: stop it",
  "#MOOD",
  "Enter      Play the mood",
  "N          Save current as new scene",
  "D          Delete a custom scene",
  "#EDIT",
  ", /        Level     O  On / off",
  "Enter      Parameters (Esc back)",
  "#EXPORT",
  "Enter on [Start]. Esc cancels.",
  "Files: /MIASMORA/rec/",
  "#LISTEN (SETTINGS)",
  "Silence slowly builds Attention;",
  "a sudden sound in the room: Hush.",
  "Mic level only, nothing recorded.",
  "M (here)   Mic test / levels",
  "R (here)   Reset DSP avg/peak, drops",
  "Shift+L    Log: that Hush was wrong",
  "Shift+K    Log: it did not react",
  "(Debug > Listen Log / Mark Keys)",
  "Log: /MIASMORA/listen_log.csv",
};
static const int NUM_HELP = sizeof(HELP_LINES) / sizeof(HELP_LINES[0]);

void drawHelp() {
  // so a report from a cursed device can be told apart: C:<frequency>, or
  // C:dbg when an effect was started from Debug > Cursed Trigger (v0.4)
  const char* cn; uint32_t cms; bool cdbg;
  const bool dbgFx = cursedLast(cn, cms, cdbg) && cdbg;
  char hr[32];
  if (gSet.cursed || dbgFx) snprintf(hr, sizeof(hr), "C:%s  %s", gSet.cursed ? CURSED_LEVEL_NAMES[gSet.cursed] : "dbg", FW_VERSION);
  else snprintf(hr, sizeof(hr), "%s", FW_VERSION);
  drawHeader("HELP / ABOUT", hr);
  char b[48];
  // (the version is in the header; v0.32 shows the last reset reason instead)
  snprintf(b, sizeof(b), "%s I2S:%s SD:%s IMU:%s RST:%s", FW_NAME, audioIoReady() ? "OK" : "FAIL",
           gSdReady ? "OK" : "--", gImuReady ? "OK" : "--", gResetReason);
  drawText(b, 4, 15, C_ACC2);
  // Load test since [R] (v0.3 / v0.31): DSP mean, p99, peak; whole audio
  // task mean ("all": DSP + dither + REC + mic); dropouts; free heap
  snprintf(b, sizeof(b), "avg%2d p99 %2d pk%3d all%2d dr%u H%uK",
           (int)(gAudioLoadMean * 100.f + 0.5f), (int)(audioLoadP99() * 100.f + 0.5f),
           (int)(gAudioLoadPeak * 100.f + 0.5f), (int)(gAudioBusyMean * 100.f + 0.5f),
           (unsigned)audioIoDropouts(), (unsigned)(ESP.getFreeHeap() / 1024));
  drawText(b, 4, 26, C_DIM);

  // Microphone / Listen: levels only, nothing is recorded. HELP's own mic
  // test ([M]) ends when HELP closes; with Listen on the mic is on anyway.
  if (audioIoMicOn()) {
    ListenDebug d;
    listenDebug(d);
    MicStats ms;
    audioIoMicStats(ms);
    // v0.33 (method A): mic level and its rise over the last ~0.3 s, full band
    // (F) and 1..4 kHz (B); our output and its rise where that sound was
    // written; windows from the nearest onset of ours heard (or -); the
    // output -> mic lag used / measured (windows of ~23 ms)
    snprintf(b, sizeof(b), "F%6.1f r%+5.1f  B%6.1f r%+5.1f", d.inDb, d.riseF, d.inBandDb, d.riseB);
    drawText(b, 4, 37, C_INK);
    char own[6], lat[8];
    if (d.ownDist == 99) snprintf(own, sizeof(own), "-"); else snprintf(own, sizeof(own), "%d", d.ownDist);
    if (d.latMeasured < 0.f) snprintf(lat, sizeof(lat), "-"); else snprintf(lat, sizeof(lat), "%.0f", d.latMeasured);
    snprintf(b, sizeof(b), "o%6.1f r%+5.1f own %s lag %d/%s", d.outDb, d.outRise, own, d.latFixed, lat);
    drawText(b, 4, 47, C_DIM);
    static const char* const ST[] = {"IDLE", "DROP", "HOLD", "NOTC", "RECV", "COOL"};
    // ... slot in use (L/R) and mic recoveries tried (rc)
    snprintf(b, sizeof(b), "fl%4.0f A%3d%% %s H%d %c rc%d f%u u%u %dus", d.floorDb,
             (int)(listenAttention() * 100.f + 0.5f), d.gated ? "WAIT" : listenMoving() ? "MOVE" : listenResting() ? "REST" : ST[d.state],
             d.hushes, ms.slot ? 'R' : 'L', d.recoveries, (unsigned)ms.freezes,
             (unsigned)(ms.underruns + ms.lost), (int)(ms.procUs + 0.5f));
    drawText(b, 4, 57, C_DIM);
    // What started the last Hush, held until the next one (time since power-on)
    uint32_t tms; float fx, bx; int ly;
    if (listenLastTrigger(tms, fx, bx, ly)) {
      uint32_t s = tms / 1000;
      snprintf(b, sizeof(b), "last %lu:%02lu:%02lu F%+5.1f B%+5.1f %s", (unsigned long)(s / 3600),
               (unsigned long)(s / 60 % 60), (unsigned long)(s % 60), fx, bx,
               ly >= 0 ? LAYER_DEFS[ly].name : "-");
    } else {
      snprintf(b, sizeof(b), "last Hush: none yet");
    }
    // with Arc on, this line takes turns with Arc's state (every 3 s)
    if (!arcShown() || (millis() / 3000) % 2 == 0) drawText(b, 4, 67, C_ACC2);
  } else if (!listenAvailable()) {
    drawText("MIC: not available", 4, 37, C_WARN);
  } else {
    drawText("MIC: off  ([M] mic test, levels only)", 4, 37, C_DIM);
  }
  // Arc (v0.5): section, progress, climax, fitted to the sleep timer
  if (arcShown() && (!audioIoMicOn() || (millis() / 3000) % 2 == 1)) {
    snprintf(b, sizeof(b), "ARC %s %d%%%s%s%s", arcFinished() ? "done" : ARC_SECTION_NAMES[arcSection()],
             (int)(arcProgress() * 100.f), arcClimaxRunning() ? " CLIMAX" : "",
             arcFollowing() ? " zz" : "", gSet.arcCycle == ARC_LOOP ? " loop" : "");
    drawText(b, 4, 67, C_ACC);
  }
  canvas.drawFastHLine(0, 78, SCREEN_W, C_LINE);
  const int VIS = 4;
  if (g_helpScroll > NUM_HELP - VIS) g_helpScroll = NUM_HELP - VIS;
  if (g_helpScroll < 0) g_helpScroll = 0;
  for (int v = 0; v < VIS && g_helpScroll + v < NUM_HELP; v++) {
    const char* l = HELP_LINES[g_helpScroll + v];
    int y = 81 + v * 10;
    if (l[0] == '#') drawText(l + 1, 4, y, C_ACC);
    else drawText(l, 4, y, C_INK);
  }
  drawNav("[;.]Scroll [M]Mic [R]Reset [Esc]");
}

bool keyHelp(const KeyEv& ev) {
  if (ev.esc || ev.k == 'h') {
    listenSetHelpTest(false);   // the mic test never outlives the HELP screen
    gHelp = false;
    return true;
  }
  if (ev.k == 'm') { listenSetHelpTest(!listenHelpTest()); return true; }
  if (ev.k == 'r') { gAudioStatsReset = true; uiToast("Load stats reset"); return true; }
  if (ev.k == ';') { g_helpScroll--; return true; }
  if (ev.k == '.') { g_helpScroll++; return true; }
  return true;
}
