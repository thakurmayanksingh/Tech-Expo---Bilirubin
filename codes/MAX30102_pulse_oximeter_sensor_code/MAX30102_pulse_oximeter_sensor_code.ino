#include <Wire.h>
#include "MAX30105.h"         // SparkFun's library (works for MAX30102)
// --- FIX: Remove heartRate.h, add the full SpO2 algorithm ---
#include "spo2_algorithm.h"   
#include <Adafruit_GFX.h>     // For the OLED
#include <Adafruit_SSD1306.h> // For the OLED

// --- NEW: Define pins for the second I2C bus ---
#define I2C_SDA_MAX 25
#define I2C_SCL_MAX 26

// --- NEW: Create a second I2C object ---
// Wire (bus 0) will be for the OLED (pins 21, 22)
// Wire1 (bus 1) will be for the MAX30102 (pins 25, 26)
// TwoWire Wire1 = TwoWire(1); // <-- FIX: Remove this line. Wire1 is already defined by the ESP32 library.

// --- OLED Display Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
// This display constructor uses the default 'Wire' object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Pulse Oximeter Setup ---
MAX30105 particleSensor;

// --- FIX: Remove old BPM variables ---
// const byte RATE_SIZE = 4; 
// ... (all old beatAvg variables removed)

// --- NEW: Variables for SpO2 and new BPM calculation ---
#define MAX_SAMPLES 100 // Gather 100 samples
uint32_t irBuffer[MAX_SAMPLES];
uint32_t redBuffer[MAX_SAMPLES];
int32_t bufferLength = MAX_SAMPLES;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate; // This will be our new BPM
int8_t validHeartRate;

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing MAX30102 Pulse Oximeter...");

  // Initialize I2C
  Wire.begin(); // Starts I2C bus 0 for OLED (pins 21, 22)
  // NEW: Start I2C bus 1 for MAX sensor on custom pins
  Wire1.begin(I2C_SDA_MAX, I2C_SCL_MAX); 

  // Initialize OLED (uses default 'Wire')
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // Initialize Pulse Oximeter
  // NEW: Pass 'Wire1' to the begin function
  if (!particleSensor.begin(Wire1, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 Sensor not found.");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("MAX30102 Sensor");
    display.println("Not Found!");
    display.println("Check wiring.");
    display.display();
    while (1);
  }
  Serial.println("Place your finger on the sensor.");

  // --- FIX: Configure sensor for SpO2 Mode (Red + IR) ---
  byte ledBrightness = 0x1F; // 31 (0-255)
  byte sampleAverage = 4;    // 1, 2, 4, 8, 16, 32
  byte ledMode = 2;          // 1=Red, 2=Red+IR, 3=Red+IR+Green
  int sampleRate = 400;      // 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;      // 69, 118, 215, 411
  int adcRange = 4096;       // 2048, 4096, 8192, 16384
  
  // Set up the sensor
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  
  // Set amplitudes for both LEDs
  particleSensor.setPulseAmplitudeRed(ledBrightness);
  particleSensor.setPulseAmplitudeIR(ledBrightness);

  // Show startup message on OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Pulse Oximeter");
  display.println("Place finger");
  display.println("on sensor...");
  display.display();
}

void loop() {
  
  // --- 1. Fill the sample buffers ---
  // This gathers 100 samples before calculating.
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    // Wait for new data
    while (particleSensor.available() == false)
      particleSensor.check(); // Check the sensor
    
    // Read both Red and IR
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
  }

  // --- 2. Calculate SpO2 and BPM from the 100-sample buffer ---
  // This algorithm analyzes the "blips" in both Red and IR light.
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  // --- 3. Check for finger ---
  // Use the *last* IR value to check.
  if (irBuffer[99] < 50000) {
    // No finger detected
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Pulse Oximeter");
    display.println("Place finger");
    display.println("on sensor...");
    display.display();
    
    Serial.println("No finger detected.");
    return;
  }

  // --- 4. Display on OLED ---
  display.clearDisplay();
  
  // Heart Rate Display
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("HEART RATE");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  display.setTextSize(3);
  display.setCursor(20, 15);
  
  // Only display if the algorithm says the BPM is valid
  if(validHeartRate) {
    if(heartRate < 100) display.print(" ");
    display.print(heartRate);
  } else {
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print("SEARCHING");
  }
  
  display.setTextSize(1);
  display.setCursor(100, 25);
  display.println("BPM");
  
  // --- FIX: SpO2 Display (now with REAL data) ---
  display.drawLine(0, 38, 128, 38, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println("BLOOD OXYGEN");
  
  display.setTextSize(2);
  display.setCursor(35, 48);
  
  // Only display if the algorithm says the SpO2 is valid
  if(validSPO2) {
    display.print(spo2);
    display.print("%");
  } else {
    display.print("-- %");
  }
  
  display.display();

  // --- 5. Print to Serial Monitor for debugging ---
  Serial.print("IR="); Serial.print(irBuffer[99]);
  Serial.print(", BPM="); Serial.print(heartRate);
  Serial.print(", ValidBPM="); Serial.print(validHeartRate);
  Serial.print(", SpO2="); Serial.print(spo2);
  Serial.print(", ValidSpO2="); Serial.println(validSPO2);
}