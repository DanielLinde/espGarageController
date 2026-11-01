#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "secrets.h"

// Network credentials live in secrets.h (not committed to git)
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* hostname = "espGarageDoor";

#define HTTP_REST_PORT 80

const int ESP_BUILTIN_LED = 2;
const int GARAGE_BUTTON = D5;

int magnetDoorIsClosed = D7;
int magnetDoorIsOpen = D6;

bool isWifiConnected = false;
String lastDoorPosition = "";
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000; // Check WiFi status every 5 seconds
unsigned long wifiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000; // Base reconnect interval (10 seconds, increases with progressive backoff)
int reconnectAttempts = 0; // Counter for failed attempts (used for progressive backoff)
unsigned long lastLedBlink = 0;
bool ledState = false;

ESP8266WebServer http_rest_server(HTTP_REST_PORT);

void setup() {
  delay(1000);

  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    ESP.restart();
  } 

  isWifiConnected = true;

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  if (MDNS.begin(hostname)) {
    Serial.println("mDNS responder startad");
  }

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(ESP_BUILTIN_LED, OUTPUT);
  pinMode(GARAGE_BUTTON, OUTPUT);
  digitalWrite(GARAGE_BUTTON, HIGH);
  pinMode(magnetDoorIsClosed, INPUT_PULLUP);
  pinMode(magnetDoorIsOpen, INPUT_PULLUP);

  config_rest_server_routing();
  http_rest_server.begin();
  Serial.println("HTTP REST Server Started");
}

void loop() {
  ArduinoOTA.handle();

  // Continuously check WiFi status
  checkWiFiStatus();
  
  if(isWifiConnected) {
    // Connection Exists
    MDNS.update(); // Keep the espGarageDoor.local responder alive
    http_rest_server.handleClient();
    updateDoorPosition();
    // LED on when WiFi is connected (LOW = LED on for ESP8266)
    digitalWrite(ESP_BUILTIN_LED, LOW);
  } else {
    // Not connected - try to reconnect
    attemptWiFiReconnect();
    // Blink LED when trying to reconnect
    unsigned long currentMillis = millis();
    if (currentMillis - lastLedBlink >= 500) {
      ledState = !ledState;
      digitalWrite(ESP_BUILTIN_LED, ledState ? LOW : HIGH);
      lastLedBlink = currentMillis;
    }
    delay(100);
  }
}

