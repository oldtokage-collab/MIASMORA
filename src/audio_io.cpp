#include "dsp/speed.h"   // first: speed-optimised (v0.32)
#include "audio_io.h"
#include <M5Cardputer.h>
#include <driver/i2s.h>
#include <esp_timer.h>
#include <atomic>
#include <math.h>
#include <string.h>
#include "config.h"

// Port and pins of the ES8311 on the Cardputer ADV (same as M5Unified's
// speaker setup, plus the ADC data line).
static const i2s_port_t IO_PORT = I2S_NUM_1;
static constexpr int PIN_BCK  = 41;
static constexpr int PIN_WS   = 43;
static constexpr int PIN_DOUT = 42;   // to the DAC
static constexpr int PIN_DIN  = 46;   // from the ADC

static constexpr int IO_DMA_BUF_LEN   = 256;   // frames per DMA buffer
static constexpr int IO_DMA_BUF_COUNT = 8;     // ~93 ms of output queued
static constexpr int IO_CHUNK         = 256;   // frames per i2s_write / i2s_read

// Output gain that reproduces the old M5Unified path exactly. Its speaker task
// scaled a mono 16-bit sample s to
//   s * 2 (mono: l += r) * magnification 16 * master_volume^2 * ch_volume^2 / 2^36
// = s * 2 * 16 * 180^2 * 255^2 / 2^36 = s * 0.98106   (setVolume(180))
// Kept as a Q15 integer factor: 32148 / 32768 = 0.98108.
static constexpr int32_t OUT_GAIN_Q15 = 32148;

static bool    g_ready = false;
static constexpr int EVQ_LEN = 32;
static QueueHandle_t g_evq = nullptr;
static volatile uint32_t g_dropouts = 0;
static int16_t g_chunk[IO_CHUNK * 2];   // stereo staging, 1 KB

// --- ES8311 over the internal I2C bus (address 0x18) -------------------------
// Values from AEOLYRA, read back from a working device.
static constexpr uint8_t ES8311_ADDR = 0x18;

static bool es8311Write(uint8_t reg, uint8_t val) {
  return M5.In_I2C.writeRegister8(ES8311_ADDR, reg, val, 400000);
}

// ADC half of the codec. The DAC side keeps what M5Unified set (incl. the
// 0x32 volume), so the output cannot change.
static bool es8311AdcOn() {
  bool ok = true;
  ok &= es8311Write(0x01, 0xBF);   // clocks: DAC + ADC
  ok &= es8311Write(0x0D, 0x03);   // analog power: DAC + ADC
  ok &= es8311Write(0x0E, 0x02);   // PGA + ADC modulator on
  ok &= es8311Write(0x14, 0x10);   // input MIC1, PGA gain minimum
  ok &= es8311Write(0x16, 0x04);   // ADC gain: 0x04 ONLY (0x08 kills the ADC)
  ok &= es8311Write(0x17, 0xBF);   // ADC digital volume 0 dB
  ok &= es8311Write(0x1C, 0x6A);   // ADC EQ bypass, digital DC offset cancel
  return ok;
}

// Back to what M5Unified's speaker start-up leaves (0x01, 0x0D) and the chip
// default (0x0E): ADC clocks and analog parts off, DAC untouched.
static bool es8311AdcOff() {
  bool ok = true;
  ok &= es8311Write(0x0E, 0x6A);
  ok &= es8311Write(0x0D, 0x01);
  ok &= es8311Write(0x01, 0xB5);
  return ok;
}

// --- band-pass for the "band" levels -------------------------------------------
// RBJ band-pass (0 dB peak), centre 2 kHz, Q 0.7: roughly 1..4 kHz.
struct BandPass {
  float b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void init(float f0, float q) {
    float w = 2.f * 3.14159265f * f0 / SR;
    float al = sinf(w) / (2.f * q);
    float a0 = 1.f + al;
    b0 = al / a0;
    a1 = -2.f * cosf(w) / a0;
    a2 = (1.f - al) / a0;
    z1 = z2 = 0.f;
  }
  inline float tick(float x) {   // transposed direct form II, b1 = 0, b2 = -b0
    float y = b0 * x + z1;
    z1 = -a1 * y + z2;
    z2 = -b0 * x - a2 * y;
    return y;
  }
  void flush() {
    if (fabsf(z1) < 1e-15f) z1 = 0.f;
    if (fabsf(z2) < 1e-15f) z2 = 0.f;
  }
};

