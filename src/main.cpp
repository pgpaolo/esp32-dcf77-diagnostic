#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

#ifndef DCF_DATA_PIN
#define DCF_DATA_PIN 13   // D7 / GPIO13
#endif
#ifndef DCF_PON_PIN
#define DCF_PON_PIN 5     // D1 / GPIO5
#endif
#ifndef DCF_PON_ACTIVE_LOW
#define DCF_PON_ACTIVE_LOW 1
#endif
#ifndef DCF_IDLE_LOW
#define DCF_IDLE_LOW 1
#endif

#ifndef OLED_ENABLED
#define OLED_ENABLED 1
#endif
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 14     // HW-364A OLED SDA = GPIO14
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 12     // HW-364A OLED SCL = GPIO12
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif
#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif

static const char *WIFI_CFG_FILE = "/wifi.cfg";
static const char *RADIO_CFG_FILE = "/radio.cfg";
static const uint16_t DNS_PORT = 53;

// --- Radio duty-cycle ---
// Dopo N frame DCF77 completi e validi, il ricevitore viene spento tramite PON.
// L'orologio continua in HOLDOVER locale; allo scadere del timer la radio viene
// riaccesa e parte una nuova acquisizione. Parametri persistenti in LittleFS.
bool radioDutyEnabled = true;
uint8_t radioSyncTarget = 2;             // default richiesto: 2 sync validi
uint16_t radioOffMinutes = 60;           // default: 60 minuti, modificabile da Web
bool radioAutoSleeping = false;
uint32_t radioSleepStartedMs = 0;
uint32_t radioSleepDurationMs = 0;
uint32_t radioSleepCount = 0;
uint32_t radioWakeCount = 0;
bool radioSleepPending = false;


struct EdgeEvent {
  uint32_t tUs;
  uint32_t dtUs;
  uint32_t pulseUs;
  uint8_t level;
};

static const uint8_t QSIZE = 96;
volatile EdgeEvent q[QSIZE];
volatile uint8_t qHead = 0, qTail = 0;
volatile uint32_t totalEdges = 0;
volatile uint32_t droppedEvents = 0;
volatile uint32_t lastEdgeUs = 0;
volatile uint32_t pulseStartUs = 0;

uint32_t good0 = 0, good1 = 0, noise = 0;
uint32_t reportStartMs = 0;
uint32_t reportEdges = 0;
uint32_t edgesLast5s = 0;
uint32_t lastPulseUs = 0;
uint32_t lastPulseAtMs = 0;
bool receiverOn = true;
bool usePullup = false; // default: INPUT puro, come la configurazione che aveva dato segnale

// Istogramma impulsi: <10, 10-30, 30-60, 60-80, 80-140, 140-170, 170-240, 240-400, >400 ms
uint32_t hist[9] = {0};

// --- Clock Recovery v1.8: fase statistica + frame indicizzato sugli slot DCF ---
// Obiettivo: ignorare gli spike casuali e ricostruire il DCF77 sulla periodicita' di 1 Hz.
static const uint32_t DCF_SECOND_US = 1000000UL;
static const uint32_t PHASE_BIN_US = 25000UL;       // 40 bin per secondo
static const uint8_t  PHASE_BINS = 40;
static const uint32_t PHASE_EVAL_MS = 5000UL;
static const uint32_t PHASE_RESET_MS = 20000UL;
static const uint32_t PHASE_GATE_US = 180000UL;     // candidato deve iniziare entro +/-180 ms dalla fase
static const uint32_t GLITCH_MIN_US = 5000UL;       // <5 ms ignorato completamente dal clock recovery
static const uint32_t SEARCH_MIN_US = 60000UL;      // candidati usati per trovare la fase
static const uint32_t SEARCH_MAX_US = 280000UL;
static const uint32_t MERGE_GAP_US = 25000UL;       // ricompone impulsi spezzati da gap <=25 ms
static const uint32_t BIT0_MIN_US = 65000UL;
static const uint32_t BIT0_MAX_US = 155000UL;
static const uint32_t BIT1_MIN_US = 155001UL;
static const uint32_t BIT1_MAX_US = 275000UL;

bool pllLocked = false;
bool frameSynced = false;
uint8_t acqStreak = 0;                              // visualizzato come forza del picco fase
uint32_t filteredAccepted = 0;
uint32_t filteredBit0 = 0, filteredBit1 = 0;
uint32_t rejectedShort = 0, rejectedWidth = 0, rejectedPhase = 0;
uint32_t minuteMarkers = 0;          // marker osservati o ricostruiti sulla cadenza 60 s
uint32_t inferredMarkers = 0;         // marker ricostruiti temporalmente (impulso spurio nel secondo 59)
uint32_t rejectedMarkers = 0;         // impulsi validi ma caduti nello slot marker
uint32_t lostBitGaps = 0;             // slot dati mancanti
int32_t lastMarkerSlot = INT32_MIN;   // slot assoluto del secondo 59 (senza impulso)
bool frameCorrupt = false;            // mantenuto per compatibilita UI; ora i missing sono tracciati da frameSeen[]
uint32_t frameValid = 0, frameInvalid = 0;
uint32_t timeValid = 0, dateValid = 0;
uint32_t recoveredBitsTotal = 0;
uint8_t lastFrameRecovered = 0;
uint8_t lastFrameMissingRelevant = 0;
uint8_t lastFrameMissingAfterRecovery = 0;
uint8_t lastFramePhysicalSeen = 0;
bool lastFrameClockSaved = false;
bool lastFrameDateSaved = false;
uint32_t frameSavedWithRecovery = 0;
uint8_t frameBits[59] = {0};
bool frameSeen[59] = {false};
uint8_t frameSeenCount = 0;
uint8_t frameLen = 0;                 // avanzamento 0..59, MAI oltre 59
String lastDecoded = "--:--";
String lastDecodedDate = "--/--/----";

// --- Clock locale derivato da DCF77 ---
// DCF77 non trasmette un campo numerico dei secondi: il secondo viene derivato
// localmente dal ritmo 1 Hz e riallineato ad ogni frame/minute marker valido.
bool hasValidClock = false;
uint8_t syncHour = 0, syncMinute = 0;
uint32_t syncBaseMillis = 0;
uint32_t lastSyncMillis = 0;
uint32_t consecutiveFrameOk = 0;
uint8_t lastWeekday = 0;
bool lastCEST = false, lastCET = false;
bool dstChangeAnnounce = false;
bool leapSecondAnnounce = false;

// Ultimi impulsi utili alla UI (microsecondi, ring buffer).
static const uint8_t RECENT_PULSES = 8;
uint32_t recentPulseUs[RECENT_PULSES] = {0};
uint8_t recentPulseHead = 0;

bool verboseRaw = false;

// Ricerca statistica della fase modulo 1 secondo.
uint16_t phaseHist[PHASE_BINS] = {0};
uint32_t phaseLastStart[PHASE_BINS] = {0};
uint32_t phaseSearchStartedMs = 0;
uint32_t phaseLastEvalMs = 0;
uint8_t phasePeakBin = 0;
uint16_t phasePeakScore = 0;
uint16_t phaseSecondScore = 0;
uint32_t phaseOffsetUs = 0;
uint32_t slotAnchorUs = 0;
float phaseDominance = 0.0f;

// Ricostruzione del miglior cluster dentro ogni slot da 1 s.
bool slotActive = false;
int32_t activeSlot = 0;
uint32_t clusterStartUs = 0;
uint32_t clusterEndUs = 0;
uint32_t bestClusterUs = 0;
uint16_t slotFragments = 0;
uint32_t mergedSlots = 0;
uint32_t invalidSlots = 0;
int32_t lastValidSlot = INT32_MIN;
uint32_t lastValidBitAtMs = 0;

// RF QUIET TEST
bool quietPending = false;
bool quietRunning = false;
bool quietResultReady = false;
uint32_t quietRequestMs = 0;
uint32_t quietStartMs = 0;
const uint32_t QUIET_DURATION_MS = 60000UL;
uint32_t quietEdgesStart = 0;
uint32_t quietGood0 = 0, quietGood1 = 0, quietNoise = 0;
uint32_t quietHist[9] = {0};
uint32_t quietEdges = 0;
uint32_t quietDroppedStart = 0;
uint32_t quietDropped = 0;

// AUTO A/B DATA bias: 60 s INPUT + 60 s INPUT_PULLUP, poi selezione automatica
enum AbStage : uint8_t { AB_IDLE=0, AB_INPUT=1, AB_PULLUP=2, AB_DONE=3 };
AbStage abStage = AB_IDLE;
bool abRunning = false;
bool abReady = false;
uint32_t abStageStartMs = 0;
const uint32_t AB_STAGE_MS = 60000UL;
uint32_t abBaseEdges = 0, abBaseGood0 = 0, abBaseGood1 = 0, abBaseNoise = 0;
uint32_t abInputEdges = 0, abInputGood0 = 0, abInputGood1 = 0, abInputNoise = 0;
uint32_t abPullEdges = 0, abPullGood0 = 0, abPullGood1 = 0, abPullNoise = 0;
float abInputValidPct = 0.0f, abPullValidPct = 0.0f;
String abWinner = "-";

