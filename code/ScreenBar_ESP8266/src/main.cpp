#include <Arduino.h>
#include <SoftwareSerial.h>

// ESP-01: GPIO2 = RX, GPIO0 = TX (connect to CH32 TX/RX)
const uint8_t CH32_RX_PIN = 2;
const uint8_t CH32_TX_PIN = 0;

SoftwareSerial ch32Serial(CH32_RX_PIN, CH32_TX_PIN);  // RX, TX

static char rxBuf[256];
static uint16_t rxLen = 0;

void setup() {
  Serial.begin(115200);      // USB-to-UART for debug
  ch32Serial.begin(115200);  // Link to CH32

  Serial.println("ESP8266 ready, waiting for CH32 lines...");
}

void loop() {
  // Collect a line from CH32 (terminated by '\n') and print to Serial
  while (ch32Serial.available() > 0) {
    char c = ch32Serial.read();
    if (c == '\r') {
      continue;  // ignore CR
    }

    if (c == '\n' || rxLen >= sizeof(rxBuf) - 1) {
      rxBuf[rxLen] = '\0';
      if (rxLen > 0) {
        Serial.print("CH32 -> ");
        Serial.println(rxBuf);
      }
      rxLen = 0;
    } else {
      rxBuf[rxLen++] = c;
    }
  }

  delay(5);
}