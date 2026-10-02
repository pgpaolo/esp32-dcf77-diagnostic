#pragma once
#include <cstdint>
#include <cstddef>
#include <climits>
#include <cstdlib>
#include <algorithm>

#define IRAM_ATTR
constexpr uint8_t LOW = 0, HIGH = 1, A0 = 17;
constexpr int TIM_DIV16 = 0, TIM_EDGE = 0, TIM_LOOP = 0;
extern uint32_t testMicros;
extern int testPinLevel;
inline uint32_t micros() { return testMicros; }
inline uint32_t millis() { return testMicros / 1000; }
inline int digitalRead(uint8_t) { return testPinLevel; }
inline void noInterrupts() {}
inline void interrupts() {}
inline void timer1_isr_init() {}
inline void timer1_attachInterrupt(void (*)()) {}
inline void timer1_enable(int, int, int) {}
inline void timer1_write(uint16_t) {}
