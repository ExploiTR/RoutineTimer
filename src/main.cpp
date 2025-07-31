#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#ifdef USE_BME280
#include <Adafruit_BME280.h>
#else
#include <Adafruit_BMP280.h>
#endif
#include <time.h>
#include <math.h>

// Platform-specific includes
#ifdef ESP32
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "FTPClient.h"

// =============================================================================
// CONFIGURABLE PARAMETERS
// =============================================================================

// I2C Configuration
#ifndef SDA_PIN
#define SDA_PIN 21  // Default for ESP32
#endif
#ifndef SCL_PIN
#define SCL_PIN 22  // Default for ESP32
#endif
const uint32_t I2C_CLOCK = 100000;    // 100kHz I2C clock

// BME280 I2C Addresses (also used for BMP280)
const uint8_t BME280_ADDR_PRIMARY = 0x76;
const uint8_t BME280_ADDR_SECONDARY = 0x77;

// Serial Communication
const uint32_t SERIAL_BAUD = 115200;

// Sleep and Wake Configuration
const uint64_t SLEEP_TIME_US = 5 * 60 * 1000000ULL;  // 5 minutes
const int READINGS_PER_CYCLE = 5;
const unsigned long READING_INTERVAL = 3000;    // 1 second
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
const char* FTP_SERVER = "192.168.0.1";
const int FTP_PORT = 21;
const char* FTP_USER = "admin";
const char* FTP_PASSWORD = "f6a3067773";
const char* FTP_BASE_PATH = "/G/USD_TPL/";

// Sensor Configuration
#ifdef USE_BME280
const Adafruit_BME280::sensor_mode SENSOR_MODE = Adafruit_BME280::MODE_NORMAL;
const Adafruit_BME280::sensor_sampling TEMP_OVERSAMPLING = Adafruit_BME280::SAMPLING_X2;
const Adafruit_BME280::sensor_sampling PRESSURE_OVERSAMPLING = Adafruit_BME280::SAMPLING_X16;
const Adafruit_BME280::sensor_sampling HUMIDITY_OVERSAMPLING = Adafruit_BME280::SAMPLING_X1;
const Adafruit_BME280::sensor_filter FILTER_SETTING = Adafruit_BME280::FILTER_X16;
const Adafruit_BME280::standby_duration STANDBY_TIME = Adafruit_BME280::STANDBY_MS_500;
#else
const Adafruit_BMP280::sensor_mode SENSOR_MODE = Adafruit_BMP280::MODE_NORMAL;
const Adafruit_BMP280::sensor_sampling TEMP_OVERSAMPLING = Adafruit_BMP280::SAMPLING_X2;
const Adafruit_BMP280::sensor_sampling PRESSURE_OVERSAMPLING = Adafruit_BMP280::SAMPLING_X16;
const Adafruit_BMP280::sensor_filter FILTER_SETTING = Adafruit_BMP280::FILTER_X16;
const Adafruit_BMP280::standby_duration STANDBY_TIME = Adafruit_BMP280::STANDBY_MS_500;
#endif

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

#ifdef USE_BME280
Adafruit_BME280 bme;
#else
Adafruit_BMP280 bmp;
#endif
FTPClient ftpClient;

// Data collection variables
float tempSum = 0;
float pressureSum = 0;
float humiditySum = 0;
int sampleCount = 0;

bool serialVerboseMode = true;

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
void scanI2CDevices();

