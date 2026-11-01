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
<html lang='sv'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>espGarageController</title>
<style>
  :root{
    --bg1:#0f172a; --bg2:#1e293b; --card:rgba(255,255,255,.06);
    --border:rgba(255,255,255,.12); --txt:#e2e8f0; --muted:#94a3b8;
    --accent:#38bdf8; --accent2:#818cf8;
    --open:#f59e0b; --closed:#22c55e; --moving:#38bdf8; --unknown:#94a3b8;
  }
  *{box-sizing:border-box}
  body{margin:0;min-height:100vh;color:var(--txt);padding:32px 16px;
    font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;
    background:radial-gradient(1200px 600px at 20% -10%,#1e3a8a44,transparent),
      radial-gradient(1000px 500px at 100% 0%,#6d28d944,transparent),
      linear-gradient(160deg,var(--bg1),var(--bg2));}
  .wrap{max-width:680px;margin:0 auto}
  header{display:flex;align-items:center;gap:14px;margin-bottom:6px}
  .logo{width:46px;height:46px;border-radius:13px;display:grid;place-items:center;flex:0 0 auto;
    background:linear-gradient(135deg,var(--accent),var(--accent2));box-shadow:0 8px 24px #38bdf855}
  h1{font-size:1.5rem;margin:0;font-weight:700;letter-spacing:-.02em}
  .sub{color:var(--muted);font-size:.9rem;margin:2px 0 0}
  .card{background:var(--card);border:1px solid var(--border);border-radius:18px;padding:20px;margin-top:16px;
    backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
  .status{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap}
  .badge{display:inline-flex;align-items:center;gap:12px;font-weight:700;font-size:1.25rem}
  .dot{width:16px;height:16px;border-radius:50%;box-shadow:0 0 0 5px #ffffff14;transition:background .3s}
  .btn{appearance:none;border:0;cursor:pointer;font:inherit;font-weight:600;color:#06121f;
    background:linear-gradient(135deg,var(--accent),var(--accent2));padding:12px 22px;border-radius:12px;
    box-shadow:0 8px 20px #38bdf844;transition:transform .08s ease}
  .btn:hover{transform:translateY(-1px)} .btn:active{transform:translateY(1px)}
  h2{font-size:.78rem;text-transform:uppercase;letter-spacing:.09em;color:var(--muted);margin:0 0 10px}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:12px}
  .stat{background:rgba(255,255,255,.04);border:1px solid var(--border);border-radius:12px;padding:12px 14px}
  .stat .k{color:var(--muted);font-size:.7rem;text-transform:uppercase;letter-spacing:.06em}
  .stat .v{font-size:1.05rem;font-weight:600;margin-top:4px;word-break:break-all}
  .route{display:flex;align-items:center;gap:12px;padding:12px 0;border-bottom:1px solid var(--border)}
  .route:last-child{border-bottom:0}
  .method{font-size:.68rem;font-weight:700;color:var(--accent);border:1px solid var(--accent);border-radius:6px;padding:3px 8px}
  .route a{color:var(--txt);text-decoration:none;font-weight:600}
  .route a:hover{color:var(--accent)}
  .route .desc{color:var(--muted);font-size:.84rem;margin-top:2px}
  footer{text-align:center;color:var(--muted);font-size:.76rem;margin-top:22px}
</style>
</head>
<body>
<div class='wrap'>
  <header>
    <div class='logo'>
      <svg width='26' height='26' viewBox='0 0 24 24' fill='none' stroke='#06121f' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M3 10l9-6 9 6v11H3z'/><path d='M3 14h18M3 18h18'/></svg>
    </div>
    <div>
      <h1>espGarageController</h1>
      <p class='sub'>ESP8266 &middot; smart garageportstyrning</p>
    </div>
  </header>

  <div class='card'>
    <div class='status'>
      <span class='badge'><span class='dot' id='dot' style='background:var(--unknown)'></span><span id='doorState'>Laddar...</span></span>
      <button class='btn' id='moveBtn'>Kör porten</button>
    </div>
  </div>

  <div class='card'>
    <h2>Enhet</h2>
    <div class='grid'>
      <div class='stat'><div class='k'>IP</div><div class='v' id='ip'>-</div></div>
      <div class='stat'><div class='k'>Signal</div><div class='v' id='rssi'>-</div></div>
      <div class='stat'><div class='k'>Nat</div><div class='v' id='ssid'>-</div></div>
      <div class='stat'><div class='k'>Upptid</div><div class='v' id='uptime'>-</div></div>
    </div>
  </div>

  <div class='card'>
    <h2>API-rutter</h2>
    <div class='route'><span class='method'>GET</span><div><a href='/moveDoor'>/moveDoor</a><div class='desc'>Oppna eller stang porten.</div></div></div>
    <div class='route'><span class='method'>GET</span><div><a href='/doorStatus'>/doorStatus</a><div class='desc'>Hamta portens aktuella status.</div></div></div>
    <div class='route'><span class='method'>GET</span><div><a href='/deviceInfo'>/deviceInfo</a><div class='desc'>Enhetsinformation (IP, signal, sensorer).</div></div></div>
  </div>

  <footer>Live-status uppdateras automatiskt</footer>
</div>

<script>
  var COLORS={open:'var(--open)',closed:'var(--closed)',moving:'var(--moving)',unknown:'var(--unknown)'};
  var LABEL={open:'Oppen',closed:'Stangd',moving:'Ror sig',unknown:'Okand'};
  function $(id){return document.getElementById(id);}
  function refresh(){
    fetch('/deviceInfo',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
      var s=(d.doorStatus||'unknown').toLowerCase();
      $('doorState').textContent=LABEL[s]||s;
      $('dot').style.background=COLORS[s]||COLORS.unknown;
      $('ip').textContent=d.ipAddress||'-';
      $('rssi').textContent=(d.rssi!=null?d.rssi+' dBm':'-');
      $('ssid').textContent=d.ssid||'-';
      var u=d.uptime||0, h=Math.floor(u/3600), m=Math.floor((u%3600)/60);
      $('uptime').textContent=h+'h '+m+'m';
    }).catch(function(){ $('doorState').textContent='Offline'; });
  }
  $('moveBtn').addEventListener('click',function(){
    if(!confirm('Kor porten nu?'))return;
    fetch('/moveDoor',{cache:'no-store'}).catch(function(){});
    setTimeout(refresh,800);
  });
  refresh(); setInterval(refresh,3000);
</script>
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