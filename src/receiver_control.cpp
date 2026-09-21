#include "receiver_control.h"

void ReceiverControl::begin() {
    _band = ReceiverBand::DCF77_775;

    if (DUAL_FREQUENCY_RECEIVER) {
        pinMode(PIN_RX_BAND, OUTPUT);
        pinMode(PIN_RX_PON, OUTPUT);

        // C-MAX PON is active-low: LOW = receiver on.
        digitalWrite(PIN_RX_PON, LOW);
        applyBand();
        _readyAtMs = millis() + RX_SETTLE_MS;
    } else {
        _readyAtMs = millis();
    }
}

bool ReceiverControl::setBand(ReceiverBand band) {
    if (!DUAL_FREQUENCY_RECEIVER && band != ReceiverBand::DCF77_775) {
        return false;
    }
    if (_band == band) return true;

    _band = band;
    applyBand();
    _readyAtMs = millis() + RX_SETTLE_MS;
    return true;
}

bool ReceiverControl::toggleBand() {
    if (!DUAL_FREQUENCY_RECEIVER) return false;
    return setBand(_band == ReceiverBand::DCF77_775 ? ReceiverBand::LF_60
                                                    : ReceiverBand::DCF77_775);
}

void ReceiverControl::applyBand() {
    if (!DUAL_FREQUENCY_RECEIVER) return;

    // CMMR-6D-7760:
    // BAND=GND -> higher frequency (77.5 kHz)
    // BAND=VDD -> lower frequency  (60.0 kHz)
    digitalWrite(PIN_RX_BAND, _band == ReceiverBand::DCF77_775 ? LOW : HIGH);
}

bool ReceiverControl::ready() const {
    return static_cast<int32_t>(millis() - _readyAtMs) >= 0;
}

uint32_t ReceiverControl::settleRemainingMs() const {
    if (ready()) return 0;
    return _readyAtMs - millis();
}

const char *ReceiverControl::bandLabel() const {
    return _band == ReceiverBand::DCF77_775 ? "77.5k DCF" : "60.0k RAW";
}

float ReceiverControl::frequencyKHz() const {
    return _band == ReceiverBand::DCF77_775 ? 77.5f : 60.0f;
}
