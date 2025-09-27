# smart_irrigation
smart_irrigation is a C++-based smart watering system for Raspberry Pi. It automates plant irrigation by monitoring environmental conditions (like soil moisture) and controlling a water pump through GPIO pins.

✨ Features
	•	🌡️ Reads sensor data (soil moisture, DHT22, etc.)
	•	💧 Automatically controls water pump via relay
	•	⚡ Manual override option
	•	📜 Lightweight C++ implementation (external frameworks needed)

🛠 Tech Stack
	•	Language: C++
	•	Platform: Raspberry Pi OS
	•	Hardware: Raspberry Pi, soil moisture sensor, PIR sensor,relay module, switch, water pump, servo motor, lcd display and led light

### 1. Clone the repo
git clone https://github.com/Mogapis/smart_i.git
cd smart_i

### 2. Compile


### 3. Run
sudo ./smart_i


## Project Structure
smart_i/
| -- main.cpp          # Core logic for reading sensors and controlling pump
| -- cycle_count.txt   # Number of cycles for Maintance
| -- README.md         # Project documentation
