/*
  ethernet.ino
  Teensy 4.1 — health monitor + UDP output

  Pins:
      23 — Analog  ch1 (A1) — variable freq  → analogRead rising-edge count
      22 — Analog  ch2 (A2) — ~20 kHz        → FreqMeasureMulti (pin 22, timer-capable)
      25 — Digital ch3 (D1) — ~100 kHz       → ISR edge counter  (pin 25, NOT timer-capable)
      24 — Digital ch4 (D2) — ~500 kHz       → ISR edge counter  (pin 24, attachInterrupt
                                                conflicts with FreqMeasureMulti so use ISR)

  WHY ISR for both D1 and D2:
    attachInterrupt() and FreqMeasureMulti CANNOT share the same pin.
    FMM reconfigures the pin mux to hardware timer capture mode; attaching
    an interrupt on the same pin fights that and causes FMM to never fire.
    Solution: use ISR edge counting (CHANGE interrupt) for BOTH D1 and D2.
    edges / 2 / windowSec = frequency in Hz. Accurate enough for health monitoring.

  WHY FreqMeasureMulti for A2 only:
    analogRead loop is too slow (~3-4 µs/call) to catch rising edges above
    ~20 kHz reliably. Pin 22 is timer-capable and has no ISR conflict.
*/

#include <QNEthernet.h>
#include <FreqMeasureMulti.h>
using namespace qindesign::network;

// ── Network ────────────────────────────────────────────────────────────────
static const IPAddress TEENSY_IP(192, 168, 2, 2);
static const IPAddress PC_IP    (192, 168, 2, 1);
static const IPAddress SUBNET   (255, 255, 255, 0);
static const IPAddress GATEWAY  (192, 168, 2, 1);
static const uint16_t  UDP_PORT = 5000;

// ── Pins ───────────────────────────────────────────────────────────────────
const int ANALOG_PIN_1  = 23;
const int ANALOG_PIN_2  = 22;
const int DIGITAL_PIN_1 = 25;
const int DIGITAL_PIN_2 = 24;

// ── Thresholds ─────────────────────────────────────────────────────────────
const int   THRESHOLD_EDGE   = 2048;
const int   PEAK_HEALTHY_MIN = 3500;
const int   PEAK_WARNING_MIN = 2000;
const float TARGET_A2        = 20.0f;   // kHz expected for A2
const float TARGET_D1        = 100.0f;  // kHz expected for D1
const float TARGET_D2        = 200.0f;  // kHz expected for D2
const float FREQ_TOLERANCE   = 0.05f;   // ±5%

// ── FreqMeasureMulti — A2 only (pin 22, no ISR conflict) ──────────────────
FreqMeasureMulti meterA2;
float currentFreqA2 = 0.0f;

// ── ISR state — waveform toggle + edge count for D1 and D2 ────────────────
volatile bool     pinStateD1  = false;
volatile uint32_t d1EdgeCount = 0;

volatile bool     pinStateD2  = false;
volatile uint32_t d2EdgeCount = 0;

void isrD1() { pinStateD1 = !pinStateD1; d1EdgeCount++; }
void isrD2() { pinStateD2 = !pinStateD2; d2EdgeCount++; }

// ── Analog A1 (rising-edge count in main loop, fine for low freq) ──────────
uint32_t analogCount1 = 0;
bool     lastState1   = false;
int      maxValA1     = 0;
int      maxValA2     = 0;

elapsedMillis printTimeout;
const int SAMPLE_WINDOW = 100;  // ms

// ── Waveform buffers ───────────────────────────────────────────────────────
const int numSamples = 200;
uint16_t bufferA1[numSamples];
uint16_t bufferA2[numSamples];
uint8_t  bufferD1[numSamples];
uint8_t  bufferD2[numSamples];

EthernetUDP udp;

// ══════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}
    Serial.println("--- UDP Health Monitor ---");

    // A2 only on FreqMeasureMulti — no ISR on pin 22
    meterA2.begin(ANALOG_PIN_2);

    // D1 and D2 both use CHANGE interrupts for edge counting + waveform
    // No FreqMeasureMulti on these pins — zero conflict
    attachInterrupt(digitalPinToInterrupt(DIGITAL_PIN_1), isrD1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DIGITAL_PIN_2), isrD2, CHANGE);

    pinMode(ANALOG_PIN_1, INPUT);
    analogReadResolution(12);
    analogReadAveraging(1);

    Ethernet.begin(TEENSY_IP, SUBNET, GATEWAY);
    udp.begin(UDP_PORT);
    Serial.print("[Teensy] IP: "); Serial.println(Ethernet.localIP());
    printTimeout = 0;
}