ESP8266WebServer server(80);
DNSServer dnsServer;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
Adafruit_SH1106G displaySH1106(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
enum OledDriver : uint8_t { OLED_NONE=0, OLED_SSD1306=1, OLED_SH1106=2 };
OledDriver oledDriver = OLED_NONE;
bool displayPresent = false;
bool displayEnabled = OLED_ENABLED;
uint32_t lastDisplayMs = 0;
uint8_t displayPage = 0;

// v2.5.3: il display viene completamente silenziato durante la ricezione DCF77.
bool oledRfQuietDuringRx = true;
bool oledRfQuietActive = false;

// Forward declarations OLED v2.4.3.
// Le definizioni effettive restano piu' avanti nel file.
extern uint8_t displayI2cAddr;
extern bool displayI2cAck;
extern String displayDriverName;
extern uint8_t displaySdaPin;
extern uint8_t displaySclPin;
extern String displayPinMap;

bool i2cProbe(uint8_t addr);
void oledClear();
void oledSetTextColor();
void oledSetTextSize(uint8_t s);
void oledSetCursor(int16_t x, int16_t y);
void oledPrint(const String &s);
void oledPrint(const __FlashStringHelper *s);
void oledPrint(int v);
void oledPrint(unsigned long v);
void oledPrint(float v, int digits);
void oledPrintln(const String &s);
void oledPrintln(const __FlashStringHelper *s);
void oledPrintln(int v);
void oledFlush();
void oledPower(bool on);
void oledSetRfQuiet(bool quiet);
bool initOledDriver(OledDriver drv);
void initDisplay();
void drawDisplay();

// --- DCF PRIORITY SCHEDULER v2.3 ---
// Il front-end DCF77 ha priorita' assoluta nei primi 350 ms di ogni secondo
// ricostruito. OLED, HTTP, mDNS, DNS e operazioni di configurazione vengono
// eseguiti nella finestra di servizio, lontano dal fronte utile.
static const uint32_t DCF_PROTECT_END_US = 350000UL;
static const uint32_t DCF_SERVICE_START_US = 420000UL;
static const uint32_t DCF_SERVICE_END_US = 900000UL;
uint32_t deferredServiceLoops = 0;
uint32_t oledUpdates = 0;

uint32_t dcfPhaseNowUs() {
  if (!pllLocked) return 999999UL;
  const uint32_t rel = (uint32_t)(micros() - slotAnchorUs);
  return rel % DCF_SECOND_US;
}

bool dcfProtectedWindow() {
  if (!pllLocked) return false;
  return dcfPhaseNowUs() < DCF_PROTECT_END_US;
}

bool dcfServiceWindow() {
  if (!pllLocked) return true;
  const uint32_t p = dcfPhaseNowUs();
  return p >= DCF_SERVICE_START_US && p <= DCF_SERVICE_END_US;
}

struct WiFiConfig {
  String ssid;
  String password;
  String hostname;
  bool fromFlash = false;
};
WiFiConfig wifiCfg;
bool configPortalActive = false;
bool restartPending = false;
uint32_t restartRequestedMs = 0;
String setupApSsid;
const char *SETUP_AP_PASSWORD = "dcf77setup";

// Forward declaration: setDataBias() reinstalla l'ISR prima della sua definizione.
void IRAM_ATTR onEdge();
bool loadRadioConfig();
bool saveRadioConfig();
void enterRadioAutoSleep();
void wakeRadioAuto();
void tickRadioDutyCycle();
uint32_t radioSleepRemainingSec();

void setReceiver(bool on) {
  receiverOn = on;
#if DCF_PON_ACTIVE_LOW
  digitalWrite(DCF_PON_PIN, on ? LOW : HIGH);
#else
  digitalWrite(DCF_PON_PIN, on ? HIGH : LOW);
#endif
}

void setDataBias(bool pullup) {
  usePullup = pullup;
  detachInterrupt(digitalPinToInterrupt(DCF_DATA_PIN));
  pinMode(DCF_DATA_PIN, pullup ? INPUT_PULLUP : INPUT);
  delay(5);
  noInterrupts();
  pulseStartUs = 0;
  lastEdgeUs = micros();
  qHead = qTail = 0;
  interrupts();
  attachInterrupt(digitalPinToInterrupt(DCF_DATA_PIN), onEdge, CHANGE);
}

void IRAM_ATTR onEdge() {
  const uint32_t now = micros();
  const uint8_t level = digitalRead(DCF_DATA_PIN);
  const uint32_t dt = now - lastEdgeUs;
  lastEdgeUs = now;

  uint32_t pulse = 0;
#if DCF_IDLE_LOW
  if (level == HIGH) {
    pulseStartUs = now;
  } else if (pulseStartUs != 0) {
    pulse = now - pulseStartUs;
    pulseStartUs = 0;
  }
#else
  if (level == LOW) {
    pulseStartUs = now;
  } else if (pulseStartUs != 0) {
    pulse = now - pulseStartUs;
    pulseStartUs = 0;
  }
#endif

  const uint8_t next = (uint8_t)((qHead + 1) % QSIZE);
  if (next != qTail) {
    q[qHead].tUs = now;
    q[qHead].dtUs = dt;
    q[qHead].pulseUs = pulse;
    q[qHead].level = level;
    qHead = next;
  } else {
    droppedEvents++;
  }
  totalEdges++;
}

bool popEvent(EdgeEvent &e) {
  noInterrupts();
  if (qTail == qHead) {
    interrupts();
    return false;
  }
  e.tUs = q[qTail].tUs;
  e.dtUs = q[qTail].dtUs;
  e.pulseUs = q[qTail].pulseUs;
  e.level = q[qTail].level;
  qTail = (uint8_t)((qTail + 1) % QSIZE);
  interrupts();
  return true;
}

uint8_t pulseBin(uint32_t us) {
  const uint32_t ms = us / 1000UL;
  if (ms < 10) return 0;
  if (ms < 30) return 1;
  if (ms < 60) return 2;
  if (ms < 80) return 3;
  if (ms <= 140) return 4;
  if (ms < 170) return 5;
  if (ms <= 240) return 6;
  if (ms <= 400) return 7;
  return 8;
}

const char *classifyPulse(uint32_t us, bool quiet) {
  if (!us) return "";
  const uint32_t ms = us / 1000UL;
  const uint8_t b = pulseBin(us);
  hist[b]++;
  if (quiet) quietHist[b]++;

  if (ms >= 80 && ms <= 140) {
    good0++;
    if (quiet) quietGood0++;
    return "BIT 0";
  }
  if (ms >= 170 && ms <= 240) {
    good1++;
    if (quiet) quietGood1++;
    return "BIT 1";
  }
  noise++;
  if (quiet) quietNoise++;
  return "NOISE";
}


static inline uint32_t absDiffUs(int32_t x) { return (uint32_t)(x < 0 ? -x : x); }

uint8_t bcdMinute(const uint8_t *b) {
  return b[21] + 2*b[22] + 4*b[23] + 8*b[24] + 10*b[25] + 20*b[26] + 40*b[27];
}
uint8_t bcdHour(const uint8_t *b) {
  return b[29] + 2*b[30] + 4*b[31] + 8*b[32] + 10*b[33] + 20*b[34];
}
uint8_t bcdDay(const uint8_t *b) {
  return b[36] + 2*b[37] + 4*b[38] + 8*b[39] + 10*b[40] + 20*b[41];
}
uint8_t bcdMonth(const uint8_t *b) {
  return b[45] + 2*b[46] + 4*b[47] + 8*b[48] + 10*b[49];
}
uint8_t bcdYear(const uint8_t *b) {
  return b[50] + 2*b[51] + 4*b[52] + 8*b[53] + 10*b[54] + 20*b[55] + 40*b[56] + 80*b[57];
}
bool parityOk(const uint8_t *b, uint8_t from, uint8_t to, uint8_t parityBit) {
  uint8_t p = 0;
  for (uint8_t i = from; i <= to; i++) p ^= b[i];
  return p == b[parityBit];
}

void clearFrameBuffer() {
  memset(frameBits, 0, sizeof(frameBits));
  memset(frameSeen, 0, sizeof(frameSeen));
  frameSeenCount = 0;
  frameLen = 0;
  frameCorrupt = false;
}

bool recoverParityBlock(uint8_t *b, bool *seen, uint8_t from, uint8_t to, uint8_t parityBit, uint8_t &recovered) {
  int8_t missing = -1;
  uint8_t missingCount = 0;
  for (uint8_t i = from; i <= to; ++i) {
    if (!seen[i]) { missing = (int8_t)i; missingCount++; }
  }
  if (!seen[parityBit]) { missing = (int8_t)parityBit; missingCount++; }
  if (missingCount > 1) return false;

  if (missingCount == 1) {
    if ((uint8_t)missing == parityBit) {
      uint8_t x = 0;
      for (uint8_t i = from; i <= to; ++i) x ^= b[i];
      b[parityBit] = x;
      seen[parityBit] = true;
    } else {
      uint8_t x = b[parityBit];
      for (uint8_t i = from; i <= to; ++i) if (i != (uint8_t)missing) x ^= b[i];
      b[(uint8_t)missing] = x;
      seen[(uint8_t)missing] = true;
    }
    recovered++;
  }
  return parityOk(b, from, to, parityBit);
}

void finalizeFrame() {
  if (!frameSynced) return;

  // v2.4: finalizzazione "radio clock" tollerante alle erasure.
  // - bit 20 e' un bit di protocollo fisso (=1) e puo' essere ricostruito in modo deterministico;
  // - fino a UN bit mancante per ciascun blocco protetto puo' essere ricostruito dalla parita';
  // - quindi fino a 3 erasure distribuite tra minuti/ore/data sono recuperabili;
  // - i bit 0..19 (eccetto il 20) non sono necessari per salvare ora/data.
  uint8_t b[59];
  bool seen[59];
  memcpy(b, frameBits, sizeof(b));
  memcpy(seen, frameSeen, sizeof(seen));

  lastFramePhysicalSeen = frameSeenCount;
  lastFrameClockSaved = false;
  lastFrameDateSaved = false;

  uint8_t recovered = 0;
  uint8_t missingRelevant = 0;
  for (uint8_t i = 20; i <= 58; ++i) if (!seen[i]) missingRelevant++;
  lastFrameMissingRelevant = missingRelevant;

  // Bit 20 (start of encoded time) e' sempre 1 nello schema DCF77 standard.
  // Se manca fisicamente non ha senso perdere un minuto intero: lo ricostruiamo.
  if (!seen[20]) {
    b[20] = 1;
    seen[20] = true;
    recovered++;
  }

  const bool minOk = recoverParityBlock(b, seen, 21, 27, 28, recovered);
  const bool hourOk = recoverParityBlock(b, seen, 29, 34, 35, recovered);
  const bool dateParityOk = recoverParityBlock(b, seen, 36, 57, 58, recovered);

  uint8_t missingAfter = 0;
  for (uint8_t i = 20; i <= 58; ++i) if (!seen[i]) missingAfter++;
  lastFrameMissingAfterRecovery = missingAfter;

  const bool structure = seen[20] && b[20] == 1;
  const uint8_t mi = bcdMinute(b);
  const uint8_t hh = bcdHour(b);
  const uint8_t dd = bcdDay(b);
  const uint8_t mo = bcdMonth(b);
  const uint8_t yy = bcdYear(b);

  const bool timeRanges = mi < 60 && hh < 24;
  const bool dateRanges = dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12;
  const bool tOk = structure && minOk && hourOk && timeRanges;
  const bool dOk = tOk && dateParityOk && dateRanges;

  lastFrameRecovered = recovered;
  recoveredBitsTotal += recovered;

  if (tOk) {
    timeValid++;
    lastFrameClockSaved = true;
    char t[8];
    snprintf(t, sizeof(t), "%02u:%02u", hh, mi);
    lastDecoded = t;

    // Il frame DCF descrive il minuto che inizia al marker corrente.
    syncHour = hh;
    syncMinute = mi;
    syncBaseMillis = millis();
    lastSyncMillis = syncBaseMillis;
    hasValidClock = true;

    lastCEST = seen[17] && b[17];
    lastCET  = seen[18] && b[18];
    dstChangeAnnounce = seen[16] && b[16];
    leapSecondAnnounce = seen[19] && b[19];
  }
  if (dOk) {
    dateValid++;
    lastFrameDateSaved = true;
    char d[16];
    snprintf(d, sizeof(d), "%02u/%02u/20%02u", dd, mo, yy);
    lastDecodedDate = d;
    lastWeekday = b[42] + 2*b[43] + 4*b[44];
  }

  if (tOk && dOk) {
    frameValid++;
    consecutiveFrameOk++;
    if (recovered > 0) frameSavedWithRecovery++;

    // Duty cycle radio: dopo il numero configurato di sync completi,
    // pianifica lo spegnimento. Viene eseguito nel loop, fuori dalla
    // finalizzazione del frame.
    if (radioDutyEnabled && !radioAutoSleeping &&
        consecutiveFrameOk >= radioSyncTarget) {
      radioSleepPending = true;
    }
  } else {
    frameInvalid++;
    consecutiveFrameOk = 0;
  }

  clearFrameBuffer();
}

void putFrameBitByRelativeSlot(int32_t rel, uint8_t bit) {
  if (!frameSynced || rel < 1 || rel > 59) return;
  const uint8_t idx = (uint8_t)(rel - 1);
  if (!frameSeen[idx]) {
    frameSeen[idx] = true;
    frameSeenCount++;
  }
  frameBits[idx] = bit;
  if (rel > frameLen) frameLen = (uint8_t)rel;
}

void resetClockRecovery(bool clearPhase = true) {
  pllLocked = false;
  frameSynced = false;
  acqStreak = 0;
  slotActive = false;
  activeSlot = 0;
  clusterStartUs = clusterEndUs = bestClusterUs = 0;
  slotFragments = 0;
  lastValidSlot = INT32_MIN;
  lastMarkerSlot = INT32_MIN;
  clearFrameBuffer();
  if (clearPhase) {
    memset(phaseHist, 0, sizeof(phaseHist));
    memset(phaseLastStart, 0, sizeof(phaseLastStart));
    phasePeakBin = 0;
    phasePeakScore = phaseSecondScore = 0;
    phaseOffsetUs = slotAnchorUs = 0;
    phaseDominance = 0.0f;
    phaseSearchStartedMs = millis();
    phaseLastEvalMs = millis();
  }
}

void evaluatePhaseLock() {
  uint16_t scores[PHASE_BINS];
  uint16_t best = 0, second = 0;
  uint8_t bestBin = 0;

  // Score circolare a 3 bin: il segnale vero puo' cadere sul confine di due bin.
  for (uint8_t i = 0; i < PHASE_BINS; i++) {
    const uint8_t prev = (uint8_t)((i + PHASE_BINS - 1) % PHASE_BINS);
    const uint8_t next = (uint8_t)((i + 1) % PHASE_BINS);
    scores[i] = phaseHist[prev] + phaseHist[i] + phaseHist[next];
    if (scores[i] > best) { best = scores[i]; bestBin = i; }
  }

  // Il secondo picco deve essere geograficamente separato dal primo.
  // I bin adiacenti condividono quasi gli stessi campioni e non sono concorrenti reali.
  for (uint8_t i = 0; i < PHASE_BINS; i++) {
    uint8_t d = (i > bestBin) ? (i - bestBin) : (bestBin - i);
    if (d > PHASE_BINS / 2) d = PHASE_BINS - d;
    if (d <= 2) continue;
    if (scores[i] > second) second = scores[i];
  }

  phasePeakBin = bestBin;
  phasePeakScore = best;
  phaseSecondScore = second;
  phaseDominance = (second > 0) ? ((float)best / (float)second) : (float)best;
  acqStreak = (best > 255) ? 255 : (uint8_t)best;

  // Seleziona come ancora il bin elementare piu' popolato nel gruppo vincente.
  const uint8_t prev = (uint8_t)((bestBin + PHASE_BINS - 1) % PHASE_BINS);
  const uint8_t next = (uint8_t)((bestBin + 1) % PHASE_BINS);
  uint8_t anchorBin = bestBin;
  if (phaseHist[prev] > phaseHist[anchorBin]) anchorBin = prev;
  if (phaseHist[next] > phaseHist[anchorBin]) anchorBin = next;

  // Un vero DCF genera un candidato quasi ogni secondo nella stessa fase.
  // Soglia volutamente moderata: il frame/parita' faranno la validazione finale.
  if (best >= 6 && (second == 0 || best >= (uint16_t)(second + 2))) {
    const uint32_t anchor = phaseLastStart[anchorBin];
    if (anchor) {
      pllLocked = true;
      slotAnchorUs = anchor;
      phaseOffsetUs = anchor % DCF_SECOND_US;
      slotActive = false;
      lastValidSlot = INT32_MIN;
      lastValidBitAtMs = millis();
    }
  }
}

void addPhaseCandidate(uint32_t startUs) {
  const uint32_t phase = startUs % DCF_SECOND_US;
  uint8_t bin = (uint8_t)(phase / PHASE_BIN_US);
  if (bin >= PHASE_BINS) bin = PHASE_BINS - 1;
  if (phaseHist[bin] < 65535) phaseHist[bin]++;
  phaseLastStart[bin] = startUs;

  const uint32_t nowMs = millis();
  if (nowMs - phaseLastEvalMs >= PHASE_EVAL_MS) {
    phaseLastEvalMs = nowMs;
    evaluatePhaseLock();
  }
  if (!pllLocked && nowMs - phaseSearchStartedMs >= PHASE_RESET_MS) {
    // Mantiene il picco piu' recente evitando che vecchio rumore domini per sempre.
    memset(phaseHist, 0, sizeof(phaseHist));
    memset(phaseLastStart, 0, sizeof(phaseLastStart));
    phaseSearchStartedMs = nowMs;
    phasePeakScore = phaseSecondScore = 0;
    phaseDominance = 0.0f;
  }
}

void handleDecodedBit(int32_t slotId, uint8_t bit) {
  filteredAccepted++;
  if (bit == 0) filteredBit0++; else filteredBit1++;
  lastValidBitAtMs = millis();

  if (lastValidSlot == INT32_MIN) {
    lastValidSlot = slotId;
    return;
  }

  const int32_t gapSlots = slotId - lastValidSlot;
  if (gapSlots <= 0) {
    rejectedPhase++;
    return;
  }

  // PRIMO AGGANCIO AL MINUTO: cerchiamo il classico buco di un secondo
  // (due slot tra due impulsi validi). Dopo questo punto NON dipendiamo piu'
  // dai gap per contare i 59 bit: usiamo direttamente la posizione dello slot.
  if (!frameSynced) {
    if (gapSlots == 2) {
      lastMarkerSlot = slotId - 1;
      minuteMarkers++;
      frameSynced = true;
      clearFrameBuffer();
      putFrameBitByRelativeSlot(1, bit); // bit 0 del nuovo minuto
    }
    lastValidSlot = slotId;
    return;
  }

  // Conta slot dati persi solo all'interno della finestra dati del minuto.
  if (gapSlots > 1) {
    for (int32_t s = lastValidSlot + 1; s < slotId; ++s) {
      int32_t relMiss = s - lastMarkerSlot;
      while (relMiss > 60) relMiss -= 60;
      if (relMiss >= 1 && relMiss <= 59) lostBitGaps++;
    }
  }

  // Dopo un marker noto, il prossimo marker DEVE cadere 60 slot dopo.
  // Questo evita il bug v1.7 in cui il frame poteva crescere a 152/59:
  // se il marker fisico viene coperto da rumore, lo ricostruiamo dal clock 1 Hz.
  while (slotId > lastMarkerSlot + 60) {
    const int32_t expectedMarker = lastMarkerSlot + 60;
    const bool observedGap = (gapSlots == 2 && (slotId - 1) == expectedMarker);

    finalizeFrame();
    lastMarkerSlot = expectedMarker;
    minuteMarkers++;
    if (!observedGap) inferredMarkers++;
    clearFrameBuffer();
  }

  const int32_t rel = slotId - lastMarkerSlot;

  if (rel >= 1 && rel <= 59) {
    putFrameBitByRelativeSlot(rel, bit);
  } else if (rel == 60) {
    // Il secondo 59 DCF77 deve essere privo di impulso. Se qui compare un
    // candidato 0/1, e' quasi certamente rumore: NON lo inseriamo nel frame.
    rejectedMarkers++;
  } else {
    // Fuori dalla geometria del minuto: preserva il PLL ma forza una nuova
    // ricerca del marker se la distanza e' chiaramente assurda.
    rejectedPhase++;
    if (rel > 65 || rel < 0) {
      frameSynced = false;
      lastMarkerSlot = INT32_MIN;
      clearFrameBuffer();
    }
  }

  lastValidSlot = slotId;
}

void finalizeActiveSlot() {
  if (!slotActive) return;
  uint32_t span = bestClusterUs;
  if (clusterStartUs && clusterEndUs) {
    const uint32_t currentSpan = clusterEndUs - clusterStartUs;
    if (currentSpan > span) span = currentSpan;
  }

  mergedSlots++;
  if (span >= BIT0_MIN_US && span <= BIT0_MAX_US) {
    handleDecodedBit(activeSlot, 0);
  } else if (span >= BIT1_MIN_US && span <= BIT1_MAX_US) {
    handleDecodedBit(activeSlot, 1);
  } else {
    invalidSlots++;
    rejectedWidth++;
  }

  slotActive = false;
  clusterStartUs = clusterEndUs = bestClusterUs = 0;
  slotFragments = 0;
}

void addPulseToSlot(int32_t slotId, uint32_t startUs, uint32_t endUs) {
  if (!slotActive || slotId != activeSlot) {
    if (slotActive) finalizeActiveSlot();
    slotActive = true;
    activeSlot = slotId;
    clusterStartUs = startUs;
    clusterEndUs = endUs;
    bestClusterUs = endUs - startUs;
    slotFragments = 1;
    return;
  }

  slotFragments++;
  const int32_t gap = (int32_t)(startUs - clusterEndUs);
  if (gap <= (int32_t)MERGE_GAP_US) {
    if ((int32_t)(endUs - clusterEndUs) > 0) clusterEndUs = endUs;
  } else {
    const uint32_t span = clusterEndUs - clusterStartUs;
    if (span > bestClusterUs) bestClusterUs = span;
    clusterStartUs = startUs;
    clusterEndUs = endUs;
  }
}

void processFilteredPulse(uint32_t endUs, uint32_t pulseUs) {
  if (pulseUs < GLITCH_MIN_US) { rejectedShort++; return; }
  const uint32_t startUs = endUs - pulseUs;

  if (!pllLocked) {
    if (pulseUs >= SEARCH_MIN_US && pulseUs <= SEARCH_MAX_US) addPhaseCandidate(startUs);
    else rejectedWidth++;
    return;
  }

  // Slot piu' vicino rispetto all'ancora di fase.
  const int32_t delta = (int32_t)(startUs - slotAnchorUs);
  int32_t slotId;
  if (delta >= 0) slotId = (delta + (int32_t)(DCF_SECOND_US / 2)) / (int32_t)DCF_SECOND_US;
  else slotId = -(((-delta) + (int32_t)(DCF_SECOND_US / 2)) / (int32_t)DCF_SECOND_US);
  const int32_t expectedStartDelta = slotId * (int32_t)DCF_SECOND_US;
  const int32_t phaseErr = delta - expectedStartDelta;

  if (absDiffUs(phaseErr) > PHASE_GATE_US) {
    rejectedPhase++;
    return;
  }

  // Anche frammenti brevi vengono accettati qui: possono essere pezzi dello stesso impulso DCF.
  if (pulseUs > 320000UL) {
    rejectedWidth++;
    return;
  }
  addPulseToSlot(slotId, startUs, endUs);
}

void tickClockRecovery() {
  if (!pllLocked) return;

  if (slotActive) {
    // Chiudi lo slot 350 ms dopo l'inizio del secondo successivo.
    const uint32_t slotStart = slotAnchorUs + (uint32_t)activeSlot * DCF_SECOND_US;
    const uint32_t deadline = slotStart + DCF_SECOND_US + 350000UL;
    if ((int32_t)(micros() - deadline) >= 0) finalizeActiveSlot();
  }

  // v2.4: non aspettiamo obbligatoriamente il primo impulso del minuto successivo
  // per salvare il frame. Una volta noto il marker, il clock 1 Hz determina anche
  // il marker successivo. Dopo 350 ms dello slot marker il bit 58 e' certamente
  // stato chiuso e possiamo finalizzare. Questo evita di perdere un frame buono
  // solo perche' il primo impulso del minuto seguente e' disturbato o assente.
  if (frameSynced && lastMarkerSlot != INT32_MIN) {
    const uint32_t nowUs = micros();
    const int32_t delta = (int32_t)(nowUs - slotAnchorUs);
    int32_t currentSlot;
    if (delta >= 0) currentSlot = delta / (int32_t)DCF_SECOND_US;
    else currentSlot = -(((-delta) + (int32_t)DCF_SECOND_US - 1) / (int32_t)DCF_SECOND_US);
    const uint32_t phaseUs = dcfPhaseNowUs();

    while (currentSlot >= lastMarkerSlot + 60 && phaseUs >= 350000UL) {
      finalizeFrame();
      lastMarkerSlot += 60;
      minuteMarkers++;
      inferredMarkers++;
      // finalizeFrame() ha gia' azzerato il buffer del nuovo minuto.
    }
  }

  // Se il lock non produce alcun bit utile per 15 s, torna in ricerca fase.
  if (lastValidBitAtMs && millis() - lastValidBitAtMs > 15000UL) {
    resetClockRecovery(true);
  }
}

String signalStateFiltered() {
  if (!receiverOn) return "RX OFF";
  if (!pllLocked) return "RICERCA FASE 1 Hz";
  if (!frameSynced) return "CLOCK LOCK - ATTESA MARKER";
  if (timeValid > 0) return "DCF77 ORA SINCRONIZZATA";
  return "CLOCK LOCK - FRAME IN ACQUISIZIONE";
}

float filterQualityPct() {
  const uint32_t rej = rejectedWidth + rejectedPhase;
  const uint32_t tot = filteredAccepted + rej;
  return tot ? 100.0f * filteredAccepted / tot : 0.0f;
}

String signalState() {
  const uint32_t valid = good0 + good1;
  if (!receiverOn) return "RX OFF";
  if (edgesLast5s == 0) return "NESSUN FRONTE";
  if (valid == 0 && noise > 0) return "SOLO RUMORE";
  if (valid > 0 && noise == 0) return "SEGNALE DCF PRESENTE";
  if (valid > noise) return "DCF PRESENTE / DISTURBATO";
  return "SEGNALE MOLTO DISTURBATO";
}

float validPct(uint32_t v, uint32_t n) {
  const uint32_t d = v + n;
  return d ? (100.0f * v / d) : 0.0f;
}

String currentClockString() {
  if (!hasValidClock) return "--:--:--";
  const uint32_t elapsedSec = (millis() - syncBaseMillis) / 1000UL;
  uint32_t total = ((uint32_t)syncHour * 3600UL + (uint32_t)syncMinute * 60UL + elapsedSec) % 86400UL;
  const uint8_t hh = total / 3600UL;
  const uint8_t mm = (total % 3600UL) / 60UL;
  const uint8_t ss = total % 60UL;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hh, mm, ss);
  return String(buf);
}