// --- microphone state -----------------------------------------------------------
// v0.33: a window per audio-task pass (~23 ms, Listen looks for sudden rises
// now). A pass that read less than this carries on into the next one.
static constexpr int MIC_WIN_MIN = BUF * 3 / 4;
static constexpr float MIC_DC_COEF = 1.f - 0.9943f;   // one-pole DC tracker, ~20 Hz

// v0.26: the ADC data arrives in one of the two I2S slots. AEOLYRA found it in
// the left one, but one v0.25 session read near-digital silence all along
// (as if the slots had swapped at start-up). Both slots are measured now and
// the one that carries a signal is used.
static constexpr float SLOT_SWITCH_DB = 10.f;   // other slot this much louder ...
static constexpr int   SLOT_SWITCH_WINDOWS = 13; // ... for this many windows (0.3 s): switch

static volatile bool g_micOn = false;
static volatile bool g_micRestart = false;   // UI -> audio: reset the statistics
static volatile bool g_reinstallReq = false; // UI -> audio: reinstall the I2S driver (recovery)
static volatile bool g_reinstallDone = false;
static int16_t  g_inBuf[IO_CHUNK * 2];       // one stereo read, 1 KB
static float    g_dc[2] = {0.f, 0.f};
static BandPass g_bpIn, g_bpOut;
// v0.33: our output's "full band" level is measured above ~200 Hz, which is
// what the tiny speaker actually plays. HEART's beats and SUB rise a lot in
// the digital signal and hardly at all at the mic; Listen would otherwise
// take a clap that falls on such a rise for our own sound.
static constexpr float OUT_HP_COEF = 0.055f;   // two one-pole high-passes, ~200 Hz
static float g_hpOut1 = 0.f, g_hpOut2 = 0.f;
static double   g_inSum = 0.0, g_outSum = 0.0, g_inBand = 0.0, g_outBand = 0.0;
static double   g_slotSum[2] = {0.0, 0.0};
static int      g_inN = 0, g_outN = 0;
static uint32_t g_ownMask = 0;
static int      g_slot = 0;                  // 0 left, 1 right
static int      g_slotVotes = 0;
static bool     g_allSame = true;
static int16_t  g_first = 0;
static bool     g_haveFirst = false;
static MicStats g_stats = {0.f, 0, 0, 0, 0, 0, 0};

// Audio task -> UI: single producer, single consumer
static constexpr int RING_N = 64;   // ~1.5 s of windows
static MicWindow g_ring[RING_N];
static std::atomic<uint32_t> g_head{0}, g_tail{0};

static float toDb(double sumSq, int n) {
  if (n <= 0) return -96.f;
  float rms = sqrtf((float)(sumSq / n)) / 32768.f;
  if (rms < 1.6e-5f) return -96.f;
  return 20.f * log10f(rms);
}

static bool i2sInstall() {
  i2s_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  cfg.sample_rate = SR;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = IO_DMA_BUF_COUNT;
  cfg.dma_buf_len = IO_DMA_BUF_LEN;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;   // an underrun plays silence, not the last buffer again
  cfg.fixed_mclk = 0;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;
  // The event queue reports a drained TX queue (I2S_EVENT_TX_Q_OVF): an
  // audible dropout, counted for the load test (v0.3)
  if (i2s_driver_install(IO_PORT, &cfg, EVQ_LEN, &g_evq) != ESP_OK) return false;

  i2s_pin_config_t pins;
  memset(&pins, 0, sizeof(pins));
  pins.mck_io_num = I2S_PIN_NO_CHANGE;   // the codec clocks from BCK (reg 0x01)
  pins.bck_io_num = PIN_BCK;
  pins.ws_io_num = PIN_WS;
  pins.data_out_num = PIN_DOUT;
  pins.data_in_num = PIN_DIN;
  if (i2s_set_pin(IO_PORT, &pins) != ESP_OK) {
    i2s_driver_uninstall(IO_PORT);
    return false;
  }
  i2s_zero_dma_buffer(IO_PORT);
  return true;
}

