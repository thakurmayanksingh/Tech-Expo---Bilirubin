#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Pin Definitions ---
#define PULSE_PIN 34  // Analog pin for pulse sensor

// --- OLED Display Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Signal Processing Variables ---
int signalMin = 4095;
int signalMax = 0;
int threshold = 2048;
int baseline = 2048;

// --- BPM Calculation ---
int bpm = 0;
unsigned long lastBeatTime = 0;
bool beatDetected = false;
int beatCount = 0;
unsigned long startTime = 0;

// --- Moving Average Filter ---
#define FILTER_SAMPLES 10
int filterBuffer[FILTER_SAMPLES];
int filterIndex = 0;
int filteredValue = 0;

// --- Graph Buffer ---
int graphBuffer[SCREEN_WIDTH];
int graphIndex = 0;

// --- Calibration ---
bool calibrated = false;
int calibrationSamples = 0;
#define CALIBRATION_COUNT 200

int getFilteredValue(int raw) {
  filterBuffer[filterIndex] = raw;
  filterIndex = (filterIndex + 1) % FILTER_SAMPLES;
  
  long sum = 0;
  for(int i = 0; i < FILTER_SAMPLES; i++) {
    sum += filterBuffer[i];
  }
  return sum / FILTER_SAMPLES;
}

void setup() {
  Serial.begin(115200);

  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // Initialize buffers
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    graphBuffer[i] = SCREEN_HEIGHT / 2;
  }
  
  for(int i = 0; i < FILTER_SAMPLES; i++) {
    filterBuffer[i] = 2048;
  }

  // Show startup message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Pulse Oximeter");
  display.println("");
  display.println("Place finger on");
  display.println("sensor...");
  display.println("");
  display.println("Calibrating...");
  display.display();
  
  startTime = millis();
  Serial.println("Pulse Sensor Starting...");
}

void loop() {
  // Read raw sensor value
  int rawValue = analogRead(PULSE_PIN);
  
  // Apply moving average filter
  filteredValue = getFilteredValue(rawValue);
  
  // Auto-calibration phase
  if(!calibrated) {
    calibrationSamples++;
    
    if(filteredValue < signalMin) signalMin = filteredValue;
    if(filteredValue > signalMax) signalMax = filteredValue;
    
    if(calibrationSamples >= CALIBRATION_COUNT) {
      calibrated = true;
      baseline = (signalMax + signalMin) / 2;
      threshold = baseline + ((signalMax - baseline) * 0.6); // 60% above baseline
      
      Serial.print("Calibration complete! Min: ");
      Serial.print(signalMin);
      Serial.print(", Max: ");
      Serial.print(signalMax);
      Serial.print(", Threshold: ");
      Serial.println(threshold);
    }
    return;
  }
  
  // Adaptive threshold adjustment
  if(filteredValue < signalMin) signalMin = filteredValue;
  if(filteredValue > signalMax) signalMax = filteredValue;
  
  // Recalculate threshold every 100 beats or 10 seconds
  if(beatCount > 100 || (millis() - startTime) > 10000) {
    baseline = (signalMax + signalMin) / 2;
    threshold = baseline + ((signalMax - baseline) * 0.6);
    beatCount = 0;
    startTime = millis();
    // Gradually reset min/max for adaptation
    signalMin = filteredValue;
    signalMax = filteredValue;
  }
  
  // Beat Detection with hysteresis
  if(filteredValue > threshold && !beatDetected) {
    beatDetected = true;
    unsigned long currentTime = millis();
    unsigned long timeSinceLastBeat = currentTime - lastBeatTime;
    
    // Valid heart rate range: 40-200 BPM (300-1500 ms between beats)
    if(timeSinceLastBeat > 300 && timeSinceLastBeat < 1500) {
      bpm = 60000 / timeSinceLastBeat;
      beatCount++;
    }
    
    lastBeatTime = currentTime;
  } else if(filteredValue < (threshold - 50)) {
    // Hysteresis: signal must drop well below threshold
    beatDetected = false;
  }
  
  // Print to Serial Plotter
  Serial.print("Signal:");
  Serial.print(filteredValue);
  Serial.print(",Threshold:");
  Serial.print(threshold);
  Serial.print(",BPM:");
  Serial.println(bpm * 10); // Scale BPM for visibility
  
  // Map value to screen height
  int y = map(filteredValue, signalMin, signalMax, SCREEN_HEIGHT - 10, 10);
  y = constrain(y, 10, SCREEN_HEIGHT - 10);
  
  // Update graph buffer
  graphBuffer[graphIndex] = y;
  graphIndex = (graphIndex + 1) % SCREEN_WIDTH;
  
  // Clear display
  display.clearDisplay();
  
  // Draw BPM - Large at top
  display.setTextSize(2);
  display.setCursor(0, 0);
  if(bpm > 0 && bpm < 200) {
    display.print(bpm);
  } else {
    display.print("--");
  }
  display.setTextSize(1);
  display.print(" BPM");
  
  // Draw heart icon when beat detected
  if(beatDetected) {
    display.fillCircle(110, 8, 3, SSD1306_WHITE);
    display.fillCircle(118, 8, 3, SSD1306_WHITE);
    display.fillTriangle(107, 10, 121, 10, 114, 16, SSD1306_WHITE);
  }
  
  // Draw signal quality indicator
  int signalQuality = map(signalMax - signalMin, 0, 2000, 0, 100);
  signalQuality = constrain(signalQuality, 0, 100);
  display.setCursor(80, 0);
  display.setTextSize(1);
  if(signalQuality < 20) {
    display.print("WEAK");
  } else if(signalQuality < 50) {
    display.print("OK");
  } else {
    display.print("GOOD");
  }
  
  // Draw waveform
  for(int x = 0; x < SCREEN_WIDTH - 1; x++) {
    int idx1 = (graphIndex + x) % SCREEN_WIDTH;
    int idx2 = (graphIndex + x + 1) % SCREEN_WIDTH;
    display.drawLine(x, graphBuffer[idx1], x + 1, graphBuffer[idx2], SSD1306_WHITE);
  }
  
  // Draw threshold line (dotted)
  int thresholdY = map(threshold, signalMin, signalMax, SCREEN_HEIGHT - 10, 10);
  for(int x = 0; x < SCREEN_WIDTH; x += 4) {
    display.drawPixel(x, thresholdY, SSD1306_WHITE);
  }
  
  // Draw current position indicator
  display.drawLine(graphIndex, 20, graphIndex, SCREEN_HEIGHT - 1, SSD1306_WHITE);
  
  display.display();
  
  delay(20); // 50Hz sampling rate
}