#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

// Replace with your network credentials
const char* ssid = "WifiNetworkName";
const char* password = "wifiPassword";
const char* hostname = "DeviceHostname";

#define HTTP_REST_PORT 80

const int ESP_BUILTIN_LED = 2;
const int GARAGE_BUTTON = D5;

int magnetDoorIsClosed = D7;
int magnetDoorIsOpen = D6;

bool isWifiConnected = false;
String lastDoorPosition = "";

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
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(ESP_BUILTIN_LED, OUTPUT);
  digitalWrite(GARAGE_BUTTON, HIGH);

  pinMode(GARAGE_BUTTON, OUTPUT);
  pinMode(magnetDoorIsClosed, INPUT_PULLUP);
  pinMode(magnetDoorIsOpen, INPUT_PULLUP);

  config_rest_server_routing();
  http_rest_server.begin();
  Serial.println("HTTP REST Server Started");
}

void loop() {
  ArduinoOTA.handle();

  if(isWifiConnected) {
    // Connection Exists
      http_rest_server.handleClient();
      updateDoorPosition();
      delay(500);
  } else {
    // Not connected

  }

}

// Updates lastDoorPosition
void updateDoorPosition()
{
  int doorIsClosed = digitalRead(magnetDoorIsClosed); //1 Dörren är stängd
  int doorIsOpen = digitalRead(magnetDoorIsOpen); // 1 Dörren är öppen

  if(doorIsClosed == HIGH && doorIsOpen == HIGH) {
    lastDoorPosition = "moving";
  } else if(doorIsClosed == HIGH) {
    // Door is confimerd closed
    lastDoorPosition = "closed";
    digitalWrite(ESP_BUILTIN_LED, LOW);
  } else if(doorIsOpen == HIGH) {
    // Door is confirmed open
    lastDoorPosition = "open";
    digitalWrite(ESP_BUILTIN_LED, LOW); // LED on if door is open
  } else if(doorIsClosed == LOW && doorIsOpen == LOW) {
    // Door is not closed and not open, is moving
    lastDoorPosition = "unkown";
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
  String status = getDoorStatus(); // Använd din befintliga funktion för att få status som en sträng
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



