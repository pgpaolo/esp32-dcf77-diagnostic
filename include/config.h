#pragma once

#include <Arduino.h>

#ifndef DCF_RX_DUAL_CMAX
#define DCF_RX_DUAL_CMAX 0
#endif

// -----------------------------------------------------------------------------
// Board: LILYGO / TTGO T-Display (classic ESP32, ST7789 1.14" 240x135)
// Display pins are configured in platformio.ini.
// -----------------------------------------------------------------------------

#if defined(ESP8266)
constexpr uint8_t PIN_OLED_SDA = 14;
constexpr uint8_t PIN_OLED_SCL = 12;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint32_t OLED_PAGE_MS = 5000;
constexpr uint8_t PIN_BUTTON_PAGE = 0; // optional BOOT: do not hold at reset
constexpr uint8_t PIN_BUTTON_BL = 255; // absent
// MASO-S-R1 PCB connector labels: SEL, OUT, PON, GND, VDD.
constexpr uint8_t PIN_DCF77 = 13;   // D7 <- MASO OUT
constexpr uint8_t PIN_RX_BAND = 4;  // D2 -> MASO SEL
constexpr uint8_t PIN_RX_PON = 5;   // D1 -> MASO PON
constexpr uint8_t PIN_DCF77_ANALOG = A0;
constexpr uint8_t PIN_PPS = 16; // PPS disabled: GPIO16 has no edge interrupt
static_assert(DCF_RX_DUAL_CMAX == 0, "HW364A MASO-S-R1 profile is single-frequency; dual CMAX unsupported");
#else
constexpr uint8_t PIN_TFT_BACKLIGHT = 4;
constexpr uint8_t PIN_BUTTON_PAGE   = 35; // hardware pull-up on T-Display
constexpr uint8_t PIN_BUTTON_BL     = 0;  // BOOT button; avoid holding during reset

// Receiver data output.
constexpr uint8_t PIN_DCF77 = 27;
constexpr uint8_t PIN_RX_BAND = 25;
constexpr uint8_t PIN_RX_PON = 26;
constexpr uint8_t PIN_DCF77_ANALOG = 32;
constexpr uint8_t PIN_PPS = 33;
#endif

// Generic DCF77 modules often use an active-low/open-collector output.
// C-MAX exposes both TCO (positive) and TCON (inverted), so keep this
// configurable according to the pin you actually wire to GPIO27.
#if defined(ESP8266)
// MASO-S-R1 / HW364A: bench tests show the useful DCF77 pulse as ACTIVE HIGH.
// ACTIVE LOW produced the complementary waveform (~800 ms active instead of ~100/200 ms).
constexpr bool DCF77_ACTIVE_LOW = false;
constexpr bool DCF77_USE_INTERNAL_PULLUP = false; // candidate OUT as plain INPUT
#else
constexpr bool DCF77_ACTIVE_LOW = true;
constexpr bool DCF77_USE_INTERNAL_PULLUP = true;  // preserve generic ESP32 receiver default
#endif

// MASO-S-R1 silkscreen reads SEL, OUT, PON, GND, VDD.
// Physical SEL/OUT pad association is still under test; keep SEL/PON runtime-testable.
constexpr bool MASO_DRIVE_SEL = false; // SEL default = FLOAT
constexpr bool MASO_DRIVE_PON = true;  // PON default = LOW
constexpr uint8_t MASO_SEL_LEVEL = LOW;
constexpr uint8_t MASO_PON_LEVEL = LOW;

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


constexpr uint32_t RX_SETTLE_MS = 3500;

// Optional analog envelope / RSSI-like output from a custom active receiver.
constexpr bool DCF77_ANALOG_ENABLED = false;


// Optional precision reference from a GPS PPS receiver.
constexpr bool PPS_ENABLED = false;
#if defined(ESP8266)
static_assert(!PPS_ENABLED, "Assign an interrupt-capable unused GPIO before enabling PPS on HW364A");
#endif

constexpr bool PPS_RISING_EDGE = true;

// DCF77 decoder timing windows.
#if defined(ESP8266)
// MAS6180B DA6180B.005, table 5 (page 8). The MASO-S-R1 IC has not
// been identified: these are receiver-tolerant software limits, not proof
// of its electrical specification. Keep the 130..140 ms ambiguity gap.
constexpr uint32_t DCF_ZERO_MIN_US = 40000;
constexpr uint32_t DCF_ZERO_MAX_US = 130000;
constexpr uint32_t DCF_ONE_MIN_US  = 140000;
constexpr uint32_t DCF_ONE_MAX_US  = 250000;
#else
constexpr uint32_t DCF_ZERO_MIN_US = 60000;
constexpr uint32_t DCF_ZERO_MAX_US = 145000;
constexpr uint32_t DCF_ONE_MIN_US  = 155000;
constexpr uint32_t DCF_ONE_MAX_US  = 245000;
#endif
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
