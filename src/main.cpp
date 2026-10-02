#include <Arduino.h>
#if defined(ESP32)
#include "driver/gpio.h"
#endif
#include "web_portal.h"
#include "config.h"
#include "dcf77_decoder.h"
#include "receiver_control.h"
#include "ui.h"
#include "sampled_dcf77.h"

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

#if defined(ESP32)
portMUX_TYPE isrMux = portMUX_INITIALIZER_UNLOCKED;
#endif

DCF77Decoder decoder;
ReceiverControl receiver;
AnalyzerUI ui;

bool pageButtonDown = false;
uint32_t pageButtonDownMs = 0;
bool blButtonPrev = true;
uint32_t blButtonChangeMs = 0;

volatile bool dcfActiveLowRuntime = DCF77_ACTIVE_LOW;

inline bool IRAM_ATTR dcfActiveLevel(int level) {
    return dcfActiveLowRuntime ? (level == LOW) : (level == HIGH);
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
    #if defined(ESP32)
    const int level = gpio_get_level(static_cast<gpio_num_t>(PIN_DCF77));
#else
    const int level = digitalRead(PIN_DCF77);
#endif
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
#if defined(ESP32)
    portENTER_CRITICAL(&isrMux);
#else
    noInterrupts();
#endif
    if (queueTail != queueHead) {
        out.startUs = pulseQueue[queueTail].startUs;
        out.widthUs = pulseQueue[queueTail].widthUs;
        out.periodUs = pulseQueue[queueTail].periodUs;
        out.ppsOffsetUs = pulseQueue[queueTail].ppsOffsetUs;
        queueTail = (queueTail + 1) % PULSE_QUEUE_SIZE;
        available = true;
    }
#if defined(ESP32)
    portEXIT_CRITICAL(&isrMux);
#else
    interrupts();
#endif
    return available;
}

void clearPulseCapture() {
#if defined(ESP32)
    portENTER_CRITICAL(&isrMux);
#else
    noInterrupts();
#endif
    queueTail = queueHead;
    pulseStartUs = 0;
    previousStartUs = 0;
    periodAtStartUs = 0;
    insidePulse = false;
#if defined(ESP32)
    portEXIT_CRITICAL(&isrMux);
#else
    interrupts();
#endif
}

void applyDecoderMode() {
    decoder.setSignalMode(receiver.band() == ReceiverBand::DCF77_775
                              ? SignalMode::DCF77
                              : SignalMode::RAW_60KHZ);
    clearPulseCapture();
}

void printBand() {
    Serial.printf("Receiver band: %s (%.1f kHz), settle=%lu ms\n",
                  receiver.bandLabel(), receiver.frequencyKHz(),
                  (unsigned long)receiver.settleRemainingMs());
}

void toggleBand() {
    if (!receiver.toggleBand()) {
        Serial.println("Band switch ignored: generic single-frequency build.");
        return;
    }
    applyDecoderMode();
    printBand();
}

void pollButtons() {
    const bool pageNow = digitalRead(PIN_BUTTON_PAGE);
    const bool blNow = PIN_BUTTON_BL == 255 ? true : digitalRead(PIN_BUTTON_BL);
    const uint32_t now = millis();

    // GPIO35 button: short press = next page, long press >=1.2s = band toggle.
    if (!pageNow && !pageButtonDown) {
        pageButtonDown = true;
        pageButtonDownMs = now;
    } else if (pageNow && pageButtonDown) {
        const uint32_t held = now - pageButtonDownMs;
        pageButtonDown = false;
        if (held >= 1200 && receiver.isDual()) toggleBand();
        else ui.nextPage();
    }

    if (blNow != blButtonPrev && now - blButtonChangeMs > 35) {
        blButtonChangeMs = now;
        blButtonPrev = blNow;
        if (!blNow) ui.toggleBacklight();
    }
}

void pollSerialCommands() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == 'x' || c == 'X') { portalRecordingCancel(); continue; }
        if (portalRecordingBusy()) continue;
        if (c == 'b' || c == 'B') {
            toggleBand();
        } else if ((c == '7') && receiver.isDual()) {
            if (receiver.setBand(ReceiverBand::DCF77_775)) {
                applyDecoderMode();
                printBand();
            }
        } else if ((c == '6') && receiver.isDual()) {
            if (receiver.setBand(ReceiverBand::LF_60)) {
                applyDecoderMode();
                printBand();
            }
        }
    }
}

void logPulse(const RawPulse &p) {
    if (!SERIAL_PULSE_LOG) return;
    const DecoderStats &s = decoder.stats();
#if defined(ESP8266)
    Serial.printf("PULSE,mode=%s,bit=%d,width_us=%lu,period_us=%lu,jitter_us=%ld,quality=%u,frame_pos=%u,pps_us=",
                  decoder.signalMode() == SignalMode::DCF77 ? "DCF77" : "RAW60",
                  s.lastBit, (unsigned long)p.widthUs, (unsigned long)p.periodUs,
                  (long)s.lastJitterUs, s.quality, s.frameBitCount);
#else
    Serial.printf("PULSE,band=%.1f,mode=%s,bit=%d,width_us=%lu,period_us=%lu,jitter_us=%ld,quality=%u,frame_pos=%u,pps_us=",
                  receiver.frequencyKHz(),
                  decoder.signalMode() == SignalMode::DCF77 ? "DCF77" : "RAW60",
                  s.lastBit, (unsigned long)p.widthUs, (unsigned long)p.periodUs,
                  (long)s.lastJitterUs, s.quality, s.frameBitCount);
#endif
    if (p.ppsOffsetUs == INT32_MIN) Serial.println("NA");
    else Serial.println(p.ppsOffsetUs);
}
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(150);
    Serial.println();
