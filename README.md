# smart_irrigation 🌱💧

**smart_irrigation** is a C++-based smart watering system for Raspberry Pi.  
It automates plant irrigation by monitoring environmental conditions (like soil moisture) and controlling a water pump through GPIO pins.

## ✨ Features
- 🌡️ Reads sensor data (soil moisture, DHT22, PIR sensor, etc.)
- 💧 Automatically controls water pump via relay
- ⚡ Manual override option
- 📜 Lightweight C++ implementation (no external frameworks needed)

## 🛠 Tech Stack
- **Language:** C++
- **Platform:** Raspberry Pi OS
- **Hardware:** Raspberry Pi, soil moisture sensor, PIR sensor, relay module, switch, water pump, servo motor, LCD display, LED light

## 🚀 Getting Started

### 1. Clone the repo
```bash
git clone https://github.com/Mogapis/smart_i.git
cd smart_i
```
### 2. Compile
```bash
g++ -o smart_i main.cpp -lwiringPi -std=c++11 -pthread
```

### 3. Run
```bash
sudo ./smart_i
```


## Project Structure
```
smart_i/
├── main.cpp          # Core logic for reading sensors and controlling pump
├── cycle_count.txt   # Stores the number of watering cycles for maintenance
└── README.md         # Project documentation
```


## 📜 License
This project is licensed under the MIT License – see the (LICENSE) file for details.