bool audioIoBegin() {
  // The order below is the one verified on the device in AEOLYRA. Anything
  // else leaves either the ADC or the amplifier dead.
  M5Cardputer.Mic.end();   // port 0 must not claim the shared pins

  // Let M5Unified power the codec up and route its clocks. Small DMA so its
  // trailing silence is over quickly once the start-up tone ends.
  M5Cardputer.Speaker.end();
  auto sc = M5Cardputer.Speaker.config();
  sc.sample_rate = SR;
  sc.stereo = false;
  sc.dma_buf_count = 2;
  sc.dma_buf_len = 128;
  sc.task_pinned_core = APP_CPU_NUM;
  M5Cardputer.Speaker.config(sc);
  M5Cardputer.Speaker.begin();
  // The codec is only set up once something actually plays. Volume 1 makes
  // that tone practically inaudible ((1/255)^2 = -96 dB).
  M5Cardputer.Speaker.setVolume(1);
  M5Cardputer.Speaker.tone(1000, 30);
  uint32_t t0 = millis();
  while (M5Cardputer.Speaker.isPlaying() && millis() - t0 < 500) delay(2);
  delay(80);   // the speaker task writes its silence tail, then sleeps for good
  // Speaker.end() is deliberately NOT called: it powers the codec down.

  i2s_driver_uninstall(IO_PORT);
  g_ready = i2sInstall();
  g_bpIn.init(2000.f, 0.7f);
  g_bpOut.init(2000.f, 0.7f);
  Serial.printf("[audio_io] full duplex on I2S%d: %s\n", (int)IO_PORT, g_ready ? "ok" : "FAILED");
  return g_ready;
}

void audioIoWrite(const int16_t* mono, int frames) {
  if (!g_ready) {
    // No driver: still take the buffer's time, or the audio task would spin
    // on PRO_CPU and starve the idle task (task watchdog).
    vTaskDelay(pdMS_TO_TICKS(frames * 1000 / SR + 1));
    return;
  }
  const bool measure = g_micOn;
  int done = 0;
  while (done < frames) {
    int n = frames - done;
    if (n > IO_CHUNK) n = IO_CHUNK;
    float ss = 0.f, sb = 0.f;
    for (int i = 0; i < n; i++) {
      int32_t v = ((int32_t)mono[done + i] * OUT_GAIN_Q15 + 16384) >> 15;
      g_chunk[i * 2] = (int16_t)v;       // the same value left and right
      g_chunk[i * 2 + 1] = (int16_t)v;
      if (measure) {
        float x = (float)v;
        g_hpOut1 += OUT_HP_COEF * (x - g_hpOut1);
        float h = x - g_hpOut1;
        g_hpOut2 += OUT_HP_COEF * (h - g_hpOut2);
        h -= g_hpOut2;
        ss += h * h;
        float y = g_bpOut.tick(x);
        sb += y * y;
      }
    }
    if (measure) {
      g_bpOut.flush();
      if (fabsf(g_hpOut1) < 1e-15f) g_hpOut1 = 0.f;
      if (fabsf(g_hpOut2) < 1e-15f) g_hpOut2 = 0.f;
      g_outSum += ss; g_outBand += sb; g_outN += n;
    }
    size_t wrote = 0;
    i2s_write(IO_PORT, g_chunk, (size_t)n * 2 * sizeof(int16_t), &wrote, portMAX_DELAY);
    done += n;
  }
}

// --- microphone ---------------------------------------------------------------
bool audioIoMicEnable(bool on) {
  if (!g_ready) return false;
  if (on) {
    if (g_micOn) return true;
    if (!es8311AdcOn()) {
      es8311AdcOff();
      return false;
    }
    // Old windows are dropped; the producer is idle while the mic is off
    g_tail.store(g_head.load(std::memory_order_acquire), std::memory_order_release);
    g_micRestart = true;   // seen by the audio task together with g_micOn
    g_micOn = true;
  } else {
    if (!g_micOn) return true;
    g_micOn = false;
    es8311AdcOff();
  }
  return true;
}

bool audioIoMicOn() { return g_micOn; }

static void micResetWindow() {
  g_inSum = g_outSum = g_inBand = g_outBand = 0.0;
  g_slotSum[0] = g_slotSum[1] = 0.0;
  g_inN = g_outN = 0;
  g_ownMask = 0;
  g_allSame = true;
  g_haveFirst = false;
}

