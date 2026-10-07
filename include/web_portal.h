#pragma once
#include "dcf77_decoder.h"

enum class OledViewMode : uint8_t {
    AUTO = 0,
    CLOCK = 1,
    SIGNAL = 2,
    DECODER = 3,
    DIAGNOSTICS = 4
};

enum class ReceiverControlRequest : uint8_t {
    NONE = 0,
    POWER_ON = 1,
    POWER_OFF = 2,
    RESTART = 3
};

void portalBegin();
void portalPoll(DCF77Decoder &decoder);
bool portalTakeResetRequest();
const char *portalAddress();
OledViewMode portalDisplayMode();
const char *portalDisplayModeLabel();
ReceiverControlRequest portalTakeReceiverControlRequest();
void portalReportReceiverStatus(bool enabled, bool restarting);