void setup() {
    // Initialize serial communication
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    
    #ifdef ESP32
    Serial.println("\n=== ESP32 BME280 Environmental Logger ===");
    Serial.println("Device: ESP32 WROOM-32");
    Serial.printf("I2C Pins: SDA=%d, SCL=%d\n", SDA_PIN, SCL_PIN);
    Serial.println("Sensor: BME280 (Temp + Pressure + Humidity)");
    Serial.println("File suffix: (none) - indoor sensor");
    #elif defined(ESP8266)
    Serial.println("\n=== ESP8266 BMP280 Environmental Logger ===");
    Serial.println("Device: ESP8266 NodeMCU v2");
    Serial.printf("I2C Pins: SDA=%d (D1), SCL=%d (D2)\n", SDA_PIN, SCL_PIN);
    Serial.println("Sensor: BMP280 (Temp + Pressure only)");
    Serial.println("File suffix: _outside - outdoor sensor");
    #endif
    
    Serial.println("Wake up from sleep - starting data collection cycle");
    if(serialVerboseMode) Serial.printf("[L%d] setup() started\n", __LINE__);
    
    // Optimize power consumption
    if(serialVerboseMode) Serial.printf("[L%d] Calling optimizePowerConsumption()\n", __LINE__);
    optimizePowerConsumption();
    
    // Initialize BME280 sensor
    if(serialVerboseMode) Serial.printf("[L%d] Calling initializeBME280()\n", __LINE__);
    if (!initializeBME280()) {
        Serial.println("Failed to initialize BME280. Running I2C scan for debugging...");
        scanI2CDevices();
        Serial.println("Going to sleep...");
        if(serialVerboseMode) Serial.printf("[L%d] BME280 init failed, calling goToSleep()\n", __LINE__);
        goToSleep();
        return;
    }
    if(serialVerboseMode) Serial.printf("[L%d] BME280 initialization successful\n", __LINE__);
    
    // Collect sensor readings
    if(serialVerboseMode) Serial.printf("[L%d] Calling collectSensorReadings()\n", __LINE__);
    collectSensorReadings();
    if(serialVerboseMode) Serial.printf("[L%d] Sensor readings collection completed\n", __LINE__);
    
    // Connect to WiFi
    if(serialVerboseMode) Serial.printf("[L%d] Calling connectToWiFi()\n", __LINE__);
    if (!connectToWiFi()) {
        Serial.println("WiFi connection failed. Going to sleep...");
        if(serialVerboseMode) Serial.printf("[L%d] WiFi connection failed, calling goToSleep()\n", __LINE__);
        goToSleep();
        return;
    }
    if(serialVerboseMode) Serial.printf("[L%d] WiFi connection successful\n", __LINE__);
    
    // Sync time
    if(serialVerboseMode) Serial.printf("[L%d] Calling syncTime()\n", __LINE__);
    if (!syncTime()) {
        Serial.println("Time sync failed. Continuing with system time...");
        if(serialVerboseMode) Serial.printf("[L%d] Time sync failed, continuing anyway\n", __LINE__);
    }
    if(serialVerboseMode) Serial.printf("[L%d] Time sync completed\n", __LINE__);
    
    // Calculate averages
    if(serialVerboseMode) Serial.printf("[L%d] Calculating averages from %d samples\n", __LINE__, sampleCount);
    float avgTemp = tempSum / sampleCount;
    float avgPressure = pressureSum / sampleCount;
    
    #ifdef USE_BME280
    float avgHumidity = humiditySum / sampleCount;
    Serial.printf("Data collected: %d samples\n", sampleCount);
    Serial.printf("Averages - Temp: %.1f°C, Pressure: %.1fhPa, Humidity: %.2f%%\n", 
                 avgTemp, avgPressure, avgHumidity);
    #else
    float avgHumidity = 0.0; // BMP280 doesn't have humidity
    Serial.printf("Data collected: %d samples\n", sampleCount);
    Serial.printf("Averages - Temp: %.1f°C, Pressure: %.1fhPa (BMP280 - no humidity)\n", 
                 avgTemp, avgPressure);
    #endif
    
    // Upload data to FTP
    if(serialVerboseMode) Serial.printf("[L%d] Calling uploadDataToFTP()\n", __LINE__);
    bool uploadSuccess = uploadDataToFTP(avgTemp, avgPressure, avgHumidity);
    
    if (uploadSuccess) {
        Serial.println("Data upload successful!");
        if(serialVerboseMode) Serial.printf("[L%d] FTP upload completed successfully\n", __LINE__);
    } else {
        Serial.println("Data upload failed!");
        if(serialVerboseMode) Serial.printf("[L%d] FTP upload failed\n", __LINE__);
    }
    
    // Disconnect WiFi and optimize power
    if(serialVerboseMode) Serial.printf("[L%d] Disconnecting WiFi and powering down\n", __LINE__);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    #ifdef ESP32
    esp_wifi_stop();
    #endif
    
    Serial.println("WiFi disconnected and powered down");
    
    // Go to sleep
    Serial.println("Going to sleep for 5 minutes...");
    if(serialVerboseMode) Serial.printf("[L%d] Calling goToSleep()\n", __LINE__);
    goToSleep();
}

void loop() {
    // This should never be reached due to deep sleep
    // If reached, something went wrong - go back to sleep
    if(serialVerboseMode) Serial.printf("[L%d] ERROR: loop() reached unexpectedly!\n", __LINE__);
    goToSleep();
}

// =============================================================================
// FUNCTION IMPLEMENTATIONS
// =============================================================================

