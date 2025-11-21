#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include "Adafruit_TCS34725.h" 
#include <math.h> 

// ================= CONFIGURATION =================
const char* ssid = "samajhdar_phone";
const char* password = "jaishreeram";

// YOUR GOOGLE SCRIPT URL
const char* googleScriptURL = "https://script.google.com/macros/s/AKfycbytzTfJpRDiDah9ZWVcH6q2DK8Xu9Mkm9jPYi3z81zXEtmx-8qG7V5QqjA8f6vtgFHU/exec"; 

// ================= SENSOR PINS =================
#define DHTPIN 4
#define DHTTYPE DHT11
#define ECG_PIN 34        
#define ECG_LO_PLUS 32    
#define ECG_LO_MINUS 27   
#define I2C_SDA_MAX 25
#define I2C_SCL_MAX 26
#define SSR_PIN 33        
#define ONBOARD_LED 2     

// ================= SETTINGS =================
#define TREATMENT_THRESHOLD 10.0 
float white_r = 5600.0; 
float white_g = 2500.0; 
float white_b = 1950.0;

// ================= OBJECTS =================
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
MAX30105 particleSensor;
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_16X);
WebServer server(80);

// ================= VARIABLES =================
float temp = 0;
float humid = 0;
int ecgValue = 0;
int32_t spo2 = 0;
int8_t validSPO2 = 0;
int32_t heartRate = 0;
int8_t validHeartRate = 0;
bool fingerDetected = false;
float bilirubin_score = 0.0;
bool treatmentActive = false;

// --- DASHBOARD BUFFER ---
#define DASHBOARD_BUFFER_SIZE 200 
int dashboardBuffer[DASHBOARD_BUFFER_SIZE];
int dashboardIndex = 0;

// --- ECG FILTER ---
#define FILTER_SIZE 5
int filterBuffer[FILTER_SIZE];
int filterIndex = 0;

// --- BPM SMOOTHING ---
#define AVG_SIZE 5          
int32_t bpmHistory[AVG_SIZE]; 
byte historyIndex = 0;

// SpO2 Buffer
#define MAX_SAMPLES 100
uint32_t irBuffer[MAX_SAMPLES];
uint32_t redBuffer[MAX_SAMPLES];

// Timers
unsigned long lastCloudUpload = 0;
const long cloudInterval = 30000; 
unsigned long lastOledSwitch = 0;
bool showBiliScreen = false; 

