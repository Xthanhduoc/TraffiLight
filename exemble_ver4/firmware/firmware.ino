
/// chạy on 4 led
#include <Wire.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

#define SDA_PIN1 14
#define SCL_PIN1 15
#define SDA_PIN2 16
#define SCL_PIN2 17


#define SDA_PIN3 18
#define SCL_PIN3 19
#define SDA_PIN4 21
#define SCL_PIN4 22

#define senSorLux 13
#define led 32

#define LED_PIN1 4
#define LED_PIN2 5
#define LED_PIN3 25
#define LED_PIN4 23

#define LED_COUNT 3

Adafruit_NeoPixel strip1(LED_COUNT, LED_PIN1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT, LED_PIN2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip3(LED_COUNT, LED_PIN3, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip4(LED_COUNT, LED_PIN4, NEO_GRB + NEO_KHZ800);

// ===== WiFi Manager & Network =====
WiFiManager wm;
bool wifiConnected = false;
bool apMode = false;
unsigned long wifiReconnectMillis = 0;
const unsigned long wifiReconnectInterval = 30000;
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 1000;

// Biến cho blink LED khi kết nối WiFi (non-blocking)
bool wifiConnectBlink = false;
unsigned long wifiConnectBlinkMillis = 0;
int wifiConnectBlinkCount = 0;

// Biến cho blink LED khi kết nối MQTT (non-blocking)
bool mqttConnectBlink = false;
unsigned long mqttConnectBlinkMillis = 0;
int mqttConnectBlinkCount = 0;

// Cấu hình AP cố định
const char* apName = "TrafficLight-AP";
const char* apPassword = NULL;

// ===== MQTT Configuration =====
char mqttServer[40] = "broker.emqx.io";
int mqttPort = 1883;
char mqttTopicSubscribe[40] = "traffic/control";
char mqttTopicPublish[40] = "traffic/status";

// Lưu trữ các parameter để cập nhật sau
WiFiManagerParameter* param_mqtt_server;
WiFiManagerParameter* param_mqtt_port;
WiFiManagerParameter* param_mqtt_topic_sub;
WiFiManagerParameter* param_mqtt_topic_pub;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool mqttConnected = false;
unsigned long mqttReconnectMillis = 0;
const unsigned long mqttReconnectInterval = 5000;
unsigned long lastMqttPublish = 0;
const unsigned long mqttPublishInterval = 3000;
unsigned long lastMqttLoop = 0;
const unsigned long mqttLoopInterval = 10;

String mqttClientIdStr;

// ===== NIGHT MODE =====
bool nightMode = false;           // Trạng thái đèn đang ở chế độ ban đêm
bool nightModeManual = false;     // Chế độ ban đêm thủ công (do người dùng bật)
bool autoMode = false;            // Chế độ tự động (đọc cảm biến)
unsigned long darkStartMillis = 0;
const unsigned long nightDelay = 5000;

// ===== TM1650 ADDRESS =====
#define TM1650_ADDR_SYS   0x24
#define TM1650_ADDR_DIG1  0x34
#define TM1650_ADDR_DIG2  0x35
#define TM1650_ADDR_DIG3  0x36
#define TM1650_ADDR_DIG4  0x37

// ===== LIGHT STATE =====
enum LightState { RED, GREEN, YELLOW };

// ===== TRAFFIC LIGHT STRUCT =====
struct TrafficLight {
    LightState state;
    int counter;
    unsigned long previousMillis;
    bool blinkState;
    unsigned long blinkMillis;
    int flashCount; // Biến đếm số lần chớp khi vào chế độ ưu tiên
};

TrafficLight light[4];

// ===== TIME CONFIG =====
int redTime = 15;
int greenTime = 10;
int yellowTime = 5;

const unsigned long interval = 1000;
const unsigned long blinkInterval = 500;

// ===== PRIORITY MODE =====
bool priorityMode = false;
int priorityId = -1;

// ===== 7 SEG TABLE =====
const uint8_t segTable[10] = {
        0x3F,0x06,0x5B,0x4F,0x66,
        0x6D,0x7D,0x07,0x7F,0x6F
};

// ===================================================
// ============== HELPER FUNCTIONS ===================
// ===================================================

String getUniqueClientId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char clientId[30];
  snprintf(clientId, 30, "ESP32_Traffic_%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(clientId);
}

// Callback khi cấu hình được lưu
void saveConfigCallback() {
  Serial.println("Config saved!");
  updateMQTTConfigFromParams();
}

// Cập nhật cấu hình MQTT từ parameters
void updateMQTTConfigFromParams() {
  if (param_mqtt_server) {
    strcpy(mqttServer, param_mqtt_server->getValue());
  }
  if (param_mqtt_port) {
    String portStr = param_mqtt_port->getValue();
    if (portStr.length() > 0) {
      mqttPort = portStr.toInt();
    }
  }
  if (param_mqtt_topic_sub) {
    strcpy(mqttTopicSubscribe, param_mqtt_topic_sub->getValue());
  }
  if (param_mqtt_topic_pub) {
    strcpy(mqttTopicPublish, param_mqtt_topic_pub->getValue());
  }
}

// ===================================================
// ================= WIFI MANAGER ====================
// ===================================================

void setupWiFi() {

  Serial.println("\n--- Initializing WiFi in AP+STA Mode ---");

  wm.setConfigPortalTimeout(180);
  wm.setTitle("Traffic Light Configuration");
  wm.setConnectTimeout(20);
  wm.setConfigPortalBlocking(false);

  wm.setSaveConfigCallback(saveConfigCallback);

  // Thêm route tùy chỉnh để lấy danh sách WiFi dạng JSON
  wm.setWebServerCallback([]() {
      wm.server->on("/wifiscan", []() {
          int n = WiFi.scanNetworks();
          String json = "[";
          for (int i = 0; i < n; i++) {
            if (i > 0) json += ",";
            json += "{\"s\":\"" + WiFi.SSID(i) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
          }
          json += "]";
          wm.server->send(200, "application/json", json);
      });
  });

  // =========================
  // STYLE giống app mobile
  // =========================
  const char* custom_css = R"rawliteral(
<style>
body{ font-family:Arial,Helvetica,sans-serif; background:linear-gradient(180deg,#9EC7C9,#BFD2D6); color:#1e293b; padding:10px; }
.wrap{ max-width:420px; margin:auto; }

/* Header */
.header{ background:linear-gradient(90deg,#2F80ED,#00B4B6); padding:14px; border-radius:30px; text-align:center; font-weight:bold; color:white; font-size:18px; box-shadow:0 4px 10px rgba(0,0,0,0.25); margin-bottom:20px; }

/* Card */
.card{ background:white; border-radius:18px; padding:20px; box-shadow:0 6px 14px rgba(0,0,0,0.2); margin-bottom:20px; }

/* WiFi List Styles */
.wifi-item { display: flex; justify-content: space-between; padding: 12px; border-bottom: 1px solid #edf2f7; cursor: pointer; transition: background 0.2s; }
.wifi-item:hover { background: #f7fafc; }
.wifi-item:last-child { border-bottom: none; }
.wifi-name { font-weight: 500; color: #2d3748; }
.wifi-rssi { color: #2F80ED; font-size: 13px; }

/* Hide Redundant WiFiManager Elements */
#w, .l, .q { display: none !important; } /* Hide default list and scan button */
.msg { margin: 10px 0; padding: 10px; border-radius: 10px; background: #fffbeb; color: #92400e; border: 1px solid #fef3c7; }

/* Hide MQTT Parameters and related labels/separators for a clean WiFi Login */
[id^='mqtt_'], label[for^='mqtt_'], hr { display: none !important; }

/* Input & Buttons */
input{ border-radius:14px !important; border:1px solid #d1d5db !important; padding:12px !important; font-size:14px !important; width:100%; box-sizing:border-box; margin-bottom:10px; }
button{ background:#2F80ED !important; border:none !important; border-radius:30px !important; font-size:15px !important; padding:12px !important; color:white !important; font-weight:bold; width:100%; cursor:pointer; }
button:hover{ background:#1d4ed8 !important; }
button[name="save"] { margin-top: 10px; background:#2F80ED !important; border-radius:30px !important; height: 50px; font-size: 18px !important; }

</style>

<script>
function updateWifiList() {
  const listDiv = document.getElementById('wifi-list');
  const statusDiv = document.getElementById('scan-status');
  if(!listDiv) return;

  statusDiv.innerHTML = 'Requesting...';
  listDiv.innerHTML = '<p style="text-align:center;padding:10px;color:#94a3b8">Loading...</p>';

  fetch('/wifiscan')
    .then(response => response.json())
    .then(data => {
      data.sort((a, b) => b.r - a.r);
      let html = '';
      data.forEach(net => {
        html += `<div class="wifi-item" onclick="fillSSID('${net.s}')">
                  <span class="wifi-name">${net.s}</span>
                  <span class="wifi-rssi" style="color:#2F80ED">${net.r} dBm</span>
                </div>`;
      });
      listDiv.innerHTML = html || '<p style="text-align:center;padding:10px;">No WiFi found</p>';
      statusDiv.innerHTML = 'Done';
    })
    .catch(err => {
      statusDiv.innerHTML = 'Error';
      listDiv.innerHTML = '<p style="text-align:center;padding:10px;color:#ef4444">Scan failed</p>';
    });
}

function fillSSID(ssid) {
  const sInput = document.getElementById('s');
  if (sInput) {
    sInput.value = ssid;
    const pInput = document.getElementById('p');
    if (pInput) pInput.focus();
  } else {
    window.location.href = '/wifi?s=' + encodeURIComponent(ssid);
  }
}



document.addEventListener('DOMContentLoaded', () => {
  const path = window.location.pathname;

  // Tự động chuyển hướng từ trang chủ sang /wifi để hiện danh sách và cấu hình ngay
  if (path === '/' || path === '/index.html') {
    window.location.href = '/wifi';
    return;
  }

  injectWifiList();

  const urlParams = new URLSearchParams(window.location.search);
  const sParam = urlParams.get('s');
  const sInput = document.getElementById('s');
  if (sInput) {
    if (sParam) {
      sInput.value = sParam;
    } else if (sInput.value === "No AP Set" || sInput.value === "None") {
      sInput.value = "";
    }
  }

  updateWifiList();
  setInterval(updateWifiList, 20000);
});
</script>
)rawliteral";

  wm.setCustomHeadElement(custom_css);

  // =========================
  // Header giao diện
  // =========================
  const char* header_html = R"rawliteral(
<div class="header">
🚦 ĐIỀU KHIỂN ĐÈN GIAO THÔNG
</div>
)rawliteral";

  wm.setCustomMenuHTML(header_html);

  // =========================
  // MQTT Parameters
  // =========================
  param_mqtt_server = new WiFiManagerParameter("mqtt_server", "MQTT Server", mqttServer, 40);
  param_mqtt_port = new WiFiManagerParameter("mqtt_port", "MQTT Port", String(mqttPort).c_str(), 6);
  param_mqtt_topic_sub = new WiFiManagerParameter("mqtt_topic_sub", "Subscribe Topic", mqttTopicSubscribe, 40);
  param_mqtt_topic_pub = new WiFiManagerParameter("mqtt_topic_pub", "Publish Topic", mqttTopicPublish, 40);

  wm.addParameter(param_mqtt_server);
  wm.addParameter(param_mqtt_port);
  wm.addParameter(param_mqtt_topic_sub);
  wm.addParameter(param_mqtt_topic_pub);

  // =========================
  // Menu
  // =========================
  std::vector<const char*> menu = {}; // Không hiển thị menu mặc định để hiện danh sách trực tiếp
  wm.setMenu(menu);
  wm.setShowInfoUpdate(false); // Tắt hiển thị thông tin cập nhật dư thừa

  // =========================
  // Auto connect
  // =========================
  if (!wm.autoConnect(apName, apPassword)) {

    Serial.println("Failed to connect to saved WiFi - Starting AP mode");
    apMode = true;

  }
  else {

    Serial.println("Connected to saved WiFi");
    apMode = false;
    updateMQTTConfigFromParams();

  }

  wm.startWebPortal();

  Serial.println("WiFi AP: TrafficLight-AP (for configuration and fallback)");
  Serial.println("Connect to this AP to configure WiFi or MQTT settings");
}









void checkWiFiConnection() {
  unsigned long now = millis();

  if (now - lastWiFiCheck < wifiCheckInterval) {
    return;
  }
  lastWiFiCheck = now;

  wm.process();

  bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

  if (currentlyConnected) {
    if (!wifiConnected) {
      wifiConnected = true;
      apMode = false;

      Serial.println("\n*** WiFi Connected! ***");
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      updateMQTTConfigFromParams();

      Serial.println("MQTT Configuration updated:");
      Serial.print("Server: "); Serial.println(mqttServer);
      Serial.print("Port: "); Serial.println(mqttPort);
      Serial.print("Subscribe: "); Serial.println(mqttTopicSubscribe);
      Serial.print("Publish: "); Serial.println(mqttTopicPublish);

      wifiConnectBlink = true;
      wifiConnectBlinkCount = 0;
      wifiConnectBlinkMillis = now;

      connectMQTT();
    }
    return;
  }

  if (wifiConnected) {
    wifiConnected = false;
    mqttConnected = false;
    Serial.println("*** WiFi Connection Lost! ***");
    Serial.println("AP mode is still active for configuration");
  }

  if (now - wifiReconnectMillis >= wifiReconnectInterval) {
    wifiReconnectMillis = now;

    if (!apMode) {
      Serial.println("Attempting to reconnect WiFi...");
      WiFi.reconnect();
    }
  }
}

void resetWiFiConfig() {
  Serial.println("Resetting WiFi configuration...");
  wm.resetSettings();
  delay(1000);
  Serial.println("Restarting in AP mode...");
  ESP.restart();
}

// ===================================================
// ================= MQTT FUNCTIONS ==================
// ===================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT Message received on topic: ");
  Serial.println(topic);

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Command: ");
  Serial.println(message);

  parseInput(message);

  if (mqttClient.connected()) {
    String response = "ACK: " + message;
    mqttClient.publish(mqttTopicPublish, response.c_str());
  }
}

void connectMQTT() {
  if (!wifiConnected) {
    return;
  }

  if (mqttClient.connected()) {
    mqttConnected = true;
    return;
  }

  Serial.print("Connecting to MQTT broker...");

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(30);

  mqttClientIdStr = getUniqueClientId();

  if (mqttClient.connect(mqttClientIdStr.c_str())) {
    Serial.println(" connected!");
    Serial.print("Client ID: ");
    Serial.println(mqttClientIdStr);

    mqttClient.subscribe(mqttTopicSubscribe);

    Serial.print("Subscribed to topic: ");
    Serial.println(mqttTopicSubscribe);

    String onlineMsg = "ONLINE|IP:" + WiFi.localIP().toString();
    mqttClient.publish(mqttTopicPublish, onlineMsg.c_str());
    Serial.println("📨 Sent ONLINE message");

    mqttConnected = true;

    lastMqttPublish = 0;

    publishStatus();

    mqttConnectBlink = true;
    mqttConnectBlinkCount = 0;
    mqttConnectBlinkMillis = millis();

  } else {
    Serial.print(" failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" retry later");
    mqttConnected = false;
  }
}

void checkMQTTConnection() {
  if (!wifiConnected) {
    mqttConnected = false;
    return;
  }

  unsigned long now = millis();

  if (mqttClient.connected()) {
    if (now - lastMqttLoop >= mqttLoopInterval) {
      lastMqttLoop = now;
      mqttClient.loop();
    }

    if (now - lastMqttPublish >= mqttPublishInterval) {
      lastMqttPublish = now;
      publishStatus();
    }

    mqttConnected = true;
    return;
  }

  if (mqttConnected) {
    mqttConnected = false;
    Serial.println("*** MQTT Connection Lost! ***");
    lastMqttPublish = 0;
  }

  if (now - mqttReconnectMillis >= mqttReconnectInterval) {
    mqttReconnectMillis = now;
    Serial.println("🔄 Attempting MQTT reconnect...");
    connectMQTT();
  }
}

// Hàm publishStatus đã được sửa theo yêu cầu - chỉ giữ mode, times, lights
void publishStatus() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<512> doc;

  // Xác định mode
  if (nightMode) doc["mode"] = "NIGHT";
  else if (priorityMode) doc["mode"] = "PRIORITY";
  else doc["mode"] = "NORMAL";

  // Tạo object times
  JsonObject times = doc.createNestedObject("times");
  times["red"] = redTime;
  times["green"] = greenTime;
  times["yellow"] = yellowTime;

  // Tạo array lights
  JsonArray lights = doc.createNestedArray("lights");
  for (int i = 0; i < 4; i++) {
    JsonObject lightObj = lights.createNestedObject();
    lightObj["id"] = i;
    switch(light[i].state) {
      case RED: lightObj["state"] = "RED"; break;
      case GREEN: lightObj["state"] = "GREEN"; break;
      case YELLOW: lightObj["state"] = "YELLOW"; break;
    }
    lightObj["counter"] = light[i].counter;
  }

  String output;
  serializeJson(doc, output);
  mqttClient.publish(mqttTopicPublish, output.c_str());

  Serial.print("📤 Published: ");
  Serial.println(output);
}

// ===================================================
// ================= RGB LED FUNCTIONS ==============
// ===================================================

void updateRGBLeds(int id, LightState state) {
  if (id == 0) {
    if (state == GREEN) denxanh1();
    else if (state == YELLOW) denvang1();
    else if (state == RED) dendo1();
  } else if (id == 1) {
    if (state == GREEN) denxanh2();
    else if (state == YELLOW) denvang2();
    else if (state == RED) dendo2();
  } else if (id == 2) {
    if (state == GREEN) denxanh3();
    else if (state == YELLOW) denvang3();
    else if (state == RED) dendo3();
  } else if (id == 3) {
    if (state == GREEN) denxanh4();
    else if (state == YELLOW) denvang4();
    else if (state == RED) dendo4();
  }
}

// ===== LED1 =====
void denxanh1() {
  strip1.clear();
  strip1.setPixelColor(0, strip1.Color(0, 255, 0));
  strip1.show();
}

void denvang1() {
  strip1.clear();
  strip1.setPixelColor(1, strip1.Color(255, 150, 0));
  strip1.show();
}

void dendo1() {
  strip1.clear();
  strip1.setPixelColor(2, strip1.Color(255, 0, 0));
  strip1.show();
}

void denvangAll1() {
  for(int i=0; i<LED_COUNT; i++) strip1.setPixelColor(i, strip1.Color(255, 150, 0));
  strip1.show();
}

// ===== LED2 =====
void denxanh2() {
  strip2.clear();
  strip2.setPixelColor(0, strip2.Color(0, 255, 0));
  strip2.show();
}

void denvang2() {
  strip2.clear();
  strip2.setPixelColor(1, strip2.Color(255, 150, 0));
  strip2.show();
}

void dendo2() {
  strip2.clear();
  strip2.setPixelColor(2, strip2.Color(255, 0, 0));
  strip2.show();
}

void denvangAll2() {
  for(int i=0; i<LED_COUNT; i++) strip2.setPixelColor(i, strip2.Color(255, 150, 0));
  strip2.show();
}

// ===== LED3 =====
void denxanh3() {
  strip3.clear();
  strip3.setPixelColor(0, strip3.Color(0, 255, 0));
  strip3.show();
}

void denvang3() {
  strip3.clear();
  strip3.setPixelColor(1, strip3.Color(255, 150, 0));
  strip3.show();
}

void dendo3() {
  strip3.clear();
  strip3.setPixelColor(2, strip3.Color(255, 0, 0));
  strip3.show();
}

void denvangAll3() {
  for(int i=0; i<LED_COUNT; i++) strip3.setPixelColor(i, strip3.Color(255, 150, 0));
  strip3.show();
}

// ===== LED4 =====
void denxanh4() {
  strip4.clear();
  strip4.setPixelColor(0, strip4.Color(0, 255, 0));
  strip4.show();
}

void denvang4() {
  strip4.clear();
  strip4.setPixelColor(1, strip4.Color(255, 150, 0));
  strip4.show();
}

void dendo4() {
  strip4.clear();
  strip4.setPixelColor(2, strip4.Color(255, 0, 0));
  strip4.show();
}

void denvangAll4() {
  for(int i=0; i<LED_COUNT; i++) strip4.setPixelColor(i, strip4.Color(255, 150, 0));
  strip4.show();
}

void clearRGBLed(int id) {
  if (id == 0) {
    strip1.clear();
    strip1.show();
  } else if (id == 1) {
    strip2.clear();
    strip2.show();
  } else if (id == 2) {
    strip3.clear();
    strip3.show();
  } else if (id == 3) {
    strip4.clear();
    strip4.show();
  }
}

// ===================================================
// ================= TM1650 ==========================
// ===================================================

void tm1650_write(int id, uint8_t addr, uint8_t data) {
  int sda, scl;
  if (id == 0) { sda = SDA_PIN1; scl = SCL_PIN1; }
  else if (id == 1) { sda = SDA_PIN2; scl = SCL_PIN2; }
  else if (id == 2) { sda = SDA_PIN3; scl = SCL_PIN3; }
  else if (id == 3) { sda = SDA_PIN4; scl = SCL_PIN4; }
  else return;

  pinMode(sda, OUTPUT);
  pinMode(scl, OUTPUT);

  // Helper lambda for writing a byte
  auto writeByte = [&](uint8_t b) {
      for (int i = 0; i < 8; i++) {
        digitalWrite(sda, (b & 0x80) ? HIGH : LOW);
        b <<= 1;
        delayMicroseconds(5);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);
        digitalWrite(scl, LOW);
        delayMicroseconds(5);
      }
      // ACK Pulse
      pinMode(sda, INPUT_PULLUP);
      delayMicroseconds(5);
      digitalWrite(scl, HIGH);
      delayMicroseconds(5);
      digitalWrite(scl, LOW);
      delayMicroseconds(5);
      pinMode(sda, OUTPUT);
  };

  // Start condition
  digitalWrite(sda, HIGH);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, LOW);
  delayMicroseconds(5);

  writeByte(addr << 1);
  writeByte(data);

  // Stop condition
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
}

void tm1650_init(int id) {
  tm1650_write(id, TM1650_ADDR_SYS, 0x01); // 7-segment mode, display on
  delay(1);
  tm1650_write(id, TM1650_ADDR_DIG1, 0x00); // Clear digit 1
  tm1650_write(id, TM1650_ADDR_DIG2, 0x00); // Clear digit 2
}

void display00Only(int id) {
  tm1650_write(id, TM1650_ADDR_DIG1, segTable[0]);
  tm1650_write(id, TM1650_ADDR_DIG2, segTable[0]);
}

void display00Light(int id) {
  display00Only(id);

  if (id == 0) denvangAll1();
  else if (id == 1) denvangAll2();
  else if (id == 2) denvangAll3();
  else if (id == 3) denvangAll4();
}

void displayNumberOn(int id, uint8_t tens, uint8_t units, int num) {
  if (num < 0) num = 0;

  int t = num / 10;
  int u = num % 10;

  if (num < 10) {
    tm1650_write(id, tens, 0x00); // Tắt chữ số hàng chục
    tm1650_write(id, units, segTable[u]);
  } else {
    tm1650_write(id, tens, segTable[t]);
    tm1650_write(id, units, segTable[u]);
  }
}

void displayLight(int id) {
  displayNumberOn(id, TM1650_ADDR_DIG1, TM1650_ADDR_DIG2, light[id].counter);
  updateRGBLeds(id, light[id].state);
}

void clearLight(int id) {
  tm1650_write(id, TM1650_ADDR_DIG1, 0x00);
  tm1650_write(id, TM1650_ADDR_DIG2, 0x00);
  clearRGBLed(id);
}

// ===================================================
// ============== NIGHT MODE FUNCTIONS ===============
// ===================================================

void setNightModeManual(bool enable) {
  if (enable) {
    // Bật chế độ thủ công - TẮT chế độ tự động
    if (!nightMode) {
      nightMode = true;
      nightModeManual = true;
      autoMode = false;  // Tắt chế độ tự động
      digitalWrite(led, HIGH); // Bật đèn đường khi vào chế độ ban đêm
      Serial.println("NIGHT MODE MANUAL ON - Auto mode disabled");
      if (mqttClient.connected()) {
        mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_MANUAL_ON");
      }
    }
  } else {
    // Tắt chế độ thủ công
    if (nightMode && nightModeManual) {
      nightMode = false;
      nightModeManual = false;
      autoMode = false;  // Tắt luôn chế độ tự động
      darkStartMillis = 0;
      digitalWrite(led, LOW); // Tắt đèn đường khi tắt chế độ ban đêm
      Serial.println("NIGHT MODE MANUAL OFF - Back to normal mode");
      if (mqttClient.connected()) {
        mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_MANUAL_OFF");
      }
      initSystem();
    }
  }
}

// Hàm kiểm tra chế độ ban đêm tự động - CHỈ chạy khi autoMode = true
void checkNightMode() {
  // Nếu không ở chế độ tự động thì bỏ qua
  if (!autoMode) {
    return;
  }

  // Nếu đang ở chế độ thủ công thì cũng bỏ qua (phòng trường hợp)
  if (nightModeManual) {
    return;
  }

  int luxState = digitalRead(senSorLux);
  unsigned long now = millis();

  if (luxState == 1) {
    if (darkStartMillis == 0) {
      darkStartMillis = now;
    }

    if (!nightMode && (now - darkStartMillis >= nightDelay)) {
      nightMode = true;
      digitalWrite(led, HIGH); // Bật đèn đường khi vào chế độ ban đêm tự động
      Serial.println("NIGHT MODE ON (AUTO)");
      if (mqttClient.connected()) {
        mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_AUTO_ON");
      }
    }
  } else {
    darkStartMillis = 0;

    if (nightMode && !nightModeManual) {
      nightMode = false;
      digitalWrite(led, LOW); // Tắt đèn đường khi tắt chế độ ban đêm tự động
      Serial.println("NIGHT MODE OFF (AUTO)");
      if (mqttClient.connected()) {
        mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_AUTO_OFF");
      }
      initSystem();
    }
  }
}

void handleNightMode() {
  static bool blinkState = false;
  static unsigned long blinkMillis = 0;

  unsigned long now = millis();

  if (now - blinkMillis >= blinkInterval) {
    blinkMillis = now;
    blinkState = !blinkState;

    // LED đã được bật trong checkNightMode hoặc setNightModeManual
    // Không cần điều khiển ở đây nữa

    for (int i = 0; i < 4; i++) {
      if (blinkState)
        display00Light(i);
      else
        clearLight(i);
    }
  }
}

// ===================================================
// ================= PRIORITY ========================
// ===================================================

void handlePriority(int id) {
  unsigned long now = millis();

  // Nếu đã chớp nhanh đủ 3 lần (6 lần đổi trạng thái) thì đứng yên
  if (light[id].flashCount >= 6) {
    display00Only(id);
    updateRGBLeds(id, light[id].state);
    return;
  }

  // Chớp nhanh 200ms khi mới vào chế độ ưu tiên
  if (now - light[id].blinkMillis >= 200) {
    light[id].blinkMillis = now;
    light[id].blinkState = !light[id].blinkState;
    light[id].flashCount++;

    if (light[id].blinkState) {
      display00Only(id);
      updateRGBLeds(id, light[id].state);
    } else {
      clearLight(id);
    }
  }
}

// ===================================================
// ================= STATE MACHINE ===================
// ===================================================

void updateStateMachine(int id) {
  if (light[id].counter > 0) return;

  switch (light[id].state) {
    case GREEN:
      light[id].state = YELLOW;
          light[id].counter = yellowTime;
          break;

    case YELLOW:
      light[id].state = RED;
          light[id].counter = redTime;
          break;

    case RED:
      light[id].state = GREEN;
          light[id].counter = greenTime;
          break;
  }
}

void updateLight(int id) {
  if (priorityMode) {
    handlePriority(id);
    return;
  }

  unsigned long now = millis();

  if (now - light[id].previousMillis >= interval) {
    light[id].previousMillis = now;
    displayLight(id);
    light[id].counter--;
    updateStateMachine(id);
  }
}

// ===================================================
// ================= INIT SYSTEM =====================
// ===================================================

void initSystem() {
  priorityMode = false;
  priorityId = -1;

  // Tắt đèn đường khi khởi tạo lại hệ thống
  digitalWrite(led, LOW);

  light[0].state = GREEN;
  light[2].state = GREEN;

  light[1].state = RED;
  light[3].state = RED;

  for (int i = 0; i < 4; i++) {
    if (light[i].state == GREEN)
      light[i].counter = greenTime;
    else
      light[i].counter = redTime;

    light[i].previousMillis = millis() - interval;
    light[i].flashCount = 10; // Đảm bảo không chớp khi khởi tạo lại
    displayLight(i);
  }

  if (mqttClient.connected()) {
    publishStatus();
  }
}

// ===================================================
// ================= SERIAL INPUT ====================
// ===================================================

void parseInput(String input) {
  input.trim();

  if (input == "WIFIRESET") {
    resetWiFiConfig();
    return;
  }

  if (input == "WIFISTATUS") {
    Serial.println("=== WiFi Status ===");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi Connected:");
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi Disconnected");
    }
    Serial.print("AP Mode: ");
    Serial.println(apMode ? "ACTIVE (can configure)" : "STANDBY");
    return;
  }

  if (input == "MQTTSTATUS") {
    if (mqttClient.connected()) {
      Serial.println("MQTT Connected:");
      Serial.print("Server: "); Serial.println(mqttServer);
      Serial.print("Subscribe: "); Serial.println(mqttTopicSubscribe);
      Serial.print("Client ID: "); Serial.println(mqttClientIdStr);
    } else {
      Serial.println("MQTT Disconnected");
    }
    return;
  }

  if (input == "MQTTRECONNECT") {
    connectMQTT();
    return;
  }

  // ===== NIGHT MODE COMMANDS =====
  if (input == "NM") {
    // Bật chế độ ban đêm thủ công
    if (!nightMode || !nightModeManual) {
      setNightModeManual(true);
    } else {
      Serial.println("Night mode manual already ON");
    }
    return;
  }

  if (input == "NMO") {
    // Tắt chế độ ban đêm thủ công - về chế độ bình thường
    Serial.println("NIGHT MODE OFF - SWITCH TO NORMAL MODE");

    nightMode = false;
    nightModeManual = false;
    autoMode = false;  // Tắt chế độ tự động
    darkStartMillis = 0;

    // Tắt đèn đường ngay lập tức
    digitalWrite(led, LOW);

    redTime = 15;
    greenTime = 10;
    yellowTime = 5;

    if (mqttClient.connected()) {
      mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_MANUAL_OFF");
    }

    initSystem();
    return;
  }

  if (input == "NA") {
    // Bật chế độ tự động - đọc cảm biến ánh sáng
    Serial.println("NIGHT MODE AUTO - Sensor reading enabled");

    // Tắt chế độ thủ công nếu đang bật
    if (nightModeManual) {
      nightModeManual = false;
    }

    // Bật chế độ tự động
    autoMode = true;

    // Reset biến đếm
    darkStartMillis = 0;

    // Kiểm tra ngay trạng thái cảm biến
    int luxState = digitalRead(senSorLux);
    if (luxState == 1) {
      Serial.println("Sensor detected dark - NIGHT_MODE_AUTO_ON");
      if (mqttClient.connected()) {
        mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_AUTO_ON");
      }
      if (!nightMode) {
        nightMode = true;
        digitalWrite(led, HIGH); // Bật đèn đường
      }
    } else {
      Serial.println("Sensor detected light - NIGHT_MODE_AUTO_OFF");
      if (mqttClient.connected()) {
        mqttClient.publish(mqttTopicPublish, "STATUS|NIGHT_MODE_AUTO_OFF");
      }
      if (nightMode) {
        nightMode = false;
        digitalWrite(led, LOW); // Tắt đèn đường
        initSystem();
      }
    }
    return;
  }

  if (input.startsWith("P") && input.length() > 1) {
    int id = input.substring(1).toInt();

    if (id >= 0 && id <= 3) {
      // Khi vào chế độ ưu tiên - tắt tất cả chế độ night
      nightMode = false;
      nightModeManual = false;
      autoMode = false;
      darkStartMillis = 0;

      // Tắt đèn đường
      digitalWrite(led, LOW);

      priorityMode = true;
      priorityId = id;
      Serial.println("PRIORITY DIRECTION MODE");
      if (mqttClient.connected()) {
        String msg = "STATUS|PRIORITY_" + String(id);
        mqttClient.publish(mqttTopicPublish, msg.c_str());
      }

      for (int i = 0; i < 4; i++) {
        if (i == id) {
          light[i].state = GREEN;
          light[i].counter = greenTime;
        } else {
          light[i].state = RED;
          light[i].counter = redTime;
        }
        light[i].flashCount = 0;          // Reset đếm số lần chớp
        light[i].blinkMillis = millis();  // Bắt đầu nhịp chớp mới
        light[i].previousMillis = millis() - interval;
      }
    }
    return;
  }

  if (input == "P") {
    // Khi vào chế độ đông xe - tắt tất cả chế độ night
    nightMode = false;
    nightModeManual = false;
    autoMode = false;
    darkStartMillis = 0;

    // Tắt đèn đường
    digitalWrite(led, LOW);

    redTime = 30;
    greenTime = 25;
    yellowTime = 5;
    Serial.println("CHE DO CAO DIEM");
    initSystem();
    return;
  }

  if (input == "L") {
    // Khi vào chế độ ít xe - tắt tất cả chế độ night
    nightMode = false;
    nightModeManual = false;
    autoMode = false;
    darkStartMillis = 0;

    // Tắt đèn đường
    digitalWrite(led, LOW);

    redTime = 12;
    greenTime = 10;
    yellowTime = 2;
    Serial.println("CHE DO THAP DIEM");
    initSystem();
    return;
  }

  if (input == "N") {
    // Khi vào chế độ bình thường - tắt tất cả chế độ night
    nightMode = false;
    nightModeManual = false;
    autoMode = false;
    darkStartMillis = 0;

    // Tắt đèn đường
    digitalWrite(led, LOW);

    redTime = 15;
    greenTime = 10;
    yellowTime = 5;
    Serial.println("CHE DO BINH THUONG");
    initSystem();
    return;
  }

  if (input == "E") {
    priorityMode = false;
    priorityId = -1;
    initSystem();
    Serial.println("PRIORITY OFF");
    return;
  }

  int rIndex = input.indexOf('R');
  int gIndex = input.indexOf('G');
  int yIndex = input.indexOf('Y');

  if (rIndex != -1 && gIndex != -1 && yIndex != -1) {
    // Khi vào chế độ tùy chỉnh - tắt tất cả chế độ night
    nightMode = false;
    nightModeManual = false;
    autoMode = false;
    darkStartMillis = 0;

    // Tắt đèn đường
    digitalWrite(led, LOW);

    int r = input.substring(rIndex + 1, gIndex).toInt();
    int g = input.substring(gIndex + 1, yIndex).toInt();
    int y = input.substring(yIndex + 1).toInt();

    if (r != (g + y)) {
      Serial.println("Red phai = Green + Yellow");
      return;
    }

    redTime = r;
    greenTime = g;
    yellowTime = y;

    Serial.println("CAP NHAT TUY CHINH");
    initSystem();
    return;
  }

  Serial.println("Unknown command!");
}

// ===================================================
// ================= SETUP ===========================
// ===================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=================================");
  Serial.println("Traffic Light System Starting...");
  Serial.println("=================================");

  pinMode(senSorLux, INPUT);
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);

  strip1.begin();
  strip1.show();
  strip2.begin();
  strip2.show();
  strip3.begin();
  strip3.show();
  strip4.begin();
  strip4.show();

  for (int i = 0; i < 4; i++) {
    tm1650_init(i);
  }

  initSystem();
  // BẬT CHẾ ĐỘ TỰ ĐỘNG NGAY KHI KHỞI ĐỘNG
  autoMode = true;
  Serial.println("AUTO MODE ENABLED - System will read light sensor");

  setupWiFi();

  Serial.println("\n*** System Ready ***");
  Serial.println("AP: TrafficLight-AP (always on for configuration)");
  Serial.println("Traffic lights are running independently");
  Serial.println("\nCommands: N, P, L, P0-P3, E, RxxGxxYxx, NM, NMO, NA");
  Serial.println("WiFi: WIFISTATUS, WIFIRESET");
  Serial.println("MQTT: MQTTSTATUS, MQTTRECONNECT");
}

// ===================================================
// ================= LOOP ============================
// ===================================================

void handleBlinks() {
  if (nightMode) return;

  unsigned long now = millis();

  if (wifiConnectBlink) {
    if (now - wifiConnectBlinkMillis >= 100) {
      wifiConnectBlinkMillis = now;
      if (wifiConnectBlinkCount % 2 == 0) {
        digitalWrite(led, HIGH);
      } else {
        digitalWrite(led, LOW);
      }
      wifiConnectBlinkCount++;
      if (wifiConnectBlinkCount >= 6) {
        wifiConnectBlink = false;
        digitalWrite(led, LOW);
      }
    }
  }

  if (mqttConnectBlink) {
    if (now - mqttConnectBlinkMillis >= 100) {
      mqttConnectBlinkMillis = now;
      if (mqttConnectBlinkCount % 2 == 0) {
        digitalWrite(led, HIGH);
      } else {
        digitalWrite(led, LOW);
      }
      mqttConnectBlinkCount++;
      if (mqttConnectBlinkCount >= 10) {
        mqttConnectBlink = false;
        digitalWrite(led, LOW);
      }
    }
  }
}

void loop() {
  if (nightMode) {
    handleNightMode();
  } else {
    for (int i = 0; i < 4; i++) {
      updateLight(i);
    }
  }

  handleBlinks();

  checkWiFiConnection();

  if (wifiConnected) {
    checkMQTTConnection();
  }

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      Serial.print("Received: ");
      Serial.println(input);
      parseInput(input);
    }
  }

  // Chỉ kiểm tra cảm biến khi autoMode = true
  if (autoMode) {
    checkNightMode();
  }

  static unsigned long lastBackupPublish = 0;
  unsigned long now = millis();
  if (mqttClient.connected() && (now - lastBackupPublish >= (mqttPublishInterval + 1000))) {
    if (now - lastMqttPublish > (mqttPublishInterval + 500)) {
      lastBackupPublish = now;
      Serial.println("⚠️ Backup publish triggered (slow publish detected)");
      publishStatus();
      lastMqttPublish = now;
    }
  }

  delay(10);
}