#include "raw_recording.h"
#if defined(ESP8266)
#include <stdlib.h>
#include <string.h>
namespace {
uint8_t *buffer = nullptr;
volatile bool running = false;
volatile uint32_t count = 0, firstUs = 0, lastUs = 0, gaps = 0, maxGap = 0;
}
bool rawRecordPrepare() {
    if (running) return false;
    if (!buffer) buffer = static_cast<uint8_t *>(malloc(RAW_RECORD_BYTES));
    if (!buffer) return false;
    memset(buffer, 0, RAW_RECORD_BYTES);
    count = firstUs = lastUs = gaps = maxGap = 0;
    return true;
}
void rawRecordStart() { if (buffer) running = true; }
void rawRecordStop() { running = false; }
void IRAM_ATTR rawRecordSample(bool high) {
    if (!running) return;
    const uint32_t pos = count;
    if (pos >= RAW_RECORD_SAMPLES) { running = false; return; }
    const uint32_t now = micros();
    if (pos) {
        const uint32_t gap = now - lastUs;
        if (gap > maxGap) maxGap = gap;
        if (gap > 1500) ++gaps;
    } else firstUs = now;
    lastUs = now;
    if (high) buffer[pos >> 3] |= static_cast<uint8_t>(1U << (pos & 7));
    count = pos + 1;
    if (count == RAW_RECORD_SAMPLES) running = false;
}
bool rawRecordRunning() { return running; }
uint32_t rawRecordCount() { return count; }
uint32_t rawRecordDurationUs() { return count > 1 ? lastUs - firstUs : 0; }
uint32_t rawRecordTimingGaps() { return gaps; }
uint32_t rawRecordMaxGapUs() { return maxGap; }
const uint8_t *rawRecordData() { return buffer; }
#endif
