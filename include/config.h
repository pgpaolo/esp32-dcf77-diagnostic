#pragma once
#include <Arduino.h>

// DCF-3850N-800 / SP6007 baseline RAW receiver configuration.
//
// Product pin names:
//   G  = Ground
//   V  = Operating voltage (1.1..3.3 V)
//   T  = Demodulated DCF77 data
//   P1 = Power on/off, MUST be logic LOW for reception
//
// HW-364A mapping:
constexpr uint8_t PIN_DCF77_DATA = 13;  // D7 -> T
constexpr uint8_t PIN_DCF77_PON  = 5;   // D1 -> P1, forced LOW

constexpr uint8_t PIN_OLED_SDA = 14;    // D5
constexpr uint8_t PIN_OLED_SCL = 12;    // D6
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr uint32_t SERIAL_BAUD = 115200UL;
constexpr uint32_t OLED_REFRESH_MS = 250UL;

// RAW pulse classification is diagnostic only.
// Working ESP8266 DCF77 examples observe positive pulses on DATA.
constexpr uint32_t RAW_ZERO_MIN_US = 50000UL;
constexpr uint32_t RAW_ZERO_MAX_US = 150000UL;
constexpr uint32_t RAW_ONE_MIN_US  = 150001UL;
constexpr uint32_t RAW_ONE_MAX_US  = 260000UL;
constexpr uint32_t RAW_MINUTE_GAP_MIN_US = 1500000UL;
constexpr uint32_t RAW_MINUTE_GAP_MAX_US = 2500000UL;