// ================= HTML DASHBOARD =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang='en'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <meta http-equiv='refresh' content='5'>
  <title>Health Monitor Dashboard</title>
  <style>
    /* --- RESET & BASE STYLES --- */
    * { box-sizing: border-box; }
    body { 
      font-family: system-ui, -apple-system, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      margin: 0; 
      background-color: #f0f2f5; 
      color: #333;
      display: flex;
      flex-direction: column;
      min-height: 100vh;
    }

    /* --- HEADER --- */
    .header { 
      background: linear-gradient(135deg, #0c6980 0%, #084b5c 100%);
      color: white; 
      padding: 15px 20px; 
      box-shadow: 0 2px 10px rgba(0,0,0,0.15);
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .header h1 { margin: 0; font-size: 1.5rem; font-weight: 600; letter-spacing: 0.5px; }
    .status-dot {
      height: 10px; width: 10px; background-color: #00ff00;
      border-radius: 50%; display: inline-block;
      box-shadow: 0 0 8px #00ff00;
      margin-right: 8px;
      animation: pulse 2s infinite;
    }
    @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }

    /* --- LAYOUT CONTAINER --- */
    .container {
      padding: 20px;
      max-width: 1200px;
      margin: 0 auto;
      width: 100%;
      flex: 1; /* Pushes footer down */
    }

    /* --- SENSOR GRID --- */
    .grid-layout {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
      gap: 20px;
      margin-bottom: 20px;
    }

    /* --- CARDS --- */
    .card { 
      background: white; 
      border-radius: 12px; 
      padding: 20px; 
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
      transition: transform 0.2s ease, box-shadow 0.2s ease;
      border-top: 4px solid #0c6980; /* Default accent */
      position: relative;
      overflow: hidden;
    }
    .card:hover { transform: translateY(-2px); box-shadow: 0 8px 15px rgba(0,0,0,0.1); }
    
    .label { 
      font-size: 0.85rem; 
      text-transform: uppercase; 
      letter-spacing: 1px; 
      color: #666; 
      margin-bottom: 10px; 
      font-weight: 600;
    }
    
    .val { 
      font-size: 2.2rem; 
      font-weight: 700; 
      color: #2d3436; 
    }
    .unit { font-size: 1rem; color: #888; font-weight: 400; margin-left: 4px;}

    /* Specific Accents for visual variety */
    .card.heart { border-color: #e55039; }
    .card.spo2 { border-color: #4a69bd; }
    .card.temp { border-color: #f6b93b; }
    .card.bili { border-color: #e58e26; }

    /* --- GRAPH SECTION --- */
    .graph-card {
      background: white;
      border-radius: 12px;
      padding: 20px;
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
      width: 100%;
    }
    .graph-container {
      position: relative;
      border: 1px solid #eee;
      border-radius: 8px;
      overflow: hidden;
      background-color: #fdfdfd;
      /* Medical Grid Background Effect */
      background-image: 
        linear-gradient(#f0f0f0 1px, transparent 1px),
        linear-gradient(90deg, #f0f0f0 1px, transparent 1px);
      background-size: 20px 20px;
    }
    svg { width: 100%; height: 250px; display: block; }
    polyline { 
      fill: none; 
      stroke: #e55039; /* ECG Red */
      stroke-width: 2.5; 
      vector-effect: non-scaling-stroke;
      stroke-linejoin: round;
    }

    /* --- FOOTER --- */
    .footer { 
      background-color: #2d3436; 
      color: #b2bec3; 
      text-align: center; 
      padding: 25px; 
      font-size: 0.9rem; 
      margin-top: auto;
      border-top: 4px solid #1a1e20;
    }
    .footer strong { color: white; }
    
    /* UPDATED VISIBILITY FOR SIGNATURE */
    .signature { 
      font-family: 'Courier New', monospace; 
      margin-top: 10px; 
      color: #00e5ff; /* Bright Cyan - High Contrast */
      background: rgba(255, 255, 255, 0.05); 
      display: inline-block; 
      padding: 6px 12px; 
      border-radius: 6px;
      font-size: 0.85rem;
      letter-spacing: 0.5px;
      border: 1px solid rgba(0, 229, 255, 0.3); /* Subtle border */
    }
  </style>
</head>
<body>

  <div class='header'>
    <h1>Health Monitor</h1>
    <div style='font-size: 0.8rem; display: flex; align-items: center;'>
      <span class='status-dot'></span> LIVE
    </div>
  </div>

  <div class='container'>
    <div class='grid-layout'>
      
      <div class='card heart'>
        <div class='label'>Heart Rate</div>
        <div class='val'>%BPM%<span class='unit'>bpm</span></div>
      </div>

      <div class='card spo2'>
        <div class='label'>SpO2 Level</div>
        <div class='val'>%SPO2%<span class='unit'>%</span></div>
      </div>

      <div class='card bili'>
        <div class='label'>Bilirubin</div>
        <div class='val'>%BILI%</div>
        <div style='font-size: 0.9rem; color: #e58e26; font-weight: bold; margin-top:5px;'>%THERAPY%</div>
      </div>

      <div class='card temp'>
        <div class='label'>Temperature</div>
        <div class='val'>%TEMP%<span class='unit'>&deg;C</span></div>
      </div>

      <div class='card'>
        <div class='label'>Humidity</div>
        <div class='val'>%HUMID%<span class='unit'>%</span></div>
      </div>

    </div>

    <div class='graph-card'>
      <div class='label'>Live ECG Waveform</div>
      <div class='graph-container'>
        <svg viewBox='0 0 200 4095' preserveAspectRatio='none'>
          <polyline points='%GRAPH_POINTS%' />
        </svg>
      </div>
      <div class='label' style='margin-top: 10px; font-size: 0.75rem; text-align: right;'>Sensor Reading: %ECG%</div>
    </div>
  </div>

  <div class='footer'>
    <div>&copy; 2025 All Rights Reserved &bull; <strong>Mayank & Harsh</strong></div>
    <div class='signature'>&lt; collaboratively engineered by duo /&gt;</div>
  </div>

</body>
</html>
)rawliteral";

// ================= FUNCTIONS =================

int getFilteredECG(int newValue) {
  filterBuffer[filterIndex] = newValue;
  filterIndex = (filterIndex + 1) % FILTER_SIZE;
  long sum = 0;
  for(int i = 0; i < FILTER_SIZE; i++) sum += filterBuffer[i];
  return sum / FILTER_SIZE;
}

int readECG() {
  if((digitalRead(ECG_LO_PLUS) == 1) || (digitalRead(ECG_LO_MINUS) == 1)){
    return 2048; 
  } else {
    int raw = analogRead(ECG_PIN);
    return getFilteredECG(raw); 
  }
}

void handleRoot() {
  String s = index_html;
  if (fingerDetected) {
    s.replace("%BPM%", String(heartRate));
    s.replace("%SPO2%", String(spo2));
  } else {
    s.replace("%BPM%", "--");
    s.replace("%SPO2%", "--");
  }
  s.replace("%TEMP%", String(temp));
  s.replace("%HUMID%", String(humid));
  s.replace("%ECG%", String(ecgValue));
  s.replace("%BILI%", String(bilirubin_score, 1));
  if (treatmentActive) {
    s.replace("%THERAPY%", "<span class='warning'>THERAPY ACTIVE</span>");
  } else {
    s.replace("%THERAPY%", "Normal");
  }

  String points = "";
  points.reserve(1500); 
  for(int i=0; i<DASHBOARD_BUFFER_SIZE; i++){
    int idx = (dashboardIndex + i) % DASHBOARD_BUFFER_SIZE;
    int val = dashboardBuffer[idx];
    points += String(i) + "," + String(4095 - val) + " "; 
  }
  s.replace("%GRAPH_POINTS%", points);
  server.send(200, "text/html", s);
}

void sendToCloud() {
  if(WiFi.status() == WL_CONNECTED){
    WiFiClientSecure client;       
    client.setInsecure();          
    HTTPClient http;
    if (http.begin(client, googleScriptURL)) {
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Content-Type", "application/json");
      StaticJsonDocument<300> doc;
      if (fingerDetected) { doc["bpm"] = heartRate; doc["spo2"] = spo2; } 
      else { doc["bpm"] = 0; doc["spo2"] = 0; }
      doc["temp"] = temp; doc["humid"] = humid; doc["ecg"] = ecgValue;
      doc["bili"] = bilirubin_score; doc["therapy"] = treatmentActive ? "ON" : "OFF";
      String jsonStr; serializeJson(doc, jsonStr);
      
      // Send Post
      int code = http.POST(jsonStr);
      
      // Only print to Serial if ERROR to protect Plotter
      if(code <= 0) { 
        Serial.print("Cloud Error:"); 
        Serial.println(code); 
      }
      http.end();
    }
  }
}

void readBasicSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) humid = h;
}

void readColorSensor() {
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  if (r == 0) r = 1; if (g == 0) g = 1; if (b == 0) b = 1;
  float od_r = -log10((float)r / white_r);
  float od_g = -log10((float)g / white_g);
  float od_b = -log10((float)b / white_b);
  if (od_r < 0) od_r = 0; if (od_g < 0) od_g = 0; if (od_b < 0) od_b = 0;
  float melanin_index = od_r;
  float hemoglobin_index = od_g - od_r;
  if (hemoglobin_index < 0) hemoglobin_index = 0;
  float bilirubin_raw = od_b - (hemoglobin_index * 1.0) - (melanin_index * 1.0);
  bilirubin_score = bilirubin_raw * 100.0;
  if (bilirubin_score < 0) bilirubin_score = 0;
  if (bilirubin_score > TREATMENT_THRESHOLD) {
    digitalWrite(SSR_PIN, HIGH); digitalWrite(ONBOARD_LED, HIGH); treatmentActive = true;
  } else {
    digitalWrite(SSR_PIN, LOW); digitalWrite(ONBOARD_LED, LOW); treatmentActive = false;
  }
}

void updateOLED() {
  if (millis() - lastOledSwitch > 3000) {
    showBiliScreen = !showBiliScreen;
    lastOledSwitch = millis();
  }
  display.clearDisplay();
  if (showBiliScreen) {
    display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
    display.println("BILIRUBIN MONITOR"); display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.setTextSize(2); display.setCursor(0, 20); display.print("BILI: "); display.print(bilirubin_score, 1);
    display.setTextSize(1); display.setCursor(0, 45);
    if (treatmentActive) {
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); display.print(" THERAPY ON "); display.setTextColor(SSD1306_WHITE); 
    } else { display.print(" Therapy Off"); }
    display.setCursor(0, 56); display.print("Temp: "); display.print(temp, 1); display.print("C");
  } else {
    if (!fingerDetected) {
      display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
      display.println("Pulse Oximeter"); display.println("Place finger...");
      display.setCursor(0, 30); display.print("ECG: "); display.print(ecgValue);
    } else {
      display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
      display.println("HEART RATE"); display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      display.setTextSize(3); display.setCursor(20, 15);
      if(heartRate > 40 && heartRate < 200) { if(heartRate < 100) display.print(" "); display.print(heartRate); }
      else { display.setTextSize(2); display.setCursor(0, 20); display.print("Wait..."); }
      display.setTextSize(1); display.setCursor(100, 25); display.println("BPM");
      display.drawLine(0, 38, 128, 38, SSD1306_WHITE);
      display.setTextSize(1); display.setCursor(0, 40); display.println("BLOOD OXYGEN");
      display.setTextSize(2); display.setCursor(35, 48);
      if(validSPO2) { display.print(spo2); display.print("%"); } else { display.print("-- %"); }
    }
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); 
  Wire1.begin(I2C_SDA_MAX, I2C_SCL_MAX); 
  dht.begin();
  pinMode(ECG_LO_PLUS, INPUT); pinMode(ECG_LO_MINUS, INPUT);
  pinMode(SSR_PIN, OUTPUT); pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(SSR_PIN, LOW); digitalWrite(ONBOARD_LED, LOW);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  display.clearDisplay(); display.println("Initializing..."); display.display();

  particleSensor.begin(Wire1, I2C_SPEED_FAST);
  tcs.begin(TCS34725_ADDRESS, &Wire1);
  
  byte ledBrightness = 0x1F; byte sampleAverage = 4; byte ledMode = 2; int sampleRate = 400; int pulseWidth = 411; int adcRange = 4096; 
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(ledBrightness); particleSensor.setPulseAmplitudeIR(ledBrightness);

  for(int i=0; i<AVG_SIZE; i++) bpmHistory[i] = 0;
  for(int i=0; i<DASHBOARD_BUFFER_SIZE; i++) dashboardBuffer[i] = 2048; 
  for(int i=0; i<FILTER_SIZE; i++) filterBuffer[i] = 2048; 

  // --- FIX: SHOW IP ADDRESS BEFORE SPAMMING ---
  display.clearDisplay(); 
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.display();

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int retry = 0;
  // Increase retry to 40 times (20 seconds) to ensure connection
  while (WiFi.status() != WL_CONNECTED && retry < 40) { 
    delay(500); 
    Serial.print("."); 
    retry++; 
  }
  Serial.println("");
  
  if(WiFi.status() == WL_CONNECTED) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("WiFi Connected!");
    
    // SHOW IP BIG
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print(WiFi.localIP());
    display.display();
    
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // *** CRITICAL DELAY ***
    // Keeps IP on screen for 8 seconds before data starts
    delay(8000); 
  } else {
    display.clearDisplay(); 
    display.setTextSize(2);
    display.println("WiFi Failed"); 
    display.setTextSize(1);
    display.println("Offline Mode"); 
    display.display();
    delay(3000);
  }
  
  server.on("/", handleRoot);
  server.begin();
  
  Serial.println("--- STARTING DATA STREAM ---");
}

void loop() {
  for (byte i = 0 ; i < MAX_SAMPLES ; i++) {
    while (particleSensor.available() == false) {
      particleSensor.check();
      server.handleClient();
    }
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
    
    ecgValue = readECG();          
    dashboardBuffer[dashboardIndex] = ecgValue;
    dashboardIndex = (dashboardIndex + 1) % DASHBOARD_BUFFER_SIZE;
    
    // --- PLOTTER FEED (PURE) ---
    Serial.print(0); Serial.print(" "); 
    Serial.print(4095); Serial.print(" "); 
    Serial.println(ecgValue);
    
    server.handleClient();
  }

  long irValue = irBuffer[99];
  if (irValue < 50000) {
    fingerDetected = false;
    for(int i=0; i<AVG_SIZE; i++) bpmHistory[i] = 0;
    historyIndex = 0;
  } else {
    fingerDetected = true;
    int32_t rawBPM; int8_t rawValidBPM;
    maxim_heart_rate_and_oxygen_saturation(irBuffer, MAX_SAMPLES, redBuffer, &spo2, &validSPO2, &rawBPM, &rawValidBPM);
    if (rawValidBPM && rawBPM > 40 && rawBPM < 160) {
       bpmHistory[historyIndex] = rawBPM; historyIndex++;
       if (historyIndex >= AVG_SIZE) historyIndex = 0;
       long sum = 0; byte count = 0;
       for(int i=0; i<AVG_SIZE; i++) { if(bpmHistory[i] > 0) { sum += bpmHistory[i]; count++; } }
       if(count > 0) { heartRate = sum / count; validHeartRate = 1; }
    }
  }

  readBasicSensors();
  readColorSensor();
  updateOLED();

  if (millis() - lastCloudUpload > cloudInterval) {
    sendToCloud();
    lastCloudUpload = millis();
  }
}