String syncState() {
  if (!hasValidClock) return "SEARCH";
  const uint32_t age = (millis() - lastSyncMillis) / 1000UL;
  if (pllLocked && age <= 90UL) return "SYNC";
  return "HOLDOVER";
}

String timezoneState() {
  if (lastCEST && !lastCET) return "CEST";
  if (lastCET && !lastCEST) return "CET";
  return "CET/CEST ?";
}

String weekdayName(uint8_t w) {
  static const char* names[] = {"-","Lun","Mar","Mer","Gio","Ven","Sab","Dom"};
  return (w <= 7) ? String(names[w]) : String("-");
}

String frameMapString() {
  String m; m.reserve(59);
  for (uint8_t i=0;i<59;i++) m += frameSeen[i] ? (frameBits[i] ? '1' : '0') : '?';
  return m;
}

void addRecentPulsesJson(String &s) {
  s += "[";
  for (uint8_t k=0;k<RECENT_PULSES;k++) {
    if (k) s += ',';
    const uint8_t idx = (uint8_t)((recentPulseHead + RECENT_PULSES - 1 - k) % RECENT_PULSES);
    s += String(recentPulseUs[idx] / 1000.0f, 1);
  }
  s += "]";
}

void addHistJson(String &s, const uint32_t *h) {
  s += "[";
  for (int i=0;i<9;i++) { if (i) s += ','; s += String(h[i]); }
  s += "]";
}

float abScore(uint32_t edges, uint32_t good, uint32_t noise) {
  // Preferisce meno fronti spurii ma premia la quota di impulsi 0/1 plausibili.
  // DCF pulito: ~120 fronti/minuto con CHANGE. Un eccesso di fronti penalizza.
  const float valid = (good + noise) ? (100.0f * good / (good + noise)) : 0.0f;
  const float excess = edges > 120 ? (float)(edges - 120) : 0.0f;
  return valid - excess * 0.03f;
}

void abSnapshotBase() {
  uint32_t e;
  noInterrupts(); e = totalEdges; interrupts();
  abBaseEdges = e; abBaseGood0 = good0; abBaseGood1 = good1; abBaseNoise = noise;
}

void abCapture(bool pullupStage) {
  uint32_t e;
  noInterrupts(); e = totalEdges; interrupts();
  const uint32_t de = e - abBaseEdges;
  const uint32_t d0 = good0 - abBaseGood0;
  const uint32_t d1 = good1 - abBaseGood1;
  const uint32_t dn = noise - abBaseNoise;
  if (pullupStage) {
    abPullEdges = de; abPullGood0 = d0; abPullGood1 = d1; abPullNoise = dn;
    abPullValidPct = validPct(d0+d1, dn);
  } else {
    abInputEdges = de; abInputGood0 = d0; abInputGood1 = d1; abInputNoise = dn;
    abInputValidPct = validPct(d0+d1, dn);
  }
}

void startAbTest() {
  if (abRunning || quietRunning || quietPending) return;
  abRunning = true; abReady = false; abStage = AB_INPUT; abWinner = "-";
  abInputEdges=abInputGood0=abInputGood1=abInputNoise=0;
  abPullEdges=abPullGood0=abPullGood1=abPullNoise=0;
  abInputValidPct=abPullValidPct=0.0f;
  setDataBias(false);
  resetClockRecovery(true);
  abSnapshotBase();
  abStageStartMs = millis();
  Serial.println(F("[AUTO A/B] START: INPUT 60 s"));
}

void tickAbTest() {
  if (!abRunning) return;
  if ((uint32_t)(millis() - abStageStartMs) < AB_STAGE_MS) return;
  if (abStage == AB_INPUT) {
    abCapture(false);
    setDataBias(true);
    resetClockRecovery(true);
    abSnapshotBase();
    abStage = AB_PULLUP;
    abStageStartMs = millis();
    Serial.println(F("[AUTO A/B] INPUT completato; INPUT_PULLUP 60 s"));
    return;
  }
  if (abStage == AB_PULLUP) {
    abCapture(true);
    const float sIn = abScore(abInputEdges, abInputGood0+abInputGood1, abInputNoise);
    const float sPu = abScore(abPullEdges, abPullGood0+abPullGood1, abPullNoise);
    const bool choosePull = sPu > sIn;
    setDataBias(choosePull);
    resetClockRecovery(true);
    abWinner = choosePull ? "INPUT_PULLUP" : "INPUT";
    abStage = AB_DONE; abRunning = false; abReady = true;
    Serial.printf("[AUTO A/B] FINE: INPUT edges=%lu valid=%.1f%% | PULLUP edges=%lu valid=%.1f%% | scelto=%s\n",
      (unsigned long)abInputEdges, abInputValidPct, (unsigned long)abPullEdges, abPullValidPct, abWinner.c_str());
  }
}

