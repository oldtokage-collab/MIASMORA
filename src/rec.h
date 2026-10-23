// MIASMORA — live recording (spec 6.7)
//
// The audio task only copies each finished buffer into a RAM ring buffer;
// a separate low-priority task on the other core drains the ring to the SD
// card. SD write latency (which can spike to hundreds of ms) therefore never
// stalls the audio task.
#pragma once
#include <Arduino.h>

bool     recStart(const String& moodTag);   // false: SD missing / no memory / file error
void     recStop();                         // asynchronous: the writer drains and closes
bool     recActive();                       // capturing or still finishing
bool     recCapturing();
uint32_t recElapsedMs();
uint32_t recDropped();                      // samples lost to a full ring (should stay 0)
String   recLastPath();

void     recPush(const int16_t* s, int n);  // audio task only
