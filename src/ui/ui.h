// MIASMORA — UI entry points used by main.cpp
#pragma once
#include <stdint.h>

void uiInit();             // canvas, colours (a dark screen)
void uiSplashRun();        // the start screen, ~6.7 s (ui_splash.cpp); the audio task must be running
void uiLoop(float dt);     // input, shake, display power, drawing
