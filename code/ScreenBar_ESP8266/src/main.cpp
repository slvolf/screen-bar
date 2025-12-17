#include <Arduino.h>
#include <ESP8266WiFi.h>

// WIFI 与 TCP 目标配置，请按需修改
constexpr char kSsid[] = "slvolf-desktop";
constexpr char kPassword[] = "slvolf-desktop";
constexpr char kServerHost[] = "192.168.137.1";  // Win电脑热点默认IP
constexpr uint16_t kServerPort = 9000;           // 电脑软件监听端口

// ESP-01S 默认 UART0：TX=GPIO1, RX=GPIO3
// 接线：CH32 TX -> ESP RX(GPIO3)，CH32 RX -> ESP TX(GPIO1)，GND 共地

static char rxBuf[256];
static uint16_t rxLen = 0;
static WiFiClient client;

static void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(kSsid, kPassword);
  // 非阻塞重试，由 loop 中反复触发
}

static void ensureTcpConnected() {
  if (client.connected()) {
    return;
  }
  client.stop();
  client.connect(kServerHost, kServerPort);
}

void setup() {
  Serial.begin(9600);  // 与 CH32 波特率一致
  ensureWiFiConnected();
}

void loop() {
  ensureWiFiConnected();
  if (WiFi.status() == WL_CONNECTED) {
    ensureTcpConnected();
  } else {
    client.stop();
  }

  // 从 CH32 按行接收（\n 结束），收到一行后推送到 TCP
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }

    if (c == '\n' || rxLen >= sizeof(rxBuf) - 1) {
      rxBuf[rxLen] = '\0';
      if (rxLen > 0 && client.connected()) {
        client.print(rxBuf);
        client.print('\n');
        client.flush();
      }
      rxLen = 0;
    } else {
      rxBuf[rxLen++] = c;
    }
  }

  delay(5);
}