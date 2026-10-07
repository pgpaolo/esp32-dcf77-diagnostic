#include <Arduino.h>
#include "config.h"
#include "ui.h"
#include "web_portal.h"

namespace {
struct CapturedPulse {
    uint32_t widthUs;
    uint32_t periodUs;
};

constexpr uint8_t QUEUE_SIZE = 16;
volatile CapturedPulse queueBuf[QUEUE_SIZE];
volatile uint8_t qHead = 0, qTail = 0;

volatile uint32_t riseUs = 0;
volatile uint32_t previousRiseUs = 0;
volatile uint32_t periodAtRiseUs = 0;
volatile uint32_t totalEdgesIsr = 0;
volatile uint32_t lastEdgeMsIsr = 0;
volatile bool highPulse = false;

RawSignalStats stats;
AnalyzerUI ui;

uint32_t lastRateMs = 0;
uint32_t lastRateEdges = 0;

void IRAM_ATTR onDcfEdge() {
    const uint32_t nowUs = micros();
    const bool level = digitalRead(PIN_DCF77_DATA) == HIGH;

    ++totalEdgesIsr;
    lastEdgeMsIsr = millis();

    // DCF-3850N-800 baseline: idle LOW, received pulse HIGH.
    if (level && !highPulse) {
        periodAtRiseUs = previousRiseUs ? nowUs - previousRiseUs : 0;
        previousRiseUs = nowUs;
        riseUs = nowUs;
        highPulse = true;
        return;
    }

    if (!level && highPulse) {
        const uint8_t next = (qHead + 1U) % QUEUE_SIZE;
        if (next != qTail) {
            queueBuf[qHead].widthUs = nowUs - riseUs;
            queueBuf[qHead].periodUs = periodAtRiseUs;
            qHead = next;
        }
        highPulse = false;
    }
}

bool popPulse(CapturedPulse &out) {
    noInterrupts();
    if (qTail == qHead) {
        interrupts();
        return false;
    }
    out.widthUs = queueBuf[qTail].widthUs;
    out.periodUs = queueBuf[qTail].periodUs;
    qTail = (qTail + 1U) % QUEUE_SIZE;
    interrupts();
    return true;
}

int8_t classifyRaw(uint32_t widthUs) {
    if (widthUs >= RAW_ZERO_MIN_US && widthUs <= RAW_ZERO_MAX_US) return 0;
    if (widthUs >= RAW_ONE_MIN_US && widthUs <= RAW_ONE_MAX_US) return 1;
    return -1;
}

void processPulse(const CapturedPulse &p) {
    ++stats.totalPulses;
    stats.lastPulseUs = p.widthUs;
    stats.lastPeriodUs = p.periodUs;
    stats.lastBitGuess = classifyRaw(p.widthUs);

    const bool valid = stats.lastBitGuess >= 0;
    if (valid) ++stats.validPulses;
    else ++stats.invalidPulses;

    if (p.periodUs >= RAW_MINUTE_GAP_MIN_US &&
        p.periodUs <= RAW_MINUTE_GAP_MAX_US) {
        ++stats.minuteGaps;
    }

    RawPulseSample sample;
    sample.ageMs = 0;
    sample.widthUs = p.widthUs;
    sample.periodUs = p.periodUs;
    sample.bitGuess = stats.lastBitGuess;
    sample.valid = valid;
    portalPushPulse(sample);

    Serial.printf("RAW level=%d pulse=%.1fms period=%.1fms guess=%d edges=%lu\n",
                  digitalRead(PIN_DCF77_DATA),
                  p.widthUs / 1000.0f,
                  p.periodUs / 1000.0f,
                  stats.lastBitGuess,
                  (unsigned long)stats.totalEdges);
}
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(100);
    Serial.println();
    Serial.println("DCF77 RAW receiver - HW364A / DCF-3850N-800");

    // IMPORTANT: proven wiring uses plain INPUT. Do not enable ESP8266 pull-up.
    pinMode(PIN_DCF77_DATA, INPUT);

    // Product documentation: P1 must be logic LOW.
    pinMode(PIN_DCF77_PON, OUTPUT);
    digitalWrite(PIN_DCF77_PON, LOW);
    stats.ponLow = true;

    attachInterrupt(digitalPinToInterrupt(PIN_DCF77_DATA), onDcfEdge, CHANGE);

    ui.begin();
    portalBegin();

    Serial.printf("T/DATA -> D7/GPIO%d, INPUT (no pull-up)\n", PIN_DCF77_DATA);
    Serial.printf("P1/PON  -> D1/GPIO%d, forced LOW\n", PIN_DCF77_PON);
    Serial.printf("Initial DATA level: %s\n",
                  digitalRead(PIN_DCF77_DATA) ? "HIGH" : "LOW");
}

void loop() {
    CapturedPulse p;
    while (popPulse(p)) processPulse(p);

    uint32_t edges;
    uint32_t lastEdge;
    noInterrupts();
    edges = totalEdgesIsr;
    lastEdge = lastEdgeMsIsr;
    interrupts();

    stats.totalEdges = edges;
    stats.dataLevel = digitalRead(PIN_DCF77_DATA) == HIGH;
    stats.lastEdgeAgeMs = lastEdge ? millis() - lastEdge : 0xFFFFFFFFUL;

    const uint32_t now = millis();
    if (now - lastRateMs >= 1000UL) {
        stats.edgesPerSecond = static_cast<uint16_t>(edges - lastRateEdges);
        lastRateEdges = edges;
        lastRateMs = now;
    }

    portalReportRaw(stats);
    portalPoll();
    ui.draw(stats);

    delay(2);
}
