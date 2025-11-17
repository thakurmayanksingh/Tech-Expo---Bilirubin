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

// ================= CONFIGURATION =================
const char* ssid = "samajhdar_phone";
const char* password = "jaishreeram";

// Google Script URL
const char* googleScriptURL = "https://script.google.com/macros/s/AKfycbyPCQ7vLwbMYIpHoMskANdxd4caDdHZzjOEOXM2hx9ihV9J7YYDU9ius4eF97xI2yLCDQ/exec"; 

// ================= SENSOR PINS =================
#define DHTPIN 4
#define DHTTYPE DHT11
#define ECG_PIN 34        
#define ECG_LO_PLUS 32    
#define ECG_LO_MINUS 33   
#define I2C_SDA_MAX 25
#define I2C_SCL_MAX 26

// ================= OBJECTS =================
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
MAX30105 particleSensor;
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

// --- NEW: LARGE CIRCULAR BUFFER FOR DASHBOARD ---
// We need 400 points to show 1 second of data (since sample rate is 400Hz)
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

// ================= HTML DASHBOARD =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="refresh" content="5">
  <title>ESP32 Health Monitor</title>
  <style>
    body { font-family: Arial; text-align: center; margin:0; background-color: #f4f4f4; }
    .header { background-color: #0c6980; color: white; padding: 10px; }
    .card { background: white; padding: 20px; margin: 10px; border-radius: 10px; box-shadow: 2px 2px 10px rgba(0,0,0,0.1); display: inline-block; width: 200px; vertical-align: top; }
    .val { font-size: 2rem; font-weight: bold; color: #0c6980; }
    .label { color: #555; }
    
    /* Graph Styling - Fixed Width/Height */
    .graph-card { width: 90%; max-width: 600px; display: block; margin: 10px auto; background: #fff; padding: 10px;}
    svg { border: 1px solid #ccc; width: 100%; height: 200px; background: #f9f9f9; }
    
    /* The ECG Line Style */
    polyline { 
      fill: none; 
      stroke: #FF0000; /* Red Line */
      stroke-width: 2; 
      vector-effect: non-scaling-stroke;
    }
  </style>
</head>
<body>
  <div class="header"><h1>Health Dashboard</h1></div>
  
  <div class="card">
    <div class="label">Heart Rate</div>
    <div class="val">%BPM% <span style="font-size:1rem">bpm</span></div>
  </div>
  
  <div class="card">
    <div class="label">SpO2</div>
    <div class="val">%SPO2% <span style="font-size:1rem">%</span></div>
  </div>

  <div class="card">
    <div class="label">Temperature</div>
    <div class="val">%TEMP% <span style="font-size:1rem">C</span></div>
  </div>

  <div class="card">
    <div class="label">Humidity</div>
    <div class="val">%HUMID% <span style="font-size:1rem">%</span></div>
  </div>

  <div class="card graph-card">
    <div class="label">ECG Waveform (Live Snapshot)</div>
    <svg viewBox="0 0 200 4095" preserveAspectRatio="none">
      <polyline points="%GRAPH_POINTS%" />
    </svg>
    <div class="label" style="font-size: 0.8rem;">Sensor Value: %ECG%</div>
  </div>

</body>
</html>
)rawliteral";

// ================= FUNCTIONS =================

// 1. ECG Filter Function
int getFilteredECG(int newValue) {
  filterBuffer[filterIndex] = newValue;
  filterIndex = (filterIndex + 1) % FILTER_SIZE;
  long sum = 0;
  for(int i = 0; i < FILTER_SIZE; i++) sum += filterBuffer[i];
  return sum / FILTER_SIZE;
}

// 2. Read ECG Safe
int readECG() {
  if((digitalRead(ECG_LO_PLUS) == 1) || (digitalRead(ECG_LO_MINUS) == 1)){
    return 2048; // Return Mid-Voltage if leads off (Flatline)
  } else {
    int raw = analogRead(ECG_PIN);
    return getFilteredECG(raw); 
  }
}

// 3. Dashboard Handler
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

  // Generate Graph Points
  // We reconstruct the circular buffer into a linear string
  String points = "";
  points.reserve(1500); // Reserve memory to prevent crash
  
  int current = dashboardIndex;
  for(int i=0; i<DASHBOARD_BUFFER_SIZE; i++){
    // Read from the oldest point to the newest point (Circular Buffer Logic)
    int idx = (dashboardIndex + i) % DASHBOARD_BUFFER_SIZE;
    int val = dashboardBuffer[idx];
    
    // Invert Y (4095 - val) so high voltage goes UP
    points += String(i) + "," + String(4095 - val) + " "; 
  }
  s.replace("%GRAPH_POINTS%", points);

  server.send(200, "text/html", s);
}

// 4. Send to Cloud
void sendToCloud() {
  if(WiFi.status() == WL_CONNECTED){
    WiFiClientSecure client;       
    client.setInsecure();          
    HTTPClient http;
    http.begin(client, googleScriptURL); 
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<200> doc;
    if (fingerDetected) {
        doc["bpm"] = heartRate;
        doc["spo2"] = spo2;
    } else {
        doc["bpm"] = 0;
        doc["spo2"] = 0;
    }
    doc["temp"] = temp;
    doc["humid"] = humid;
    doc["ecg"] = ecgValue;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    http.POST(jsonStr);
    http.end();
  }
}

void readBasicSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) humid = h;
}

void updateOLED() {
  display.clearDisplay();

  if (!fingerDetected) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Pulse Oximeter");
    display.println("Place finger");
    display.println("on sensor...");
    display.display();
  } else {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HEART RATE");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    
    display.setTextSize(3);
    display.setCursor(20, 15);
    
    if(heartRate > 40) {
      if(heartRate < 100) display.print(" ");
      display.print(heartRate);
    } else {
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.print("Wait...");
    }
    
    display.setTextSize(1);
    display.setCursor(100, 25);
    display.println("BPM");
    
    display.drawLine(0, 38, 128, 38, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.println("BLOOD OXYGEN");
    
    display.setTextSize(2);
    display.setCursor(35, 48);
    
    if(validSPO2) {
      display.print(spo2);
      display.print("%");
    } else {
      display.print("-- %");
    }
    display.display();
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  Wire.begin(); 
  Wire1.begin(I2C_SDA_MAX, I2C_SCL_MAX); 

  dht.begin();
  pinMode(ECG_LO_PLUS, INPUT);
  pinMode(ECG_LO_MINUS, INPUT);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.println("Initializing...");
  display.display();

  if (!particleSensor.begin(Wire1, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
  }
  
  byte ledBrightness = 0x1F; 
  byte sampleAverage = 4; 
  byte ledMode = 2; 
  int sampleRate = 400; 
  int pulseWidth = 411; 
  int adcRange = 4096; 
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(ledBrightness);
  particleSensor.setPulseAmplitudeIR(ledBrightness);

  // Init Buffers
  for(int i=0; i<AVG_SIZE; i++) bpmHistory[i] = 0;
  for(int i=0; i<DASHBOARD_BUFFER_SIZE; i++) dashboardBuffer[i] = 2048; // Flat line
  for(int i=0; i<FILTER_SIZE; i++) filterBuffer[i] = 2048; 

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println(WiFi.localIP());
  
  display.clearDisplay();
  display.println("WiFi Connected!");
  display.display();
  delay(1000);

  server.on("/", handleRoot);
  server.begin();
}

// ================= LOOP =================
void loop() {
  // 1. Gather Samples (MAX30102 + ECG)
  for (byte i = 0 ; i < MAX_SAMPLES ; i++) {
    while (particleSensor.available() == false) {
      particleSensor.check();
      server.handleClient();
    }
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
    
    // --- ECG LOGIC ---
    ecgValue = readECG();          
    
    // Update Dashboard Circular Buffer
    dashboardBuffer[dashboardIndex] = ecgValue;
    dashboardIndex = (dashboardIndex + 1) % DASHBOARD_BUFFER_SIZE;
    
    // PRINT FOR SERIAL PLOTTER
    // We print Bounds so the graph doesn't auto-scale wildly
    Serial.print("Lower:0,Upper:4095,ECG:");
    Serial.println(ecgValue);
    
    server.handleClient();
  }

  // 2. Finger & Heart Rate Logic
  long irValue = irBuffer[99];
  if (irValue < 50000) {
    fingerDetected = false;
    for(int i=0; i<AVG_SIZE; i++) bpmHistory[i] = 0;
    historyIndex = 0;
  } else {
    fingerDetected = true;
    int32_t rawBPM;
    int8_t rawValidBPM;
    maxim_heart_rate_and_oxygen_saturation(irBuffer, MAX_SAMPLES, redBuffer, &spo2, &validSPO2, &rawBPM, &rawValidBPM);
    
    if (rawValidBPM && rawBPM > 40 && rawBPM < 160) {
       bpmHistory[historyIndex] = rawBPM;
       historyIndex++;
       if (historyIndex >= AVG_SIZE) historyIndex = 0;
       long sum = 0;
       byte count = 0;
       for(int i=0; i<AVG_SIZE; i++) {
         if(bpmHistory[i] > 0) { sum += bpmHistory[i]; count++; }
       }
       if(count > 0) {
         heartRate = sum / count;
         validHeartRate = 1;
       }
    }
  }

  // 3. Read other sensors
  readBasicSensors();

  // 4. Update Displays
  updateOLED();

  // 5. Cloud Log
  if (millis() - lastCloudUpload > cloudInterval) {
    sendToCloud();
    lastCloudUpload = millis();
  }
}