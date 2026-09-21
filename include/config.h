#pragma once

#include <Arduino.h>

#ifndef DCF_RX_DUAL_CMAX
#define DCF_RX_DUAL_CMAX 0
#endif

// -----------------------------------------------------------------------------
// Board: LILYGO / TTGO T-Display (classic ESP32, ST7789 1.14" 240x135)
// Display pins are configured in platformio.ini.
// -----------------------------------------------------------------------------

constexpr uint8_t PIN_TFT_BACKLIGHT = 4;
constexpr uint8_t PIN_BUTTON_PAGE   = 35; // hardware pull-up on T-Display
constexpr uint8_t PIN_BUTTON_BL     = 0;  // BOOT button; avoid holding during reset

// Receiver data output.
constexpr uint8_t PIN_DCF77 = 27;

// Generic DCF77 modules often use an active-low/open-collector output.
// C-MAX exposes both TCO (positive) and TCON (inverted), so keep this
// configurable according to the pin you actually wire to GPIO27.
constexpr bool DCF77_ACTIVE_LOW = true;
constexpr bool DCF77_USE_INTERNAL_PULLUP = true;

// -----------------------------------------------------------------------------
// Optional C-MAX CMMR-6D-7760 dual-frequency receiver (60 / 77.5 kHz)
//
// Build with:
//   pio run -e ttgo-t-display-dual
//
// C-MAX BAND:
//   LOW/GND  = higher EU frequency = 77.5 kHz
//   HIGH/VDD = lower EU frequency  = 60.0 kHz
//
// PON is active-low. GPIO26 keeps the module enabled and also permits a
// controlled power-cycle if required later.
// HLD is intentionally not MCU-controlled in v1: tie HLD to VDD externally.
// -----------------------------------------------------------------------------
constexpr bool DUAL_FREQUENCY_RECEIVER = (DCF_RX_DUAL_CMAX != 0);
constexpr uint8_t PIN_RX_BAND = 25;
constexpr uint8_t PIN_RX_PON  = 26;
constexpr uint32_t RX_SETTLE_MS = 3500;

// Optional analog envelope / RSSI-like output from a custom active receiver.
constexpr bool DCF77_ANALOG_ENABLED = false;
constexpr uint8_t PIN_DCF77_ANALOG = 32;

// Optional precision reference from a GPS PPS receiver.
constexpr bool PPS_ENABLED = false;
constexpr uint8_t PIN_PPS = 33;
constexpr bool PPS_RISING_EDGE = true;

// DCF77 decoder timing windows.
constexpr uint32_t DCF_ZERO_MIN_US = 60000;
constexpr uint32_t DCF_ZERO_MAX_US = 145000;
constexpr uint32_t DCF_ONE_MIN_US  = 155000;
constexpr uint32_t DCF_ONE_MAX_US  = 245000;
constexpr uint32_t DCF_SECOND_MIN_US = 850000;
constexpr uint32_t DCF_SECOND_MAX_US = 1150000;
constexpr uint32_t DCF_MINUTE_GAP_MIN_US = 1500000;
constexpr uint32_t DCF_MINUTE_GAP_MAX_US = 2500000;

// Generic 60-kHz raw-monitor mode. Different standards (MSF, WWVB, JJY60)
// use different pulse widths, so the analyzer does NOT label them as DCF bits.
constexpr uint32_t RAW60_PULSE_MIN_US = 30000;
constexpr uint32_t RAW60_PULSE_MAX_US = 900000;

constexpr uint32_t DISPLAY_REFRESH_MS = 160;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr bool SERIAL_PULSE_LOG = true;