// ══════════════════════════════════════════════════════════════════════════
void loop() {
    // ── 1. A1 rising-edge count via analogRead ─────────────────────────────
    int val1 = analogRead(ANALOG_PIN_1);
    if (val1 > maxValA1) maxValA1 = val1;
    bool cur1 = (val1 > THRESHOLD_EDGE);
    if (cur1 && !lastState1) analogCount1++;
    lastState1 = cur1;

    // ── 2. A2 frequency from FreqMeasureMulti ─────────────────────────────
    if (meterA2.available()) {
        uint32_t c = meterA2.read();
        currentFreqA2 = 1000000000.0f / meterA2.countToNanoseconds(c);
    }

    // ── 3. Every 100 ms ────────────────────────────────────────────────────
    if (printTimeout >= SAMPLE_WINDOW) {
        float exactWindowMs = (float)printTimeout;
        printTimeout = 0;

        // Snapshot and reset both ISR edge counters atomically
        noInterrupts();
        uint32_t edges1 = d1EdgeCount; d1EdgeCount = 0;
        uint32_t edges2 = d2EdgeCount; d2EdgeCount = 0;
        interrupts();

        // Frequencies in Hz
        float freqA1_hz = (analogCount1 * 1000.0f) / exactWindowMs;
        float freqA2_hz = currentFreqA2;
        float freqD1_hz = (edges1 / 2.0f) / (exactWindowMs / 1000.0f);
        float freqD2_hz = (edges2 / 2.0f) / (exactWindowMs / 1000.0f);

        float freqA1_kHz = freqA1_hz / 1000.0f;
        float freqA2_kHz = freqA2_hz / 1000.0f;
        float freqD1_kHz = freqD1_hz / 1000.0f;
        float freqD2_kHz = freqD2_hz / 1000.0f;

        // ── ADC burst for waveform + peak tracking ─────────────────────────
        for (int i = 0; i < numSamples; i++) {
            bufferA1[i] = analogRead(ANALOG_PIN_1);
            bufferA2[i] = analogRead(ANALOG_PIN_2);
            if (bufferA1[i] > maxValA1) maxValA1 = bufferA1[i];
            if (bufferA2[i] > maxValA2) maxValA2 = bufferA2[i];
        }

        // Digital waveform — ISR toggle state, NO digitalRead
        for (int i = 0; i < numSamples; i++) {
            bufferD1[i] = pinStateD1 ? 1 : 0;
            bufferD2[i] = pinStateD2 ? 1 : 0;
            delayMicroseconds(2);
        }

        // ── Health flags ───────────────────────────────────────────────────
        bool    faultDetected = false;
        uint8_t flagA1, flagA2, flagD1, flagD2;

        if      (maxValA1 > PEAK_HEALTHY_MIN) flagA1 = 0;
        else if (maxValA1 > PEAK_WARNING_MIN) { flagA1 = 1; faultDetected = true; }
        else                                  { flagA1 = 2; faultDetected = true; }

        if      (maxValA2 > PEAK_HEALTHY_MIN) flagA2 = 0;
        else if (maxValA2 > PEAK_WARNING_MIN) { flagA2 = 1; faultDetected = true; }
        else                                  { flagA2 = 2; faultDetected = true; }

        flagD1 = (freqD1_kHz >= TARGET_D1 * (1.0f - FREQ_TOLERANCE)) ? 0 : 2;
        if (flagD1) faultDetected = true;

        flagD2 = (freqD2_kHz >= TARGET_D2 * (1.0f - FREQ_TOLERANCE)) ? 0 : 2;
        if (flagD2) faultDetected = true;

        uint8_t fault = faultDetected ? 1 : 0;

        // ── Serial debug ───────────────────────────────────────────────────
        Serial.printf(
            "A1=%.2fkHz pk=%d %s | A2=%.2fkHz(FMM) pk=%d %s | "
            "D1=%.2fkHz(ISR) %s | D2=%.2fkHz(ISR) %s | %s\n",
            freqA1_kHz, maxValA1, flagA1==0?"OK":flagA1==1?"WARN":"CRIT",
            freqA2_kHz, maxValA2, flagA2==0?"OK":flagA2==1?"WARN":"CRIT",
            freqD1_kHz, flagD1?"FAULT":"OK",
            freqD2_kHz, flagD2?"FAULT":"OK",
            fault?"*** FAULT ***":"healthy");

        // Reset accumulators
        maxValA1 = 0; maxValA2 = 0;
        analogCount1 = 0;

        // ── UDP packet ─────────────────────────────────────────────────────
        udp.beginPacket(PC_IP, UDP_PORT);
        udp.printf("F,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%u,%u",
            freqA1_hz, freqA2_hz, freqD1_hz, freqD2_hz,
            flagA1, flagA2, flagD1, flagD2, fault);

        udp.print("|");
        for (int i = 0; i < numSamples; i++) { if (i) udp.print(","); udp.print(bufferA1[i]); }
        udp.print("|");
        for (int i = 0; i < numSamples; i++) { if (i) udp.print(","); udp.print(bufferA2[i]); }
        udp.print("|");
        for (int i = 0; i < numSamples; i++) { if (i) udp.print(","); udp.print(bufferD1[i] ? 4095 : 0); }
        udp.print("|");
        for (int i = 0; i < numSamples; i++) { if (i) udp.print(","); udp.print(bufferD2[i] ? 4095 : 0); }
        udp.print("\n");
        udp.endPacket();
    }
}
