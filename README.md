# Bilirubin Monitor & Phototherapy System (Prototype V2)

![Status](https://img.shields.io/badge/Status-Prototype_V2_Live-green)![Hardware](https://img.shields.io/badge/Hardware-ESP32_|_IoT-blue)![Sensors](https://img.shields.io/badge/Sensors-Color_|_ECG_|_SpO2-red)

## Project Theme & Overview

This project is a **non-invasive, IoT-enabled health monitoring and treatment system**. It is designed specifically for neonatal care to detect jaundice (high bilirubin levels) and automatically trigger phototherapy treatment when necessary.

**Current Progress (Prototype V2):**
We have successfully built a fully functional prototype that integrates five distinct environmental and biological sensors. The system runs on a dual-core ESP32 microcontroller, handling data acquisition, automated treatment logic, local display updates, and a web server simultaneously.

---

## Key Features

* **Jaundice Detection:** Uses a **TCS34725 Color Sensor** with a custom medical algorithm to calculate a "Bilirubin Score" based on skin pigmentation (Yellow Index).
* **Automated Phototherapy:** A closed-loop system that automatically triggers a **Solid State Relay (SSR)** to turn on Blue Therapy Lights when bilirubin levels exceed the medical threshold (10.0).
* **ECG/EKG Monitoring:** Visualizes heart electrical activity via an **AD8232 sensor**.
* **Pulse Oximetry:** Measures Heart Rate (BPM) and Blood Oxygen (SpO2) using the **MAX30105 particle sensor**.
* **Environmental Sensing:** Tracks room temperature and humidity (**DHT11**).
* **Dual-View Interface:**
    * **OLED Screen:** Cycles between "Heart/SpO2" view and "Bilirubin/Therapy" view.
    * **Web Dashboard:** A WiFi-hosted website with **live SVG graphing** of the ECG waveform and therapy status.
* **Cloud Integration:** Automatically pushes data to Google Sheets for historical logging.

---

## Hardware Architecture

To replicate this prototype, you need the following components:

| Component | Model used | Purpose |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DOIT DEVKIT V1 | The brain of the system (WiFi + BLE). |
| **Color Sensor** | TCS34725 (RGB) | **NEW:** Detects skin color/bilirubin levels. |
| **Biometric Sensor** | MAX30105 (or MAX30102) | Reads Pulse and SpO2. |
| **ECG Module** | AD8232 | Reads electrical heart signals. |
| **Env. Sensor** | DHT11 | Reads Temperature & Humidity. |
| **Display** | 0.96" OLED (SSD1306) | Displays immediate data locally. |
| **Actuator** | Solid State Relay (SSR) | **NEW:** Controls high-power Blue Therapy Lights. |

---

## Wiring & Pin Configuration



[Image of ESP32 pinout diagram]


> **⚠️ CRITICAL WARNING:** This code uses **two separate I2C buses** and specific pins to avoid conflicts. Please wire exactly as shown below.

| Sensor/Device | Pin Name | ESP32 Pin | Connection Type |
| :--- | :--- | :--- | :--- |
| **DHT11** | DATA / OUT | **GPIO 4** | Digital Input |
| **AD8232 (ECG)** | OUTPUT | **GPIO 34** | Analog ADC |
| | LO+ | **GPIO 32** | Leads Off Detection |
| | LO- | **GPIO 27** | **CHANGED:** Moved to 27 to free up pin 33. |
| **MAX30105** | SDA | **GPIO 25** | Secondary I2C (`Wire1`) |
| | SCL | **GPIO 26** | Secondary I2C (`Wire1`) |
| **TCS34725** | SDA | **GPIO 25** | Secondary I2C (`Wire1`) (Shared) |
| | SCL | **GPIO 26** | Secondary I2C (`Wire1`) (Shared) |
| **OLED Display**| SDA | **GPIO 21** | Primary I2C (`Wire`) |
| | SCL | **GPIO 22** | Primary I2C (`Wire`) |
| **Therapy Relay** | Control (+) | **GPIO 33** | **NEW:** Triggers Blue Light. |
| **Indicator LED** | Anode (+) | **GPIO 2** | Onboard Visual Indicator. |

*Note: Power all 5V sensors from the VIN (5V) pin and 3.3V sensors from the 3V3 pin. Connect all GNDs together.*

---

## Installation Guide (From Scratch)

If you are setting this up on a new laptop, follow these steps exactly.

### Step 1: Install Arduino IDE
Download and install the latest Arduino IDE from the [official website](https://www.arduino.cc/en/software).

### Step 2: Setup ESP32 Board Manager
By default, Arduino IDE doesn't know how to talk to ESP32.
1.  Open Arduino IDE.
2.  Go to **File** > **Preferences**.
3.  In the box "Additional Boards Manager URLs", paste this link:
    `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
4.  Click **OK**.
5.  Go to **Tools** > **Board** > **Boards Manager**.
6.  Search for **esp32** (by Espressif Systems) and click **Install**.

### Step 3: Install Required Libraries
The code relies on several external libraries. You must install them inside the IDE.

**How to install a library:**
1.  Go to **Sketch** > **Include Library** > **Manage Libraries...**
2.  Search for the **exact names** below and click **Install**.

| Library Name to Search | Author/Maintainer | Purpose |
| :--- | :--- | :--- |
| `SparkFun MAX3010x` | SparkFun Electronics | Controls the SpO2 sensor. |
| `Adafruit SSD1306` | Adafruit | Controls the OLED screen. |
| `Adafruit GFX` | Adafruit | Graphics core for the OLED. |
| `Adafruit TCS34725` | Adafruit | **NEW:** Controls the Color Sensor. |
| `DHT sensor library` | Adafruit | Controls the Temp/Humid sensor. |
| `ArduinoJson` | Benoit Blanchon | Formats data for cloud upload. |

---

## How to Run the Code

1.  **Clone/Download:** Download this repository to your computer.
2.  **Open Code:** Open the file `Integrated_Health_Bilirubin_System.ino` in Arduino IDE.
3.  **Configure WiFi:**
    Scroll to lines 16-17 in the code:
    ```cpp
    const char* ssid = "YOUR_WIFI_NAME";      // Replace with your WiFi Name
    const char* password = "YOUR_PASSWORD";  // Replace with your WiFi Password
    ```
4.  **Select Board:**
    Go to **Tools** > **Board** > **ESP32 Arduino** > **DOIT ESP32 DEVKIT V1**.
5.  **Upload:**
    * Connect your ESP32 via USB.
    * Select the correct **Port** under **Tools**.
    * Click the **Right Arrow (➡️)** icon to upload.

---

## Using the Dashboard

Once the code is uploaded:

1.  **Watch the OLED:** Immediately after uploading, the OLED will display the IP Address for 8 seconds.
2.  **Open Dashboard:** Type that IP address into your phone or laptop browser (must be on the same WiFi).
3.  **Cloud Data:** Open the Google Sheet below to see historical logs.
    * [View Live Google Sheet Logs](#) *(Add your link here)*

### Dashboard Features
* **Live ECG Graph:** The dashboard uses a circular buffer to push 200 data points per second, drawing a live SVG line graph of your heart rhythm.
* **Therapy Status:** Shows "NORMAL" or flashes red "THERAPY ACTIVE" based on real-time bilirubin readings.
* **Auto-Refresh:** The page and sensor values update automatically every 5 seconds.

---

## Credits & License

**Team Members:**
* Mayank Singh
* Harsh Sharma

**License:** Open Source.
*Developed for Tech Expo 2025.*
