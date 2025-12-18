#include <Arduino.h>
#include <ESP8266WiFi.h>

// WIFI 与 TCP 目标配置，请按需修改
constexpr char kSsid[] = "slvolf-phone";
constexpr char kPassword[] = "slvolf-phone";
constexpr char kServerHost[] = "192.168.100.160";  // 电脑软件监听 IP
constexpr uint16_t kServerPort = 9000;             // 电脑软件监听端口

// ESP-01S 默认 UART0：TX=GPIO1, RX=GPIO3
// 接线：CH32 TX -> ESP RX(GPIO3)，CH32 RX -> ESP TX(GPIO1)，GND 共地

static char rxBuf[256];
static uint16_t rxLen = 0;
static WiFiClient client;
static bool g_wifiWasConnected = false;
static bool g_tcpWasConnected = false;

static void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  static uint32_t lastAttemptMs = 0;
  const uint32_t now = millis();
  if (now - lastAttemptMs >= 2000) {  // 2 秒尝试一次，避免刷屏
    Serial.printf("[WiFi] Connecting to SSID: %s...\r\n", kSsid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(kSsid, kPassword);
    wl_status_t res = (wl_status_t)WiFi.waitForConnectResult(
        10000);  // 最多等待 10 秒，便于调试原因
    Serial.printf("[WiFi] waitForConnectResult=%d, status=%d\r\n", res,
                  WiFi.status());
    WiFi.printDiag(Serial);  // 打印当前 STA 状态、RSSI、信道等
    lastAttemptMs = now;
  }
  // 非阻塞重试，由 loop 中反复触发
}

static void ensureTcpConnected() {
  if (client.connected()) {
    return;
  }
  static uint32_t lastAttemptMs = 0;
  const uint32_t now = millis();
  if (now - lastAttemptMs >= 2000) {  // 2 秒尝试一次，避免刷屏
    client.stop();
    Serial.printf("[TCP] Connecting %s:%u...\r\n", kServerHost, kServerPort);
    const bool ok = client.connect(kServerHost, kServerPort);
    if (ok) {
      Serial.println("[TCP] Connected.");
    } else {
      Serial.println("[TCP] Connect failed.");
    }
    lastAttemptMs = now;
  }
}

void setup() {
  Serial.begin(9600);  // 与 CH32 波特率一致
  Serial.println();
  Serial.println("==== ScreenBar ESP8266 Boot ====");
  Serial.printf("UART: %ld baud\r\n", 9600L);
  Serial.printf("WiFi SSID: %s\r\n", kSsid);
  Serial.printf("TCP Target: %s:%u\r\n", kServerHost, kServerPort);
  Serial.printf("MAC: %s\r\n", WiFi.macAddress().c_str());
  Serial.println("Init done, starting connections...");
  ensureWiFiConnected();
}

void loop() {
  ensureWiFiConnected();
  if (WiFi.status() == WL_CONNECTED) {
    if (!g_wifiWasConnected) {
      Serial.printf("[WiFi] Connected. IP: %s\r\n",
                    WiFi.localIP().toString().c_str());
      g_wifiWasConnected = true;
    }
    ensureTcpConnected();
  } else {
    if (g_wifiWasConnected) {
      Serial.println("[WiFi] Disconnected.");
      g_wifiWasConnected = false;
    }
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
        if (!g_tcpWasConnected) {
          // 如果刚刚连上，记录状态（避免额外变量更新点，放在这里兜底一次）
          g_tcpWasConnected = true;
        }
      }
      rxLen = 0;
    } else {
      rxBuf[rxLen++] = c;
    }
  }

  // 从 TCP 读取数据并转发给 CH32（保持原始换行，CH32 以 \n 作为一条指令结束）
  while (client.connected() && client.available() > 0) {
    int c = client.read();
    if (c >= 0) {
      Serial.write((char)c);
    }
  }

  // TCP 连接状态变化提示（避免在 ensureTcpConnected 中重复提示）
  bool tcpNow = client.connected();
  if (tcpNow && !g_tcpWasConnected) {
    Serial.println("[TCP] Connected.");
  } else if (!tcpNow && g_tcpWasConnected) {
    Serial.println("[TCP] Disconnected.");
  }
  g_tcpWasConnected = tcpNow;

  delay(5);
}