/*
 * ESP32 FPGA Solder Joint Health Monitoring System
 * Real-time voltage monitoring with web dashboard
 * 
 * Requirements:
 * - ESP32 Development Board
 * - ADC pin (GPIO 34) connected to FPGA output voltage
 * - SPIFFS/LittleFS configured for storing web files
 * - Libraries: ESP32 core, AsyncWebServer, ArduinoJson
 * 
 * Installation:
 * 1. Install ESP32 board package in Arduino IDE
 * 2. Install required libraries: AsyncWebServer, ArduinoJson via Library Manager
 * 3. Upload the sketch to ESP32
 * 4. Upload data/index.html to SPIFFS via ESP32 Sketch Data Upload
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ==================== CONFIGURATION ====================
#define SSID "Siddhen's iPhone"           // Change to your WiFi network name
#define PASSWORD "hello2030" 
#define VOLTAGE_PIN 34                  // ADC pin for FPGA voltage input (GPIO 34)
#define SAMPLE_RATE 1000                // Sampling interval in milliseconds
#define VOLTAGE_THRESHOLD 0.5           // Voltage threshold for failure detection (in volts)
#define CALIBRATION_OFFSET 0.0          // Voltage calibration offset
#define CALIBRATION_SCALE 1.0           // Voltage scaling factor

// ==================== GLOBAL VARIABLES ====================
AsyncWebServer server(80);
float currentVoltage = 0.0;
float minVoltage = 5.0;
float maxVoltage = 0.0;
float voltageSum = 0.0;
int sampleCount = 0;
unsigned long lastSampleTime = 0;
bool systemFailed = false;
const int maxDataPoints = 120;         // Store 2 minutes of data (120 * 1 sec)
float voltageHistory[maxDataPoints];
int dataIndex = 0;

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n========================================");
  Serial.println("  FPGA Solder Joint Health Monitor");
  Serial.println("========================================");
  
  // Initialize LittleFS
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("LittleFS Mount Failed!");
    return;
  }
  Serial.println("✓ LittleFS initialized");
  
  // Configure ADC
  analogReadResolution(12);             // 12-bit resolution (0-4095)
  adcAttachPin(VOLTAGE_PIN);
  Serial.println("✓ ADC configured");
  
  // Initialize voltage history
  for (int i = 0; i < maxDataPoints; i++) {
    voltageHistory[i] = 0.0;
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Setup web server routes
  setupWebServer();
  
  Serial.println("\n✓ ESP32 Monitor Ready!");
  Serial.print("Access dashboard at: http://");
  Serial.println(WiFi.localIP());
  Serial.println("========================================\n");
}

// ==================== WIFI CONNECTION ====================
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi Connection Failed!");
    Serial.println("  Check SSID and Password");
  }
}

// ==================== WEB SERVER SETUP ====================
void setupWebServer() {
  // Serve static files (HTML/CSS/JS/assets)
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  
  // API endpoint: return current voltage and system status as JSON
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<256> jsonResponse;
    
    jsonResponse["voltage"] = currentVoltage;
    jsonResponse["min"] = minVoltage;
    jsonResponse["max"] = maxVoltage;
    jsonResponse["avg"] = (sampleCount > 0) ? (voltageSum / sampleCount) : 0.0;
    jsonResponse["status"] = systemFailed ? "FAILED" : "HEALTHY";
    jsonResponse["timestamp"] = millis();
    
    String response;
    serializeJson(jsonResponse, response);
    
    request->send(200, "application/json", response);
  });
  
  // API endpoint: return voltage history for chart
  server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<1024> jsonResponse;
    JsonArray voltages = jsonResponse.createNestedArray("voltages");
    
    // Return data in chronological order
    int startIdx = (dataIndex < maxDataPoints) ? 0 : dataIndex;
    for (int i = 0; i < maxDataPoints; i++) {
      int idx = (startIdx + i) % maxDataPoints;
      voltages.add(voltageHistory[idx]);
    }
    
    String response;
    serializeJson(jsonResponse, response);
    request->send(200, "application/json", response);
  });
  
  // API endpoint: get system status
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<128> jsonResponse;
    jsonResponse["running"] = true;
    jsonResponse["failed"] = systemFailed;
    jsonResponse["uptime"] = millis() / 1000;  // in seconds
    
    String response;
    serializeJson(jsonResponse, response);
    request->send(200, "application/json", response);
  });
  
  // Start server
  server.begin();
  Serial.println("✓ Web server started");
}

// ==================== VOLTAGE SAMPLING ====================
void sampleVoltage() {
  // Read ADC value (0-4095 for 0-3.3V)
  int adcValue = analogRead(VOLTAGE_PIN);
  
  // Convert to voltage: ADC / 4095 * 3.3V * calibration
  currentVoltage = (adcValue / 4095.0) * 3.3 * CALIBRATION_SCALE + CALIBRATION_OFFSET;
  
  // Clamp to valid range
  if (currentVoltage < 0) currentVoltage = 0;
  if (currentVoltage > 3.3) currentVoltage = 3.3;
  
  // Update statistics
  minVoltage = min(minVoltage, currentVoltage);
  maxVoltage = max(maxVoltage, currentVoltage);
  voltageSum += currentVoltage;
  sampleCount++;
  
  // Store in history buffer
  voltageHistory[dataIndex] = currentVoltage;
  dataIndex = (dataIndex + 1) % maxDataPoints;
  
  // Check for failure condition
  if (currentVoltage <= VOLTAGE_THRESHOLD && !systemFailed) {
    systemFailed = true;
    Serial.println("\n⚠️  WARNING: SYSTEM FAILURE DETECTED!");
    Serial.println("   Voltage dropped below threshold!");
  }
  
  // Reset failure if voltage recovers
  if (currentVoltage > VOLTAGE_THRESHOLD && systemFailed) {
    systemFailed = false;
    Serial.println("\n✓ System recovered - voltage restored");
  }
}

// ==================== MAIN LOOP ====================
void loop() {
  // Sample voltage at regular intervals
  if (millis() - lastSampleTime >= SAMPLE_RATE) {
    sampleVoltage();
    lastSampleTime = millis();
    
    // Debug output every 5 samples
    if (sampleCount % 5 == 0) {
      Serial.printf("Voltage: %.3f V | Min: %.3f V | Max: %.3f V | Status: %s\n",
                    currentVoltage, minVoltage, maxVoltage,
                    systemFailed ? "⚠️  FAILED" : "✓ HEALTHY");
    }
  }
  
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Attempting to reconnect...");
    connectToWiFi();
  }
  
  delay(10);  // Small delay to prevent watchdog timeout
}
