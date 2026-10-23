// MIASMORA — memory use at start-up, for SETTINGS > System > Debug > Memory
// (spec v1.22 2.2). setup() marks the free heap after each step; the page
// shows what each step took and the heap now.
#pragma once
#include <stdint.h>

static constexpr int MEM_MAX_MARKS = 12;
void memMark(const char* stage);           // after a start-up step (setup() only)
int  memMarkCount();
const char* memMarkName(int i);
uint32_t memMarkFree(int i);               // free heap after that step (bytes)
// now: free heap, the lowest it has been since power-on, the largest free block
void memNow(uint32_t& freeB, uint32_t& minB, uint32_t& largestB);