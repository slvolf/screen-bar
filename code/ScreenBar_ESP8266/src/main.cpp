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
static bool g_hasTargetBssid = false;
static uint8_t g_targetBssid[6] = {0};
static int32_t g_targetChannel = 0;

static void printBssid(const uint8_t* bssid) {
  if (!bssid) {
    Serial.print("(null)");
    return;
  }
  for (int i = 0; i < 6; ++i) {
    if (i) Serial.print(":");
    if (bssid[i] < 16) Serial.print('0');
    Serial.print(bssid[i], HEX);
  }
}

static bool scanAndChooseAp() {
  Serial.println("[WiFi] Scanning APs...");
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (n <= 0) {
    Serial.println("[WiFi] Scan found no networks.");
    return false;
  }
  int best = -1;
  int bestRssi = -1000;
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    int32_t chan = WiFi.channel(i);
    bool hidden = WiFi.isHidden(i);
    uint8_t* bssid = WiFi.BSSID(i);
    Serial.print("  -> ");
    Serial.print(ssid);
    Serial.print("  RSSI=");
    Serial.print(rssi);
    Serial.print(" dBm  CH=");
    Serial.print(chan);
    Serial.print("  HID=");
    Serial.print(hidden);
    Serial.print("  BSSID=");
    printBssid(bssid);
    Serial.println();
    if (ssid == kSsid && rssi > bestRssi) {
      bestRssi = rssi;
      best = i;
    }
  }
  if (best < 0) {
    Serial.println("[WiFi] Target SSID not found in scan.");
    return false;
  }
  memcpy(g_targetBssid, WiFi.BSSID(best), 6);
  g_targetChannel = WiFi.channel(best);
  g_hasTargetBssid = true;
  Serial.print("[WiFi] Using AP BSSID=");
  printBssid(g_targetBssid);
  Serial.print(" CH=");
  Serial.println(g_targetChannel);
  WiFi.scanDelete();
  return true;
}

static void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  static uint32_t lastAttemptMs = 0;
  const uint32_t now = millis();
  if (now - lastAttemptMs >= 2000) {  // 2 秒尝试一次，避免刷屏
    Serial.printf("[WiFi] Connecting to SSID: %s...\r\n", kSsid);
    WiFi.mode(WIFI_STA);
    if (!g_hasTargetBssid) {
      scanAndChooseAp();
    }
    if (g_hasTargetBssid) {
      WiFi.begin(kSsid, kPassword, g_targetChannel, g_targetBssid);
    } else {
      WiFi.begin(kSsid, kPassword);
    }
    wl_status_t res = (wl_status_t)WiFi.waitForConnectResult(
        10000);  // 最多等待 10 秒，便于调试原因
    Serial.printf("[WiFi] waitForConnectResult=%d, status=%d\r\n", res,
                  WiFi.status());
    WiFi.printDiag(Serial);  // 打印当前 STA 状态、RSSI、信道等
    if (res != WL_CONNECTED) {
      // Failure hints in English
      if (res == WL_NO_SSID_AVAIL) {
        Serial.println(
            "[Hint] No SSID found: ensure hotspot is 2.4GHz; avoid WPA3.");
        Serial.println(
            "[Hint] If SSID is hidden, keep hidden scan enabled; fix channel "
            "to 1/6/11 and move closer.");
      } else if (res == WL_CONNECT_FAILED) {
        Serial.println(
            "[Hint] Connect failed: wrong passphrase or incompatible auth "
            "(ESP8266 doesn't support WPA3).");
        Serial.println("[Hint] Use WPA2-PSK or WPA/WPA2 mixed mode.");
      }
      // Drop cached BSSID, rescan next attempt
      g_hasTargetBssid = false;
    }
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
  WiFi.persistent(false);  // 避免频繁写入 flash
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);         // 断线后自动重连
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // 关闭省电模式以减小延迟
  WiFi.setOutputPower(20.5);           // 提升发射功率帮助弱信号
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
    static uint32_t lastRssiLog = 0;
    const uint32_t now = millis();
    if (now - lastRssiLog >= 5000) {
      Serial.printf("[WiFi] RSSI=%d dBm, channel=%d\r\n", WiFi.RSSI(),
                    WiFi.channel());
      lastRssiLog = now;
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