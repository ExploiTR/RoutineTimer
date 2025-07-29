#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include "FTPClient.h"

// =============================================================================
// CONFIGURABLE PARAMETERS
// =============================================================================

// I2C Configuration
#define SDA_PIN 21
#define SCL_PIN 22
const uint32_t I2C_CLOCK = 100000;    // 100kHz I2C clock

// BME280 I2C Addresses
const uint8_t BME280_ADDR_PRIMARY = 0x76;
const uint8_t BME280_ADDR_SECONDARY = 0x77;

// Serial Communication
const uint32_t SERIAL_BAUD = 115200;

// Sleep and Wake Configuration
const uint64_t SLEEP_TIME_US = 5 * 60 * 1000000ULL;  // 5 minutes
const int READINGS_PER_CYCLE = 30;
const unsigned long READING_INTERVAL = 1000;    // 1 second
const unsigned long WARMUP_TIME = 2000;         // 2 seconds

// WiFi Configuration
const char* WIFI_SSID = "AX72-IoT";
const char* WIFI_PASSWORD = "SecureIoT_Ax72";
const unsigned long WIFI_TIMEOUT = 10000;       // 10 seconds

// NTP Configuration
const char* NTP_SERVER = "time.google.com";
const long GMT_OFFSET_SEC = 5.5 * 3600;         // IST UTC+5:30
const int DAYLIGHT_OFFSET_SEC = 0;

// FTP Configuration
const char* FTP_SERVER = "192.168.1.1";
const int FTP_PORT = 21;
const char* FTP_USER = "admin";
const char* FTP_PASSWORD = "f6a3067773";
const char* FTP_BASE_PATH = "/G/USD_TPL/";

// BME280 Sensor Configuration
const Adafruit_BME280::sensor_mode SENSOR_MODE = Adafruit_BME280::MODE_NORMAL;
const Adafruit_BME280::sensor_sampling TEMP_OVERSAMPLING = Adafruit_BME280::SAMPLING_X2;
const Adafruit_BME280::sensor_sampling PRESSURE_OVERSAMPLING = Adafruit_BME280::SAMPLING_X16;
const Adafruit_BME280::sensor_sampling HUMIDITY_OVERSAMPLING = Adafruit_BME280::SAMPLING_X1;
const Adafruit_BME280::sensor_filter FILTER_SETTING = Adafruit_BME280::FILTER_X16;
const Adafruit_BME280::standby_duration STANDBY_TIME = Adafruit_BME280::STANDBY_MS_500;

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

Adafruit_BME280 bme;
FTPClient ftpClient;

// Data collection variables
float tempSum = 0;
float pressureSum = 0;
float humiditySum = 0;
int sampleCount = 0;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

bool initializeBME280();
bool connectToWiFi();
bool syncTime();
String getCurrentDateString();
String getCurrentTimeString();
String getCSVFilename();
bool uploadDataToFTP(float avgTemp, float avgPressure, float avgHumidity);
void collectSensorReadings();
void optimizePowerConsumption();
void goToSleep();

void setup() {
    // Initialize serial communication
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    
    Serial.println("\n=== BME280 Environmental Logger ===");
    Serial.println("Wake up from sleep - starting data collection cycle");
    
    // Optimize power consumption
    optimizePowerConsumption();
    
    // Initialize BME280 sensor
    if (!initializeBME280()) {
        Serial.println("Failed to initialize BME280. Going to sleep...");
        goToSleep();
        return;
    }
    
    // Collect sensor readings
    collectSensorReadings();
    
    // Connect to WiFi
    if (!connectToWiFi()) {
        Serial.println("WiFi connection failed. Going to sleep...");
        goToSleep();
        return;
    }
    
    // Sync time
    if (!syncTime()) {
        Serial.println("Time sync failed. Continuing with system time...");
    }
    
    // Calculate averages
    float avgTemp = tempSum / sampleCount;
    float avgPressure = pressureSum / sampleCount;
    float avgHumidity = humiditySum / sampleCount;
    
    Serial.printf("Data collected: %d samples\n", sampleCount);
    Serial.printf("Averages - Temp: %.1f°C, Pressure: %.1fhPa, Humidity: %.2f%%\n", 
                 avgTemp, avgPressure, avgHumidity);
    
    // Upload data to FTP
    bool uploadSuccess = uploadDataToFTP(avgTemp, avgPressure, avgHumidity);
    
    if (uploadSuccess) {
        Serial.println("Data upload successful!");
    } else {
        Serial.println("Data upload failed!");
    }
    
    // Disconnect WiFi and optimize power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    Serial.println("WiFi disconnected and powered down");
    
    // Go to sleep
    Serial.println("Going to sleep for 5 minutes...");
    goToSleep();
}

