// ESP32 FPGA Monitor - Quick Configuration Guide
// ================================================
// Use this file to quickly configure your system

#ifndef CONFIG_H
#define CONFIG_H

// ==================== WIFI SETTINGS ====================
// Configure your WiFi network credentials below

#define WIFI_SSID "Your_WiFi_SSID"           // Replace with your network name
#define WIFI_PASSWORD "Your_WiFi_Password"   // Replace with your network password

// ==================== HARDWARE SETTINGS ====================

#define VOLTAGE_INPUT_PIN 34                 // ADC pin for FPGA voltage (GPIO 34)
#define ADC_RESOLUTION_BITS 12               // 12-bit ADC (0-4095)
#define ADC_MAX_VALUE 4095                   // Maximum ADC value
#define ADC_REF_VOLTAGE 3.3                  // Reference voltage (3.3V for ESP32)

// ==================== SAMPLING SETTINGS ====================

#define SAMPLE_RATE_MS 1000                  // Sample interval in milliseconds
#define DATA_HISTORY_SIZE 120                // Number of samples to store (120 = 2 min)

// ==================== CALIBRATION SETTINGS ====================
// Adjust these if your voltage readings are inaccurate

#define VOLTAGE_CALIBRATION_OFFSET 0.0       // Add/subtract from all readings
                                             // Example: -0.05 subtracts 0.05V
                                             
#define VOLTAGE_CALIBRATION_SCALE 1.0        // Multiply factor for all readings
                                             // Example: 0.95 reduces all readings by 5%

// How to calibrate:
// 1. Connect known voltage to GPIO 34 (use multimeter to verify)
// 2. Read voltage on dashboard
// 3. If reading = multimeter ÷ (desired reading), set CALIBRATION_SCALE = 1.0 / ratio
// 4. If reading is consistently off by fixed amount, adjust CALIBRATION_OFFSET

// ==================== FAILURE DETECTION ====================

#define VOLTAGE_FAILURE_THRESHOLD 0.5        // Voltage threshold for failure (volts)
                                             // Default: 0.5V
                                             // Lower = less sensitive
                                             // Higher = more sensitive

// ==================== WEB SERVER SETTINGS ====================

#define HTTP_PORT 80                         // Web server port (standard: 80)
#define MAX_DATA_POINTS 120                  // Chart history points

// ==================== DEBUG SETTINGS ====================

#define SERIAL_BAUD 115200                   // Serial monitor baud rate
#define DEBUG_MODE true                      // Enable debug output (true/false)
#define DEBUG_INTERVAL 5                     // Print debug info every N samples

#endif // CONFIG_H

/*
 * USAGE INSTRUCTIONS
 * ==================
 * 
 * 1. WIFI SETUP:
 *    - Change WIFI_SSID to your network name
 *    - Change WIFI_PASSWORD to your network password
 *    - Ensure WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
 * 
 * 2. PIN CONFIGURATION:
 *    - If using different ADC pin, change VOLTAGE_INPUT_PIN
 *    - GPIO 34, 35, 36, 39 are ADC input only (recommended)
 *    - GPIO 32-39 are available ADC pins on ESP32
 * 
 * 3. VOLTAGE CALIBRATION:
 *    - Test with a known voltage source
 *    - Adjust CALIBRATION_SCALE if readings are proportionally off
 *    - Adjust CALIBRATION_OFFSET if readings are consistently high/low
 * 
 * 4. FAILURE THRESHOLD:
 *    - Default is 0.5V (trigger alert if voltage drops below this)
 *    - Change to match your FPGA's minimum operating voltage
 * 
 * 5. SAMPLING:
 *    - Default 1 sample per second
 *    - Increase SAMPLE_RATE_MS for slower updates (saves power)
 *    - Decrease for faster updates (more responsive)
 * 
 * 6. DATA HISTORY:
 *    - Default stores 120 samples = 2 minutes at 1Hz
 *    - Increase for longer history (uses more RAM)
 *    - Decrease to save memory
 * 
 * TYPICAL VALUES:
 * ===============
 * 3.3V FPGA:      VOLTAGE_FAILURE_THRESHOLD = 2.5V
 * 2.5V FPGA:      VOLTAGE_FAILURE_THRESHOLD = 2.0V
 * 1.8V FPGA:      VOLTAGE_FAILURE_THRESHOLD = 1.5V
 * 
 * NOTES:
 * ======
 * - Always verify connections before uploading
 * - Test with multimeter to validate readings
 * - Monitor serial output for errors
 * - Restart ESP32 after changing WiFi settings
 */
