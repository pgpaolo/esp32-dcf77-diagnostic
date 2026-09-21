#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Board: LILYGO / TTGO T-Display (classic ESP32, ST7789 1.14" 240x135)
// Display pins are configured in platformio.ini.
// -----------------------------------------------------------------------------

constexpr uint8_t PIN_TFT_BACKLIGHT = 4;
constexpr uint8_t PIN_BUTTON_PAGE   = 35; // hardware pull-up on T-Display
constexpr uint8_t PIN_BUTTON_BL     = 0;  // BOOT button; avoid holding during reset

// DCF77 demodulated digital output.
// GPIO27 is free on the classic T-Display and does not overlap the TFT SPI pins.
constexpr uint8_t PIN_DCF77 = 27;

// Most DCF77 modules provide an active-low/open-collector output during the
// 100/200 ms carrier attenuation. Set false if your module is inverted.
constexpr bool DCF77_ACTIVE_LOW = true;
constexpr bool DCF77_USE_INTERNAL_PULLUP = true;

// Optional analog envelope / RSSI-like output from a custom active receiver.
// IMPORTANT: this is NOT available on most simple DCF77 modules.
constexpr bool DCF77_ANALOG_ENABLED = false;
constexpr uint8_t PIN_DCF77_ANALOG = 32;

// Optional precision reference from a GPS PPS receiver.
// When enabled, the UI displays DCF77 edge offset vs PPS.
constexpr bool PPS_ENABLED = false;
constexpr uint8_t PIN_PPS = 33;
constexpr bool PPS_RISING_EDGE = true;

// Decoder timing windows. Kept intentionally conservative for diagnostics.
constexpr uint32_t DCF_ZERO_MIN_US = 60000;
constexpr uint32_t DCF_ZERO_MAX_US = 145000;
constexpr uint32_t DCF_ONE_MIN_US  = 155000;
constexpr uint32_t DCF_ONE_MAX_US  = 245000;
constexpr uint32_t DCF_SECOND_MIN_US = 850000;
constexpr uint32_t DCF_SECOND_MAX_US = 1150000;
constexpr uint32_t DCF_MINUTE_GAP_MIN_US = 1500000;
constexpr uint32_t DCF_MINUTE_GAP_MAX_US = 2500000;

constexpr uint32_t DISPLAY_REFRESH_MS = 160;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr bool SERIAL_PULSE_LOG = true;