// Check WiFi connection status and update isWifiConnected
void checkWiFiStatus() {
  unsigned long currentMillis = millis();
  
  // Check WiFi status periodically
  if (currentMillis - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
    lastWifiCheck = currentMillis;
    
    wl_status_t wifiStatus = WiFi.status();
    
    if (wifiStatus == WL_CONNECTED) {
      if (!isWifiConnected) {
        // Just reconnected
        isWifiConnected = true;
        reconnectAttempts = 0;
        Serial.println("WiFi reconnected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        
        // Restart mDNS if needed
        if (!MDNS.begin(hostname)) {
          Serial.println("Error setting up MDNS responder!");
        }
      }
    } else {
      if (isWifiConnected) {
        // Just disconnected
        isWifiConnected = false;
        Serial.print("WiFi disconnected! Status: ");
        Serial.println(wifiStatus);
      }
    }
  }
}

// Attempt to reconnect to WiFi
void attemptWiFiReconnect() {
  unsigned long currentMillis = millis();
  
  // Calculate dynamic reconnect interval (progressive backoff)
  // Start with 10 seconds, increase by 10 seconds for each failed attempt
  // Max wait time: 60 seconds
  unsigned long reconnectInterval = WIFI_RECONNECT_INTERVAL + (reconnectAttempts * 10000);
  if (reconnectInterval > 60000) {
    reconnectInterval = 60000; // Max 60 seconds between attempts
  }
  
  // Try to reconnect periodically with progressive backoff
  if (currentMillis - wifiReconnectAttempt >= reconnectInterval) {
    wifiReconnectAttempt = currentMillis;
    
    reconnectAttempts++;
    Serial.print("Attempting WiFi reconnect (attempt #");
    Serial.print(reconnectAttempts);
    Serial.print(", wait was ");
    Serial.print(reconnectInterval / 1000);
    Serial.println(" seconds)...");
    
    wl_status_t currentStatus = WiFi.status();
    
    // Only disconnect if we're in a weird state
    if (currentStatus != WL_DISCONNECTED && currentStatus != WL_CONNECTION_LOST) {
      WiFi.disconnect();
      delay(500);
    }
    
    // Start connection attempt
    WiFi.begin(ssid, password);
    
    // Wait for connection with longer timeout (30 seconds)
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 30000) {
      delay(500);
      if ((millis() - startTime) % 5000 < 500) {
        Serial.print(".");
      }
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      isWifiConnected = true;
      reconnectAttempts = 0; // Reset counter on success
      Serial.println("WiFi reconnected successfully!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      
      // Restart mDNS
      MDNS.begin(hostname);
    } else {
      Serial.print("WiFi reconnection failed. Status code: ");
      Serial.println(WiFi.status());
      Serial.print("Next attempt in ");
      Serial.print((reconnectInterval + 10000) / 1000);
      Serial.println(" seconds...");
    }
  }
}

// Updates lastDoorPosition
void updateDoorPosition()
{
  int doorIsClosed = digitalRead(magnetDoorIsClosed); //1 Door is closed
  int doorIsOpen = digitalRead(magnetDoorIsOpen); // Door is open

  if(doorIsClosed == HIGH && doorIsOpen == HIGH) {
    lastDoorPosition = "moving";
  } else if(doorIsClosed == HIGH) {
    // Door is confirmed closed
    lastDoorPosition = "closed";
    // Don't change LED here - it's controlled by WiFi status
  } else if(doorIsOpen == HIGH) {
    // Door is confirmed open
    lastDoorPosition = "open";
    // Don't change LED here - it's controlled by WiFi status
  } else if(doorIsClosed == LOW && doorIsOpen == LOW) {
    // Door is not closed and not open, is moving
    lastDoorPosition = "unknown";
  }
}

String getDoorStatus() {
  updateDoorPosition();
  return lastDoorPosition;
}

void runDoor() {
  digitalWrite(GARAGE_BUTTON, LOW);
  delay(500);
  digitalWrite(GARAGE_BUTTON,HIGH);
}

/**
    REST API ROUTES
*/
void config_rest_server_routing() {
  // Standard route, maybe presenting the available routes?
  http_rest_server.on("/", HTTP_GET, []() {
http_rest_server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html lang='en'>
    <head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>API Routes Overview</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 0; background: #f4f4f4; }
        .container { max-width: 800px; margin: auto; padding: 20px; }
        h1 { text-align: center; color: #333; }
        .api-route { background: #fff; border: 1px solid #ddd; padding: 15px; margin-bottom: 10px; border-radius: 5px; }
        .route-name { font-weight: bold; color: #007BFF; }
        .description { margin: 5px 0; }
    </style>
    </head>
    <body>

    <div class='container'>
        <h1>Available API Routes</h1>
        
        <div class='api-route'>
            <div class='route-name'><a href="/moveDoor">/moveDoor</a></div>
            <div class='description'>Move door to open or closed. This route allows you to control the door's position.</div>
        </div>
        
        <div class='api-route'>
            <div class='route-name'><a href="/doorStatus">/doorStatus</a></div>
            <div class='description'>Retrieve current status of the door. Use this to check if the door is open, moving or closed.</div>
        </div>
        
        <div class='api-route'>
            <div class='route-name'><a href="/deviceInfo">/deviceInfo</a></div>
            <div class='description'>Retrieve basic info from the device. Provides essential information about the device.</div>
        </div>
    </div>

    </body>
    </html>
    )rawliteral");
  });

  // HTTP ROUTES
  http_rest_server.on("/moveDoor", HTTP_GET, moveDoor);
  http_rest_server.on("/doorStatus", HTTP_GET, doorStatus);
  http_rest_server.on("/deviceInfo", HTTP_GET, getDeviceInfo);
}

// -------------------- FUNCTIONS --------------------

// USER ACTIONS TO HANDLE
// Move the door, either open or close
void moveDoor() {
  String status = getDoorStatus();
  runDoor();
  String jsonResponse = sendJsonResponse("moveDoor", "Door operation initiated.", status);
  http_rest_server.send(200, "application/json", jsonResponse);
}

// Retrive the current open state of the door
void doorStatus() {
  String status = getDoorStatus();
  String jsonResponse = sendJsonResponse("doorStatus", "Retrieving door status.", status);
  http_rest_server.send(200, "application/json", jsonResponse);
}

// Retrive basic information of device
void getDeviceInfo() {

  String doorStatus = getDoorStatus();

  // Create a JSON document
  StaticJsonDocument<512> doc;

  // Collect general information
  doc["ipAddress"] = WiFi.localIP().toString();
  doc["macAddress"] = WiFi.macAddress();
  doc["rssi"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  doc["uptime"] = millis() / 1000; // Uppetid i sekunder
  doc["magnetDoorIsOpen"] = digitalRead(magnetDoorIsOpen);
  doc["magnetDoorIsClosed"] = digitalRead(magnetDoorIsClosed);
  doc["doorStatus"] = doorStatus;

  // Serialize and send JSON document
  String jsonString;
  serializeJson(doc, jsonString);
  http_rest_server.send(200, "application/json", jsonString);
}

// HELPERS
// Format response as json
String sendJsonResponse(const String& action, const String& message, const String& doorStatus) {
  StaticJsonDocument<256> doc;
  doc["action"] = action;
  doc["message"] = message;
  doc["doorStatus"] = doorStatus;

  String jsonString;
  serializeJson(doc, jsonString);

  return jsonString;
}