// MIASMORA — PLANCHETTE's screen and keys (phase 7, spec v1.2 6.1). See
// planchette.h. A square field with a cross; the planchette (a ring with a
// hole) follows the tilt, in another colour with HOLD while held; the values
// beside it.
#include "ui_internal.h"
#include <math.h>
#include "../planchette.h"
#include "../export.h"

void exportFromPlanchette(const char* sceneName, const SceneData& scene);   // ui_screens.cpp

static constexpr int FX = 4, FY = 16, FS = 104;   // the field

void drawPlanchette() {
  const bool vec = planchetteVector();
  drawHeader("PLANCHETTE", planchetteHeld() ? "HOLD" : (vec ? "Vector" : "Axis"));
  // the field: frame, cross, the dead zone in the middle
  canvas.drawRect(FX, FY, FS, FS, C_LINE);
  canvas.drawFastHLine(FX, FY + FS / 2, FS, C_FAINT);
  canvas.drawFastVLine(FX + FS / 2, FY, FS, C_FAINT);
  canvas.drawCircle(FX + FS / 2, FY + FS / 2, 4, C_FAINT);
  if (vec) {   // the corners: A B on top (forward), C D below
    drawText("A", FX + 3, FY + 3, C_DIM);
    drawText("B", FX + FS - 3, FY + 3, C_DIM, 1, textdatum_t::top_right);
    drawText("C", FX + 3, FY + FS - 10, C_DIM);
    drawText("D", FX + FS - 3, FY + FS - 10, C_DIM, 1, textdatum_t::top_right);
  } else {
    drawText("DREAD+", FX + FS / 2 + 3, FY + 2, C_DIM);
    drawText("dry", FX + 2, FY + FS / 2 + 2, C_DIM);
    drawText("deep", FX + FS - 2, FY + FS / 2 + 2, C_DIM, 1, textdatum_t::top_right);
  }
  // the planchette: a ring with a hole, up = forward
  const int px = FX + FS / 2 + (int)lroundf(planchetteX() * (FS / 2 - 10));
  const int py = FY + FS / 2 - (int)lroundf(planchetteY() * (FS / 2 - 10));
  const uint16_t c = planchetteHeld() ? C_ACC2 : C_ACC;
  canvas.fillCircle(px, py, 9, c);
  canvas.fillCircle(px, py, 4, C_BG);
  // the values
  char b[32];
  const int tx = FX + FS + 10;
  const int dread = constrain((int)gRt.live.dread + appDreadBoost(), 0, 100);
  if (vec) {   // the corner scenes and how much of each is heard
    float w[4];
    planchetteWeights(w);
    for (int k = 0; k < 4; k++) {
      char nm[14];
      strncpy(nm, planchetteCornerName(k), sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
      snprintf(b, sizeof(b), "%c %-13s%3d%%", 'A' + k, nm, (int)lroundf(w[k] * 100.f));
      drawText(b, tx, FY + 2 + k * 13, w[k] > 0.5f ? C_ACC2 : C_INK);
    }
    snprintf(b, sizeof(b), "DREAD  %3d", dread);
    drawText(b, tx, FY + 58, C_DIM);
  } else {
    snprintf(b, sizeof(b), "DREAD  %3d (%+d)", dread, planchetteDreadAdd());
    drawText(b, tx, FY + 6, C_INK);
    snprintf(b, sizeof(b), "Events x%.2f", planchetteRateMul());
    drawText(b, tx, FY + 22, C_INK);
    const float sh = planchetteShade();
    snprintf(b, sizeof(b), "Shade  %s%3d", sh < 0 ? "dry " : "deep", (int)lroundf(fabsf(sh) * 100.f));
    drawText(b, tx, FY + 38, C_INK);
    drawText(appMoodName(), tx, FY + 60, C_DIM);
  }
  if (planchetteHeld()) drawText("HOLD", tx, FY + 80, C_ACC2, 2);
  drawNav("[Spc]Hold [C]Ctr [Q]Save [ShX]WAV [Esc]");
}

// the keys while PLANCHETTE is on (Shift+S was taken by the caller)
void keyPlanchette(const KeyEv& ev) {
  if (ev.esc) { planchetteExit(); return; }
  if (ev.space) { planchetteToggleHold(); return; }
  if (ev.k == 'c' && !ev.shift) { planchetteRecenter(); uiToast("Centre set"); return; }
  if ((ev.enter || ev.k == 'q') && !ev.shift) { uiToast(planchetteSave()); return; }
  if (ev.shift && ev.k == 'x') {   // save, then export what was heard
    char name[24] = "";
    static SceneData saved;
    const char* msg = planchetteSave(name, sizeof(name), &saved);
    if (!name[0]) { uiToast(msg); return; }
    exportFromPlanchette(name, saved);
    return;
  }
}
