// MIASMORA — EDIT screen: layer list (DRONE / EVENTS / SPACE) and per-layer parameters
#include "ui_internal.h"
#include "../audio.h"

static int g_cursor = 0;        // layer id
static int g_scroll = 0;        // first visible display row
static bool g_params = false;   // parameter sub-view open
static int g_pCursor = 0;       // 0 = On, 1 = Level, 2.. = params

// Display rows: a separator before each group, then its layers
struct Row { bool sep; int id; };
static Row g_rows[NUM_LAYERS + NUM_GROUPS];
static int g_nRows = 0;

// Grouped for display: layers added later to a group (v0.31's drones come
// after the event layers in id order) still show at the end of their group
static void buildRows() {
  if (g_nRows) return;
  for (int g = 0; g < NUM_GROUPS; g++) {
    g_rows[g_nRows++] = {true, g};
    for (int i = 0; i < NUM_LAYERS; i++)
      if (LAYER_DEFS[i].group == g) g_rows[g_nRows++] = {false, i};
  }
}
static int rowOfLayer(int id) {
  for (int r = 0; r < g_nRows; r++) if (!g_rows[r].sep && g_rows[r].id == id) return r;
  return 0;
}
// The layer before / after the cursor in display order (wrapping)
static int stepLayer(int id, int dir) {
  buildRows();
  int r = rowOfLayer(id);
  do { r = (r + dir + g_nRows) % g_nRows; } while (g_rows[r].sep);
  return g_rows[r].id;
}

static void fmtParam(char* b, size_t n, int id, int k, const LayerState& L) {
  const ParamDef& pd = LAYER_DEFS[id].p[k];
  if (pd.nOpts) snprintf(b, n, "%s", pd.opts[L.p[k] < pd.nOpts ? L.p[k] : 0]);
  else snprintf(b, n, "%d", L.p[k]);
}

static void drawList() {
  buildRows();
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "%s", GROUP_NAMES[LAYER_DEFS[g_cursor].group]);
  drawHeader("EDIT", hdr);
  const int ROW_H = 11, TOP = 14, VIS = 10;
  int cr = rowOfLayer(g_cursor);
  // keep the cursor (and the separator above a group's first layer) in view
  int want = cr;
  if (cr > 0 && g_rows[cr - 1].sep) want = cr - 1;
  if (want < g_scroll) g_scroll = want;
  if (cr >= g_scroll + VIS) g_scroll = cr - VIS + 1;
  for (int v = 0; v < VIS && g_scroll + v < g_nRows; v++) {
    const Row& r = g_rows[g_scroll + v];
    int y = TOP + v * ROW_H;
    if (r.sep) {
      drawText(GROUP_NAMES[r.id], 4, y + 2, C_ACC);
      int w = (int)strlen(GROUP_NAMES[r.id]) * 6 + 8;
      canvas.drawFastHLine(w, y + 5, SCREEN_W - w - 4, C_ACC);
      continue;
    }
    const LayerState& L = gRt.live.L[r.id];
    bool sel = r.id == g_cursor;
    if (sel) canvas.fillRect(0, y, SCREEN_W, ROW_H, C_SEL);
    uint16_t tc = L.on ? C_INK : C_FAINT;
    drawText(LAYER_DEFS[r.id].name, 8, y + 2, sel ? C_ACC2 : tc);
    if (L.on) canvas.fillRect(64, y + 3, 5, 5, C_ACC2);
    else canvas.drawRect(64, y + 3, 5, 5, C_FAINT);
    drawBar(76, y + 2, 120, 7, L.level / 100.f, L.on ? C_ACC : C_FAINT, C_FAINT);
    char b[8];
    snprintf(b, sizeof(b), "%d", L.level);
    drawText(b, 222, y + 2, tc, 1, textdatum_t::top_right);
    float a = gEngine.activity(r.id);
    if (a > 0.02f) canvas.fillCircle(231, y + 5, 1 + (int)(a * 2.5f), rgb(90 + (int)(140 * a), 20, 20));
  }
  drawNav("[;.]Sel [,/]Lvl [O]n/off [Enter]Params");
}

