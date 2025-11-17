#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// AD8232 Pin definitions
#define ECG_OUTPUT 34  // Analog pin for ECG signal
#define LO_PLUS 32     // Leads-off detection +
#define LO_MINUS 33    // Leads-off detection -

// Graph settings
#define GRAPH_HEIGHT 48
#define GRAPH_Y_START 16
int ecgBuffer[SCREEN_WIDTH];  // Buffer to store ECG values
int bufferIndex = 0;

// Signal processing
int baseline = 2048;  // Mid-point of 12-bit ADC
int minVal = 4095;
int maxVal = 0;
int sampleCount = 0;

// Simple moving average filter
#define FILTER_SIZE 5
int filterBuffer[FILTER_SIZE];
int filterIndex = 0;

int getFilteredValue(int newValue) {
  filterBuffer[filterIndex] = newValue;
  filterIndex = (filterIndex + 1) % FILTER_SIZE;
  
  long sum = 0;
  for(int i = 0; i < FILTER_SIZE; i++) {
    sum += filterBuffer[i];
  }
  return sum / FILTER_SIZE;
}

void setup() {
  Serial.begin(115200);
  
  // Initialize AD8232 pins
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);
  
  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ECG Monitor");
  display.println("Calibrating...");
  display.display();
  
  // Initialize filter buffer
  for(int i = 0; i < FILTER_SIZE; i++) {
    filterBuffer[i] = 2048;
  }
  
  // Calibrate baseline - read 100 samples
  long sum = 0;
  for(int i = 0; i < 100; i++) {
    sum += analogRead(ECG_OUTPUT);
    delay(10);
  }
  baseline = sum / 100;
  
  // Initialize buffer with center values
  for(int i = 0; i < SCREEN_WIDTH; i++) {
    ecgBuffer[i] = GRAPH_Y_START + GRAPH_HEIGHT / 2;
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Ready!");
  display.print("Baseline: ");
  display.println(baseline);
  display.display();
  delay(1000);
  
  Serial.println("ECG Monitor Started");
  Serial.print("Baseline: ");
  Serial.println(baseline);
}

void loop() {
  // Check if leads are properly connected
  if((digitalRead(LO_PLUS) == 1) || (digitalRead(LO_MINUS) == 1)) {
    // Leads off detected
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("LEADS");
    display.println("  OFF!");
    display.setTextSize(1);
    display.println("");
    display.println("Check electrodes");
    display.display();
    Serial.println("Leads off!");
    delay(100);
    return;
  }
  
  // Read and filter ECG value
  int rawValue = analogRead(ECG_OUTPUT);
  int ecgValue = getFilteredValue(rawValue);
  
  // Track min/max for auto-scaling
  sampleCount++;
  if(ecgValue < minVal) minVal = ecgValue;
  if(ecgValue > maxVal) maxVal = ecgValue;
  
  // Reset min/max every 500 samples for adaptive scaling
  if(sampleCount >= 500) {
    sampleCount = 0;
    minVal = 4095;
    maxVal = 0;
  }
  
  // Print to Serial for Serial Plotter
  Serial.println(ecgValue);
  
  // Map ECG value to graph height with adaptive scaling
  int range = maxVal - minVal;
  if(range < 100) range = 100;  // Minimum range to avoid division issues
  
  int yPos = map(ecgValue, minVal, maxVal, GRAPH_Y_START + GRAPH_HEIGHT - 2, GRAPH_Y_START + 2);
  yPos = constrain(yPos, GRAPH_Y_START, GRAPH_Y_START + GRAPH_HEIGHT);
  
  // Store in buffer
  ecgBuffer[bufferIndex] = yPos;
  bufferIndex++;
  if(bufferIndex >= SCREEN_WIDTH) {
    bufferIndex = 0;
  }
  
  // Clear display
  display.clearDisplay();
  
  // Display header with heart rate indicator
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ECG ");
  
  // Simple heart symbol
  if((millis() / 100) % 10 < 5) {
    display.print((char)3);  // Heart character
  }
  
  // Display value
  display.setCursor(0, 8);
  display.print("Val:");
  display.print(ecgValue);
  
  // Draw graph border
  display.drawRect(0, GRAPH_Y_START, SCREEN_WIDTH, GRAPH_HEIGHT, SSD1306_WHITE);
  
  // Draw complete ECG waveform
  for(int i = 1; i < SCREEN_WIDTH - 1; i++) {
    int currentIndex = (bufferIndex + i) % SCREEN_WIDTH;
    int nextIndex = (bufferIndex + i + 1) % SCREEN_WIDTH;
    
    // Only draw if within bounds
    if(ecgBuffer[currentIndex] >= GRAPH_Y_START && 
       ecgBuffer[currentIndex] <= GRAPH_Y_START + GRAPH_HEIGHT &&
       ecgBuffer[nextIndex] >= GRAPH_Y_START && 
       ecgBuffer[nextIndex] <= GRAPH_Y_START + GRAPH_HEIGHT) {
      display.drawLine(i, ecgBuffer[currentIndex], i + 1, ecgBuffer[nextIndex], SSD1306_WHITE);
    }
  }
  
  // Draw vertical line at current position (sweeping line)
  display.drawLine(bufferIndex, GRAPH_Y_START + 1, bufferIndex, GRAPH_Y_START + GRAPH_HEIGHT - 1, SSD1306_WHITE);
  
  display.display();
  
  delay(15); // Adjust for sampling rate (~66 Hz)
}