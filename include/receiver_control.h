#pragma once

#include <Arduino.h>
#include "config.h"

enum class ReceiverBand : uint8_t {
    DCF77_775,
    LF_60
};

class ReceiverControl {
public:
    void begin();
    bool isDual() const { return DUAL_FREQUENCY_RECEIVER; }

    ReceiverBand band() const { return _band; }
    bool setBand(ReceiverBand band);
    bool toggleBand();

    bool ready() const;
    uint32_t settleRemainingMs() const;

    const char *bandLabel() const;
    float frequencyKHz() const;

private:
    ReceiverBand _band = ReceiverBand::DCF77_775;
    uint32_t _readyAtMs = 0;

    void applyBand();
};