#if defined(ESP8266)
    Serial.println("DCF77 Signal Analyzer - HW364A OLED + WiFi");
#else
    Serial.println("DCF77 / 60 kHz Signal Analyzer - TTGO T-Display");
#endif

    pinMode(PIN_BUTTON_PAGE,
#if defined(ESP8266)
            INPUT_PULLUP
#else
            INPUT
#endif
    );
    if (PIN_BUTTON_BL != 255) pinMode(PIN_BUTTON_BL, INPUT_PULLUP);

    receiver.begin();
    applyDecoderMode();

    pinMode(PIN_DCF77, DCF77_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_DCF77), onDcfEdge, CHANGE);
#if defined(ESP8266)
    sampledDcfBegin(PIN_DCF77, dcfActiveLowRuntime);
#endif

    if (PPS_ENABLED) {
        pinMode(PIN_PPS, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_PPS), onPpsEdge, PPS_RISING_EDGE ? RISING : FALLING);
    }

    if (DCF77_ANALOG_ENABLED) {
#if defined(ESP32)
        analogReadResolution(12);
#endif
        pinMode(PIN_DCF77_ANALOG, INPUT);
    }

    ui.begin();
    portalBegin();

    Serial.printf("RX profile: %s\n", receiver.isDual() ? "C-MAX CMMR-6D-7760 dual 60/77.5" : "generic DCF77");
    Serial.printf("Data GPIO%d, active %s\n", PIN_DCF77, DCF77_ACTIVE_LOW ? "LOW" : "HIGH");
    Serial.printf("GPS PPS: %s\n", PPS_ENABLED ? "enabled" : "disabled");
    if (receiver.isDual()) {
        Serial.printf("BAND GPIO%d, PON GPIO%d. Commands: 7=77.5k, 6=60k, b=toggle\n",
                      PIN_RX_BAND, PIN_RX_PON);
    }
    printBand();
}

void loop() {
    portalRecordingPoll(ui);
    pollSerialCommands();
    pollButtons();

#if defined(ESP8266)
    sampledDcfPoll();

    // Drain the raw edge queue. In DCF77 mode it is diagnostic only: the
    // decoder is driven by the timer-sampled phase detector below.
    RawPulse p;
    while (popPulse(p)) {
        if (!receiver.ready()) continue;
        if (decoder.signalMode() == SignalMode::RAW_60KHZ) {
            decoder.processPulse(p);
            logPulse(p);
        }
    }

    SampledDcfEvent sampled;
    while (sampledDcfPopEvent(sampled)) {
        if (!receiver.ready() || decoder.signalMode() != SignalMode::DCF77) continue;

        // Always expose sampled symbols to diagnostics. Before the minute
        // phase is locked secondIndex is 255 ('?'); the decoder records the
        // symbol but does not place it into a frame yet.
        decoder.processSampledSymbol(sampled.secondLocked ? sampled.secondIndex : 255,
                                     sampled.bit,
                                     sampled.confidence,
                                     sampled.minuteMarker,
                                     sampled.markerCandidate,
                                     sampled.startUs);
    }
#else
    RawPulse p;
    while (popPulse(p)) {
        if (!receiver.ready()) continue;
        decoder.processPulse(p);
        logPulse(p);
    }
#endif

    int analogRaw = -1;
    if (DCF77_ANALOG_ENABLED) analogRaw = analogRead(PIN_DCF77_ANALOG);
#if defined(ESP8266)
    const char *displayMode = decoder.signalMode() == SignalMode::DCF77 ? "DCF77 TEST" : "RAW 60k TEST";
    ui.draw(decoder, analogRaw, displayMode, receiver.ready());
#else
    ui.draw(decoder, analogRaw, receiver.bandLabel(), receiver.ready());
#endif

    portalPoll(decoder, receiver);

#if defined(ESP8266)
    const SignalMode requestedMode = portalSignalMode();
    if (requestedMode != decoder.signalMode()) {
        decoder.setSignalMode(requestedMode);
        clearPulseCapture();
        Serial.printf("MASO decoder mode changed: %s\n",
                      requestedMode == SignalMode::DCF77 ? "DCF77 77.5 kHz" : "RAW 60 kHz");
    }

    const bool requestedPolarity = portalDcfActiveLow();
    if (requestedPolarity != dcfActiveLowRuntime) {
        dcfActiveLowRuntime = requestedPolarity;
        decoder.reset();
        clearPulseCapture();
        sampledDcfSetPolarity(dcfActiveLowRuntime);
        Serial.printf("MASO OUT polarity changed: ACTIVE %s\n",
                      dcfActiveLowRuntime ? "LOW" : "HIGH");
    }
#endif

    if (portalTakeReceiverResetRequest()) {
        decoder.reset();
        clearPulseCapture();
#if defined(ESP8266)
        sampledDcfReset();
#endif
        Serial.println("MASO diagnostic reset: decoder statistics, sampled phase and pulse timing cleared");
    }

    delay(2);
}
