#include "storage.h"
#include <SPI.h>
#include "config.h"

bool gSdReady = false;
static SemaphoreHandle_t g_sdMutex = nullptr;

SdLock::SdLock() { if (g_sdMutex) xSemaphoreTakeRecursive(g_sdMutex, portMAX_DELAY); }
SdLock::~SdLock() { if (g_sdMutex) xSemaphoreGiveRecursive(g_sdMutex); }

void storageInit() {
  if (!g_sdMutex) g_sdMutex = xSemaphoreCreateRecursiveMutex();
  SdLock lk;
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  gSdReady = SD.begin(SD_CS, SPI, 25000000);
  if (gSdReady) {
    if (!SD.exists(DIR_ROOT)) SD.mkdir(DIR_ROOT);
    if (!SD.exists(DIR_SCENE)) SD.mkdir(DIR_SCENE);
    if (!SD.exists(DIR_REC)) SD.mkdir(DIR_REC);
    // A Crossfade export interrupted by a reset (power loss...) can leave its
    // temporary head file behind; it is never needed after a restart.
    if (SD.exists(DIR_REC "/~xfhead.tmp")) SD.remove(DIR_REC "/~xfhead.tmp");
  }
}

static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

void wavWriteHeader(File& f, uint32_t dataBytes) {
  uint8_t h[44];
  memcpy(h, "RIFF", 4);
  put32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16);          // fmt chunk size
  put16(h + 20, 1);           // PCM
  put16(h + 22, 1);           // mono
  put32(h + 24, SR);
  put32(h + 28, SR * 2);      // byte rate
  put16(h + 32, 2);           // block align
  put16(h + 34, 16);          // bits per sample
  memcpy(h + 36, "data", 4);
  put32(h + 40, dataBytes);
  f.write(h, 44);
}

void wavPatchSizes(File& f, uint32_t dataBytes) {
  uint8_t b[4];
  put32(b, 36 + dataBytes);
  f.seek(4);  f.write(b, 4);
  put32(b, dataBytes);
  f.seek(40); f.write(b, 4);
}

String nextNumberedPath(const char* dir, const String& prefix) {
  SdLock lk;
  char num[8];
  for (int i = 1; i <= 999; i++) {
    snprintf(num, sizeof(num), "_%03d", i);
    String p = String(dir) + "/" + prefix + num + ".wav";
    if (!SD.exists(p)) return p;
  }
  return String();
}

uint64_t sdFreeBytes() {
  SdLock lk;
  uint64_t t = SD.totalBytes(), u = SD.usedBytes();
  return t > u ? t - u : 0;
}

String sanitizeName(const char* s) {
  String o;
  for (; *s; s++) {
    char c = *s;
    if (isalnum((unsigned char)c)) o += c;
  }
  if (!o.length()) o = "Scene";
  return o;
}
