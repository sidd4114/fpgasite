#include <QNEthernet.h>
using namespace qindesign::network;

EthernetUDP udp;

IPAddress pcIP(192, 168, 2, 1);
const uint16_t pcPort = 5000;

const int analogInPin = 22;
const int numSamples = 500;
uint16_t buffer[numSamples];

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  Serial.println("Starting Ethernet...");

  IPAddress ip(192, 168, 2, 2);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress gateway(192, 168, 2, 1);

  // FULL init (important)
  Ethernet.begin(ip, subnet, gateway);

  delay(1000);

  Serial.print("Teensy IP: ");
  Serial.println(Ethernet.localIP());

  udp.begin(5000);

  Serial.println("UDP ready");

  analogReadResolution(12);
  analogReadAveraging(1);
}

void loop() {
  for (int i = 0; i < numSamples; i++) {
    buffer[i] = analogRead(analogInPin);
  }

  for (int i = 0; i < numSamples; i += 4) {
    uint16_t v1 = buffer[i];
    uint16_t v2 = (i + 1 < numSamples) ? buffer[i + 1] : v1;
    uint16_t v3 = (i + 2 < numSamples) ? buffer[i + 2] : v2;
    uint16_t v4 = (i + 3 < numSamples) ? buffer[i + 3] : v3;

    udp.beginPacket(pcIP, pcPort);
    udp.printf("%u,%u,%u,%u", v1, v2, v3, v4);
    udp.endPacket();

    delay(1);
  }
}
