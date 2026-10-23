#include "rec.h"
#include <atomic>
#include "config.h"
#include "storage.h"

static int16_t* g_ring = nullptr;
static uint32_t g_ringN = 0;                       // samples (power of two)
static std::atomic<uint32_t> g_head{0}, g_tail{0}; // free-running sample counters
static volatile bool g_capture = false;
static volatile bool g_active = false;
static volatile bool g_stopping = false;
static volatile uint32_t g_dropped = 0;
static File g_file;
static String g_path;
static uint32_t g_bytes = 0;
static volatile uint32_t g_stopAt = 0;

void recPush(const int16_t* s, int n) {
  if (!g_capture) return;
  uint32_t h = g_head.load(std::memory_order_relaxed);
  uint32_t t = g_tail.load(std::memory_order_acquire);
  if (g_ringN - (h - t) < (uint32_t)n) { g_dropped += n; return; }
  for (int i = 0; i < n; i++) g_ring[(h + i) & (g_ringN - 1)] = s[i];
  g_head.store(h + n, std::memory_order_release);
}

static void writerTask(void*) {
  static int16_t chunk[2048];
  for (;;) {
    uint32_t h = g_head.load(std::memory_order_acquire);
    uint32_t t = g_tail.load(std::memory_order_relaxed);
    uint32_t avail = h - t;
    if (avail >= 2048 || (g_stopping && avail > 0)) {
      uint32_t m = avail > 2048 ? 2048 : avail;
      for (uint32_t i = 0; i < m; i++) chunk[i] = g_ring[(t + i) & (g_ringN - 1)];
      g_tail.store(t + m, std::memory_order_release);
      SdLock lk;
      g_file.write((const uint8_t*)chunk, m * 2);
      g_bytes += m * 2;
      continue;
    }
    // The audio task may still be inside recPush() for a moment after
    // recStop(); wait a few buffers before the ring is freed.
    if (g_stopping && avail == 0 && millis() - g_stopAt > 150) {
      {
        SdLock lk;
        wavPatchSizes(g_file, g_bytes);
        g_file.close();
      }
      free(g_ring);
      g_ring = nullptr;
      g_stopping = false;
      g_active = false;
      vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool recStart(const String& moodTag) {
  if (g_active || !gSdReady) return false;
  // ~1.5 s of audio; fall back to ~0.75 s if memory is tight
  g_ringN = 32768;
  g_ring = (int16_t*)malloc(g_ringN * 2);
  if (!g_ring) { g_ringN = 16384; g_ring = (int16_t*)malloc(g_ringN * 2); }
  if (!g_ring) return false;
  g_path = nextNumberedPath(DIR_REC, moodTag + "_REC");
  if (!g_path.length()) { free(g_ring); g_ring = nullptr; return false; }
  {
    SdLock lk;
    g_file = SD.open(g_path, FILE_WRITE);
    if (!g_file) { free(g_ring); g_ring = nullptr; return false; }
    wavWriteHeader(g_file, 0);
  }
  g_bytes = 0;
  g_dropped = 0;
  g_head.store(0); g_tail.store(0);
  g_stopping = false;
  g_active = true;
  xTaskCreatePinnedToCore(writerTask, "recwr", 6144, NULL, 2, NULL, APP_CPU_NUM);
  g_capture = true;
  return true;
}

void recStop() {
  if (!g_active) return;
  g_capture = false;
  g_stopAt = millis();
  g_stopping = true;
}

bool recActive() { return g_active; }
bool recCapturing() { return g_capture; }
uint32_t recElapsedMs() { return (uint32_t)((uint64_t)g_head.load() * 1000 / SR); }
uint32_t recDropped() { return g_dropped; }
String recLastPath() { return g_path; }
