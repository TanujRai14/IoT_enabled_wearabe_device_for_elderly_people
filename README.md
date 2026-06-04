# IoT Smart Wearable for Dementia & Elderly Care 

An advanced, microcontroller-based IoT wearable design engineered to protect and track dementia patients or elderly individuals. The system integrates real-time GPS tracking, automated Haversine geofencing, dual-user RFID identification, and a rigorous 3-trigger accelerometer/gyroscope algorithm for accurate fall detection. Telemetry data is served dynamically via a local embedded HTML web dashboard over WiFi.

---

## 🚀 Core Features

- **3-Trigger Fall Detection Algorithm:** Minimizes false positives by checking consecutive physical phases via an MPU6050: Free Fall ($< 0.4g$), Sudden Acceleration/Impact ($> 3.0g$), and Gyro Angular Displacement settling.
- **Dynamic Haversine Geofencing:** Automatically calculates real-time distance from a safe home coordinate. Triggers an asynchronous interval-based hardware buzzer alarm ($2\text{s}$ ON / $5\text{s}$ OFF) if the user breaches a predefined $200\text{m}$ safety radius.
- **Multi-Patient RFID Authentication:** Supports secure tap-to-identify tracking for multiple individuals using an MFRC522 RFID reader, updating active user profiles instantly on the server.
- **Embedded Web Dashboard:** Hosts a standalone HTTP server over native Wi-Fi, broadcasting auto-refreshing ($5\text{s}$ intervals), highly scannable status diagnostics including real-time sensor health, coordinates, and clear visual alarm banners.
- **Smart GPS Fallback:** Features automatic indoor positioning fallback to prevent system lockups when satellite signals are obstructed.

---

## 📦 Hardware Architecture

- **Microcontroller:** Arduino UNO R4 WiFi / Wi-Fi Enabled ARM Core (using `WiFiS3.h`)
- **IMU Sensor:** MPU6050 (3-Axis Accelerometer & Gyroscope)
- **Positioning Module:** TinyGPS compatible Serial GPS Module
- **Authentication:** MFRC522 RFID Reader + High-Frequency Tags
- **Alert System:** Active Hardware Piezo Buzzer

---

## 📁 Firmware Breakdown

The core logic operates as an efficient, cooperative non-blocking loop running at a $100\text{ms}$ cycle time:

1. **`updateFallDetection()`**: Samples the raw I2C data from the MPU6050, scales them into g-forces and angular velocities, and sequences through the state-machine triggers to detect immediate physical impact.
2. **`haversine()`**: Computes mathematical distance across spherical coordinates:
   $$A = \sin^2\left(\frac{\Delta\text{lat}}{2}\right) + \cos(\text{lat}_1) \cdot \cos(\text{lat}_2) \cdot \sin^2\left(\frac{\Delta\text{lon}}{2}\right)$$
   $$C = 2 \cdot \text{atan2}\left(\sqrt{A}, \sqrt{1-A}\right)$$
   $$\text{Distance} = R \cdot C$$
3. **`sendWebPage()`**: Compiles a fully stylized, CSS-injected web layout detailing active statuses, sensor states, and critical alerts directly into the active TCP socket client.

---

## ⚙️ How It Operates (System Architecture)

1. **Initialization:** On boot, the system initializes I2C communication for the MPU6050, SPI for the RFID reader, and sets up a serial connection for the GPS module.
2. **Network Connection:** The firmware authenticates with the local Wi-Fi access point and instantiates an HTTP server on Port 80.
3. **Telemetry Streaming:** The hardware continuously runs the fall-detection and geofencing loops. When a user navigates to the Arduino's dynamically assigned local IP address, the board compiles the current data points and spits out the custom CSS/HTML web interface natively.