static void drawParams() {
  const LayerDef& d = LAYER_DEFS[g_cursor];
  const LayerState& L = gRt.live.L[g_cursor];
  char title[24];
  snprintf(title, sizeof(title), "EDIT > %s", d.name);
  drawHeader(title, GROUP_NAMES[d.group]);
  int nRows = 2 + d.nParams;
  // v0.37: 8 rows (HEART's six parameters) only fit 13 px apart
  const int step = nRows > 7 ? 13 : 15;
  for (int r = 0; r < nRows; r++) {
    int y = 16 + r * step;
    bool sel = r == g_pCursor;
    if (sel) canvas.fillRect(0, y - 2, SCREEN_W, step - 2, C_SEL);
    char b[24];
    const char* label;
    float frac = -1.f;
    if (r == 0) { label = "On"; snprintf(b, sizeof(b), "%s", L.on ? "ON" : "OFF"); }
    else if (r == 1) { label = d.levelName; snprintf(b, sizeof(b), "%d", L.level); frac = L.level / 100.f; }
    else {
      int k = r - 2;
      label = d.p[k].name;
      fmtParam(b, sizeof(b), g_cursor, k, L);
      if (!d.p[k].nOpts) frac = L.p[k] / 100.f;
    }
    drawText(label, 8, y, sel ? C_ACC2 : C_INK);
    if (frac >= 0.f) {
      drawBar(96, y, 100, 8, frac, C_ACC, C_FAINT);
      drawText(b, SCREEN_W - 6, y, C_INK, 1, textdatum_t::top_right);
    } else {
      char v[28];
      snprintf(v, sizeof(v), sel ? "< %s >" : "%s", b);
      drawText(v, 96, y, C_INK);
    }
  }
  drawNav("[;.]Select [,/]Change [Esc]Back");
}

// Leaving EDIT (Tab, M / X / S, Esc) closes the parameter view, so EDIT
// always reopens on the layer list. The selected layer is kept.
void editResetView() { g_params = false; }

void drawEdit() {
  if (g_params) drawParams();
  else drawList();
}

static void adjustLevel(int id, int d) {
  appSettleEdit();
  LayerState& L = gRt.live.L[id];
  int v = constrain((int)L.level + d, 0, 100);
  if (v != L.level) { L.level = (uint8_t)v; appSceneEdited(); }
}

bool keyEdit(const KeyEv& ev) {
  if (ev.shift) return false;
  if (g_params) {
    const LayerDef& d = LAYER_DEFS[g_cursor];
    int nRows = 2 + d.nParams;
    if (ev.esc) { g_params = false; return true; }
    if (ev.k == ';') { g_pCursor = (g_pCursor + nRows - 1) % nRows; return true; }
    if (ev.k == '.') { g_pCursor = (g_pCursor + 1) % nRows; return true; }
    if (ev.k == ',' || ev.k == '/' || ev.enter) {
      int dir = (ev.k == ',') ? -1 : 1;
      appSettleEdit();
      LayerState& L = gRt.live.L[g_cursor];
      if (g_pCursor == 0) {
        L.on = !L.on;
      } else if (g_pCursor == 1) {
        if (ev.enter) return true;
        adjustLevel(g_cursor, dir * stepFor(ev.heldMs));
        return true;
      } else {
        int k = g_pCursor - 2;
        const ParamDef& pd = d.p[k];
        if (pd.nOpts) L.p[k] = (uint8_t)((L.p[k] + dir + pd.nOpts) % pd.nOpts);
        else if (!ev.enter) L.p[k] = (uint8_t)constrain((int)L.p[k] + dir * stepFor(ev.heldMs), 0, 100);
        else return true;
      }
      appSceneEdited();
      return true;
    }
    return false;
  }
  if (ev.k == ';') { g_cursor = stepLayer(g_cursor, -1); return true; }
  if (ev.k == '.') { g_cursor = stepLayer(g_cursor, 1); return true; }
  if (ev.k == ',') { adjustLevel(g_cursor, -stepFor(ev.heldMs)); return true; }
  if (ev.k == '/') { adjustLevel(g_cursor, stepFor(ev.heldMs)); return true; }
  if (ev.k == 'o') {
    appSettleEdit();
    gRt.live.L[g_cursor].on = !gRt.live.L[g_cursor].on;
    appSceneEdited();
    return true;
  }
  if (ev.enter) { g_params = true; g_pCursor = 1; return true; }
  return false;
}
