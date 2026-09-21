#include <Arduino.h>
#include "driver/gpio.h"
#include "config.h"
#include "dcf77_decoder.h"
#include "ui.h"

namespace {
constexpr uint8_t PULSE_QUEUE_SIZE = 16;
volatile RawPulse pulseQueue[PULSE_QUEUE_SIZE];
volatile uint8_t queueHead = 0;
volatile uint8_t queueTail = 0;
volatile uint32_t pulseStartUs = 0;
volatile uint32_t previousStartUs = 0;
volatile uint32_t periodAtStartUs = 0;
volatile bool insidePulse = false;
volatile uint32_t latestPpsUs = 0;
volatile bool havePps = false;

portMUX_TYPE isrMux = portMUX_INITIALIZER_UNLOCKED;

DCF77Decoder decoder;
AnalyzerUI ui;

bool buttonPagePrev = true;
bool buttonBlPrev = true;
uint32_t buttonPageChangeMs = 0;
uint32_t buttonBlChangeMs = 0;

inline bool IRAM_ATTR dcfActiveLevel(int level) {
    return DCF77_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

int32_t IRAM_ATTR normalizePpsOffset(uint32_t dcfUs, uint32_t ppsUs) {
    int32_t d = static_cast<int32_t>(dcfUs - ppsUs);
    while (d > 500000) d -= 1000000;
    while (d < -500000) d += 1000000;
    return d;
}

void IRAM_ATTR onPpsEdge() {
    latestPpsUs = micros();
    havePps = true;
}

void IRAM_ATTR onDcfEdge() {
    const uint32_t now = micros();
    const int level = gpio_get_level(static_cast<gpio_num_t>(PIN_DCF77));
    const bool active = dcfActiveLevel(level);

    if (active && !insidePulse) {
        periodAtStartUs = previousStartUs ? (now - previousStartUs) : 0;
        previousStartUs = now;
        pulseStartUs = now;
        insidePulse = true;
        return;
    }

    if (!active && insidePulse) {
        RawPulse p;
        p.startUs = pulseStartUs;
        p.widthUs = now - pulseStartUs;
        p.periodUs = periodAtStartUs;
        p.ppsOffsetUs = (PPS_ENABLED && havePps) ? normalizePpsOffset(p.startUs, latestPpsUs) : INT32_MIN;

        const uint8_t next = (queueHead + 1) % PULSE_QUEUE_SIZE;
        if (next != queueTail) {
            pulseQueue[queueHead].startUs = p.startUs;
            pulseQueue[queueHead].widthUs = p.widthUs;
            pulseQueue[queueHead].periodUs = p.periodUs;
            pulseQueue[queueHead].ppsOffsetUs = p.ppsOffsetUs;
            queueHead = next;
        }
        insidePulse = false;
    }
}

bool popPulse(RawPulse &out) {
    bool available = false;
    portENTER_CRITICAL(&isrMux);
    if (queueTail != queueHead) {
        out.startUs = pulseQueue[queueTail].startUs;
        out.widthUs = pulseQueue[queueTail].widthUs;
        out.periodUs = pulseQueue[queueTail].periodUs;
        out.ppsOffsetUs = pulseQueue[queueTail].ppsOffsetUs;
        queueTail = (queueTail + 1) % PULSE_QUEUE_SIZE;
        available = true;
    }
    portEXIT_CRITICAL(&isrMux);
    return available;
}

void pollButtons() {
    const bool pageNow = digitalRead(PIN_BUTTON_PAGE);
    const bool blNow = digitalRead(PIN_BUTTON_BL);
    const uint32_t now = millis();

    if (pageNow != buttonPagePrev && now - buttonPageChangeMs > 35) {
        buttonPageChangeMs = now;
        buttonPagePrev = pageNow;
        if (!pageNow) ui.nextPage();
    }

    if (blNow != buttonBlPrev && now - buttonBlChangeMs > 35) {
        buttonBlChangeMs = now;
        buttonBlPrev = blNow;
        if (!blNow) ui.toggleBacklight();
    }
}

void logPulse(const RawPulse &p) {
    if (!SERIAL_PULSE_LOG) return;
    const DecoderStats &s = decoder.stats();
    Serial.printf("PULSE,bit=%d,width_us=%lu,period_us=%lu,jitter_us=%ld,quality=%u,frame_pos=%u,pps_us=",
                  s.lastBit, (unsigned long)p.widthUs, (unsigned long)p.periodUs,
                  (long)s.lastJitterUs, s.quality, s.frameBitCount);
    if (p.ppsOffsetUs == INT32_MIN) Serial.println("NA");
    else Serial.println(p.ppsOffsetUs);
}
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(150);
    Serial.println();
    Serial.println("DCF77 Signal Analyzer - TTGO T-Display");

    pinMode(PIN_BUTTON_PAGE, INPUT);
    pinMode(PIN_BUTTON_BL, INPUT_PULLUP);

    pinMode(PIN_DCF77, DCF77_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_DCF77), onDcfEdge, CHANGE);

    if (PPS_ENABLED) {
        pinMode(PIN_PPS, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_PPS), onPpsEdge, PPS_RISING_EDGE ? RISING : FALLING);
    }

    if (DCF77_ANALOG_ENABLED) {
        analogReadResolution(12);
        pinMode(PIN_DCF77_ANALOG, INPUT);
    }

    ui.begin();

    Serial.printf("DCF input GPIO%d, active %s\n", PIN_DCF77, DCF77_ACTIVE_LOW ? "LOW" : "HIGH");
    Serial.printf("GPS PPS: %s\n", PPS_ENABLED ? "enabled" : "disabled");
    Serial.println("CSV-like pulse diagnostics enabled on Serial.");
}

void loop() {
    RawPulse p;
    while (popPulse(p)) {
        decoder.processPulse(p);
        logPulse(p);
    }

    pollButtons();

    int analogRaw = -1;
    if (DCF77_ANALOG_ENABLED) analogRaw = analogRead(PIN_DCF77_ANALOG);
    ui.draw(decoder, analogRaw);

    delay(2);
}
