#pragma once
#include "dcf77_decoder.h"
#include "receiver_control.h"
void portalBegin();
void portalPoll(const DCF77Decoder &decoder, const ReceiverControl &receiver);
const char *portalAddress();
