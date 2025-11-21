#include <Wire.h>
#include "Adafruit_TCS34725.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h> // For log10 calculation

// --- Pin Definitions ---
#define TCS_SDA 25
#define TCS_SCL 26

// --- THERAPY CONTROL PINS ---
// Pin 33: Connect this to your Solid State Relay (SSR) Input +
#define SSR_PIN 33 
// Pin 2: The built-in Blue LED on the ESP32 (Visual Indicator)
#define ONBOARD_LED 2 

// --- THERAPY SETTINGS ---
// MEDICAL CONTEXT: 
// Normal bilirubin is < 5 mg/dL.
// Phototherapy typically begins around 10-12 mg/dL for newborns.
// We set our threshold to 10.0 to simulate this medical standard.
#define TREATMENT_THRESHOLD 10.0 

// --- OLED Display Setup (Default I2C) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Color Sensor Setup ---
// 50ms integration time provides faster updates
// Gain 16x is increased because skin reflects less light than a white paper
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_16X);

// --- CALIBRATION VALUES ---
// Your measured white paper values
float white_r = 5600.0; 
float white_g = 2500.0; 
float white_b = 1950.0;

// --- COMPENSATION FACTORS ---
float k_hemoglobin = 0.5; 
float k_melanin = 0.2;    

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing TCS34725 for Bilirubin Analysis...");

  // --- Initialize Control Pins ---
  pinMode(SSR_PIN, OUTPUT);
  pinMode(ONBOARD_LED, OUTPUT);
  
  // Start with everything OFF
  digitalWrite(SSR_PIN, LOW); 
  digitalWrite(ONBOARD_LED, LOW);

  // --- Initialize OLED (Bus 0: Pins 21, 22) ---
  Wire.begin(); 
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // --- Initialize Color Sensor (Bus 1: Pins 25, 26) ---
  Wire1.begin(TCS_SDA, TCS_SCL);

  if (!tcs.begin(TCS34725_ADDRESS, &Wire1)) {
    Serial.println("No TCS34725 found");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Sensor Not Found");
    display.display();
    while (1);
  }

  // Show startup message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Automated Phototherapy");
  display.println("System Ready");
  display.display();
  delay(1000);
}

void loop() {
  uint16_t r, g, b, c;
  
  // Read raw data
  tcs.getRawData(&r, &g, &b, &c);

  // Prevent division by zero
  if (r == 0) r = 1;
  if (g == 0) g = 1;
  if (b == 0) b = 1;

  // --- 1. Calculate OPTICAL DENSITY (Absorbance) ---
  float od_r = -log10((float)r / white_r);
  float od_g = -log10((float)g / white_g);
  float od_b = -log10((float)b / white_b);

  if (od_r < 0) od_r = 0;
  if (od_g < 0) od_g = 0;
  if (od_b < 0) od_b = 0;

  // --- 2. Component Analysis ---
  float melanin_index = od_r;
  float hemoglobin_index = od_g - od_r;
  if (hemoglobin_index < 0) hemoglobin_index = 0;

  // BILIRUBIN (Jaundice)
  float bilirubin_raw = od_b - (hemoglobin_index * 1.0) - (melanin_index * 1.0);
  float bilirubin_score = bilirubin_raw * 100.0;
  if (bilirubin_score < 0) bilirubin_score = 0;

  // --- 3. Phototherapy Control Logic ---
  bool treatmentActive = false;
  
  if (bilirubin_score > TREATMENT_THRESHOLD) {
    // CONDITION MET: Turn ON Therapy
    digitalWrite(SSR_PIN, HIGH);     // Triggers Relay
    digitalWrite(ONBOARD_LED, HIGH); // Visual check on ESP32
    treatmentActive = true;
  } else {
    // CONDITION SAFE: Turn OFF Therapy
    digitalWrite(SSR_PIN, LOW);      // Cuts Relay
    digitalWrite(ONBOARD_LED, LOW);  // Visual check off
    treatmentActive = false;
  }

  // --- Display on OLED ---
  display.clearDisplay();
  
  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("BILIRUBIN MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Display Components
  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print("Melanin: "); display.print(melanin_index, 2);
  display.setCursor(0, 25);
  display.print("Heme:    "); display.print(hemoglobin_index, 2);

  // --- Display Bilirubin Score ---
  display.setTextSize(2);
  display.setCursor(0, 38);
  display.print("BILI: ");
  display.print(bilirubin_score, 1);

  // --- Therapy Status Indicator ---
  display.setTextSize(1);
  display.setCursor(0, 56);
  if (treatmentActive) {
    // Inverted text for visibility
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
    display.print(" THERAPY ON ");
    display.setTextColor(SSD1306_WHITE); 
  } else {
    display.print(" Therapy Off");
  }

  display.display();

  // --- Serial Output ---
  Serial.print("BiliScore:"); Serial.print(bilirubin_score, 3);
  Serial.print(",RelayState:"); Serial.println(treatmentActive ? 10 : 0);
  
  delay(500);
}