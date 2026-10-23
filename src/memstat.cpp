#include "memstat.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

static const char* g_name[MEM_MAX_MARKS];
static uint32_t g_free[MEM_MAX_MARKS];
static int g_n = 0;

void memMark(const char* stage) {
  if (g_n >= MEM_MAX_MARKS) return;
  g_name[g_n] = stage;
  g_free[g_n] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
  g_n++;
}
int memMarkCount() { return g_n; }
const char* memMarkName(int i) { return (i >= 0 && i < g_n) ? g_name[i] : ""; }
uint32_t memMarkFree(int i) { return (i >= 0 && i < g_n) ? g_free[i] : 0; }

void memNow(uint32_t& freeB, uint32_t& minB, uint32_t& largestB) {
  freeB = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
  minB = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  largestB = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}