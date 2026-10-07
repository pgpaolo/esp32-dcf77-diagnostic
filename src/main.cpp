#include <Arduino.h>
#include "config.h"
#include "dcf77_decoder.h"
#include "ui.h"
#include "web_portal.h"

namespace {
constexpr uint8_t QUEUE_SIZE = 16;
volatile RawPulse queueBuf[QUEUE_SIZE];
volatile uint8_t qHead = 0, qTail = 0;
volatile uint32_t pulseStartUs = 0;
volatile uint32_t previousStartUs = 0;
volatile uint32_t periodAtStartUs = 0;
volatile bool insidePulse = false;

bool receiverEnabled = false;
bool receiverRestarting = false;
uint32_t receiverRestartDeadlineMs = 0;

DCF77Decoder decoder;
AnalyzerUI ui;

inline bool IRAM_ATTR activeLevel(int level) {
    return DCF_ACTIVE_LOW ? level == LOW : level == HIGH;
}

inline uint8_t ponLevel(bool enabled) {
    const bool active = enabled;
    return (active == DCF_PON_ACTIVE_LOW) ? LOW : HIGH;
}

void IRAM_ATTR onDcfEdge() {
    const uint32_t now = micros();
    const bool active = activeLevel(digitalRead(PIN_DCF77));

    if (active && !insidePulse) {
        periodAtStartUs = previousStartUs ? now - previousStartUs : 0;
        previousStartUs = now;
        pulseStartUs = now;
        insidePulse = true;
        return;
    }

    if (!active && insidePulse) {
        const uint8_t next = (qHead + 1U) % QUEUE_SIZE;
        if (next != qTail) {
            queueBuf[qHead].startUs = pulseStartUs;
            queueBuf[qHead].widthUs = now - pulseStartUs;
            queueBuf[qHead].periodUs = periodAtStartUs;
            qHead = next;
        }
        insidePulse = false;
    }
}

bool popPulse(RawPulse &out) {
    noInterrupts();
    if (qTail == qHead) {
        interrupts();
        return false;
    }
    out.startUs = queueBuf[qTail].startUs;
    out.widthUs = queueBuf[qTail].widthUs;
    out.periodUs = queueBuf[qTail].periodUs;
    qTail = (qTail + 1U) % QUEUE_SIZE;
    interrupts();
    return true;
}

void clearCapture() {
    noInterrupts();
    qTail = qHead;
    pulseStartUs = previousStartUs = periodAtStartUs = 0;
    insidePulse = false;
    interrupts();
}

void setReceiverPower(bool enabled) {
    if (receiverEnabled == enabled) return;

    if (!enabled) {
        detachInterrupt(digitalPinToInterrupt(PIN_DCF77));
        receiverEnabled = false;
        digitalWrite(PIN_DCF77_PON, ponLevel(false));
        clearCapture();
        Serial.println("DCF77 receiver PON: OFF");
        return;
    }

    digitalWrite(PIN_DCF77_PON, ponLevel(true));
    delay(10);
    clearCapture();
    attachInterrupt(digitalPinToInterrupt(PIN_DCF77), onDcfEdge, CHANGE);
    receiverEnabled = true;
    Serial.println("DCF77 receiver PON: ON");
}

void startReceiverRestart() {
    receiverRestarting = true;
    receiverRestartDeadlineMs = millis() + DCF_PON_RESTART_MS;
    setReceiverPower(false);
    decoder.reset();
    Serial.printf("DCF77 receiver restart: OFF for %lu ms\n",
                  (unsigned long)DCF_PON_RESTART_MS);
}

void logPulse(const RawPulse &p) {
    const auto &s = decoder.stats();
    Serial.printf("DCF77,bit=%d,width_ms=%.1f,period_ms=%.1f,quality=%u,frame=%u\n",
                  s.lastBit, p.widthUs/1000.0f, p.periodUs/1000.0f,
                  s.quality, s.frameBitCount);
}
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(100);
    Serial.println();
    Serial.println("DCF77 receiver - HW364A");

    pinMode(PIN_BUTTON_PAGE, INPUT_PULLUP);
    pinMode(PIN_DCF77, DCF_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
    pinMode(PIN_DCF77_PON, OUTPUT);
    digitalWrite(PIN_DCF77_PON, ponLevel(false));
    setReceiverPower(true);

    ui.begin();
    portalBegin();

    Serial.printf("DCF77 DATA GPIO%d, active %s, pull-up %s\n",
                  PIN_DCF77,
                  DCF_ACTIVE_LOW ? "LOW" : "HIGH",
                  DCF_USE_INTERNAL_PULLUP ? "ON" : "OFF");
    Serial.printf("DCF77 PON GPIO%d, active %s, receiver ON\n",
                  PIN_DCF77_PON,
                  DCF_PON_ACTIVE_LOW ? "LOW" : "HIGH");
}

void loop() {
    RawPulse p;
    while (popPulse(p)) {
        decoder.processPulse(p);
        logPulse(p);
    }

    static bool buttonWasDown = false;
    const bool down = digitalRead(PIN_BUTTON_PAGE) == LOW;
    if (down && !buttonWasDown) ui.nextPage();
    buttonWasDown = down;

    if (receiverRestarting &&
        static_cast<int32_t>(millis() - receiverRestartDeadlineMs) >= 0) {
        receiverRestarting = false;
        setReceiverPower(true);
        Serial.println("DCF77 receiver restart complete");
    }

    portalReportReceiverStatus(receiverEnabled, receiverRestarting);
    ui.draw(decoder);
    portalPoll(decoder);

    switch (portalTakeReceiverControlRequest()) {
        case ReceiverControlRequest::POWER_ON:
            receiverRestarting = false;
            setReceiverPower(true);
            decoder.reset();
            break;
        case ReceiverControlRequest::POWER_OFF:
            receiverRestarting = false;
            setReceiverPower(false);
            decoder.reset();
            break;
        case ReceiverControlRequest::RESTART:
            startReceiverRestart();
            break;
        default:
            break;
    }

    if (portalTakeResetRequest()) {
        decoder.reset();
        clearCapture();
        Serial.println("DCF77 decoder reset");
    }

    delay(2);
}
