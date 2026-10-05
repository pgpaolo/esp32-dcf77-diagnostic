#pragma once
#include "dcf77_decoder.h"

void portalBegin();
void portalPoll(DCF77Decoder &decoder);
bool portalTakeResetRequest();
const char *portalAddress();
