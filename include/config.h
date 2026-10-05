#pragma once
#include <Arduino.h>

// Dedicated HW-364A / ESP8266 build for a proper 77.5 kHz DCF77 receiver.
// Receiver DATA is connected to D7/GPIO13.
constexpr uint8_t PIN_DCF77 = 13;
constexpr uint8_t PIN_OLED_SDA = 14;
constexpr uint8_t PIN_OLED_SCL = 12;
constexpr uint8_t PIN_BUTTON_PAGE = 0;
constexpr uint8_t OLED_ADDRESS = 0x3C;

#ifndef DCF77_ACTIVE_LOW
#define DCF77_ACTIVE_LOW 1
#endif
#ifndef DCF77_USE_INTERNAL_PULLUP
#define DCF77_USE_INTERNAL_PULLUP 1
#endif

constexpr bool DCF_ACTIVE_LOW = DCF77_ACTIVE_LOW != 0;
constexpr bool DCF_USE_INTERNAL_PULLUP = DCF77_USE_INTERNAL_PULLUP != 0;

// Standard DCF77 amplitude-modulation pulse windows.
constexpr uint32_t DCF_ZERO_MIN_US = 60000UL;
constexpr uint32_t DCF_ZERO_MAX_US = 145000UL;
constexpr uint32_t DCF_ONE_MIN_US  = 155000UL;
constexpr uint32_t DCF_ONE_MAX_US  = 245000UL;

// Pulse-start interval: normally 1 s; after the missing second-59 pulse ~2 s.
constexpr uint32_t DCF_SECOND_MIN_US = 800000UL;
constexpr uint32_t DCF_SECOND_MAX_US = 1200000UL;
constexpr uint32_t DCF_MINUTE_GAP_MIN_US = 1700000UL;
constexpr uint32_t DCF_MINUTE_GAP_MAX_US = 2300000UL;

constexpr uint32_t SERIAL_BAUD = 115200UL;
constexpr uint32_t OLED_PAGE_MS = 5000UL;
constexpr uint32_t OLED_REFRESH_MS = 250UL;
