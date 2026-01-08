# Dual-Core Disconnect Control System

**Automotive E-Axle Research & Development**

This project implements a distributed control system for an automotive disconnect mechanism. It utilizes a **Dual-MCU Architecture** to separate "Hard Real-Time" safety logic from "Soft Real-Time" telemetry and user interface tasks.

The system features a robust **STM32** controller for millisecond-level actuator logic and an **ESP32** bridge for high-speed data logging, remote control, and a responsive web-based dashboard.

![Project Status](https://img.shields.io/badge/Status-Active_Prototype-green)
![Platform](https://img.shields.io/badge/Platform-STM32_%7C_ESP32-blue)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

## 📖 About The Project (What is this?)

This system was designed to solve the challenge of controlling a high-current disconnect actuator while simultaneously logging high-frequency data and providing a wireless interface.

Instead of burdening a single microcontroller with both motor control (safety-critical) and WiFi/Webserver tasks (latency-prone), the workload is split:
* **STM32 (The Pilot):** Handles the "Reflexes." It runs a tight 1ms loop to read sensors and control the servo/motor. It never pauses.
* **ESP32 (The Co-Pilot):** Handles the "Logbook." It records data, hosts the website, and takes commands from the user.

## 🏗 System Architecture

### 1. Real-Time Control Domain (STM32F103RB)
* **Role:** Dedicated Actuator Controller.
* **Performance:** 1000Hz (1ms) Loop Cycle.
* **Tasks:** * Reads Analog Potentiometer (Position Feedback).
    * Reads Digital IR Sensors (End-Stops).
    * Generates PWM for Servo Actuation.
    * Manages "Auto-Engage" logic based on sensor thresholds.

### 2. Telemetry & HMI Domain (ESP32 WROVER)
* **Role:** Data Bridge & Interface.
* **Tasks:** * **Dashboard:** Hosts an Async Web Server (accessible via WiFi) with live Chart.js graphs.
    * **Data Logging:** Writes CSV telemetry logs to an SD Card (SD_MMC 4-bit mode) at 10Hz.
    * **Bridge:** Communicates with the STM32 via a custom UART protocol.

## 🔌 Connection & Wiring Diagram

### Inter-Chip Bridge (UART)
This is the main link between the two brains.

| STM32 Pin | ESP32 Pin | Function | Direction |
| :--- | :--- | :--- | :--- |
| **PA9** (TX) | **GPIO 26** (RX) | **Telemetry Data** | STM32 $\to$ ESP32 |
| **PA10** (RX)| **GPIO 27** (TX) | **Command Signal** | ESP32 $\to$ STM32 |
| **GND** | **GND** | **Common Ground** | **CRITICAL** |

### STM32 Peripherals (Control Side)
| Component | STM32 Pin | Config | Function |
| :--- | :--- | :--- | :--- |
| **Potentiometer** | **PA0** | ADC1_IN0 | Position Feedback (0-3.3V) |
| **IR Sensor** | **PA1** | GPIO Input | End-Stop Detection |
| **Servo Signal** | **PB7** | TIM4_CH2 | PWM Control Signal |
| **Manual Button** | **PC2** | GPIO Input | Engage/Disengage Toggle |
| **LED (Red)** | **PC3** | GPIO Output | Status: Engaged |
| **LED (Yellow)** | **PC0** | GPIO Output | Status: Standby |

### ESP32 Peripherals (Data Side)
| Component | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **SD CMD** | **GPIO 15** | SD Card Command |
| **SD CLK** | **GPIO 14** | SD Card Clock |
| **SD D0** | **GPIO 2** | SD Data 0 |
| **SD D1-D3** | **GPIO 4, 12, 13** | SD Data 1-3 (4-Bit Mode) |

## 📊 Software Features

### Web Dashboard (HMI)
* **Live Graphing:** Plots Angle, Distance, and System State in real-time.
* **System Controls:** Engage, Disengage, and Auto-Mode toggles.
* **Memory Monitor:** Tracks RAM usage for both microcontrollers to detect memory leaks.
* **File Manager:** Download `.csv` logs directly from the browser.

### Data Protocol
The STM32 sends a CSV-formatted string to the ESP32 every 200ms:
```text
engaged, angle, pot_raw, auto_mode, stm32_free_ram \n
