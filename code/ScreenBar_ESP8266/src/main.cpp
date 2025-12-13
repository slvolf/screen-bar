#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h> 

/************************ 硬件适配（ESP-01专用） ************************/
// ESP-01引脚映射：GPIO2对应TXD2，GPIO0对应RXD2（根据实际接线调整）
// 注意：ESP-01的GPIO0/GPIO2需避开烧录引脚（烧录时GPIO0接GND，运行时悬空/上拉）
#define CH32_RX_PIN 2  // ESP8266的RX（接CH32的TX）→ GPIO2
#define CH32_TX_PIN 0  // ESP8266的TX（接CH32的RX）→ GPIO0

/************************ 配置参数（替换为你的实际信息） ************************/
const char *ssid = "slvolf";
const char *password = "SW";
const char *server_ip = "192.168.1.100"; // 电脑TCP服务器IP
const uint16_t server_port = 8888;       // 电脑TCP服务器端口

/************************ 全局对象/变量 ************************/
SoftwareSerial ch32Serial(CH32_RX_PIN, CH32_TX_PIN); // RX, TX（GPIO编号）
WiFiClient client;
char ch32_buf[128];
uint8_t ch32_len = 0;

/************************ 函数声明 ************************/
void connectToServer();

void setup() {
  // 初始化串口（调试+CH32通信）
  Serial.begin(115200);     // ESP8266自带串口（TX=GPIO1, RX=GPIO3）
  ch32Serial.begin(115200); // 软串口（与CH32通信）

  // 连接WiFi
  Serial.print("连接WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA); // 仅STA模式，节省内存
  WiFi.begin(ssid, password);
  
  // 等待WiFi连接（超时10秒）
  uint8_t wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi已连接，IP: " + WiFi.localIP().toString());
    connectToServer(); // 连接电脑TCP服务器
  } else {
    Serial.println("\nWiFi连接失败");
  }
}

void loop() {
  // 断线重连（TCP服务器）
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    Serial.println("TCP连接断开，重试连接...");
    connectToServer();
    delay(1000);
  }

  // 接收CH32数据 → 转发到电脑TCP
  if (ch32Serial.available() > 0) {
    char c = ch32Serial.read();
    if (c == '\n' || ch32_len >= 127) { // 换行符/缓冲区满触发转发
      ch32_buf[ch32_len] = '\0';
      if (client.connected()) {
        client.println(ch32_buf);
        Serial.print("转发到电脑: ");
        Serial.println(ch32_buf);
      }
      ch32_len = 0; // 重置缓冲区
    } else {
      ch32_buf[ch32_len++] = c; // 缓存字符
    }
  }

  // 接收电脑TCP数据 → 转发到CH32
  if (client.available() > 0) {
    String data = client.readStringUntil('\n');
    ch32Serial.println(data);
    Serial.print("接收自电脑: ");
    Serial.println(data);
  }

  delay(10);
}

/************************ 函数定义 ************************/
void connectToServer() {
  client.stop(); // 先关闭旧连接
  if (client.connect(server_ip, server_port)) {
    Serial.println("已连接到电脑TCP服务器");
  } else {
    Serial.println("连接电脑TCP服务器失败");
  }
}