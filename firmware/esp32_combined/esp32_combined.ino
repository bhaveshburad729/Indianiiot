#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ==========================================
// 1. NETWORK & API CONFIGURATION
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";         // REPLACE WITH YOUR WIFI NAME
const char* password = "YOUR_WIFI_PASSWORD"; // REPLACE WITH YOUR WIFI PASSWORD

// HOSTED SERVER URL 
// (Ensure this matches your production Render URL)
const char* apiBase = "https://mq-gas-censor-sensegrid-api-tronix.onrender.com/api/v1";

// ==========================================
// 2. DEVICE CONFIGURATION
// ==========================================
// REPLACE WITH YOUR FUSION DEVICE ID AND TOKEN
const char* deviceId = "YOUR_FUSION_DEVICE_ID"; 
const char* deviceToken = "YOUR_DEVICE_TOKEN"; 

// ==========================================
// 3. PIN DEFINITIONS
// ==========================================
// --- Gas & Environment ---
#define PIN_MQ135 34        // Analog Input (Gas)
#define PIN_TRIG 5          // Ultrasonic Trig
#define PIN_ECHO 18         // Ultrasonic Echo

// --- LDR & Light ---
#define PIN_LDR_ANALOG 36   // Analog Input (Light Intensity) 
#define PIN_LDR_DIGITAL 35  // Digital Input (Threshold)
#define PIN_AUTO_BULB 25    // PWM Output (LED/Bulb)

// ==========================================
// 4. GLOBALS
// ==========================================
unsigned long lastGasTime = 0;
unsigned long lastLdrTime = 0;
unsigned long lastPollTime = 0;

// Intervals (ms)
const long intervalGas = 2000;  // Send Gas Data every 2s
const long intervalLdr = 2000;  // Send LDR Data every 2s
const long intervalPoll = 1000; // Poll Outputs every 1s

void setup() {
  Serial.begin(115200);

  // --- Pin Modes ---
  pinMode(PIN_MQ135, INPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  
  pinMode(PIN_LDR_ANALOG, INPUT);
  pinMode(PIN_LDR_DIGITAL, INPUT);
  
  // --- PWM Setup (Auto Bulb) ---
  // ESP32 Core v3.0+ uses ledcAttach based on pin, or legacy ledcSetup
  // Using simple attach for broader compatibility or check core version
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_AUTO_BULB, 5000, 10); // Pin, Freq, Resolution
  #else
    ledcSetup(0, 5000, 10); // Channel 0, 5000Hz, 10-bit
    ledcAttachPin(PIN_AUTO_BULB, 0);
  #endif
  
  // Initialize Bulb Off
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
     ledcWrite(PIN_AUTO_BULB, 0);
  #else
     ledcWrite(0, 0);
  #endif

  // --- WiFi Connection ---
  WiFi.mode(WIFI_STA); // Station Mode
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed! Check SSID/Password.");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long currentMillis = millis();

    // --- 1. LDR LOCAL LOGIC (Fast Response) ---
    int rawLdr = analogRead(PIN_LDR_ANALOG);
    // Map 12-bit ADC (0-4095) to 10-bit PWM (0-1023)
    int pwmVal = map(rawLdr, 0, 4095, 0, 1023);
    
    // Write to Bulb
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
       ledcWrite(PIN_AUTO_BULB, pwmVal);
    #else
       ledcWrite(0, pwmVal);
    #endif

    // --- 2. SEND GAS DATA ---
    if (currentMillis - lastGasTime >= intervalGas) {
      lastGasTime = currentMillis;
      sendGasData();
    }

    // --- 3. SEND LDR DATA ---
    if (currentMillis - lastLdrTime >= intervalLdr) {
      lastLdrTime = currentMillis;
      // Send CURRENT values
      sendLdrData(digitalRead(PIN_LDR_DIGITAL), pwmVal); 
    }

    // --- 4. POLL OUTPUTS ---
    if (currentMillis - lastPollTime >= intervalPoll) {
      lastPollTime = currentMillis;
      pollOutputs();
    }

  } else {
    // Reconnect if lost
    Serial.println("WiFi Disconnected. Reconnecting...");
    WiFi.reconnect();
    delay(2000);
  }
}

// -----------------------------------------------------
// HTTP HELPER (Simplified for robustness)
// -----------------------------------------------------
void postData(String endpoint, String payload) {
  WiFiClientSecure client; // Using Secure Client for HTTPS
  client.setInsecure();
  HTTPClient http;

  String url = String(apiBase) + endpoint;
  
  // Begin Connection
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Device-Token", deviceToken);
    
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
      Serial.printf("[HTTP] POST %s -> %d\n", endpoint.c_str(), httpCode);
      if (httpCode != 200) {
        String response = http.getString();
        Serial.println("Error Response: " + response);
      }
    } else {
      Serial.printf("[HTTP] POST %s Failed: %s\n", endpoint.c_str(), http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect to %s\n", url.c_str());
  }
}

// -----------------------------------------------------
// TASK: SEND GAS/ENV DATA
// Endpoint: /ingest
// -----------------------------------------------------
void sendGasData() {
  // Read Sensors
  int gasRaw = analogRead(PIN_MQ135);
  
  // UltraSonic
  digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long duration = pulseIn(PIN_ECHO, HIGH);
  float distance = duration * 0.034 / 2;

  // Placeholder Temp (Add DHT logic if physical sensor exists)
  float temp = 28.0; 
  float hum = 65.0;

  // JSON
  StaticJsonDocument<256> doc;
  doc["device_id"] = deviceId;
  doc["gas"] = gasRaw;
  doc["temperature"] = temp;
  doc["humidity"] = hum;
  doc["distance"] = distance;

  String json;
  serializeJson(doc, json);

  postData("/ingest", json);
}

// -----------------------------------------------------
// TASK: SEND LDR DATA
// Endpoint: /ldr/{id}/readings
// -----------------------------------------------------
void sendLdrData(int digitalVal, int analogVal) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = deviceId;
  doc["digital_value"] = (digitalVal == HIGH); // Convert to boolean
  doc["analog_value"] = analogVal;             // Send scaled or raw value

  String json;
  serializeJson(doc, json);

  String endpoint = String("/ldr/") + String(deviceId) + "/readings";
  postData(endpoint, json);
}

// -----------------------------------------------------
// TASK: POLL OUTPUTS
// Endpoint: /ldr/{id}/outputs
// -----------------------------------------------------
void pollOutputs() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = String(apiBase) + "/ldr/" + String(deviceId) + "/outputs";

  if (http.begin(client, url)) {
    http.addHeader("Device-Token", deviceToken);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        JsonArray outputs = doc.as<JsonArray>();
        for (JsonObject out : outputs) {
          int gpio = out["gpio_pin"];
          bool active = out["is_active"];
          
          // Safety Check: Don't toggle sensor pins
          if (gpio != PIN_MQ135 && gpio != PIN_LDR_ANALOG && gpio != PIN_AUTO_BULB) {
              pinMode(gpio, OUTPUT);
              digitalWrite(gpio, active ? HIGH : LOW);
          }
        }
      } else {
        Serial.print("JSON Parse Error: ");
        Serial.println(error.c_str());
      }
    } else {
      Serial.printf("[HTTP] GET Outputs Failed: %d\n", httpCode);
    }
    http.end();
  }
}