void loop() {
    // This should never be reached due to deep sleep
    // If reached, something went wrong - go back to sleep
    goToSleep();
}

// =============================================================================
// FUNCTION IMPLEMENTATIONS
// =============================================================================

bool initializeBME280() {
    Serial.println("Initializing BME280 sensor...");
    
    // Initialize I2C with custom pins
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_CLOCK);
    
    // Try to initialize BME280 with primary address
    if (!bme.begin(BME280_ADDR_PRIMARY, &Wire)) {
        Serial.printf("Could not find BME280 sensor at 0x%02X, trying 0x%02X...\n", 
                     BME280_ADDR_PRIMARY, BME280_ADDR_SECONDARY);
        
        // Try alternative address
        if (!bme.begin(BME280_ADDR_SECONDARY, &Wire)) {
            Serial.printf("Could not find BME280 sensor at 0x%02X either!\n", BME280_ADDR_SECONDARY);
            return false;
        } else {
            Serial.printf("BME280 found at address 0x%02X!\n", BME280_ADDR_SECONDARY);
        }
    } else {
        Serial.printf("BME280 found at address 0x%02X!\n", BME280_ADDR_PRIMARY);
    }
    
    // Configure BME280 settings
    bme.setSampling(SENSOR_MODE, TEMP_OVERSAMPLING, PRESSURE_OVERSAMPLING, 
                    HUMIDITY_OVERSAMPLING, FILTER_SETTING, STANDBY_TIME);
    
    // Allow sensor to warm up
    delay(WARMUP_TIME);
    Serial.println("BME280 initialized successfully!");
    return true;
}

bool connectToWiFi() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("\nWiFi connection failed!");
        return false;
    }
}

bool syncTime() {
    Serial.println("Syncing time with NTP server...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    // Wait for time to be set
    int retries = 0;
    while (time(nullptr) < 100000 && retries < 10) {
        delay(1000);
        retries++;
        Serial.print(".");
    }
    
    if (time(nullptr) > 100000) {
        Serial.println("\nTime synchronized successfully!");
        return true;
    } else {
        Serial.println("\nTime sync failed!");
        return false;
    }
}

String getCurrentTimeString() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", timeinfo);
    return String(timeStr);
}

String getCSVFilename() {
    return getCurrentDateString() + ".csv";
}

String getCurrentDateString() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%d_%m_%Y", timeinfo);
    return String(dateStr);
}

void optimizePowerConsumption() {
    Serial.println("Optimizing power consumption...");
    
    // Disable Bluetooth
    esp_bt_controller_disable();
    
    // WiFi will be enabled only when needed
    esp_wifi_stop();
    
    Serial.println("Power optimization complete");
}

bool uploadDataToFTP(float avgTemp, float avgPressure, float avgHumidity) {
    // Configure FTP client
    ftpClient.setServer(FTP_SERVER, FTP_PORT);
    ftpClient.setCredentials(FTP_USER, FTP_PASSWORD);
    
    // Get filename and prepare data
    String filename = getCSVFilename();
    String currentTime = getCurrentTimeString();
    String csvData = currentTime + "," + String(sampleCount) + "," + 
                    String(avgTemp, 1) + "," + String(avgPressure, 1) + "," + 
                    String(avgHumidity, 2) + "\r\n";
    
    // Upload data with header creation if file doesn't exist
    return ftpClient.uploadData(FTP_BASE_PATH, filename, csvData, true);
}

void collectSensorReadings() {
    Serial.printf("Collecting %d sensor readings...\n", READINGS_PER_CYCLE);
    
    tempSum = 0;
    pressureSum = 0;
    humiditySum = 0;
    sampleCount = 0;
    
    for (int i = 0; i < READINGS_PER_CYCLE; i++) {
        float temperature = bme.readTemperature();
        float pressure = bme.readPressure() / 100.0F; // Convert Pa to hPa
        float humidity = bme.readHumidity();
        
        // Check if readings are valid
        if (!isnan(temperature) && !isnan(pressure) && !isnan(humidity)) {
            tempSum += temperature;
            pressureSum += pressure;
            humiditySum += humidity;
            sampleCount++;
            Serial.printf("Reading %d: %.1f°C, %.1fhPa, %.1f%%\n", 
                         i+1, temperature, pressure, humidity);
        } else {
            Serial.printf("Reading %d: Invalid data\n", i+1);
        }
        
        delay(READING_INTERVAL);
    }
    
    Serial.printf("Collected %d valid readings out of %d attempts\n", sampleCount, READINGS_PER_CYCLE);
}

void goToSleep() {
    Serial.println("Configuring deep sleep...");
    
    // Ensure WiFi is properly disabled
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    
    // Configure wake up timer
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);
    
    Serial.println("Entering deep sleep now");
    Serial.flush();
    esp_deep_sleep_start();
}