bool initializeBME280() {
    if(serialVerboseMode) Serial.printf("[L%d] initializeBME280() started\n", __LINE__);
    
    #ifdef USE_BME280
    Serial.println("Initializing BME280 sensor...");
    #else
    Serial.println("Initializing BMP280 sensor...");
    #endif
    
    // Initialize I2C with custom pins
    if(serialVerboseMode) Serial.printf("[L%d] Initializing I2C (SDA:%d, SCL:%d, Clock:%d)\n", __LINE__, SDA_PIN, SCL_PIN, I2C_CLOCK);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_CLOCK);
    
    // Give the sensor extra time to stabilize
    delay(500);
    Serial.println("Allowing sensor to stabilize...");
    
    // Try to initialize sensor with multiple attempts
    const int maxAttempts = 3;
    bool sensorFound = false;
    
    for (int attempt = 1; attempt <= maxAttempts && !sensorFound; attempt++) {
        if(serialVerboseMode) Serial.printf("[L%d] Attempt %d/%d: Trying sensor init at primary address 0x%02X\n", __LINE__, attempt, maxAttempts, BME280_ADDR_PRIMARY);
        
        #ifdef USE_BME280
        sensorFound = bme.begin(BME280_ADDR_PRIMARY, &Wire);
        #else
        sensorFound = bmp.begin(BME280_ADDR_PRIMARY);
        #endif
        
        if (sensorFound) {
            Serial.printf("Sensor found at address 0x%02X on attempt %d!\n", BME280_ADDR_PRIMARY, attempt);
            if(serialVerboseMode) Serial.printf("[L%d] Sensor found at primary address\n", __LINE__);
            break;
        }
        
        // Try alternative address
        if(serialVerboseMode) Serial.printf("[L%d] Attempt %d/%d: Trying sensor init at secondary address 0x%02X\n", __LINE__, attempt, maxAttempts, BME280_ADDR_SECONDARY);
        
        #ifdef USE_BME280
        sensorFound = bme.begin(BME280_ADDR_SECONDARY, &Wire);
        #else
        sensorFound = bmp.begin(BME280_ADDR_SECONDARY);
        #endif
        
        if (sensorFound) {
            Serial.printf("Sensor found at address 0x%02X on attempt %d!\n", BME280_ADDR_SECONDARY, attempt);
            if(serialVerboseMode) Serial.printf("[L%d] Sensor found at secondary address\n", __LINE__);
            break;
        }
        
        if (attempt < maxAttempts) {
            Serial.printf("Attempt %d failed, retrying in 1 second...\n", attempt);
            delay(1000);
        }
    }
    
    if (!sensorFound) {
        Serial.printf("Could not initialize sensor after %d attempts!\n", maxAttempts);
        if(serialVerboseMode) Serial.printf("[L%d] Sensor init failed after all attempts, returning false\n", __LINE__);
        return false;
    }
    
    // Configure sensor settings
    if(serialVerboseMode) Serial.printf("[L%d] Configuring sensor settings\n", __LINE__);
    
    #ifdef USE_BME280
    bme.setSampling(SENSOR_MODE, TEMP_OVERSAMPLING, PRESSURE_OVERSAMPLING, 
                    HUMIDITY_OVERSAMPLING, FILTER_SETTING, STANDBY_TIME);
    #else
    bmp.setSampling(SENSOR_MODE, TEMP_OVERSAMPLING, PRESSURE_OVERSAMPLING, 
                    FILTER_SETTING, STANDBY_TIME);
    #endif
    
    // Allow sensor to warm up
    if(serialVerboseMode) Serial.printf("[L%d] Starting warmup delay (%lu ms)\n", __LINE__, WARMUP_TIME);
    delay(WARMUP_TIME);
    
    // Test reading to make sure sensor is working
    Serial.println("Testing sensor readings...");
    
    #ifdef USE_BME280
    float testTemp = bme.readTemperature();
    float testPressure = bme.readPressure() / 100.0F;
    #else
    float testTemp = bmp.readTemperature();
    float testPressure = bmp.readPressure() / 100.0F;
    #endif
    
    if (isnan(testTemp) || isnan(testPressure)) {
        Serial.println("Sensor readings are invalid - sensor may not be working properly!");
        if(serialVerboseMode) Serial.printf("[L%d] Test readings failed, returning false\n", __LINE__);
        return false;
    }
    
    Serial.printf("Test readings: %.1f°C, %.1fhPa\n", testTemp, testPressure);
    
    #ifdef USE_BME280
    Serial.println("BME280 initialized successfully!");
    #else
    Serial.println("BMP280 initialized successfully!");
    #endif
    
    if(serialVerboseMode) Serial.printf("[L%d] initializeBME280() returning true\n", __LINE__);
    return true;
}

