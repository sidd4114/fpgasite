/*
  ethernet.ino
  ─────────────────────────────────────────────────────────────────────────────
  Teensy 4.1 — Based on teammate's unified interrupt sketch, streams over UDP.

  Pin assignment (matches teammate exactly):
      Pin 22 (A8)  — Analog  ch1 — variable freq (10–40 kHz)
      Pin 25 (A11) — Analog  ch2 — ~50 kHz
      Pin 24       — Digital ch3 — ~100 kHz  (hardware interrupt)
      Pin 23       — Digital ch4 — ~500 kHz  (hardware interrupt)

  UDP packet format (sent every 100ms):
      F,<freqA1>,<freqA2>,<freqD1>,<freqD2>|<wA1_0>,<wA1_1>,...|<wA2_0>,...|<wD1_0>,...|<wD2_0>,...\n

  Network: Teensy=192.168.2.2  PC=192.168.2.1  Port=5000
*/

#include <QNEthernet.h>
using namespace qindesign::network;

// ── Network ────────────────────────────────────────────────────────────────
static const IPAddress TEENSY_IP(192, 168, 2, 2);
static const IPAddress PC_IP    (192, 168, 2, 1);
static const IPAddress SUBNET   (255, 255, 255, 0);
static const IPAddress GATEWAY  (192, 168, 2, 1);
static const uint16_t  UDP_PORT = 5000;

// ── Pin definitions (teammate's exact pins) ────────────────────────────────
const int ANALOG_PIN_1  = 22;   // A8  — variable freq
const int ANALOG_PIN_2  = 25;   // A11 — ~50 kHz
const int DIGITAL_PIN_1 = 24;   // ~100 kHz — hardware interrupt
const int DIGITAL_PIN_2 = 23;   // ~500 kHz — hardware interrupt

// ── Frequency measurement ──────────────────────────────────────────────────
const int     THRESHOLD    = 2048;   // 12-bit midpoint (1.65V)
const uint32_t SAMPLE_WINDOW = 100; // ms — same as teammate

uint32_t analogCount1 = 0, analogCount2 = 0;
bool     lastState1   = false, lastState2 = false;

volatile uint32_t dCount1 = 0, dCount2 = 0;
void isrD1() { dCount1++; }
void isrD2() { dCount2++; }

elapsedMillis windowTimer;

// ── Waveform capture buffer ────────────────────────────────────────────────
// 200 samples captured once per window — spread evenly for display
const int NUM_WAVE = 200;
uint16_t waveA1[NUM_WAVE], waveA2[NUM_WAVE];
uint8_t  waveD1[NUM_WAVE], waveD2[NUM_WAVE];

EthernetUDP udp;

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}
    Serial.println("[Teensy] Unified interrupt mode — UDP stream");

    pinMode(ANALOG_PIN_1,  INPUT);
    pinMode(ANALOG_PIN_2,  INPUT);
    pinMode(DIGITAL_PIN_1, INPUT);
    pinMode(DIGITAL_PIN_2, INPUT);

    attachInterrupt(digitalPinToInterrupt(DIGITAL_PIN_1), isrD1, RISING);
    attachInterrupt(digitalPinToInterrupt(DIGITAL_PIN_2), isrD2, RISING);

    analogReadResolution(12);
    analogReadAveraging(1);

    Ethernet.begin(TEENSY_IP, SUBNET, GATEWAY);
    udp.begin(UDP_PORT);

    Serial.print("[Teensy] IP: ");
    Serial.println(Ethernet.localIP());
    windowTimer = 0;
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
    // ── 1. Fast analog polling (runs every iteration) ─────────────────────
    int val1 = analogRead(ANALOG_PIN_1);
    int val2 = analogRead(ANALOG_PIN_2);

    bool cur1 = (val1 > THRESHOLD);
    bool cur2 = (val2 > THRESHOLD);

    if (cur1 && !lastState1) analogCount1++;
    lastState1 = cur1;

    if (cur2 && !lastState2) analogCount2++;
    lastState2 = cur2;

    // ── 2. Every 100ms: calculate freqs, capture waveform, send UDP ───────
    if (windowTimer >= SAMPLE_WINDOW) {
        float exactWindowMs = windowTimer;
        windowTimer = 0;

        // Safely snapshot interrupt counters
        noInterrupts();
        uint32_t snapD1 = dCount1; dCount1 = 0;
        uint32_t snapD2 = dCount2; dCount2 = 0;
        interrupts();

        // Calculate frequencies
        float freqA1 = (analogCount1 * 1000.0f) / exactWindowMs;
        float freqA2 = (analogCount2 * 1000.0f) / exactWindowMs;
        float freqD1 = (snapD1       * 1000.0f) / exactWindowMs;
        float freqD2 = (snapD2       * 1000.0f) / exactWindowMs;
        analogCount1 = 0;
        analogCount2 = 0;

        // Serial debug — same format as teammate
        Serial.printf("A0=%.2f kHz  A1=%.2f kHz  D24=%.2f kHz  D23=%.2f kHz\n",
            freqA1/1e3, freqA2/1e3, freqD1/1e3, freqD2/1e3);

        // Capture waveform snapshot
        for (int i = 0; i < NUM_WAVE; i++) {
            waveA1[i] = analogRead(ANALOG_PIN_1);
            waveA2[i] = analogRead(ANALOG_PIN_2);
        }
        for (int i = 0; i < NUM_WAVE; i++) {
            waveD1[i] = digitalRead(DIGITAL_PIN_1);
            waveD2[i] = digitalRead(DIGITAL_PIN_2);
        }

        // Build and send UDP packet
        // Format: F,fA1,fA2,fD1,fD2|waveA1|waveA2|waveD1|waveD2
        udp.beginPacket(PC_IP, UDP_PORT);
        udp.printf("F,%.2f,%.2f,%.2f,%.2f", freqA1, freqA2, freqD1, freqD2);

        // waveA1
        udp.print("|");
        for (int i = 0; i < NUM_WAVE; i++) { if(i) udp.print(","); udp.print(waveA1[i]); }
        // waveA2
        udp.print("|");
        for (int i = 0; i < NUM_WAVE; i++) { if(i) udp.print(","); udp.print(waveA2[i]); }
        // waveD1 (scaled 0/4095)
        udp.print("|");
        for (int i = 0; i < NUM_WAVE; i++) { if(i) udp.print(","); udp.print(waveD1[i] ? 4095 : 0); }
        // waveD2 (scaled 0/4095)
        udp.print("|");
        for (int i = 0; i < NUM_WAVE; i++) { if(i) udp.print(","); udp.print(waveD2[i] ? 4095 : 0); }

        udp.print("\n");
        udp.endPacket();
    }
}
