// MIASMORA — UI internals shared by the screen files
#pragma once
#include <M5Cardputer.h>
#include "../config.h"
#include "../app.h"

extern M5Canvas canvas;

enum Screen : uint8_t { SCR_MAIN = 0, SCR_MOOD, SCR_EDIT, SCR_EXPORT, SCR_SETTINGS, NUM_SCREENS };
extern Screen gScreen;
extern bool gHelp;

// One key event. `k` is the key's base character with Shift undone
// (':' -> ';', '>' -> '.', 'R' -> 'r' ...), so handlers test `k` + `shift`.
struct KeyEv {
  char c, k;
  bool enter, tab, space, del, shift, fn, esc;
  uint32_t heldMs;   // 0 on the first press, hold time on auto-repeat
};

// --- colours (RGB565) -------------------------------------------------------
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
extern uint16_t C_BG, C_INK, C_DIM, C_FAINT, C_LINE, C_ACC, C_ACC2, C_SEL, C_WARN;
void uiInitColors();

// --- drawing helpers ----------------------------------------------------------
void drawText(const char* s, int x, int y, uint16_t color, int size = 1, textdatum_t datum = textdatum_t::top_left);
void drawHeader(const char* title, const char* right = nullptr);
void drawNav(const char* text);
void drawBar(int x, int y, int w, int h, float frac, uint16_t fill, uint16_t frame);
void drawToast();
int  stepFor(uint32_t heldMs);   // 2 on a tap, larger the longer a key is held

// --- screens -----------------------------------------------------------------
void drawMain();    bool keyMain(const KeyEv& ev);
void drawMood();    bool keyMood(const KeyEv& ev);
void drawEdit();    bool keyEdit(const KeyEv& ev);  void editResetView();
void drawExport();  bool keyExport(const KeyEv& ev);
void drawSettings();bool keySettings(const KeyEv& ev);  void settingsResetView();
void drawHelp();    bool keyHelp(const KeyEv& ev);
void drawVisualizer();       // MAIN's visualiser (ui_viz.cpp)
void vizIdle();              // not shown: leave the audio mailbox alone
void watcherUpdate(float dt);   // Watcher (ui_watcher.cpp): every loop, any screen (the LED)
bool watcherVisible();          // the eye is open: MAIN draws it instead of the visualiser
void watcherDraw();
bool partyOwnsScreen();         // Party Mode (ui_party.cpp, hidden): its screen, keys and LED
void drawParty();
void keyParty(const KeyEv& ev);
void partyLedUpdate();
void drawPartyReveal();
void drawPlanchette();          // PLANCHETTE (ui_planchette.cpp)
void keyPlanchette(const KeyEv& ev);
bool pgPageOpen();              // Poltergeist (ui_polter.cpp)
void pgPageOpenSet(bool on);
void drawPgPage();  void keyPgPage(const KeyEv& ev);
void drawPgCountdown(); void drawPgReveal(); void drawPgReport();
extern float gVizFftUs;      // Spectrum's FFT time (Debug > Performance)
void exportUiPoll();         // resumes live audio when an export has finished
bool exportUiActive();       // the export progress / result view owns the keyboard
bool seedEntryActive();

// --- modals (text input, confirm) ------------------------------------------------
enum TextPurpose : uint8_t { TXT_NONE = 0, TXT_SAVE_AS };
void openTextInput(TextPurpose p, const char* initial);
bool textInputActive();
void drawTextInput();
void keyTextInput(const KeyEv& ev);


void openCursedConsent(uint8_t level);
void openListenConsent();               // same modal as Cursed Mode's
bool cursedConsentActive();
void drawCursedConsent();
void keyCursedConsent(const KeyEv& ev);

void openConfirm(const char* msg, int target);
void openConfirmFn(const char* msg, void (*fn)());   // Enter runs fn
void uiWakeDisplay();                   // full brightness / on (an export has finished)
bool confirmActive();
void drawConfirm();
void keyConfirm(const KeyEv& ev);
