# Project-Reactor
Smart 3D Printed UV Flow Reactor Monitoring and Control System. A digitally-controlled reactor using Arduino Uno R4 WiFi, Node-RED Dashboard, and Python for real-time monitoring, sensor logging (temperature, turbidity, UV, RGB), and  actuator control. Includes data logging to SQLite and serial integration with a customisable dashboard.

# Project Objectives

- Real-time visualisation of temperature, UV intensity, turbidity, and RGB sensor data
- Digital control of UV LEDs and peristaltic pumps
- Track and analyse dye degradation behavior
- Store all experimental data for post-run analysis using SQLite
- Sense and response of reactor parameters

# System Architecture

- Arduino UNO R4 WiFi
↓ Serial USB
- Node-Red Dashboard
↓ TCP socket (localhost)
- Python Script -> SQLite Database

# Technologies Used

- Arduino Uno R4 WiFi — Microcontroller with built-in ESP32 co-processor
- Node-RED — Dashboard and serial communication
- Python 3 — Data logger and tools
- SQLite — Local file-based database for experimental data
- Temperature Sensor: LM35 
- UV Sensor: GUVA-S12SD
- Turbidity: SKU SEN0189 
- Colour: TCS3200 
- Phototransistor: SFH 3310 
- LEDs: Blue: HLMP-AB64-TW0xx 
- LEDs UV: VAOL-5GUV8T4 
- Pumps: SKU DFR0523 

reactor-project/
├── arduino_firmware/ # Arduino .ino files and libraries
├── python_logging/ # Python scripts for logging to SQLite
├── node_red/ # Exported Node-RED flows (.json)
├── database/ # Python to SQLite .db file
├── media/ # Screenshots, wiring diagrams
└── README.md # This file

# How to Run

----> 1. Upload Arduino Firmware
- Open `arduino_firmware/` in the Arduino IDE
- Select board: `Arduino Uno R4 WiFi`
- Upload the sketch to your board

----> 2. Start Node-RED
- Control using dashboard at localhost

----> 3. Run Python Logger
- Saves data to SQLite

----> 4. Open Database (optional)
- Observe Data


# Authors

Marion Ridgway
MSc Researcher