bool connectToWiFi() {
    if(serialVerboseMode) Serial.printf("[L%d] connectToWiFi() started\n", __LINE__);
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    if(serialVerboseMode) Serial.printf("[L%d] Setting WiFi mode to STA\n", __LINE__);
    WiFi.mode(WIFI_STA);
    if(serialVerboseMode) Serial.printf("[L%d] Starting WiFi connection\n", __LINE__);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    if(serialVerboseMode) Serial.printf("[L%d] Waiting for connection (timeout: %d ms)\n", __LINE__, WIFI_TIMEOUT);
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        if(serialVerboseMode) Serial.printf("[L%d] connectToWiFi() returning true\n", __LINE__);
        return true;
    } else {
        Serial.println("\nWiFi connection failed!");
        if(serialVerboseMode) Serial.printf("[L%d] WiFi timeout reached, returning false\n", __LINE__);
        return false;
    }
}

bool syncTime() {
    if(serialVerboseMode) Serial.printf("[L%d] syncTime() started\n", __LINE__);
    Serial.println("Syncing time with NTP server...");
    
    int ntpAttempts = 0;
    const int maxNtpAttempts = 3;
    
    while (ntpAttempts < maxNtpAttempts) {
        ntpAttempts++;
        if(serialVerboseMode) Serial.printf("[L%d] NTP attempt %d of %d\n", __LINE__, ntpAttempts, maxNtpAttempts);
        if(serialVerboseMode) Serial.printf("[L%d] Configuring NTP (server: %s, GMT offset: %ld)\n", __LINE__, NTP_SERVER, GMT_OFFSET_SEC);
        
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        
        // Wait for time to be set
        int retries = 0;
        if(serialVerboseMode) Serial.printf("[L%d] Waiting for time sync (max 10 retries)\n", __LINE__);
        while (time(nullptr) < 100000 && retries < 10) {
            delay(1000);
            retries++;
            Serial.print(".");
            if(serialVerboseMode && retries % 5 == 0) Serial.printf(" [retry %d]", retries);
        }
        
        if (time(nullptr) > 100000) {
            // Check if we got a valid year (not 1970)
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            int currentYear = timeinfo->tm_year + 1900; // tm_year is years since 1900
            
            if(serialVerboseMode) Serial.printf("\n[L%d] NTP sync completed, checking year: %d\n", __LINE__, currentYear);
            
            if (currentYear > 1970) {
                Serial.println("Time synchronized successfully!");
                if(serialVerboseMode) Serial.printf("[L%d] syncTime() returning true (year: %d, retries: %d)\n", __LINE__, currentYear, retries);
                return true;
            } else {
                Serial.printf("\nNTP returned invalid year (%d), retrying...\n", currentYear);
                if(serialVerboseMode) Serial.printf("[L%d] Invalid year detected, will retry NTP sync\n", __LINE__);
                delay(2000); // Wait before next attempt
            }
        } else {
            Serial.println("\nNTP sync timeout, retrying...");
            if(serialVerboseMode) Serial.printf("[L%d] NTP sync timeout, will retry\n", __LINE__);
            delay(2000); // Wait before next attempt
        }
    }
    
    Serial.println("Time sync failed after all attempts!");
    if(serialVerboseMode) Serial.printf("[L%d] syncTime() returning false (max NTP attempts reached)\n", __LINE__);
    return false;
}

String getCurrentTimeString() {
    if(serialVerboseMode) Serial.printf("[L%d] getCurrentTimeString() called\n", __LINE__);
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", timeinfo);
    if(serialVerboseMode) Serial.printf("[L%d] Generated time string: %s\n", __LINE__, timeStr);
    return String(timeStr);
}

String getCSVFilename() {
    if(serialVerboseMode) Serial.printf("[L%d] getCSVFilename() called\n", __LINE__);
    
    #ifndef FILENAME_SUFFIX
    #define FILENAME_SUFFIX ""
    #endif
    
    String filename = getCurrentDateString() + FILENAME_SUFFIX + ".csv";
    if(serialVerboseMode) Serial.printf("[L%d] Generated filename: %s\n", __LINE__, filename.c_str());
    return filename;
}