static void micCloseWindow() {
  MicWindow w;
  w.inDb = toDb(g_inSum, g_inN);
  w.outDb = toDb(g_outSum, g_outN);
  w.inBandDb = toDb(g_inBand, g_inN);
  w.outBandDb = toDb(g_outBand, g_outN);
  w.own = g_ownMask;
  w.n = (uint16_t)g_inN;
  w.frozen = g_allSame && g_inN > 1;   // a dead ADC reads as a constant, not as zero
  w.slot = (uint8_t)g_slot;
  w.otherDb = toDb(g_slotSum[g_slot ^ 1], g_inN);
  if (w.frozen) g_stats.freezes++;
  g_stats.windows++;
  // the other slot clearly carries the signal: use it from the next window
  if (w.otherDb > w.inDb + SLOT_SWITCH_DB) {
    if (++g_slotVotes >= SLOT_SWITCH_WINDOWS) {
      g_slot ^= 1;
      g_slotVotes = 0;
      g_stats.slotSwitches++;
      g_bpIn.z1 = g_bpIn.z2 = 0.f;
    }
  } else {
    g_slotVotes = 0;
  }
  g_stats.slot = (uint8_t)g_slot;
  uint32_t h = g_head.load(std::memory_order_relaxed);
  if (h - g_tail.load(std::memory_order_acquire) < RING_N) {
    g_ring[h % RING_N] = w;
    g_head.store(h + 1, std::memory_order_release);
  } else {
    g_stats.lost++;
  }
  micResetWindow();
}

void audioIoMicProcess() {
  // Recovery, stage 3: reinstall the driver. Done here, on the audio task
  // between two writes, so no i2s_write can be in progress meanwhile.
  if (g_reinstallReq) {
    g_reinstallReq = false;
    i2s_driver_uninstall(IO_PORT);
    g_ready = i2sInstall();
    g_micRestart = true;
    g_reinstallDone = true;
    Serial.printf("[audio_io] I2S reinstalled: %s\n", g_ready ? "ok" : "FAILED");
  }
  if (!g_ready || !g_micOn) return;
  int64_t t0 = esp_timer_get_time();

  if (g_micRestart) {
    g_micRestart = false;
    // Whatever sits in the RX queue was recorded before the ADC was on
    for (int i = 0; i < IO_DMA_BUF_COUNT + 1; i++) {
      size_t bytes = 0;
      i2s_read(IO_PORT, g_inBuf, sizeof(g_inBuf), &bytes, 0);
      if (bytes == 0) break;
    }
    micResetWindow();
    g_dc[0] = g_dc[1] = 0.f;
    g_bpIn.z1 = g_bpIn.z2 = 0.f;
    g_slotVotes = 0;
    // counters survive a restart; only the per-session statistics reset
    g_stats.procUs = 0.f;
    g_stats.windows = g_stats.freezes = g_stats.underruns = g_stats.lost = 0;
    return;
  }

  bool got = false;
  // Take what is ready, up to two buffers' worth (catches up after a stall).
  // Timeout 0: the audio task must never wait on the ADC.
  for (int pass = 0; pass < (2 * BUF) / IO_CHUNK; pass++) {
    size_t bytes = 0;
    i2s_read(IO_PORT, g_inBuf, sizeof(g_inBuf), &bytes, 0);   // partial data comes with a timeout code
    int n = (int)(bytes / (2 * sizeof(int16_t)));
    if (n <= 0) break;
    got = true;
    const int sl = g_slot;
    if (!g_haveFirst) {
      g_first = g_inBuf[sl];
      g_haveFirst = true;
      if (g_stats.windows == 0 && g_inN == 0) {   // settle the DC trackers at once
        g_dc[0] = (float)g_inBuf[0];
        g_dc[1] = (float)g_inBuf[1];
      }
    }
    // The slot in use: every sample. The other one only decides whether to
    // switch slots, so every 4th sample is enough (v0.31: mic work ~400 us
    // per buffer since both slots were read in full).
    const int ot = sl ^ 1;
    const int16_t* in = g_inBuf;
    float dcA = g_dc[sl], dcB = g_dc[ot], sA = 0.f, sB = 0.f, sb = 0.f;
    bool same = true;
    const int16_t first = g_first;
    for (int i = 0; i < n; i++) {
      const int16_t raw = in[i * 2 + sl];
      same &= raw == first;
      float x = (float)raw;
      dcA += (x - dcA) * MIC_DC_COEF;
      float y = x - dcA;
      sA += y * y;
      float bp = g_bpIn.tick(y);
      sb += bp * bp;
      if ((i & 3) == 0) {
        float xo = (float)in[i * 2 + ot];
        dcB += (xo - dcB) * (MIC_DC_COEF * 4.f);
        float yo = xo - dcB;
        sB += yo * yo * 4.f;   // stands for the 4 samples it represents
      }
    }
    if (!same) g_allSame = false;
    g_dc[sl] = dcA; g_dc[ot] = dcB;
    g_bpIn.flush();
    g_slotSum[sl] += sA;
    g_slotSum[ot] += sB;
    g_inSum += sA;
    g_inBand += sb;
    g_inN += n;
  }
  if (!got) g_stats.underruns++;
  if (g_inN >= MIC_WIN_MIN) micCloseWindow();

  float us = (float)(esp_timer_get_time() - t0);
  g_stats.procUs = g_stats.procUs * 0.9f + us * 0.1f;
}

