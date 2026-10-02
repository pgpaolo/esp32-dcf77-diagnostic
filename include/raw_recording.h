#pragma once
#include <Arduino.h>
#if defined(ESP8266)
constexpr uint32_t RAW_RECORD_SAMPLES = 180000;
constexpr uint32_t RAW_RECORD_BYTES = RAW_RECORD_SAMPLES / 8;
bool rawRecordPrepare();
void rawRecordStart();
void rawRecordStop();
void IRAM_ATTR rawRecordSample(bool high);
bool rawRecordRunning();
uint32_t rawRecordCount();
uint32_t rawRecordDurationUs();
uint32_t rawRecordTimingGaps();
uint32_t rawRecordMaxGapUs();
const uint8_t *rawRecordData();
#endif
