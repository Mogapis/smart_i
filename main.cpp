#include <iostream>
#include <fstream> // for file operations
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <wiringPi.h>
#include <wiringPiI2C.h>

// GPIO Pin Definitions
#define SOIL_SENSOR_1 0    // GPIO 17 (wiringPi pin 0)
#define SOIL_SENSOR_2 2    // GPIO 27 (wiringPi pin 2)
#define PUMP1_RELAY 21     // GPIO 5 (wiringPi pin 21)
#define PUMP2_RELAY 22     // GPIO 6 (wiringPi pin 22)
#define SERVO_PIN 3        // GPIO 22 (wiringPi pin 3)
#define STOP_SWITCH 4      // GPIO 23 (wiringPi pin 4)
#define PIR_SENSOR 5       // GPIO 24 (wiringPi pin 5)
#define ALERT_LED 1        // GPIO 18 (wiringPi pin 1)

// LCD I2C Settings
#define LCD_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// LCD Commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET 0x20

// LCD Control bits
#define LCD_DISPLAYON 0x04
#define LCD_CURSOROFF 0x00
#define LCD_BLINKOFF 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTDECREMENT 0x00
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_5x8DOTS 0x00

// PCF8574 bits
#define Rs 0x01
#define Rw 0x02
#define En 0x04
#define BACKLIGHT 0x08

class IrrigationSystem {
private:
    bool systemShutdown;
    bool lcdEnabled;
    int lcdHandle;
    int cycleCount;
    const std::string cycleFile = "cycle_count.txt";
    std::chrono::steady_clock::time_point lastLcdUpdate;
    std::pair<std::string, std::string> currentLcdMessage;
    static const int LCD_DISPLAY_TIME = 3; // seconds
    static const int WATER_DURATION = 20; // seconds

public:
    IrrigationSystem() : systemShutdown(false), lcdEnabled(false), lcdHandle(-1) {
        currentLcdMessage = std::make_pair("", "");
        lastLcdUpdate = std::chrono::steady_clock::now();
        loadCycleCount();
    }

    void loadCycleCount() {
        std::ifstream in(cycleFile);
        if (in.is_open()) {
            in >> cycleCount;
            in.close();
        } else {
            cycleCount = 0;
        }
    }

    void saveCycleCount() {
        std::ofstream out(cycleFile, std::ios::trunc);
        if (out.is_open()) {
            out << cycleCount;
            out.close();
        }
    }

    bool initialize() {
        // If maintenance required, block startup
        if (cycleCount >= 3) {
            logMessage("⚠ Maintenance required! System locked.");
            lcdMessage("Maintenance Req", "Service Needed", true);
            return false;
        }

        // Initialize wiringPi
        if (wiringPiSetup() == -1) {
            std::cerr << "Failed to initialize wiringPi" << std::endl;
            return false;
        }

        // Setup GPIO pins
        pinMode(SOIL_SENSOR_1, INPUT);
        pinMode(SOIL_SENSOR_2, INPUT);
        pinMode(PUMP1_RELAY, OUTPUT);
        pinMode(PUMP2_RELAY, OUTPUT);
        pinMode(SERVO_PIN, PWM_OUTPUT);
        pinMode(STOP_SWITCH, INPUT);
        pullUpDnControl(STOP_SWITCH, PUD_UP);
        pinMode(PIR_SENSOR, INPUT);
        pinMode(ALERT_LED, OUTPUT);

        // Initialize pumps to OFF (Active LOW relays)
        digitalWrite(PUMP1_RELAY, HIGH);
        digitalWrite(PUMP2_RELAY, HIGH);
        
        // Initialize servo to center
        pwmWrite(SERVO_PIN, 150); // Center position (1.5ms pulse)
        
        // Initialize alert LED to OFF
        digitalWrite(ALERT_LED, LOW);

        // Initialize LCD
        initializeLcd();

        return true;
    }