uint32_t abRemainSec() {
  if (!abRunning) return 0;
  const uint32_t elapsed = (uint32_t)(millis() - abStageStartMs);
  return elapsed >= AB_STAGE_MS ? 0 : (AB_STAGE_MS - elapsed) / 1000UL;
}

void handleAbStart() {
  if (abRunning || quietRunning || quietPending) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"test gia in corso\"}");
    return;
  }
  startAbTest();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true,\"seconds\":120,\"message\":\"INPUT 60 s + INPUT_PULLUP 60 s\"}");
}

void handleStatus() {
  uint32_t edges, drops, lastUs;
  noInterrupts();
  edges = totalEdges;
  drops = droppedEvents;
  lastUs = lastEdgeUs;
  interrupts();

  const uint32_t valid = good0 + good1;
  uint32_t lastEdgeAgeMs = edges ? (micros() - lastUs) / 1000UL : 0;
  uint32_t quietRemain = 0;
  if (quietRunning) {
    const uint32_t elapsed = (uint32_t)(millis() - quietStartMs);
    quietRemain = (elapsed >= QUIET_DURATION_MS) ? 0 : ((QUIET_DURATION_MS - elapsed) / 1000UL);
  }

  String s; s.reserve(2600);
  s += "{";
  s += "\"data\":\"" + String(digitalRead(DCF_DATA_PIN) ? "HIGH" : "LOW") + "\",";
  s += "\"pon\":\"" + String(digitalRead(DCF_PON_PIN) ? "HIGH" : "LOW") + "\",";
  s += "\"receiverOn\":" + String(receiverOn ? "true" : "false") + ",";
  s += "\"radioDutyEnabled\":" + String(radioDutyEnabled ? "true" : "false") + ",";
  s += "\"radioSyncTarget\":" + String((unsigned int)radioSyncTarget) + ",";
  s += "\"radioOffMinutes\":" + String((unsigned int)radioOffMinutes) + ",";
  s += "\"radioAutoSleeping\":" + String(radioAutoSleeping ? "true" : "false") + ",";
  s += "\"radioSleepRemainingSec\":" + String((unsigned long)radioSleepRemainingSec()) + ",";
  s += "\"radioSleepCount\":" + String((unsigned long)radioSleepCount) + ",";
  s += "\"radioWakeCount\":" + String((unsigned long)radioWakeCount) + ",";
  s += "\"bias\":\"" + String(usePullup ? "INPUT_PULLUP" : "INPUT") + "\",";
  s += "\"edges\":" + String(edges) + ",";
  s += "\"edgesLast5s\":" + String(edgesLast5s) + ",";
  s += "\"bit0\":" + String(good0) + ",";
  s += "\"bit1\":" + String(good1) + ",";
  s += "\"noise\":" + String(noise) + ",";
  s += "\"valid\":" + String(valid) + ",";
  s += "\"validPct\":" + String(validPct(valid, noise), 1) + ",";
  s += "\"dropped\":" + String(drops) + ",";
  s += "\"lastPulseMs\":" + String(lastPulseUs / 1000.0f, 1) + ",";
  s += "\"lastPulseAgeMs\":" + String(lastPulseAtMs ? millis() - lastPulseAtMs : 0) + ",";
  s += "\"lastEdgeAgeMs\":" + String(lastEdgeAgeMs) + ",";
  s += "\"hist\":"; addHistJson(s,hist); s += ',';
  s += "\"rssi\":" + String(WiFi.status()==WL_CONNECTED ? WiFi.RSSI() : -127) + ",";
  s += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  s += "\"state\":\"" + signalState() + "\",";
  s += "\"filteredState\":\"" + signalStateFiltered() + "\",";
  s += "\"pllLocked\":" + String(pllLocked?"true":"false") + ",";
  s += "\"frameSynced\":" + String(frameSynced?"true":"false") + ",";
  s += "\"acqStreak\":" + String(acqStreak) + ",";
  s += "\"phasePeak\":" + String(phasePeakScore) + ",";
  s += "\"phaseSecond\":" + String(phaseSecondScore) + ",";
  s += "\"phaseDominance\":" + String(phaseDominance,2) + ",";
  s += "\"phaseOffsetMs\":" + String(phaseOffsetUs/1000.0f,1) + ",";
  s += "\"mergedSlots\":" + String(mergedSlots) + ",";
  s += "\"invalidSlots\":" + String(invalidSlots) + ",";
  s += "\"filteredAccepted\":" + String(filteredAccepted) + ",";
  s += "\"filteredBit0\":" + String(filteredBit0) + ",";
  s += "\"filteredBit1\":" + String(filteredBit1) + ",";
  s += "\"rejectedShort\":" + String(rejectedShort) + ",";
  s += "\"rejectedWidth\":" + String(rejectedWidth) + ",";
  s += "\"rejectedPhase\":" + String(rejectedPhase) + ",";
  s += "\"minuteMarkers\":" + String(minuteMarkers) + ",";
  s += "\"inferredMarkers\":" + String(inferredMarkers) + ",";
  s += "\"rejectedMarkers\":" + String(rejectedMarkers) + ",";
  s += "\"lostBitGaps\":" + String(lostBitGaps) + ",";
  s += "\"frameCorrupt\":" + String(frameCorrupt?"true":"false") + ",";
  s += "\"frameLen\":" + String(frameLen) + ",";
  s += "\"frameSeen\":" + String(frameSeenCount) + ",";
  s += "\"frameValid\":" + String(frameValid) + ",";
  s += "\"frameInvalid\":" + String(frameInvalid) + ",";
  s += "\"timeValid\":" + String(timeValid) + ",";
  s += "\"dateValid\":" + String(dateValid) + ",";
  s += "\"recoveredBits\":" + String(recoveredBitsTotal) + ",";
  s += "\"lastFrameRecovered\":" + String(lastFrameRecovered) + ",";
  s += "\"lastFrameMissingRelevant\":" + String(lastFrameMissingRelevant) + ",";
  s += "\"lastFrameMissingAfterRecovery\":" + String(lastFrameMissingAfterRecovery) + ",";
  s += "\"lastFramePhysicalSeen\":" + String(lastFramePhysicalSeen) + ",";
  s += "\"lastFrameClockSaved\":" + String(lastFrameClockSaved?"true":"false") + ",";
  s += "\"lastFrameDateSaved\":" + String(lastFrameDateSaved?"true":"false") + ",";
  s += "\"frameSavedWithRecovery\":" + String(frameSavedWithRecovery) + ",";
  s += "\"filterQualityPct\":" + String(filterQualityPct(),1) + ",";
  s += "\"lastDecoded\":\"" + lastDecoded + "\",";
  s += "\"lastDecodedDate\":\"" + lastDecodedDate + "\",";
  s += "\"clock\":\"" + currentClockString() + "\",";
  s += "\"syncState\":\"" + syncState() + "\",";
  s += "\"syncAgeSec\":" + String(hasValidClock ? ((millis()-lastSyncMillis)/1000UL) : 0) + ",";
  s += "\"consecutiveOk\":" + String(consecutiveFrameOk) + ",";
  s += "\"timezone\":\"" + timezoneState() + "\",";
  s += "\"weekday\":\"" + weekdayName(lastWeekday) + "\",";
  s += "\"dstChangeAnnounce\":" + String(dstChangeAnnounce?"true":"false") + ",";
  s += "\"leapSecondAnnounce\":" + String(leapSecondAnnounce?"true":"false") + ",";
  s += "\"wifiSsid\":\"" + String(WiFi.status()==WL_CONNECTED ? WiFi.SSID() : "") + "\",";
  s += "\"wifiConnected\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") + ",";
  s += "\"configPortal\":" + String(configPortalActive?"true":"false") + ",";
  s += "\"displayPresent\":" + String(displayPresent?"true":"false") + ",";
  s += "\"displayEnabled\":" + String(displayEnabled?"true":"false") + ",";
  s += "\"displayDriver\":\"" + displayDriverName + "\",";
  s += "\"dcfProtected\":" + String(dcfProtectedWindow()?"true":"false") + ",";
  s += "\"dcfPhaseMs\":" + String(pllLocked ? dcfPhaseNowUs()/1000.0f : -1.0f,1) + ",";
  s += "\"deferredServiceLoops\":" + String(deferredServiceLoops) + ",";
  s += "\"oledUpdates\":" + String(oledUpdates) + ",";
  s += "\"displayI2cAck\":" + String(displayI2cAck ? "true" : "false") + ",";
  s += "\"displayI2cAddr\":" + String(displayI2cAddr) + ",";
  s += "\"displaySdaPin\":" + String((unsigned int)displaySdaPin) + ",";
  s += "\"displaySclPin\":" + String((unsigned int)displaySclPin) + ",";
  s += "\"displayPinMap\":\"" + displayPinMap + "\",";
  s += "\"oledRfQuietDuringRx\":" + String(oledRfQuietDuringRx ? "true" : "false") + ",";
  s += "\"oledRfQuietActive\":" + String(oledRfQuietActive ? "true" : "false") + ",";
  s += "\"uptimeSec\":" + String(millis()/1000UL) + ",";
  s += "\"frameMap\":\"" + frameMapString() + "\",";
  s += "\"recentPulses\":"; addRecentPulsesJson(s); s += ',';
  s += "\"verboseRaw\":" + String(verboseRaw?"true":"false") + ",";
  s += "\"abRunning\":" + String(abRunning?"true":"false") + ",";
  s += "\"abReady\":" + String(abReady?"true":"false") + ",";
  s += "\"abStage\":\"" + String(abStage==AB_INPUT?"INPUT":abStage==AB_PULLUP?"INPUT_PULLUP":abStage==AB_DONE?"DONE":"IDLE") + "\",";
  s += "\"abRemain\":" + String(abRemainSec()) + ",";
  s += "\"abInputEdges\":" + String(abInputEdges) + ",";
  s += "\"abInputBit0\":" + String(abInputGood0) + ",";
  s += "\"abInputBit1\":" + String(abInputGood1) + ",";
  s += "\"abInputNoise\":" + String(abInputNoise) + ",";
  s += "\"abInputValidPct\":" + String(abInputValidPct,1) + ",";
  s += "\"abPullEdges\":" + String(abPullEdges) + ",";
  s += "\"abPullBit0\":" + String(abPullGood0) + ",";
  s += "\"abPullBit1\":" + String(abPullGood1) + ",";
  s += "\"abPullNoise\":" + String(abPullNoise) + ",";
  s += "\"abPullValidPct\":" + String(abPullValidPct,1) + ",";
  s += "\"abWinner\":\"" + abWinner + "\",";
  s += "\"quietRunning\":" + String(quietRunning?"true":"false") + ",";
  s += "\"quietRemain\":" + String(quietRemain) + ",";
  s += "\"quietReady\":" + String(quietResultReady?"true":"false") + ",";
  s += "\"quietEdges\":" + String(quietEdges) + ",";
  s += "\"quietBit0\":" + String(quietGood0) + ",";
  s += "\"quietBit1\":" + String(quietGood1) + ",";
  s += "\"quietNoise\":" + String(quietNoise) + ",";
  s += "\"quietValidPct\":" + String(validPct(quietGood0+quietGood1, quietNoise),1) + ",";
  s += "\"quietDropped\":" + String(quietDropped) + ",";
  s += "\"quietHist\":"; addHistJson(s,quietHist);
  s += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void clearCounters() {
  noInterrupts();
  totalEdges = 0; droppedEvents = 0; qHead = qTail = 0;
  interrupts();
  good0 = good1 = noise = 0;
  memset(hist, 0, sizeof(hist));
  reportEdges = 0; edgesLast5s = 0; lastPulseUs = 0; lastPulseAtMs = 0;
  filteredAccepted = filteredBit0 = filteredBit1 = 0;
  rejectedShort = rejectedWidth = rejectedPhase = 0;
  minuteMarkers = rejectedMarkers = lostBitGaps = 0;
  frameValid = frameInvalid = 0; timeValid = dateValid = 0; recoveredBitsTotal = 0; lastFrameRecovered = 0; lastFrameMissingRelevant = 0; lastFrameMissingAfterRecovery = 0; lastFramePhysicalSeen = 0; lastFrameClockSaved = false; lastFrameDateSaved = false; frameSavedWithRecovery = 0; clearFrameBuffer();
  lastMarkerSlot = INT32_MIN; frameCorrupt = false;
  mergedSlots = invalidSlots = 0;
  lastDecoded = "--:--"; lastDecodedDate = "--/--/----";
  hasValidClock = false; syncHour = syncMinute = 0; syncBaseMillis = lastSyncMillis = 0;
  consecutiveFrameOk = 0; lastWeekday = 0; lastCEST = lastCET = false;
  dstChangeAnnounce = leapSecondAnnounce = false;
  memset(recentPulseUs, 0, sizeof(recentPulseUs)); recentPulseHead = 0;
  resetClockRecovery(true);
}

