// Include the required libraries
#include "DHT.h"

// Define the pin you connected the DHT11 Data pin to
#define DHTPIN 4     // We used GPIO 4 in the wiring example

// Define the type of DHT sensor you are using (DHT11, DHT22, etc.)
#define DHTTYPE DHT11

// Initialize the DHT sensor object
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  // Start the Serial Monitor at 115200 baud speed
  Serial.begin(115200); 
  Serial.println("DHT11 Test!");

  // Initialize the DHT sensor
  dht.begin();
}

void loop() {
  // Wait a couple of seconds between measurements.
  // The DHT11 is slow and can only be read every 2 seconds.
  delay(2000);

  // Read the humidity
  float h = dht.readHumidity();
  
  // Read the temperature as Celsius (the default)
  float t = dht.readTemperature();
  
  // Read the temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return; // Exit this loop iteration
  }

  // Compute heat index in Fahrenheit (the 'true' at the end)
  float hif = dht.computeHeatIndex(f, h, true);

  // Compute heat index in Celsius
  float hic = dht.computeHeatIndex(t, h, false);

  // Print the results to the Serial Monitor
  Serial.print("Humidity: ");
  Serial.print(h);
  Serial.print("%  Temperature: ");
  Serial.print(t);
  Serial.print("°C / ");
  Serial.print(f);
  Serial.print("°F  Heat Index: ");
  Serial.print(hic);
  Serial.print("°C / ");
  Serial.print(hif);
  Serial.println("°F");
}