String getCurrentDateString() {
    if(serialVerboseMode) Serial.printf("[L%d] getCurrentDateString() called\n", __LINE__);
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%d_%m_%Y", timeinfo);
    if(serialVerboseMode) Serial.printf("[L%d] Generated date string: %s\n", __LINE__, dateStr);
    return String(dateStr);
}

void optimizePowerConsumption() {
    if(serialVerboseMode) Serial.printf("[L%d] optimizePowerConsumption() started\n", __LINE__);
    Serial.println("Optimizing power consumption...");
    
    #ifdef ESP32
    // Disable Bluetooth (ESP32 only)
    if(serialVerboseMode) Serial.printf("[L%d] Disabling Bluetooth\n", __LINE__);
    esp_bt_controller_disable();
    
    // WiFi will be enabled only when needed
    if(serialVerboseMode) Serial.printf("[L%d] Stopping WiFi\n", __LINE__);
    esp_wifi_stop();
    #elif defined(ESP8266)
    // ESP8266 doesn't have Bluetooth, just ensure WiFi is off
    if(serialVerboseMode) Serial.printf("[L%d] Stopping WiFi (ESP8266)\n", __LINE__);
    WiFi.mode(WIFI_OFF);
    #endif
    
    Serial.println("Power optimization complete");
    if(serialVerboseMode) Serial.printf("[L%d] optimizePowerConsumption() completed\n", __LINE__);
}

bool uploadDataToFTP(float avgTemp, float avgPressure, float avgHumidity) {
    if(serialVerboseMode) Serial.printf("[L%d] uploadDataToFTP() started\n", __LINE__);
    // Configure FTP client
    if(serialVerboseMode) Serial.printf("[L%d] Configuring FTP client (server: %s:%d)\n", __LINE__, FTP_SERVER, FTP_PORT);
    ftpClient.setServer(FTP_SERVER, FTP_PORT);
    ftpClient.setCredentials(FTP_USER, FTP_PASSWORD);
    
    // Get filename and prepare data
    if(serialVerboseMode) Serial.printf("[L%d] Getting filename and preparing CSV data\n", __LINE__);
    String filename = getCSVFilename();
    String currentTime = getCurrentTimeString();
    
    #ifdef USE_BME280
    // BME280: Include humidity
    String csvData = currentTime + "," + String(sampleCount) + "," + 
                    String(avgTemp, 1) + "," + String(avgPressure, 1) + "," + 
                    String(avgHumidity, 2) + "\r\n";
    #else
    // BMP280: No humidity, use N/A or 0
    String csvData = currentTime + "," + String(sampleCount) + "," + 
                    String(avgTemp, 1) + "," + String(avgPressure, 1) + "," + 
                    "N/A\r\n";
    #endif
    
    if(serialVerboseMode) {
        Serial.printf("[L%d] Filename: %s\n", __LINE__, filename.c_str());
        Serial.printf("[L%d] CSV data: %s", __LINE__, csvData.c_str());
        Serial.printf("[L%d] Calling ftpClient.uploadData()\n", __LINE__);
    }
    
    // Upload data with header creation if file doesn't exist
    bool result = ftpClient.uploadData(FTP_BASE_PATH, filename, csvData, true);
    if(serialVerboseMode) Serial.printf("[L%d] uploadDataToFTP() returning %s\n", __LINE__, result ? "true" : "false");
    return result;
}