void handleAction() {
  const String action = server.arg("do");
  if (abRunning && (action == "input" || action == "pullup" || action == "clear" || action == "reset")) { server.send(409, "text/plain", "AUTO A/B in corso"); return; }
  if (action == "on") {
    radioSleepPending = false;
    if (radioAutoSleeping) wakeRadioAuto();
    else {
      if (oledRfQuietDuringRx) oledSetRfQuiet(true);
      setReceiver(true);
      setDataBias(usePullup);
    }
  }
  else if (action == "off") {
    radioSleepPending = false;
    radioAutoSleeping = false;
    detachInterrupt(digitalPinToInterrupt(DCF_DATA_PIN));
    setReceiver(false);
    delay(10);
    if (oledRfQuietDuringRx) oledSetRfQuiet(false);
  }
  else if (action == "reset") { setReceiver(false); delay(500); setReceiver(true); }
  else if (action == "clear") clearCounters();
  else if (action == "input") setDataBias(false);
  else if (action == "pullup") setDataBias(true);
  else if (action == "rawlog") verboseRaw = !verboseRaw;
  // OLED non e' piu' controllabile manualmente:
  // RADIO ON  -> OLED RF QUIET/OFF
  // RADIO OFF -> OLED ON in HOLDOVER

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "OK");
}

void handleQuietStart() {
  if (quietRunning || quietPending || abRunning) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"test gia in corso\"}");
    return;
  }
  quietPending = true;
  quietRequestMs = millis();
  quietResultReady = false;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true,\"seconds\":60,\"message\":\"Wi-Fi verra spento tra 800 ms\"}");
}

void startQuietTest() {
  quietPending = false;
  quietRunning = true;
  quietResultReady = false;
  memset(quietHist, 0, sizeof(quietHist));
  quietGood0 = quietGood1 = quietNoise = quietEdges = quietDropped = 0;
  noInterrupts();
  quietEdgesStart = totalEdges;
  quietDroppedStart = droppedEvents;
  qHead = qTail = 0;
  interrupts();
  quietStartMs = millis();

  Serial.println(F("[RF QUIET] START 60 s - Wi-Fi OFF, log edge sospeso"));
  MDNS.end();
  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
}


bool saveRadioConfig() {
  File f = LittleFS.open(RADIO_CFG_FILE, "w");
  if (!f) return false;
  f.println(radioDutyEnabled ? "1" : "0");
  f.println((unsigned int)radioSyncTarget);
  f.println((unsigned int)radioOffMinutes);
  f.close();
  return true;
}

bool loadRadioConfig() {
  File f = LittleFS.open(RADIO_CFG_FILE, "r");
  if (!f) return false;

  String a = f.readStringUntil('\n'); a.trim();
  String b = f.readStringUntil('\n'); b.trim();
  String c = f.readStringUntil('\n'); c.trim();
  f.close();

  radioDutyEnabled = (a == "1" || a == "true" || a == "ON");
  int s = b.toInt();
  int m = c.toInt();

  if (s < 1) s = 1;
  if (s > 10) s = 10;
  if (m < 1) m = 1;
  if (m > 1440) m = 1440;

  radioSyncTarget = (uint8_t)s;
  radioOffMinutes = (uint16_t)m;
  return true;
}

uint32_t radioSleepRemainingSec() {
  if (!radioAutoSleeping) return 0;
  const uint32_t elapsed = millis() - radioSleepStartedMs;
  if (elapsed >= radioSleepDurationMs) return 0;
  return (radioSleepDurationMs - elapsed + 999UL) / 1000UL;
}

void enterRadioAutoSleep() {
  if (!radioDutyEnabled || radioAutoSleeping || !receiverOn) return;

  radioSleepPending = false;
  radioAutoSleeping = true;
  radioSleepStartedMs = millis();
  radioSleepDurationMs = (uint32_t)radioOffMinutes * 60000UL;
  radioSleepCount++;

  // Nessun interrupt durante il periodo OFF: evitiamo fronti flottanti/spuri.
  detachInterrupt(digitalPinToInterrupt(DCF_DATA_PIN));
  noInterrupts();
  qHead = qTail = 0;
  pulseStartUs = 0;
  interrupts();

  setReceiver(false);
  delay(10);
  if (oledRfQuietDuringRx) {
    displayPage = 0;
    lastDisplayMs = 0;
    oledSetRfQuiet(false);
    drawDisplay();   // mostra subito ora/data appena la radio e' OFF
  }

  // Il clock DCF resta valido e continua localmente, ma PLL/frame vengono
  // azzerati per una nuova acquisizione pulita alla riaccensione.
  resetClockRecovery(true);
  consecutiveFrameOk = 0;

  Serial.printf("[RADIO TIMER] OFF dopo sync validi. Pausa=%u min, wake tra %lu s\n",
                (unsigned int)radioOffMinutes,
                (unsigned long)radioSleepRemainingSec());
}

void wakeRadioAuto() {
  if (!radioAutoSleeping) return;

  if (oledRfQuietDuringRx) {
    // Spegne il display PRIMA di riaccendere il ricevitore.
    oledSetRfQuiet(true);
    delay(15);
  }

  setReceiver(true);
  delay(20);

  noInterrupts();
  qHead = qTail = 0;
  pulseStartUs = 0;
  lastEdgeUs = micros();
  interrupts();

  pinMode(DCF_DATA_PIN, usePullup ? INPUT_PULLUP : INPUT);
  attachInterrupt(digitalPinToInterrupt(DCF_DATA_PIN), onEdge, CHANGE);

  resetClockRecovery(true);
  consecutiveFrameOk = 0;
  radioAutoSleeping = false;
  radioSleepStartedMs = 0;
  radioSleepDurationMs = 0;
  radioWakeCount++;

  Serial.println(F("[RADIO TIMER] ON - nuova acquisizione DCF77"));
}

void tickRadioDutyCycle() {
  if (radioAutoSleeping) {
    if ((uint32_t)(millis() - radioSleepStartedMs) >= radioSleepDurationMs) {
      wakeRadioAuto();
    }
    return;
  }

  if (radioSleepPending && radioDutyEnabled && receiverOn) {
    // La finalizzazione del frame e' conclusa: ora possiamo spegnere la radio.
    enterRadioAutoSleep();
  }
}

bool saveWiFiConfig(const String &ssid, const String &password, const String &hostname) {
  File f = LittleFS.open(WIFI_CFG_FILE, "w");
  if (!f) return false;
  f.println(ssid);
  f.println(password);
  f.println(hostname.length() ? hostname : "dcf77-clock");
  f.close();
  return true;
}

bool loadWiFiConfig() {
  wifiCfg = WiFiConfig();
  File f = LittleFS.open(WIFI_CFG_FILE, "r");
  if (f) {
    wifiCfg.ssid = f.readStringUntil('\n'); wifiCfg.ssid.trim();
    wifiCfg.password = f.readStringUntil('\n'); wifiCfg.password.trim();
    wifiCfg.hostname = f.readStringUntil('\n'); wifiCfg.hostname.trim();
    f.close();
    if (wifiCfg.ssid.length()) wifiCfg.fromFlash = true;
  }
  if (!wifiCfg.ssid.length()) {
    wifiCfg.ssid = WIFI_SSID;
    wifiCfg.password = WIFI_PASSWORD;
    wifiCfg.hostname = "dcf77-clock";
    wifiCfg.fromFlash = false;
  }
  if (!wifiCfg.hostname.length()) wifiCfg.hostname = "dcf77-clock";
  return wifiCfg.ssid.length() > 0;
}

void stopConfigPortal() {
  dnsServer.stop();
  if (configPortalActive) WiFi.softAPdisconnect(true);
  configPortalActive = false;
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP_STA);
  setupApSsid = "DCF77-Setup-" + String(ESP.getChipId(), HEX);
  setupApSsid.toUpperCase();
  WiFi.softAP(setupApSsid.c_str(), SETUP_AP_PASSWORD);
  delay(100);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  configPortalActive = true;
  Serial.printf("Config AP: %s  IP=%s  password=%s\n", setupApSsid.c_str(), WiFi.softAPIP().toString().c_str(), SETUP_AP_PASSWORD);
}

void connectWiFi() {
  WiFi.forceSleepWake();
  delay(1);
  stopConfigPortal();
  loadWiFiConfig();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(wifiCfg.hostname);
  WiFi.begin(wifiCfg.ssid.c_str(), wifiCfg.password.c_str());
  Serial.printf("Wi-Fi: connessione a %s", wifiCfg.ssid.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250); Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi OK - IP: "); Serial.println(WiFi.localIP());
    if (MDNS.begin(wifiCfg.hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS: http://%s.local/\n", wifiCfg.hostname.c_str());
    }
  } else {
    Serial.println("Wi-Fi non connesso: avvio portale di configurazione.");
    startConfigPortal();
  }
}

String htmlEscape(const String &in) {
  String o; o.reserve(in.length()+12);
  for (size_t i=0;i<in.length();++i) {
    char c=in[i];
    if (c=='&') o += F("&amp;"); else if (c=='<') o += F("&lt;"); else if (c=='>') o += F("&gt;");
    else if (c=='\"') o += F("&quot;"); else if (c=='\'') o += F("&#39;"); else o += c;
  }
  return o;
}

