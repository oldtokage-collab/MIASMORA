// MIASMORA — SD card access, shared lock and WAV helpers
#pragma once
#include <Arduino.h>
#include <SD.h>

extern bool gSdReady;

void storageInit();

// Every SD operation from any task holds this lock (UI saves, the export
// task and the live-recording writer task share the card).
struct SdLock {
  SdLock();
  ~SdLock();
};

// WAV (PCM 16-bit mono, SR). The header is written with unknown sizes first
// and patched once the data length is known.
void wavWriteHeader(File& f, uint32_t dataBytes);
void wavPatchSizes(File& f, uint32_t dataBytes);

// "<dir>/<prefix>_NNN.wav" with the first free NNN (001..999). Empty on failure.
String nextNumberedPath(const char* dir, const String& prefix);

// Free bytes on the card (may take a moment on large cards)
uint64_t sdFreeBytes();

// Letters and digits only (file names)
String sanitizeName(const char* s);