    void initializeLcd() {
        lcdHandle = wiringPiI2CSetup(LCD_ADDRESS);
        if (lcdHandle == -1) {
            std::cout << "LCD initialization failed: Unable to setup I2C" << std::endl;
            std::cout << "Continuing without LCD display..." << std::endl;
            lcdEnabled = false;
            return;
        }

        try {
            // LCD initialization sequence
            lcdWrite4bits(0x03 << 4);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            lcdWrite4bits(0x03 << 4);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            lcdWrite4bits(0x03 << 4);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
            lcdWrite4bits(0x02 << 4);

            lcdCommand(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS);
            lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF);
            lcdClear();
            lcdCommand(LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT);

            lcdEnabled = true;
            std::cout << "LCD initialized successfully" << std::endl;
        } catch (...) {
            std::cout << "LCD initialization failed: Communication error" << std::endl;
            std::cout << "Continuing without LCD display..." << std::endl;
            lcdEnabled = false;
        }
    }

    void lcdWrite4bits(uint8_t value) {
        wiringPiI2CWrite(lcdHandle, value | BACKLIGHT);
        lcdPulseEnable(value);
    }

    void lcdPulseEnable(uint8_t data) {
        wiringPiI2CWrite(lcdHandle, data | En | BACKLIGHT);
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        wiringPiI2CWrite(lcdHandle, (data & ~En) | BACKLIGHT);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    void lcdCommand(uint8_t value) {
        lcdWrite4bits(value & 0xF0);
        lcdWrite4bits((value << 4) & 0xF0);
    }

    void lcdWrite(uint8_t value) {
        lcdWrite4bits(Rs | (value & 0xF0));
        lcdWrite4bits(Rs | ((value << 4) & 0xF0));
    }

    void lcdClear() {
        lcdCommand(LCD_CLEARDISPLAY);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    void lcdSetCursor(uint8_t col, uint8_t row) {
        uint8_t row_offsets[] = { 0x00, 0x40 };
        lcdCommand(0x80 | (col + row_offsets[row]));
    }

    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    void logMessage(const std::string& message) {
        std::cout << "[" << getCurrentTimestamp() << "] " << message << std::endl;
    }

    void lcdMessage(const std::string& line1 = "", const std::string& line2 = "", bool forceUpdate = false) {
        if (!lcdEnabled || lcdHandle == -1) {
            return;
        }

        auto currentTime = std::chrono::steady_clock::now();
        auto newMessage = std::make_pair(line1, line2);

        auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastLcdUpdate).count();

        if (forceUpdate || newMessage != currentLcdMessage || timeDiff >= LCD_DISPLAY_TIME) {
            try {
                lcdClear();
                
                // Write first line
                std::string truncatedLine1 = line1.substr(0, 16);
                for (char c : truncatedLine1) {
                    lcdWrite(c);
                }

                // Write second line if provided
                if (!line2.empty()) {
                    lcdSetCursor(0, 1);
                    std::string truncatedLine2 = line2.substr(0, 16);
                    for (char c : truncatedLine2) {
                        lcdWrite(c);
                    }
                }

                currentLcdMessage = newMessage;
                lastLcdUpdate = currentTime;

            } catch (...) {
                std::cout << "LCD Error: Communication failed" << std::endl;
            }
        }
    }

    void emergencyShutdown(const std::string& reason = "Manual Switch") {
        logMessage("EMERGENCY STOP ACTIVATED! Reason: " + reason);
        lcdMessage("EMERGENCY STOP!", reason.substr(0, 16), true);

        // Stop pumps immediately (Active LOW)
        digitalWrite(PUMP1_RELAY, HIGH);
        digitalWrite(PUMP2_RELAY, HIGH);

        // Reset servo to center
        pwmWrite(SERVO_PIN, 150);

        // Turn alert LED ON
        digitalWrite(ALERT_LED, HIGH);

        systemShutdown = true;
        logMessage("System OFF - Pumps stopped, servo reset, alert active");
    }

    bool checkEmergencySwitch() {
        if (digitalRead(STOP_SWITCH) == HIGH) { // Switch pressed = LOW
            emergencyShutdown("Manual Switch");
            return true;
        }
        return false;
    }

    bool checkMotion() {
        if (digitalRead(PIR_SENSOR) == HIGH) { // Motion detected
            logMessage("⚠ Motion detected → Pausing watering");
            lcdMessage("Motion Detected", "Watering Paused", true);
            
            // Stop pumps (Active LOW)
            digitalWrite(PUMP1_RELAY, HIGH);
            digitalWrite(PUMP2_RELAY, HIGH);
            
            // Reset servo
            pwmWrite(SERVO_PIN, 150);
            
            // Turn alert LED ON
            digitalWrite(ALERT_LED, HIGH);
            
            return true; // Pause only, don't kill system
        } else {
            digitalWrite(ALERT_LED, LOW);
            return false;
        }
    }

    std::pair<std::string, std::string> getSystemStatus(int remainingTime) {
        bool soil1Dry = digitalRead(SOIL_SENSOR_1) == HIGH;
        bool soil2Dry = digitalRead(SOIL_SENSOR_2) == HIGH;
        
        std::string timeLine = "Time Left: " + std::to_string(remainingTime) + "s";

        if (soil1Dry && soil2Dry) {
            return std::make_pair("Both Areas DRY", timeLine);
        } else if (soil1Dry && !soil2Dry) {
            return std::make_pair("Area 1: Watering", timeLine);
        } else if (soil2Dry && !soil1Dry) {
            return std::make_pair("Area 2: Watering", timeLine);
        } else {
            return std::make_pair("Both Areas WET", timeLine);
        }
    }

    void setServoPosition(int position) {
        // Convert position (-1, 0, 1) to PWM values
        // -1 = 50 (0.5ms), 0 = 150 (1.5ms), 1 = 250 (2.5ms)
        int pwmValue;
        if (position == -1) {
            pwmValue = 50;  // Left position
        } else if (position == 1) {
            pwmValue = 250; // Right position
        } else {
            pwmValue = 150; // Center position
        }
        
        pwmWrite(SERVO_PIN, pwmValue);
    }

    void run() {
        logMessage("Irrigation System with PIR Intrusion Safety Started");
        if (lcdEnabled) {
            logMessage("LCD display is active");
        } else {
            logMessage("Running without LCD display");
        }

        lcdMessage("System Started", "Monitoring...", true);

        auto startTime = std::chrono::steady_clock::now();

        try {
            while (!systemShutdown) {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();
                int remaining = WATER_DURATION - elapsed;

                // Stop after 30 seconds
                if (remaining <= 0) {
                    logMessage("30s watering period finished");
                    lcdMessage("30s Watering Done", "System Idle", true);

                    cycleCount++;
                    saveCycleCount();
                    logMessage("Cycle count updated to " + std::to_string(cycleCount));

                    if (cycleCount >= 3) {
                        logMessage("⚠ Maintenance required after 3 cycles!");
                        lcdMessage("Maintenance Req", "Service Needed", true);
                    }

                    break;
                }


                // Check emergency switch
                if (checkEmergencySwitch()) {
                    break;
                }

                // Check motion
                if (checkMotion()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }

                // Pump logic
                bool soil1Dry = digitalRead(SOIL_SENSOR_1) == HIGH;
                bool soil2Dry = digitalRead(SOIL_SENSOR_2) == HIGH;

                if (soil1Dry) {
                    digitalWrite(PUMP1_RELAY, LOW); // Active LOW - turn ON
                    logMessage("Soil1 DRY → Pump1 ON");
                } else {
                    digitalWrite(PUMP1_RELAY, HIGH); // Active LOW - turn OFF
                    logMessage("Soil1 WET → Pump1 OFF");
                }

                if (soil2Dry) {
                    digitalWrite(PUMP2_RELAY, LOW); // Active LOW - turn ON
                    logMessage("Soil2 DRY → Pump2 ON");
                } else {
                    digitalWrite(PUMP2_RELAY, HIGH); // Active LOW - turn OFF
                    logMessage("Soil2 WET → Pump2 OFF");
                }

                // Servo update
                if (soil1Dry && !soil2Dry) {
                    setServoPosition(-1); // Left
                } else if (soil2Dry && !soil1Dry) {
                    setServoPosition(1);  // Right
                } else {
                    setServoPosition(0);  // Center
                }

                // Update LCD with current status
                auto status = getSystemStatus(remaining);
                lcdMessage(status.first, status.second);

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } catch (...) {
            logMessage("Exception occurred in main loop");
        }

        // Cleanup
        digitalWrite(PUMP1_RELAY, HIGH);  // Turn OFF (Active LOW)
        digitalWrite(PUMP2_RELAY, HIGH);  // Turn OFF (Active LOW)
        setServoPosition(0);              // Center servo
        digitalWrite(ALERT_LED, LOW);     // Turn OFF alert LED
        lcdMessage("System OFF", "All devices OFF", true);
        logMessage("System shutdown complete - All devices OFF");
    }
};

int main() {
    IrrigationSystem system;
    
    if (!system.initialize()) {
        std::cerr << "Failed to initialize system" << std::endl;
        return 1;
    }
    
    system.run();
    
    return 0;
}

// #include <iostream>
// #include <string>
// #include <chrono>
// #include <thread>
// #include <ctime>
// #include <iomanip>
// #include <sstream>
// #include <wiringPi.h>
// #include <wiringPiI2C.h>

// // GPIO Pin Definitions
// #define SOIL_SENSOR_1 0    // GPIO 17 (wiringPi pin 0)
// #define SOIL_SENSOR_2 2    // GPIO 27 (wiringPi pin 2)
// #define PUMP1_RELAY 21     // GPIO 5 (wiringPi pin 21)
// #define PUMP2_RELAY 22     // GPIO 6 (wiringPi pin 22)
// #define SERVO_PIN 3        // GPIO 22 (wiringPi pin 3)
// #define STOP_SWITCH 4      // GPIO 23 (wiringPi pin 4)
// #define PIR_SENSOR 5       // GPIO 24 (wiringPi pin 5)
// #define ALERT_LED 1        // GPIO 18 (wiringPi pin 1)

// // LCD I2C Settings
// #define LCD_ADDRESS 0x27
// #define LCD_COLS 16
// #define LCD_ROWS 2

// // LCD Commands
// #define LCD_CLEARDISPLAY 0x01
// #define LCD_RETURNHOME 0x02
// #define LCD_ENTRYMODESET 0x04
// #define LCD_DISPLAYCONTROL 0x08
// #define LCD_FUNCTIONSET 0x20

// // LCD Control bits
// #define LCD_DISPLAYON 0x04
// #define LCD_CURSOROFF 0x00
// #define LCD_BLINKOFF 0x00
// #define LCD_ENTRYLEFT 0x02
// #define LCD_ENTRYSHIFTDECREMENT 0x00
// #define LCD_8BITMODE 0x10
// #define LCD_4BITMODE 0x00
// #define LCD_2LINE 0x08
// #define LCD_5x8DOTS 0x00

// // PCF8574 bits
// #define Rs 0x01
// #define Rw 0x02
// #define En 0x04
// #define BACKLIGHT 0x08

// class IrrigationSystem {
// private:
//     bool systemShutdown;
//     bool lcdEnabled;
//     int lcdHandle;
//     std::chrono::steady_clock::time_point lastLcdUpdate;
//     std::pair<std::string, std::string> currentLcdMessage;
//     static const int LCD_DISPLAY_TIME = 3; // seconds
//     static const int WATER_DURATION = 30; // seconds

// public:
//     IrrigationSystem() : systemShutdown(false), lcdEnabled(false), lcdHandle(-1) {
//         currentLcdMessage = std::make_pair("", "");
//         lastLcdUpdate = std::chrono::steady_clock::now();
//     }

//     bool initialize() {
//         // Initialize wiringPi
//         if (wiringPiSetup() == -1) {
//             std::cerr << "Failed to initialize wiringPi" << std::endl;
//             return false;
//         }

//         // Setup GPIO pins
//         pinMode(SOIL_SENSOR_1, INPUT);
//         pinMode(SOIL_SENSOR_2, INPUT);
//         pinMode(PUMP1_RELAY, OUTPUT);
//         pinMode(PUMP2_RELAY, OUTPUT);
//         pinMode(SERVO_PIN, PWM_OUTPUT);
//         pinMode(STOP_SWITCH, INPUT);
//         pullUpDnControl(STOP_SWITCH, PUD_UP);
//         pinMode(PIR_SENSOR, INPUT);
//         pinMode(ALERT_LED, OUTPUT);

//         // Initialize pumps to OFF (Active LOW relays)
//         digitalWrite(PUMP1_RELAY, HIGH);
//         digitalWrite(PUMP2_RELAY, HIGH);
        
//         // Initialize servo to center
//         pwmWrite(SERVO_PIN, 150); // Center position (1.5ms pulse)
        
//         // Initialize alert LED to OFF
//         digitalWrite(ALERT_LED, LOW);

//         // Initialize LCD
//         initializeLcd();

//         return true;
//     }

//     void initializeLcd() {
//         lcdHandle = wiringPiI2CSetup(LCD_ADDRESS);
//         if (lcdHandle == -1) {
//             std::cout << "LCD initialization failed: Unable to setup I2C" << std::endl;
//             std::cout << "Continuing without LCD display..." << std::endl;
//             lcdEnabled = false;
//             return;
//         }

//         try {
//             // LCD initialization sequence
//             lcdWrite4bits(0x03 << 4);
//             std::this_thread::sleep_for(std::chrono::milliseconds(5));
//             lcdWrite4bits(0x03 << 4);
//             std::this_thread::sleep_for(std::chrono::milliseconds(5));
//             lcdWrite4bits(0x03 << 4);
//             std::this_thread::sleep_for(std::chrono::microseconds(150));
//             lcdWrite4bits(0x02 << 4);

//             lcdCommand(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS);
//             lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF);
//             lcdClear();
//             lcdCommand(LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT);

//             lcdEnabled = true;
//             std::cout << "LCD initialized successfully" << std::endl;
//         } catch (...) {
//             std::cout << "LCD initialization failed: Communication error" << std::endl;
//             std::cout << "Continuing without LCD display..." << std::endl;
//             lcdEnabled = false;
//         }
//     }

//     void lcdWrite4bits(uint8_t value) {
//         wiringPiI2CWrite(lcdHandle, value | BACKLIGHT);
//         lcdPulseEnable(value);
//     }

//     void lcdPulseEnable(uint8_t data) {
//         wiringPiI2CWrite(lcdHandle, data | En | BACKLIGHT);
//         std::this_thread::sleep_for(std::chrono::microseconds(1));
//         wiringPiI2CWrite(lcdHandle, (data & ~En) | BACKLIGHT);
//         std::this_thread::sleep_for(std::chrono::microseconds(50));
//     }

//     void lcdCommand(uint8_t value) {
//         lcdWrite4bits(value & 0xF0);
//         lcdWrite4bits((value << 4) & 0xF0);
//     }

//     void lcdWrite(uint8_t value) {
//         lcdWrite4bits(Rs | (value & 0xF0));
//         lcdWrite4bits(Rs | ((value << 4) & 0xF0));
//     }

//     void lcdClear() {
//         lcdCommand(LCD_CLEARDISPLAY);
//         std::this_thread::sleep_for(std::chrono::milliseconds(2));
//     }

//     void lcdSetCursor(uint8_t col, uint8_t row) {
//         uint8_t row_offsets[] = { 0x00, 0x40 };
//         lcdCommand(0x80 | (col + row_offsets[row]));
//     }

//     std::string getCurrentTimestamp() {
//         auto now = std::chrono::system_clock::now();
//         auto time_t = std::chrono::system_clock::to_time_t(now);
        
//         std::stringstream ss;
//         ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
//         return ss.str();
//     }

//     void logMessage(const std::string& message) {
//         std::cout << "[" << getCurrentTimestamp() << "] " << message << std::endl;
//     }

//     void lcdMessage(const std::string& line1 = "", const std::string& line2 = "", bool forceUpdate = false) {
//         if (!lcdEnabled || lcdHandle == -1) {
//             return;
//         }

//         auto currentTime = std::chrono::steady_clock::now();
//         auto newMessage = std::make_pair(line1, line2);

//         auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastLcdUpdate).count();

//         if (forceUpdate || newMessage != currentLcdMessage || timeDiff >= LCD_DISPLAY_TIME) {
//             try {
//                 lcdClear();
                
//                 // Write first line
//                 std::string truncatedLine1 = line1.substr(0, 16);
//                 for (char c : truncatedLine1) {
//                     lcdWrite(c);
//                 }

//                 // Write second line if provided
//                 if (!line2.empty()) {
//                     lcdSetCursor(0, 1);
//                     std::string truncatedLine2 = line2.substr(0, 16);
//                     for (char c : truncatedLine2) {
//                         lcdWrite(c);
//                     }
//                 }

//                 currentLcdMessage = newMessage;
//                 lastLcdUpdate = currentTime;

//             } catch (...) {
//                 std::cout << "LCD Error: Communication failed" << std::endl;
//             }
//         }
//     }

//     void emergencyShutdown(const std::string& reason = "Manual Switch") {
//         logMessage("EMERGENCY STOP ACTIVATED! Reason: " + reason);
//         lcdMessage("EMERGENCY STOP!", reason.substr(0, 16), true);

//         // Stop pumps immediately (Active LOW)
//         digitalWrite(PUMP1_RELAY, HIGH);
//         digitalWrite(PUMP2_RELAY, HIGH);

//         // Reset servo to center
//         pwmWrite(SERVO_PIN, 150);

//         // Turn alert LED ON
//         digitalWrite(ALERT_LED, HIGH);

//         systemShutdown = true;
//         logMessage("System OFF - Pumps stopped, servo reset, alert active");
//     }

//     bool checkEmergencySwitch() {
//         if (digitalRead(STOP_SWITCH) == HIGH) { // Switch pressed = LOW
//             emergencyShutdown("Manual Switch");
//             return true;
//         }
//         return false;
//     }

//     bool checkMotion() {
//         if (digitalRead(PIR_SENSOR) == HIGH) { // Motion detected
//             logMessage("⚠ Motion detected → Pausing watering");
//             lcdMessage("Motion Detected", "Watering Paused", true);
            
//             // Stop pumps (Active LOW)
//             digitalWrite(PUMP1_RELAY, HIGH);
//             digitalWrite(PUMP2_RELAY, HIGH);
            
//             // Reset servo
//             pwmWrite(SERVO_PIN, 150);
            
//             // Turn alert LED ON
//             digitalWrite(ALERT_LED, HIGH);
            
//             return true; // Pause only, don't kill system
//         } else {
//             digitalWrite(ALERT_LED, LOW);
//             return false;
//         }
//     }

//     std::pair<std::string, std::string> getSystemStatus(int remainingTime) {
//         bool soil1Dry = digitalRead(SOIL_SENSOR_1) == HIGH;
//         bool soil2Dry = digitalRead(SOIL_SENSOR_2) == HIGH;
        
//         std::string timeLine = "Time Left: " + std::to_string(remainingTime) + "s";

//         if (soil1Dry && soil2Dry) {
//             return std::make_pair("Both Areas DRY", timeLine);
//         } else if (soil1Dry && !soil2Dry) {
//             return std::make_pair("Area 1: Watering", timeLine);
//         } else if (soil2Dry && !soil1Dry) {
//             return std::make_pair("Area 2: Watering", timeLine);
//         } else {
//             return std::make_pair("Both Areas WET", timeLine);
//         }
//     }

//     void setServoPosition(int position) {
//         // Convert position (-1, 0, 1) to PWM values
//         // -1 = 50 (0.5ms), 0 = 150 (1.5ms), 1 = 250 (2.5ms)
//         int pwmValue;
//         if (position == -1) {
//             pwmValue = 50;  // Left position
//         } else if (position == 1) {
//             pwmValue = 250; // Right position
//         } else {
//             pwmValue = 150; // Center position
//         }
        
//         pwmWrite(SERVO_PIN, pwmValue);
//     }

//     void run() {
//         logMessage("Irrigation System with PIR Intrusion Safety Started");
//         if (lcdEnabled) {
//             logMessage("LCD display is active");
//         } else {
//             logMessage("Running without LCD display");
//         }

//         lcdMessage("System Started", "Monitoring...", true);

//         auto startTime = std::chrono::steady_clock::now();

//         try {
//             while (!systemShutdown) {
//                 auto currentTime = std::chrono::steady_clock::now();
//                 auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();
//                 int remaining = WATER_DURATION - elapsed;

//                 // Stop after 30 seconds
//                 if (remaining <= 0) {
//                     logMessage("30s watering period finished");
//                     lcdMessage("30s Watering Done", "System Idle", true);
//                     break;
//                 }

//                 // Check emergency switch
//                 if (checkEmergencySwitch()) {
//                     break;
//                 }

//                 // Check motion
//                 if (checkMotion()) {
//                     std::this_thread::sleep_for(std::chrono::seconds(1));
//                     continue;
//                 }

//                 // Pump logic
//                 bool soil1Dry = digitalRead(SOIL_SENSOR_1) == HIGH;
//                 bool soil2Dry = digitalRead(SOIL_SENSOR_2) == HIGH;

//                 if (soil1Dry) {
//                     digitalWrite(PUMP1_RELAY, LOW); // Active LOW - turn ON
//                     logMessage("Soil1 DRY → Pump1 ON");
//                 } else {
//                     digitalWrite(PUMP1_RELAY, HIGH); // Active LOW - turn OFF
//                     logMessage("Soil1 WET → Pump1 OFF");
//                 }

//                 if (soil2Dry) {
//                     digitalWrite(PUMP2_RELAY, LOW); // Active LOW - turn ON
//                     logMessage("Soil2 DRY → Pump2 ON");
//                 } else {
//                     digitalWrite(PUMP2_RELAY, HIGH); // Active LOW - turn OFF
//                     logMessage("Soil2 WET → Pump2 OFF");
//                 }

//                 // Servo update
//                 if (soil1Dry && !soil2Dry) {
//                     setServoPosition(-1); // Left
//                 } else if (soil2Dry && !soil1Dry) {
//                     setServoPosition(1);  // Right
//                 } else {
//                     setServoPosition(0);  // Center
//                 }

//                 // Update LCD with current status
//                 auto status = getSystemStatus(remaining);
//                 lcdMessage(status.first, status.second);

//                 std::this_thread::sleep_for(std::chrono::seconds(1));
//             }
//         } catch (...) {
//             logMessage("Exception occurred in main loop");
//         }

//         // Cleanup
//         digitalWrite(PUMP1_RELAY, HIGH);  // Turn OFF (Active LOW)
//         digitalWrite(PUMP2_RELAY, HIGH);  // Turn OFF (Active LOW)
//         setServoPosition(0);              // Center servo
//         digitalWrite(ALERT_LED, LOW);     // Turn OFF alert LED
//         lcdMessage("System OFF", "All devices OFF", true);
//         logMessage("System shutdown complete - All devices OFF");
//     }
// };

// int main() {
//     IrrigationSystem system;
    
//     if (!system.initialize()) {
//         std::cerr << "Failed to initialize system" << std::endl;
//         return 1;
//     }
    
//     system.run();
    
//     return 0;
// }