void handleWifiScan() {
  int n = WiFi.scanNetworks(false, true);
  String out = "[";
  for (int i=0;i<n;i++) {
    if (i) out += ',';
    String ss=WiFi.SSID(i); ss.replace("\\","\\\\"); ss.replace("\"","\\\"");
    out += "{\"ssid\":\"" + ss + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"enc\":" + String(WiFi.encryptionType(i)==ENC_TYPE_NONE?"false":"true") + "}";
  }
  out += "]";
  WiFi.scanDelete();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleWifiSave() {
  if (!server.hasArg("ssid")) { server.send(400,"application/json","{\"ok\":false,\"message\":\"SSID mancante\"}"); return; }
  String ssid=server.arg("ssid"); String pass=server.arg("password"); String host=server.arg("hostname");
  ssid.trim(); host.trim();
  if (!ssid.length()) { server.send(400,"application/json","{\"ok\":false,\"message\":\"SSID vuoto\"}"); return; }
  if (!host.length()) host="dcf77-clock";
  host.replace(" ","-");
  if (!saveWiFiConfig(ssid,pass,host)) { server.send(500,"application/json","{\"ok\":false,\"message\":\"Errore scrittura flash\"}"); return; }
  server.send(200,"application/json","{\"ok\":true,\"message\":\"Configurazione salvata. Riavvio...\"}");
  restartPending=true; restartRequestedMs=millis();
}

void handleWifiReset() {
  LittleFS.remove(WIFI_CFG_FILE);
  server.send(200,"application/json","{\"ok\":true,\"message\":\"Configurazione cancellata. Riavvio...\"}");
  restartPending=true; restartRequestedMs=millis();
}


void handleRadioSave() {
  // POST o GET: i parametri sono volutamente semplici per compatibilita ESP8266.
  radioDutyEnabled = server.hasArg("enabled") && server.arg("enabled") != "0";

  int s = server.hasArg("syncs") ? server.arg("syncs").toInt() : radioSyncTarget;
  int m = server.hasArg("minutes") ? server.arg("minutes").toInt() : radioOffMinutes;
  if (s < 1) s = 1;
  if (s > 10) s = 10;
  if (m < 1) m = 1;
  if (m > 1440) m = 1440;

  radioSyncTarget = (uint8_t)s;
  radioOffMinutes = (uint16_t)m;

  if (!radioDutyEnabled && radioAutoSleeping) {
    wakeRadioAuto();
  }
  if (!radioDutyEnabled && receiverOn && oledRfQuietDuringRx) {
    oledSetRfQuiet(true);
  }
  radioSleepPending = false;
  saveRadioConfig();

  String r = F("{\"ok\":true,\"enabled\":");
  r += radioDutyEnabled ? "true" : "false";
  r += ",\"syncs\":" + String((unsigned int)radioSyncTarget);
  r += ",\"minutes\":" + String((unsigned int)radioOffMinutes) + "}";
  server.send(200, "application/json", r);
}


void handleWifiPage() {
  String page = F(R"HTML(<!doctype html><html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DCF77 Wi-Fi</title><style>
  body{margin:0;background:#09131d;color:#eef8ff;font-family:Segoe UI,Arial}.w{max-width:760px;margin:auto;padding:22px}.c{background:#10283a;border:1px solid #27465d;border-radius:16px;padding:18px}h1{margin:0 0 4px}.m{color:#8fb5ce;font-size:12px}.row{margin-top:14px}label{display:block;font-size:12px;color:#a7c8dc;margin-bottom:5px}input,select{width:100%;padding:11px;border-radius:8px;border:1px solid #345b72;background:#081923;color:#fff}.btn{margin-top:12px;border:1px solid #345b72;background:#173f56;color:#fff;border-radius:8px;padding:10px 14px;font-weight:700;cursor:pointer}.danger{background:#57202a}.net{padding:9px;border-bottom:1px solid #28465a;cursor:pointer}.net:hover{background:#17384c}.ok{color:#4ff0ad}.warn{color:#ffd166}a{color:#8fd4ff}</style></head><body><div class="w"><div class="c"><h1>Configurazione rete</h1><div class="m">DCF77 RC8000 · ricerca Wi-Fi e credenziali salvate in LittleFS</div><p><a href="/">← Torna alla console</a></p><div id="state" class="m"></div><button class="btn" onclick="scan()">Cerca reti Wi-Fi</button><div id="nets"></div><form id="f"><div class="row"><label>SSID</label><input id="ssid" name="ssid" required></div><div class="row"><label>Password</label><input id="password" name="password" type="password" autocomplete="new-password"></div><div class="row"><label>Hostname</label><input id="hostname" name="hostname" value="HOST"></div><button class="btn" type="submit">Salva e riavvia</button></form><button class="btn danger" onclick="resetCfg()">Cancella configurazione salvata</button><p class="m">Se la connessione fallisce, il dispositivo crea l'AP <b>APSSID</b> con password <b>dcf77setup</b>. Apri 192.168.4.1.</p><p class="m warn">La scansione Wi-Fi parte solo premendo Cerca reti ed e' servita nella finestra DCF sicura; puo' comunque ridurre temporaneamente la qualita'.</p></div></div><script>
async function scan(){document.getElementById('nets').innerHTML='Scansione...';let r=await fetch('/api/wifi/scan');let a=await r.json();document.getElementById('nets').innerHTML=a.map(n=>`<div class="net" onclick="document.getElementById('ssid').value=${JSON.stringify(n.ssid)}"><b>${n.ssid||'(nascosta)'}</b> · ${n.rssi} dBm ${n.enc?'🔒':''}</div>`).join('')||'Nessuna rete trovata'}
document.getElementById('f').onsubmit=async e=>{e.preventDefault();let b=new URLSearchParams(new FormData(e.target));let r=await fetch('/api/wifi/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});let j=await r.json();document.getElementById('state').textContent=j.message||''};async function resetCfg(){if(!confirm('Cancellare la configurazione Wi-Fi salvata?'))return;let r=await fetch('/api/wifi/reset',{method:'POST'});let j=await r.json();document.getElementById('state').textContent=j.message||''}</script></body></html>)HTML");
  page.replace("HOST", htmlEscape(wifiCfg.hostname)); page.replace("APSSID", htmlEscape(setupApSsid.length()?setupApSsid:"DCF77-Setup-XXXX"));
  server.send(200,"text/html",page);
}

uint8_t displayI2cAddr = 0;
bool displayI2cAck = false;
String displayDriverName = "NONE";

// HW-364A esiste in almeno due revisioni/batch con SDA/SCL invertiti.
// Proviamo automaticamente entrambe le coppie:
//   A: SDA=GPIO14(D5), SCL=GPIO12(D6)
//   B: SDA=GPIO12(D6), SCL=GPIO14(D5)
uint8_t displaySdaPin = OLED_SDA_PIN;
uint8_t displaySclPin = OLED_SCL_PIN;
String displayPinMap = "non rilevato";

bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void oledClear() {
  if (oledDriver == OLED_SSD1306) display.clearDisplay();
  else if (oledDriver == OLED_SH1106) displaySH1106.clearDisplay();
}
void oledSetTextColor() {
  if (oledDriver == OLED_SSD1306) display.setTextColor(SSD1306_WHITE);
  else if (oledDriver == OLED_SH1106) displaySH1106.setTextColor(SH110X_WHITE);
}
void oledSetTextSize(uint8_t s) {
  if (oledDriver == OLED_SSD1306) display.setTextSize(s);
  else if (oledDriver == OLED_SH1106) displaySH1106.setTextSize(s);
}
void oledSetCursor(int16_t x, int16_t y) {
  if (oledDriver == OLED_SSD1306) display.setCursor(x,y);
  else if (oledDriver == OLED_SH1106) displaySH1106.setCursor(x,y);
}
void oledPrint(const String &s) {
  if (oledDriver == OLED_SSD1306) display.print(s);
  else if (oledDriver == OLED_SH1106) displaySH1106.print(s);
}
void oledPrint(const __FlashStringHelper *s) {
  if (oledDriver == OLED_SSD1306) display.print(s);
  else if (oledDriver == OLED_SH1106) displaySH1106.print(s);
}
void oledPrint(int v) {
  if (oledDriver == OLED_SSD1306) display.print(v);
  else if (oledDriver == OLED_SH1106) displaySH1106.print(v);
}
void oledPrint(unsigned long v) {
  if (oledDriver == OLED_SSD1306) display.print(v);
  else if (oledDriver == OLED_SH1106) displaySH1106.print(v);
}
void oledPrint(float v, int digits=0) {
  if (oledDriver == OLED_SSD1306) display.print(v,digits);
  else if (oledDriver == OLED_SH1106) displaySH1106.print(v,digits);
}
void oledPrintln(const String &s) {
  if (oledDriver == OLED_SSD1306) display.println(s);
  else if (oledDriver == OLED_SH1106) displaySH1106.println(s);
}
void oledPrintln(const __FlashStringHelper *s) {
  if (oledDriver == OLED_SSD1306) display.println(s);
  else if (oledDriver == OLED_SH1106) displaySH1106.println(s);
}
void oledPrintln(int v) {
  if (oledDriver == OLED_SSD1306) display.println(v);
  else if (oledDriver == OLED_SH1106) displaySH1106.println(v);
}
void oledFlush() {
  if (oledDriver == OLED_SSD1306) display.display();
  else if (oledDriver == OLED_SH1106) displaySH1106.display();
}
void oledPower(bool on) {
  if (oledDriver == OLED_SSD1306) display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  else if (oledDriver == OLED_SH1106) displaySH1106.oled_command(on ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
}

void oledSetRfQuiet(bool quiet) {
  if (!displayPresent || oledDriver == OLED_NONE) {
    oledRfQuietActive = quiet;
    return;
  }

  if (quiet) {
    // Silenzia il pannello prima della ricerca DCF77.
    if (oledDriver == OLED_SSD1306) {
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      display.ssd1306_command(SSD1306_CHARGEPUMP);
      display.ssd1306_command(0x10); // charge pump OFF
    } else if (oledDriver == OLED_SH1106) {
      displaySH1106.oled_command(SH110X_DISPLAYOFF);
    }
    oledRfQuietActive = true;
  } else {
    if (!displayEnabled) {
      oledRfQuietActive = false;
      oledPower(false);
      return;
    }

    if (oledDriver == OLED_SSD1306) {
      display.ssd1306_command(SSD1306_CHARGEPUMP);
      display.ssd1306_command(0x14); // charge pump ON
      display.ssd1306_command(SSD1306_DISPLAYON);
    } else if (oledDriver == OLED_SH1106) {
      displaySH1106.oled_command(SH110X_DISPLAYON);
    }
    oledRfQuietActive = false;
    lastDisplayMs = 0;
  }
}

bool initOledDriver(OledDriver drv) {
  if (!displayI2cAddr) return false;
  Wire.begin(displaySdaPin, displaySclPin);
  Wire.setClock(100000);
  delay(10);

  if (drv == OLED_SSD1306) {
    bool ok = display.begin(SSD1306_SWITCHCAPVCC, displayI2cAddr, true, false);
    if (!ok) return false;
    oledDriver = OLED_SSD1306;
    displayDriverName = "SSD1306";
  } else if (drv == OLED_SH1106) {
    bool ok = displaySH1106.begin(displayI2cAddr, true);
    // La libreria SH110X puo' richiamare Wire.begin(): ripristiniamo i pin custom.
    Wire.begin(displaySdaPin, displaySclPin);
    Wire.setClock(100000);
    if (!ok) return false;
    oledDriver = OLED_SH1106;
    displayDriverName = "SH1106";
  } else return false;

  displayPresent = true;
  displayEnabled = true;

  // Nessuna schermata di boot: il display e' controllato unicamente
  // dal ciclo RADIO ON/OFF. Viene mantenuto nero finche' la radio e' attiva.
  oledClear();
  oledFlush();
  return true;
}

void initDisplay() {
#if OLED_ENABLED
  displayPresent = false;
  displayI2cAck = false;
  displayI2cAddr = 0;
  displayDriverName = "NONE";
  displayPinMap = "non rilevato";

  struct OledPins { uint8_t sda; uint8_t scl; const char *label; };
  const OledPins candidates[] = {
    {14, 12, "SDA GPIO14 - SCL GPIO12"},
    {12, 14, "SDA GPIO12 - SCL GPIO14"}
  };

  const uint8_t addresses[] = { OLED_I2C_ADDR, 0x3C, 0x3D };

  // Alcuni lotti HW-364A hanno SDA/SCL invertiti rispetto ad altri.
  // Facciamo un vero probe I2C su entrambe le revisioni senza coinvolgere
  // D1/GPIO5, che nel nostro progetto resta PON del ricevitore DCF77.
  for (uint8_t p = 0; p < 2 && !displayI2cAck; p++) {
    displaySdaPin = candidates[p].sda;
    displaySclPin = candidates[p].scl;

    Wire.begin(displaySdaPin, displaySclPin);
    Wire.setClock(100000);
    delay(25);

    for (uint8_t a = 0; a < 3; a++) {
      // Evita probe duplicati quando OLED_I2C_ADDR == 0x3C.
      bool duplicate = false;
      for (uint8_t k = 0; k < a; k++) {
        if (addresses[k] == addresses[a]) duplicate = true;
      }
      if (duplicate) continue;

      if (i2cProbe(addresses[a])) {
        displayI2cAddr = addresses[a];
        displayI2cAck = true;
        displayPinMap = candidates[p].label;
        break;
      }
    }
  }

  if (!displayI2cAck) {
    displayPresent = false;
    displayDriverName = "NO ACK";
    Serial.println(F("OLED: NON RILEVATO."));
    Serial.println(F("  Provato HW-364A rev.A: SDA GPIO14, SCL GPIO12"));
    Serial.println(F("  Provato HW-364A rev.B: SDA GPIO12, SCL GPIO14"));
    Serial.println(F("  Indirizzi: 0x3C e 0x3D"));
    Serial.println(F("  D1/GPIO5 NON viene usato: resta dedicato a PON DCF77."));
    return;
  }

  Serial.printf("OLED: ACK 0x%02X con %s\n", displayI2cAddr, displayPinMap.c_str());

  // HW-364A normalmente monta SSD1306. Manteniamo SH1106 come fallback.
  if (initOledDriver(OLED_SSD1306)) {
    Serial.printf("OLED SSD1306 OK: 0x%02X, %s\n",
                  displayI2cAddr, displayPinMap.c_str());
  } else if (initOledDriver(OLED_SH1106)) {
    Serial.printf("OLED fallback SH1106 OK: 0x%02X, %s\n",
                  displayI2cAddr, displayPinMap.c_str());
  } else {
    displayPresent = false;
    oledDriver = OLED_NONE;
    displayDriverName = "ACK / INIT KO";
    Serial.printf("OLED: ACK a 0x%02X ma init driver fallita (%s).\n",
                  displayI2cAddr, displayPinMap.c_str());
  }
#endif
}

void drawDisplay() {
  if (!displayPresent || !displayEnabled || oledDriver == OLED_NONE) return;
  if (oledRfQuietDuringRx && receiverOn) return;
  if (oledRfQuietActive) return;
  if (millis() - lastDisplayMs < 1000UL) return;

  lastDisplayMs = millis();
  oledUpdates++;
  oledClear(); oledSetTextColor();

  if (displayPage == 0) {
    oledSetTextSize(1); oledSetCursor(0,0); oledPrint(F("DCF77 ")); oledPrint(syncState());
    oledSetTextSize(2); oledSetCursor(0,15); oledPrint(currentClockString());
    oledSetTextSize(1); oledSetCursor(0,37); oledPrint(lastDecodedDate); oledPrint(" "); oledPrint(timezoneState());
    oledSetCursor(0,50); oledPrint(F("Q ")); oledPrint(filterQualityPct(),0); oledPrint(F("% Frame ")); oledPrint((unsigned long)frameValid); oledPrint("/"); oledPrint((unsigned long)frameInvalid);
  } else {
    oledSetTextSize(1); oledSetCursor(0,0);
    if (radioAutoSleeping) {
      oledPrint(F("Radio OFF "));
      oledPrint((unsigned long)radioSleepRemainingSec());
      oledPrintln(F("s"));
    } else {
      oledPrint(F("Clock ")); oledPrintln(pllLocked?F("LOCK"):F("SEARCH"));
    }
    oledPrint(F("DCF ")); oledPrint((unsigned long)good0); oledPrint("/"); oledPrintln((int)good1);
    oledPrint(F("Noise ")); oledPrintln((int)noise);
    oledPrint(F("WiFi ")); 
    if(WiFi.status()==WL_CONNECTED){oledPrint(WiFi.RSSI()); oledPrintln(F(" dBm"));} else oledPrintln(F("OFF/AP"));
    oledPrint(F("IP ")); oledPrintln(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString());
    oledPrint(F("Bias ")); oledPrintln(usePullup?F("PULLUP"):F("INPUT"));
  }
  oledFlush();
  if ((millis()/5000UL)%2 != displayPage) displayPage = (millis()/5000UL)%2;
}

void finishQuietTest() {
  noInterrupts();
  quietEdges = totalEdges - quietEdgesStart;
  quietDropped = droppedEvents - quietDroppedStart;
  interrupts();
  quietRunning = false;
  quietResultReady = true;

  Serial.println();
  Serial.println(F("[RF QUIET] FINE"));
  Serial.printf("edges=%lu bit0=%lu bit1=%lu noise=%lu valid=%.1f%% dropped=%lu\n",
    (unsigned long)quietEdges, (unsigned long)quietGood0, (unsigned long)quietGood1,
    (unsigned long)quietNoise, validPct(quietGood0+quietGood1, quietNoise), (unsigned long)quietDropped);
  connectWiFi();
}

void handleRoot() {
  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DCF77 · HW-364A Console v2.4</title>
<style>
:root{--bg:#0b141d;--panel:#102131;--panel2:#153044;--line:#27465d;--text:#f4f9fd;--muted:#8fb5ce;--ok:#4ff0ad;--warn:#ffd166;--bad:#fb7185;--cyan:#5ac8fa;--blue:#60a5fa;--pink:#f472b6;--shadow:0 14px 34px rgba(0,0,0,.24)}
*{box-sizing:border-box}body{margin:0;background:#09131d;color:var(--text);font-family:Inter,Segoe UI,Arial,sans-serif}.wrap{max-width:1180px;margin:auto;padding:22px}.shell{background:linear-gradient(180deg,#17364b,#102739);border:1px solid #1d4057;border-radius:19px;padding:20px;box-shadow:var(--shadow)}
.top{display:flex;justify-content:space-between;align-items:flex-start;gap:14px}.brand h1{margin:0;font-size:25px}.brand .sub{margin-top:5px;color:#8fd4ff;font-size:12px}.pills{display:flex;gap:7px;flex-wrap:wrap;justify-content:flex-end}.pill{padding:6px 10px;border-radius:999px;background:#0e2637;border:1px solid #23506a;font-size:11px;font-weight:700}.pill.ok{color:var(--ok)}.pill.warn{color:var(--warn)}
.modebar{display:flex;align-items:center;gap:12px;margin-top:14px;background:#0d2a3d;padding:10px 14px;border-radius:9px}.modebar b{font-size:11px}.modeTag{background:#21556b;color:#4ff0ad;padding:6px 20px;border-radius:999px;font-size:11px;font-weight:800}.viewbtn{border:0;border-radius:5px;padding:7px 14px;font-weight:700;cursor:pointer}.viewbtn.active{background:#eef5fa;color:#173148}.viewbtn:not(.active){background:#173b50;color:#a8c9dc}
.hero{display:grid;grid-template-columns:1.05fr 1.55fr;gap:14px;margin-top:14px}.card{background:#0d2c40;border:1px solid #163d54;border-radius:10px;padding:15px}.clock{font-size:42px;font-weight:850;letter-spacing:.02em;color:var(--ok);line-height:1}.date{margin-top:8px;font-size:13px}.micro{color:#9bc0d8;font-size:11px}.statusGrid{display:grid;grid-template-columns:1fr auto;gap:10px 18px;font-size:12px}.statusGrid .val{text-align:right;font-weight:800}.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.section{margin-top:14px;background:#0d2c40;border:1px solid #163d54;border-radius:10px;padding:15px}.section h2{font-size:14px;margin:0 0 6px}.quality{font-size:11px;color:#9bc0d8}.bar{height:7px;background:#153748;border-radius:99px;overflow:hidden;margin:10px 0}.bar i{display:block;height:100%;background:linear-gradient(90deg,#4ff0ad,#60a5fa);width:0;transition:width .25s}
.bits{display:grid;grid-template-columns:repeat(59,minmax(11px,1fr));gap:2px;overflow-x:auto;padding-bottom:4px}.bit{min-width:13px;height:39px;border:1px solid #44677c;border-radius:3px;display:flex;flex-direction:column;align-items:center;justify-content:center;font-size:9px;background:#183545;color:#9dc0d4}.bit .idx{font-size:7px;opacity:.7}.bit.seen{color:white}.bit.one{font-weight:900}.bit.cur{outline:2px solid white;outline-offset:1px}.service.seen{background:#1f5f58}.control.seen{background:#7b671b}.minute.seen{background:#18734d}.hour.seen{background:#17658c}.datebit.seen{background:#7a315f}.legend{display:flex;gap:14px;flex-wrap:wrap;margin-top:10px;font-size:10px}.legend span:before{content:'';display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:5px}.l1:before{background:#1f5f58}.l2:before{background:#7b671b}.l3:before{background:#18734d}.l4:before{background:#17658c}.l5:before{background:#7a315f}
.pulses{display:flex;gap:7px;flex-wrap:wrap}.pulse{background:#15384c;border:1px solid #2a536b;border-radius:8px;padding:7px 9px;font-size:11px}.pulse.good0{color:#8ef2c7}.pulse.good1{color:#8bc9ff}.pulse.noise{color:#ffcf83}
.adv{display:none}.adv.show{display:block}.grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:9px}.metric{background:#0e293b;border:1px solid #23465c;border-radius:9px;padding:11px;min-height:74px}.k{font-size:9px;text-transform:uppercase;letter-spacing:.06em;color:#8fb5ce}.v{font-size:20px;font-weight:800;margin-top:5px}.v.small{font-size:13px}.controls{display:flex;gap:8px;flex-wrap:wrap;margin-top:13px}.btn{border:1px solid #345b72;background:#16394d;color:#f5fbff;border-radius:7px;padding:8px 11px;font-weight:700;font-size:11px;text-decoration:none;cursor:pointer}.btn:hover{background:#1d4861}.btn.quiet{background:#5a3f00;border-color:#946a00}.hist{display:grid;grid-template-columns:repeat(9,1fr);gap:5px}.hb{background:#0f2a3b;border:1px solid #23465c;border-radius:7px;padding:7px;text-align:center}.hb b{display:block}.hb span{font-size:8px;color:#8fb5ce}.advancedJson{margin-top:10px}.advancedJson summary{cursor:pointer;color:#a7c8dc;font-size:11px}.advancedJson pre{white-space:pre-wrap;background:#06111a;border:1px solid #1d384b;border-radius:8px;padding:10px;color:#9db8c9;max-height:330px;overflow:auto}.foot{color:#789aae;font-size:10px;margin-top:12px}.hidden{display:none!important}
@media(max-width:850px){.hero{grid-template-columns:1fr}.grid{grid-template-columns:repeat(3,1fr)}.bits{grid-template-columns:repeat(59,16px)}}@media(max-width:560px){.wrap{padding:8px}.shell{padding:12px}.top{flex-direction:column}.pills{justify-content:flex-start}.grid{grid-template-columns:repeat(2,1fr)}.clock{font-size:36px}.hist{grid-template-columns:repeat(3,1fr)}}
</style></head><body><div class="wrap"><div class="shell">
<div class="top"><div class="brand"><h1>DCF77 · HW-364A</h1><div class="sub">RC8000 · ESP8266 · 77,5 kHz · radio-controlled clock</div></div><div class="pills"><span id="syncPill" class="pill warn">SEARCH</span><a class="pill" href="/wifi" style="text-decoration:none;color:#8fd4ff">Wi-Fi</a><span class="pill">v2.4</span></div></div>
<div class="modebar"><b>Modalità decoder</b><span class="modeTag">ACCUMULO</span><span class="micro">frame + deterministic erasure recovery</span><span style="flex:1"></span><button id="basicBtn" class="viewbtn active" onclick="setView(false)">Basic</button><button id="advBtn" class="viewbtn" onclick="setView(true)">Advanced</button></div>
<div class="hero">
 <div class="card"><div id="clock" class="clock">--:--:--</div><div class="date"><span id="weekday">-</span> <span id="dd">--/--/----</span> · <span id="tz">-</span> · <b id="syncLabel">SEARCH</b></div><div class="micro" style="margin-top:7px">Secondi locali riallineati al marker DCF77 · ultimo sync <span id="syncAge">-</span></div></div>
 <div class="card"><div class="statusGrid"><div>Stato minuto</div><div id="fstate" class="val">...</div><div>Qualità filtro</div><div id="fq" class="val">...</div><div>Posizione frame</div><div id="framePos" class="val">...</div><div>Frame consecutivi OK</div><div id="consec" class="val">0</div><div>Clock 1 Hz</div><div id="pll" class="val">...</div></div><div class="bar"><i id="qbar"></i></div></div>
</div>
<div class="section"><h2>Accumulo e decodifica bit</h2><div class="quality">Candidati coerenti: <b id="fa">0</b> · confidence filtro: <b id="conf">0%</b> · bit incerti/persi: <b id="lost">0</b> · recuperati totali: <b id="recMini">0</b></div><div class="quality" style="margin-top:5px">Ultimo frame: fisici <b id="phys">0</b>/59 · mancanti prima/dopo recovery <b id="missrec">0 / 0</b> · recuperati <b id="lastrec">0</b> · clock <b id="clksave">-</b> · data <b id="datesave">-</b></div><div class="bar"><i id="bitbar"></i></div><div id="bits" class="bits"></div><div class="legend"><span class="l1">0-14 servizio</span><span class="l2">15-20 controllo</span><span class="l3">21-28 minuti</span><span class="l4">29-35 ore</span><span class="l5">36-58 data</span></div></div>
<div class="section"><h2>Impulsi recenti</h2><div id="pulses" class="pulses"></div><div class="micro" style="margin-top:9px">Ultimo: <span id="pulse">-</span> · fronti 5 s: <span id="e5">-</span> · RAW validi: <span id="vp">-</span></div></div>
<div id="advanced" class="adv">
 <div class="section"><h2>Diagnostica avanzata</h2><div class="grid">
  <div class="metric"><div class="k">Stato segnale</div><div id="state" class="v small">...</div></div><div class="metric"><div class="k">DATA / bias</div><div class="v small"><span id="data">-</span> · <span id="bias">-</span></div></div><div class="metric"><div class="k">Fronti totali</div><div id="edges" class="v">0</div></div><div class="metric"><div class="k">BIT 0 / BIT 1</div><div id="rawbits" class="v">0 / 0</div></div><div class="metric"><div class="k">Noise</div><div id="noise" class="v">0</div></div>
  <div class="metric"><div class="k">RSSI / IP</div><div class="v small"><span id="rssi">-</span><br><span id="ip">-</span></div></div><div class="metric"><div class="k">Fase dominante</div><div id="po" class="v small">-</div></div><div class="metric"><div class="k">Slot / invalidi</div><div id="ms" class="v small">-</div></div><div class="metric"><div class="k">Marker / ricostruiti</div><div id="mk" class="v small">-</div></div><div class="metric"><div class="k">Frame OK / KO</div><div id="fr" class="v small">-</div></div>
  <div class="metric"><div class="k">Time / Date OK</div><div id="tdok" class="v small">-</div></div><div class="metric"><div class="k">Recupero parità</div><div id="rec" class="v small">-</div></div><div class="metric"><div class="k">Annuncio DST</div><div id="dst" class="v small">-</div></div><div class="metric"><div class="k">Leap second</div><div id="leap" class="v small">-</div></div><div class="metric"><div class="k">Uptime</div><div id="uptime" class="v small">-</div></div><div class="metric"><div class="k">Wi-Fi</div><div id="wifiState" class="v small">-</div></div><div class="metric"><div class="k">Display OLED</div><div id="dispState" class="v small">-</div></div><div class="metric"><div class="k">DCF Priority</div><div id="prioState" class="v small">-</div></div>
 </div>
 <div class="controls"><a class="btn" href="/action?do=on">RX ON</a><a class="btn" href="/action?do=off">RX OFF</a><a class="btn" href="/action?do=reset">Reset PON</a><a class="btn" href="/action?do=clear">Azzera contatori</a><a class="btn" href="/action?do=input">DATA INPUT</a><a class="btn" href="/action?do=pullup">DATA PULLUP</a><a class="btn" href="/action?do=rawlog">RAW LOG</a><span class="muted">OLED gestito automaticamente dal timer: RADIO ON = display spento · RADIO OFF = display acceso</span></div>
<div class="card" style="margin-top:14px">
  <h3>Timer radio DCF77</h3>
  <div class="muted">Dopo N sincronizzazioni valide spegne il ricevitore via PON, mantiene l'ora in HOLDOVER e lo riattiva automaticamente.</div>
  <div style="display:flex;gap:12px;flex-wrap:wrap;align-items:end;margin-top:12px">
    <label><input id="rEnable" type="checkbox"> Automatico</label>
    <label>Sync prima OFF<br><input id="rSyncs" type="number" min="1" max="10" value="2" style="width:90px"></label>
    <label>OFF per minuti<br><input id="rMinutes" type="number" min="1" max="1440" value="60" style="width:110px"></label>
    <button class="btn" onclick="saveRadio()">SALVA TIMER</button>
  </div>
  <div id="rState" class="muted" style="margin-top:10px">-</div>
</div><div><a class="btn" href="/wifi">CONFIGURA WI-FI</a><button class="btn quiet" onclick="quiet()">RF QUIET 60 s</button><button class="btn" onclick="abtest()">AUTO A/B 60+60 s</button></div>
 <h2>Istogramma impulsi</h2><div id="hist" class="hist"></div>
 <h2>Test automatici</h2><div class="grid"><div class="metric"><div class="k">Stato A/B</div><div id="abs" class="v small">Mai eseguito</div></div><div class="metric"><div class="k">INPUT</div><div id="abi" class="v small">-</div></div><div class="metric"><div class="k">PULLUP</div><div id="abp" class="v small">-</div></div><div class="metric"><div class="k">Scelta</div><div id="abw" class="v small">-</div></div><div class="metric"><div class="k">RF QUIET</div><div id="qs" class="v small">Mai eseguito</div></div></div><div id="qhist" class="hist" style="margin-top:7px"></div>
 <details class="advancedJson"><summary>JSON diagnostico</summary><pre id="raw">Caricamento...</pre></details>
 </div>
</div><div class="foot">DCF77: minuti, ore, data, giorno settimana, CET/CEST, annuncio cambio ora e leap second. I secondi sono derivati dal clock locale ESP8266 e riallineati ad ogni sincronizzazione valida.</div>
</div></div><script>
const labels=['<10','10-30','30-60','60-80','80-140','140-170','170-240','240-400','>400'];
function setView(a){document.getElementById('advanced').classList.toggle('show',a);document.getElementById('advBtn').classList.toggle('active',a);document.getElementById('basicBtn').classList.toggle('active',!a);localStorage.setItem('dcfAdv',a?'1':'0')}
function drawHist(id,a){document.getElementById(id).innerHTML=a.map((v,i)=>`<div class="hb"><b>${v}</b><span>${labels[i]} ms</span></div>`).join('')}
function bitClass(i){if(i<=14)return'service';if(i<=20)return'control';if(i<=28)return'minute';if(i<=35)return'hour';return'datebit'}
function drawBits(map,pos){let h='';for(let i=0;i<59;i++){const c=map&&map[i]?map[i]:'?';h+=`<div class="bit ${bitClass(i)} ${c!='?'?'seen':''} ${c=='1'?'one':''} ${i==pos?'cur':''}"><span>${c}</span><span class="idx">${i}</span></div>`}document.getElementById('bits').innerHTML=h}
function pulseClass(ms){if(ms>=80&&ms<=140)return'good0';if(ms>=170&&ms<=240)return'good1';return'noise'}
function drawPulses(a){document.getElementById('pulses').innerHTML=a.map(v=>`<span class="pulse ${pulseClass(v)}">${v.toFixed(1)} ms</span>`).join('')}
function ageText(s){if(s===0)return'adesso';if(s<60)return s+' s fa';if(s<3600)return Math.floor(s/60)+' min fa';return Math.floor(s/3600)+' h fa'}
function uptimeText(s){let d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60);return (d?d+'g ':'')+h+'h '+m+'m'}
function syncClass(x){return x==='SYNC'?'ok':x==='HOLDOVER'?'warn':'warn'}
async function upd(){try{const r=await fetch('/api/status',{cache:'no-store'});const j=await r.json();
 let re=document.getElementById('rEnable');if(re)re.checked=!!j.radioDutyEnabled;
 let rs=document.getElementById('rSyncs');if(rs&&document.activeElement!==rs)rs.value=j.radioSyncTarget||2;
 let rm=document.getElementById('rMinutes');if(rm&&document.activeElement!==rm)rm.value=j.radioOffMinutes||60;
 let rst=document.getElementById('rState');if(rst){
   if(j.radioAutoSleeping){
     let sec=j.radioSleepRemainingSec||0,mm=Math.floor(sec/60),ss=sec%60;
     rst.textContent='RADIO OFF · OLED ON · riattivazione ricerca tra '+mm+'m '+ss+'s · HOLDOVER attivo';
   }else{
     rst.textContent='RADIO ON · OLED OFF · sync consecutivi '+(j.consecutiveOk||0)+'/'+(j.radioSyncTarget||2)+' · pausa '+(j.radioOffMinutes||60)+' min';
   }
 }
 document.getElementById('clock').textContent=j.clock;document.getElementById('dd').textContent=j.lastDecodedDate;document.getElementById('weekday').textContent=j.weekday;document.getElementById('tz').textContent=j.timezone;document.getElementById('syncLabel').textContent=j.syncState;document.getElementById('syncLabel').className=syncClass(j.syncState);let sp=document.getElementById('syncPill');sp.textContent=j.syncState;sp.className='pill '+syncClass(j.syncState);document.getElementById('syncAge').textContent=j.syncState==='SEARCH'?'-':ageText(j.syncAgeSec);document.getElementById('consec').textContent=j.consecutiveOk;
 let fs=document.getElementById('fstate');fs.textContent=j.filteredState;fs.className='val '+(j.pllLocked?'ok':'warn');document.getElementById('fq').textContent=j.filterQualityPct.toFixed(1)+'%';document.getElementById('qbar').style.width=Math.max(0,Math.min(100,j.filterQualityPct))+'%';document.getElementById('pll').textContent=j.pllLocked?'LOCK':'SEARCH';document.getElementById('framePos').textContent=j.frameSeen+' visti · '+j.frameLen+'/59';document.getElementById('fa').textContent=j.filteredAccepted;document.getElementById('conf').textContent=j.filterQualityPct.toFixed(1)+'%';document.getElementById('lost').textContent=j.lostBitGaps;document.getElementById('recMini').textContent=j.recoveredBits;document.getElementById('phys').textContent=j.lastFramePhysicalSeen;document.getElementById('missrec').textContent=j.lastFrameMissingRelevant+' / '+j.lastFrameMissingAfterRecovery;document.getElementById('lastrec').textContent=j.lastFrameRecovered;document.getElementById('clksave').textContent=j.lastFrameClockSaved?'SALVATO':'-';document.getElementById('datesave').textContent=j.lastFrameDateSaved?'SALVATA':'-';document.getElementById('bitbar').style.width=Math.min(100,(j.frameSeen/59)*100)+'%';drawBits(j.frameMap,Math.max(0,j.frameLen-1));drawPulses(j.recentPulses);document.getElementById('pulse').textContent=j.lastPulseMs.toFixed(1)+' ms';document.getElementById('e5').textContent=j.edgesLast5s;document.getElementById('vp').textContent=j.validPct.toFixed(1)+'%';
 document.getElementById('state').textContent=j.state;document.getElementById('data').textContent=j.data;document.getElementById('bias').textContent=j.bias;document.getElementById('edges').textContent=j.edges;document.getElementById('rawbits').textContent=j.bit0+' / '+j.bit1;document.getElementById('noise').textContent=j.noise;document.getElementById('rssi').textContent=j.rssi+' dBm';document.getElementById('ip').textContent=j.ip;document.getElementById('po').textContent=j.phaseOffsetMs.toFixed(1)+' ms';document.getElementById('ms').textContent=j.mergedSlots+' / '+j.invalidSlots;document.getElementById('mk').textContent=j.minuteMarkers+' / '+j.inferredMarkers;document.getElementById('fr').textContent=j.frameValid+' / '+j.frameInvalid;document.getElementById('tdok').textContent=j.timeValid+' / '+j.dateValid;document.getElementById('rec').textContent=j.recoveredBits+' tot · ultimo '+j.lastFrameRecovered;document.getElementById('dst').textContent=j.dstChangeAnnounce?'ANNUNCIATO':'no';document.getElementById('leap').textContent=j.leapSecondAnnounce?'ANNUNCIATO':'no';document.getElementById('uptime').textContent=uptimeText(j.uptimeSec);let ws=document.getElementById('wifiState');if(ws)ws.textContent=j.wifiConnected?(j.wifiSsid+' · '+j.rssi+' dBm'):(j.configPortal?'SETUP AP':'OFF');let ds=document.getElementById('dispState');if(ds)ds.textContent=j.displayPresent?((j.radioAutoSleeping?'ON · HOLDOVER':'OFF · RICERCA DCF77')+' · '+(j.displayDriver||'?')+' · 0x'+Number(j.displayI2cAddr||0).toString(16).toUpperCase()+' · '+(j.displayPinMap||'')):(j.displayI2cAck?'ACK / INIT KO':'NON RILEVATO · provate entrambe le revisioni D5/D6');let ps=document.getElementById('prioState');if(ps)ps.textContent=j.pllLocked?(j.dcfProtected?'PROTECTED '+j.dcfPhaseMs.toFixed(0)+' ms':'SERVICE '+j.dcfPhaseMs.toFixed(0)+' ms'):'SEARCH';drawHist('hist',j.hist);
 if(j.abRunning)document.getElementById('abs').textContent=j.abStage+' · '+j.abRemain+' s';else if(j.abReady)document.getElementById('abs').textContent='COMPLETATO';document.getElementById('abi').textContent=j.abInputEdges+' / '+j.abInputValidPct.toFixed(1)+'%';document.getElementById('abp').textContent=j.abPullEdges+' / '+j.abPullValidPct.toFixed(1)+'%';document.getElementById('abw').textContent=j.abWinner;if(j.quietRunning)document.getElementById('qs').textContent='IN CORSO · '+j.quietRemain+' s';else if(j.quietReady){document.getElementById('qs').textContent='COMPLETATO';drawHist('qhist',j.quietHist)}document.getElementById('raw').textContent=JSON.stringify(j,null,2);
 }catch(e){let q=document.getElementById('qs');if(q)q.textContent='Wi-Fi OFF / test in corso...'}}
async function abtest(){if(!confirm('AUTO A/B: 60 s INPUT + 60 s INPUT_PULLUP. Continuare?'))return;try{await fetch('/api/ab/start',{cache:'no-store'})}catch(e){}}
async function quiet(){if(!confirm('RF QUIET: Wi-Fi spento per circa 60 secondi. Continuare?'))return;try{await fetch('/api/quiet/start',{cache:'no-store'})}catch(e){}}

async function saveRadio(){
  const en=document.getElementById('rEnable').checked?1:0;
  const sy=document.getElementById('rSyncs').value||2;
  const mi=document.getElementById('rMinutes').value||60;
  await fetch('/api/radio/save?enabled='+en+'&syncs='+sy+'&minutes='+mi,{method:'POST',cache:'no-store'});
  upd();
}
setView(localStorage.getItem('dcfAdv')==='1');upd();setInterval(upd,2000);
</script></body></html>
)HTML";
  server.send_P(200, "text/html", PAGE);
}

void setup() {
  Serial.begin(115200); delay(300);
  if (!LittleFS.begin()) Serial.println(F("LittleFS mount fallito."));
  else loadRadioConfig();
  initDisplay();
  if (oledRfQuietDuringRx && displayPresent) oledSetRfQuiet(true);
  pinMode(DCF_PON_PIN, OUTPUT); setReceiver(true);
  pinMode(DCF_DATA_PIN, INPUT); // default volutamente senza pull-up
  delay(200);
  lastEdgeUs = micros();
  attachInterrupt(digitalPinToInterrupt(DCF_DATA_PIN), onEdge, CHANGE);
  resetClockRecovery(true);

  Serial.println();
  Serial.println(F("========================================================"));
  Serial.println(F(" DCF77 RC8000 CONSOLE v2.5.4 - TIMER-OWNED OLED + RADIO DUTY"));
  Serial.println(F("========================================================"));
  Serial.printf("DATA GPIO%d/D7 | PON GPIO%d/D1 | DATA bias=INPUT | PON active LOW\n", DCF_DATA_PIN, DCF_PON_PIN);
  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/reset", HTTP_POST, handleWifiReset);
  server.on("/api/quiet/start", HTTP_GET, handleQuietStart);
  server.on("/api/ab/start", HTTP_GET, handleAbStart);
  server.on("/api/radio/save", HTTP_POST, handleRadioSave);
  server.on("/action", HTTP_GET, handleAction);
  server.onNotFound([](){ if (configPortalActive) { server.sendHeader("Location", "/wifi", true); server.send(302, "text/plain", ""); } else server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.println(F("Web server attivo sulla porta 80."));
  Serial.println(F("DCF Priority: RADIO ON = OLED/I2C RF QUIET; RADIO OFF = OLED attivo in HOLDOVER."));
  reportStartMs = millis();
}

void loop() {
  // dopo aver risposto alla richiesta HTTP, spegniamo davvero il Wi-Fi
  if (quietPending && millis() - quietRequestMs >= 800) startQuietTest();

  if (!quietRunning) {
    // DCF PRIORITY: durante il lock non eseguiamo servizi web/I2C nella
    // finestra critica 0..350 ms. Le richieste restano in attesa per poche
    // centinaia di ms e vengono servite tra 420 e 900 ms.
    if (dcfServiceWindow()) {
      if (configPortalActive) dnsServer.processNextRequest();
      server.handleClient();
      if (WiFi.status() == WL_CONNECTED) MDNS.update();
    } else {
      deferredServiceLoops++;
    }
  }

  tickRadioDutyCycle();

  EdgeEvent e;
  while (!radioAutoSleeping && popEvent(e)) {
    if (e.pulseUs) {
      lastPulseUs = e.pulseUs;
      lastPulseAtMs = millis();
      recentPulseUs[recentPulseHead] = e.pulseUs;
      recentPulseHead = (uint8_t)((recentPulseHead + 1) % RECENT_PULSES);
      const char *c = classifyPulse(e.pulseUs, quietRunning);
      processFilteredPulse(e.tUs, e.pulseUs);
      if (!quietRunning && verboseRaw) {
        Serial.printf("EDGE %-4s dt=%8.1f ms pulse=%6.1f ms %s\n", e.level ? "HIGH" : "LOW", e.dtUs/1000.0f, e.pulseUs/1000.0f, c);
      }
    } else if (!quietRunning && verboseRaw) {
      Serial.printf("EDGE %-4s dt=%8.1f ms\n", e.level ? "HIGH" : "LOW", e.dtUs/1000.0f);
    }
    yield();
  }

  if (quietRunning && millis() - quietStartMs >= QUIET_DURATION_MS) finishQuietTest();

  tickAbTest();
  if (!radioAutoSleeping) tickClockRecovery();
  drawDisplay();
  if (restartPending && millis() - restartRequestedMs > 1500UL) ESP.restart();

  if (!quietRunning && millis() - reportStartMs >= 5000) {
    uint32_t edges; noInterrupts(); edges = totalEdges; interrupts();
    edgesLast5s = edges - reportEdges;
    Serial.printf("[5s] DATA=%s RX=%s bias=%s edges=%lu (%lu/5s) valid=%lu noise=%lu valid%%=%.1f PLL=%s filt=%lu rejS=%lu rejW=%lu rejP=%lu frames=%lu/%lu\n",
      digitalRead(DCF_DATA_PIN)?"HIGH":"LOW", receiverOn?"ON":"OFF", usePullup?"PULLUP":"INPUT",
      (unsigned long)edges, (unsigned long)edgesLast5s,
      (unsigned long)(good0+good1), (unsigned long)noise, validPct(good0+good1,noise), radioAutoSleeping?"RADIO-OFF":(pllLocked?"LOCK":"SEARCH"), (unsigned long)filteredAccepted, (unsigned long)rejectedShort, (unsigned long)rejectedWidth, (unsigned long)rejectedPhase, (unsigned long)frameValid, (unsigned long)frameInvalid);
    reportEdges = edges; reportStartMs = millis();
  }
}