void audioIoNoteOwnOnsets(uint32_t mask) {
  if (g_micOn) g_ownMask |= mask;
}

bool audioIoMicPop(MicWindow& w) {
  uint32_t t = g_tail.load(std::memory_order_relaxed);
  if (t == g_head.load(std::memory_order_acquire)) return false;
  w = g_ring[t % RING_N];
  g_tail.store(t + 1, std::memory_order_release);
  return true;
}

// Copied field by field from the UI side; a counter may be one window older
// than its neighbour, which does not matter for a display.
void audioIoMicStats(MicStats& out) {
  out.procUs = g_stats.procUs;
  out.windows = g_stats.windows;
  out.freezes = g_stats.freezes;
  out.underruns = g_stats.underruns;
  out.lost = g_stats.lost;
  out.slot = g_stats.slot;
  out.slotSwitches = g_stats.slotSwitches;
}

// The recovery ladder from AEOLYRA (its stage 4, restarting M5Unified's
// speaker, is left out: nothing may touch M5Cardputer.Speaker after the
// takeover). UI thread only (I2C).
bool audioIoMicRecover(int stage) {
  if (!g_ready || !g_micOn) return false;
  bool ok = true;
  switch (stage) {
    case 1:   // write the ADC registers again
      ok = es8311AdcOn();
      break;
    case 2:   // ADC analog part off, then on again (the DAC stays powered)
      ok = es8311Write(0x0D, 0x01);
      delay(10);
      ok &= es8311AdcOn();
      break;
    case 3: { // reinstall the I2S driver on the audio task, then the ADC registers
      g_reinstallDone = false;
      g_reinstallReq = true;
      uint32_t t0 = millis();
      while (!g_reinstallDone && millis() - t0 < 500) delay(5);
      if (!g_reinstallDone) { g_reinstallReq = false; return false; }   // audio task not running
      ok = g_ready && es8311AdcOn();
      break;
    }
    default:
      return false;
  }
  g_micRestart = true;
  return ok;
}

void audioIoDacPower(bool on) {
  if (!g_ready) return;
  static bool isOn = true;
  if (on == isOn) return;
  isOn = on;
  if (on) {
    es8311Write(0x12, 0x00);   // DAC up
    es8311Write(0x13, 0x10);   // output stage on
    for (int v = 0x20; v <= 0xBF; v += 0x20) { es8311Write(0x32, (uint8_t)v); delay(4); }
    es8311Write(0x32, 0xBF);   // 0 dB, as M5Unified sets it
  } else {
    for (int v = 0xBF; v > 0; v -= 0x20) { es8311Write(0x32, (uint8_t)v); delay(4); }
    es8311Write(0x32, 0x00);   // muted
    es8311Write(0x13, 0x00);   // output stage off
    es8311Write(0x12, 0x02);   // DAC down
  }
}

void audioIoSuspend(bool suspend) {
  if (!g_ready) return;
  if (suspend) { i2s_zero_dma_buffer(IO_PORT); i2s_stop(IO_PORT); }
  else { i2s_zero_dma_buffer(IO_PORT); i2s_start(IO_PORT); }
}

void audioIoEnd() {
  if (!g_ready) return;
  g_micOn = false;   // no register writes here: the chip is about to sleep
  g_ready = false;
  i2s_zero_dma_buffer(IO_PORT);
  delay(20);
  i2s_driver_uninstall(IO_PORT);
}

bool audioIoReady() { return g_ready; }

void audioIoPollEvents(bool count) {
  if (!g_ready || !g_evq) return;
  i2s_event_t ev;
  while (xQueueReceive(g_evq, &ev, 0) == pdTRUE) {
    // The driver posts this when the ISR finds no new buffer to send: the
    // output ran dry and played silence
    if (count && ev.type == I2S_EVENT_TX_Q_OVF) g_dropouts++;
  }
}

uint32_t audioIoDropouts() { return g_dropouts; }
void audioIoResetDropouts() { g_dropouts = 0; }