void collectSensorReadings() {
    if(serialVerboseMode) Serial.printf("[L%d] collectSensorReadings() started\n", __LINE__);
    Serial.printf("Collecting %d sensor readings...\n", READINGS_PER_CYCLE);
    
    if(serialVerboseMode) Serial.printf("[L%d] Initializing variables\n", __LINE__);
    tempSum = 0;
    pressureSum = 0;
    humiditySum = 0;
    sampleCount = 0;
    
    for (int i = 0; i < READINGS_PER_CYCLE; i++) {
        if(serialVerboseMode) Serial.printf("[L%d] Reading sensor data (iteration %d)\n", __LINE__, i+1);
        
        #ifdef USE_BME280
        float temperature = bme.readTemperature();
        float pressure = bme.readPressure() / 100.0F; // Convert Pa to hPa
        float humidity = bme.readHumidity();
        #else
        float temperature = bmp.readTemperature();
        float pressure = bmp.readPressure() / 100.0F; // Convert Pa to hPa
        float humidity = 0.0; // BMP280 doesn't have humidity sensor
        #endif
        
        // Check if readings are valid
        bool tempValid = !isnan(temperature);
        bool pressureValid = !isnan(pressure);
        
        #ifdef USE_BME280
        bool humidityValid = !isnan(humidity);
        #else
        bool humidityValid = true; // BMP280 - humidity not applicable
        #endif
        
        if (tempValid && pressureValid && humidityValid) {
            tempSum += temperature;
            pressureSum += pressure;
            
            #ifdef USE_BME280
            humiditySum += humidity;
            Serial.printf("Reading %d: %.1f°C, %.1fhPa, %.1f%%\n", 
                         i+1, temperature, pressure, humidity);
            #else
            Serial.printf("Reading %d: %.1f°C, %.1fhPa (BMP280 - no humidity)\n", 
                         i+1, temperature, pressure);
            #endif
            
            sampleCount++;
            if(serialVerboseMode) Serial.printf("[L%d] Valid reading added to sums\n", __LINE__);
        } else {
            Serial.printf("Reading %d: Invalid data\n", i+1);
            if(serialVerboseMode) Serial.printf("[L%d] Invalid reading detected (T:%.1f, P:%.1f, H:%.1f)\n", __LINE__, temperature, pressure, humidity);
        }
        
        if(serialVerboseMode) Serial.printf("[L%d] Delaying %d ms before next reading\n", __LINE__, READING_INTERVAL);
        delay(READING_INTERVAL);
    }
    
    Serial.printf("Collected %d valid readings out of %d attempts\n", sampleCount, READINGS_PER_CYCLE);
    if(serialVerboseMode) Serial.printf("[L%d] collectSensorReadings() completed\n", __LINE__);
}

void goToSleep() {
    if(serialVerboseMode) Serial.printf("[L%d] goToSleep() started\n", __LINE__);
    Serial.println("Configuring deep sleep...");
    
    // Ensure WiFi is properly disabled
    if(serialVerboseMode) Serial.printf("[L%d] Ensuring WiFi is fully disabled\n", __LINE__);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    #ifdef ESP32
    esp_wifi_stop();
    
    // Configure wake up timer
    if(serialVerboseMode) Serial.printf("[L%d] Configuring wake timer (%llu microseconds)\n", __LINE__, SLEEP_TIME_US);
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);
    
    Serial.println("Entering deep sleep now");
    if(serialVerboseMode) Serial.printf("[L%d] About to enter deep sleep\n", __LINE__);
    Serial.flush();
    esp_deep_sleep_start();
    
    #elif defined(ESP8266)
    // Configure wake up timer (ESP8266 uses different function)
    uint32_t sleep_time_seconds = SLEEP_TIME_US / 1000000;
    if(serialVerboseMode) Serial.printf("[L%d] Configuring wake timer (%d seconds)\n", __LINE__, sleep_time_seconds);
    
    Serial.println("Entering deep sleep now");
    if(serialVerboseMode) Serial.printf("[L%d] About to enter deep sleep\n", __LINE__);
    Serial.flush();
    ESP.deepSleep(SLEEP_TIME_US);
    #endif
}

void scanI2CDevices() {
    Serial.println("\n=== I2C Device Scanner ===");
    Serial.printf("Scanning I2C bus (SDA:%d, SCL:%d)...\n", SDA_PIN, SCL_PIN);
    
    byte error, address;
    int nDevices = 0;
    
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.printf("I2C device found at address 0x%02X", address);
            if (address == 0x76 || address == 0x77) {
                Serial.print(" <- This could be BME280/BMP280!");
            }
            Serial.println();
            nDevices++;
        }
        else if (error == 4) {
            Serial.printf("Unknown error at address 0x%02X\n", address);
        }
    }
    
    if (nDevices == 0) {
        Serial.println("No I2C devices found!");
        Serial.println("\nTroubleshooting tips:");
        Serial.println("1. Check wiring:");
        Serial.println("   BMP280 VCC -> 3.3V (NOT 5V!)");
        Serial.println("   BMP280 GND -> GND");
        Serial.printf("   BMP280 SDA -> D1 (GPIO%d)\n", SDA_PIN);
        Serial.printf("   BMP280 SCL -> D2 (GPIO%d)\n", SCL_PIN);
        Serial.println("2. Ensure sensor has power (LED should be on if present)");
        Serial.println("3. Check if you have BME280 instead of BMP280");
        Serial.println("4. Try different I2C pins if wiring is correct");
    } else {
        Serial.printf("Found %d I2C device(s)\n", nDevices);
    }
    Serial.println("========================\n");
}
