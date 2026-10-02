#pragma once
#include "dcf77_decoder.h"
#include "receiver_control.h"
#include "ui.h"
void portalRecordingPoll(AnalyzerUI &ui);
bool portalRecordingBusy();
void portalRecordingCancel();
void portalBegin();
void portalPoll(const DCF77Decoder &decoder, const ReceiverControl &receiver);
bool portalTakeReceiverResetRequest();
bool portalDcfActiveLow();
SignalMode portalSignalMode();
const char *portalAddress();
