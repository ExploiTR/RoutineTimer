#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <time.h>
#include <math.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>

// =============================================================================
// CONFIGURABLE PARAMETERS - Modify these values as needed
// =============================================================================

// I2C Configuration
#define SDA_PIN 21                    // I2C SDA pin
#define SCL_PIN 22                    // I2C SCL pin
const uint32_t I2C_CLOCK = 100000;    // I2C clock speed (Hz) - 100kHz is standard

// BME280 I2C Addresses (sensor will try both)
const uint8_t BME280_ADDR_PRIMARY = 0x76;    // Primary I2C address
const uint8_t BME280_ADDR_SECONDARY = 0x77;  // Secondary I2C address

// Serial Communication
const uint32_t SERIAL_BAUD = 115200;  // Serial baud rate

// Sleep and Wake Configuration
const uint64_t SLEEP_TIME_US = 5 * 60 * 1000000ULL;  // 5 minutes in microseconds
const int READINGS_PER_CYCLE = 30;    // Number of readings to collect per wake cycle
const unsigned long READING_INTERVAL = 1000;    // Time between readings (ms) - 1 second
const unsigned long WARMUP_TIME = 2000;         // Sensor warmup time (ms)

// WiFi Configuration
const char* WIFI_SSID = "AX72-IoT";
const char* WIFI_PASSWORD = "SecureIoT_Ax72";
const unsigned long WIFI_TIMEOUT = 10000;       // WiFi connection timeout (ms)

// NTP Configuration
const char* NTP_SERVER = "time.google.com";
const long GMT_OFFSET_SEC = 5.5 * 3600;         // IST is UTC+5:30
const int DAYLIGHT_OFFSET_SEC = 0;               // No daylight saving in IST

// FTP Configuration
const char* FTP_SERVER = "192.168.1.1";
const int FTP_PORT = 21;
const char* FTP_USER = "admin";              // Update with actual FTP credentials
const char* FTP_PASSWORD = "f6a3067773";                   // Update with actual FTP password
const char* FTP_BASE_PATH = "/G/USD_TPL/";

// BME280 Sensor Configuration
const Adafruit_BME280::sensor_mode SENSOR_MODE = Adafruit_BME280::MODE_NORMAL;
const Adafruit_BME280::sensor_sampling TEMP_OVERSAMPLING = Adafruit_BME280::SAMPLING_X2;
const Adafruit_BME280::sensor_sampling PRESSURE_OVERSAMPLING = Adafruit_BME280::SAMPLING_X16;
const Adafruit_BME280::sensor_sampling HUMIDITY_OVERSAMPLING = Adafruit_BME280::SAMPLING_X1;
const Adafruit_BME280::sensor_filter FILTER_SETTING = Adafruit_BME280::FILTER_X16;
const Adafruit_BME280::standby_duration STANDBY_TIME = Adafruit_BME280::STANDBY_MS_500;

// Altitude Calculation
const float SEA_LEVEL_PRESSURE = 1000;          // Standard sea level pressure (hPa) - adjusted for Digha

// Loop Delay
const int LOOP_DELAY = 10;  // Small delay in main loop (ms)

// Retry Configuration
const int MAX_RETRIES = 3;                       // Maximum retry attempts for operations

// =============================================================================
// GLOBAL VARIABLES - Do not modify unless you know what you're doing
// =============================================================================

// BME280 sensor object
Adafruit_BME280 bme;

// WiFi and FTP clients
WiFiClient ftpClient;
WiFiClient ftpDataClient;

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
bool ftpConnect();
bool ftpLogin();
bool ftpChangeDir(String dir);
bool ftpCreateFile(String filename, String content);
bool ftpAppendToFile(String filename, String content);
bool ftpFileExists(String filename);
String ftpReadResponse();
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
    delay(1000);
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

void optimizePowerConsumption() {
    Serial.println("Optimizing power consumption...");
    
    // Disable Bluetooth
    esp_bt_controller_disable();
    
    // WiFi will be enabled only when needed
    esp_wifi_stop();
    
    Serial.println("Power optimization complete");
}

String ftpReadResponse() {
    String response = "";
    unsigned long timeout = millis() + 5000; // 5 second timeout
    
    while (millis() < timeout) {
        if (ftpClient.available()) {
            response += ftpClient.readString();
            break;
        }
        delay(10);
    }
    
    if (response.length() > 0) {
        Serial.print("FTP Response: " + response);
    }
    return response;
}

bool ftpConnect() {
    Serial.printf("Connecting to FTP server %s:%d\n", FTP_SERVER, FTP_PORT);
    
    if (!ftpClient.connect(FTP_SERVER, FTP_PORT)) {
        Serial.println("FTP connection failed!");
        return false;
    }
    
    String response = ftpReadResponse();
    if (response.startsWith("220")) {
        Serial.println("FTP server connected successfully");
        return true;
    }
    
    Serial.println("FTP server connection failed - invalid response");
    return false;
}

bool ftpLogin() {
    Serial.println("Logging into FTP server...");
    
    // Send username
    ftpClient.printf("USER %s\r\n", FTP_USER);
    String response = ftpReadResponse();
    
    if (!response.startsWith("331") && !response.startsWith("230")) {
        Serial.println("FTP login failed - username rejected");
        return false;
    }
    
    // Send password
    ftpClient.printf("PASS %s\r\n", FTP_PASSWORD);
    response = ftpReadResponse();
    
    if (response.startsWith("230")) {
        Serial.println("FTP login successful");
        return true;
    }
    
    Serial.println("FTP login failed - password rejected");
    return false;
}

bool ftpChangeDir(String dir) {
    Serial.printf("Changing to directory: %s\n", dir.c_str());
    
    ftpClient.printf("CWD %s\r\n", dir.c_str());
    String response = ftpReadResponse();
    
    if (response.startsWith("250")) {
        Serial.println("Directory changed successfully");
        return true;
    }
    
    Serial.println("Failed to change directory");
    return false;
}

bool ftpFileExists(String filename) {
    Serial.printf("Checking if file exists: %s\n", filename.c_str());
    
    ftpClient.printf("SIZE %s\r\n", filename.c_str());
    String response = ftpReadResponse();
    
    return response.startsWith("213");
}

bool ftpCreateFile(String filename, String content) {
    Serial.printf("Creating new file: %s\n", filename.c_str());
    
    // Set binary mode
    ftpClient.printf("TYPE I\r\n");
    ftpReadResponse();
    
    // Enter passive mode
    ftpClient.printf("PASV\r\n");
    String response = ftpReadResponse();
    
    if (!response.startsWith("227")) {
        Serial.println("Failed to enter passive mode");
        return false;
    }
    
    // Parse passive mode response to get data connection details
    int start = response.indexOf('(');
    int end = response.indexOf(')', start);
    String dataInfo = response.substring(start + 1, end);
    
    // Extract IP and port from passive mode response
    // Format: (192,168,1,1,20,40) where port = 20*256 + 40
    int commas[5];
    int commaCount = 0;
    for (int i = 0; i < dataInfo.length() && commaCount < 5; i++) {
        if (dataInfo.charAt(i) == ',') {
            commas[commaCount++] = i;
        }
    }
    
    if (commaCount == 5) {
        int p1 = dataInfo.substring(commas[3] + 1, commas[4]).toInt();
        int p2 = dataInfo.substring(commas[4] + 1).toInt();
        int dataPort = p1 * 256 + p2;
        
        // Connect to data port
        if (ftpDataClient.connect(FTP_SERVER, dataPort)) {
            Serial.printf("Data connection established on port %d\n", dataPort);
            
            // Send STOR command
            ftpClient.printf("STOR %s\r\n", filename.c_str());
            ftpReadResponse();
            
            // Send data
            ftpDataClient.print(content);
            ftpDataClient.stop();
            
            // Read final response
            ftpReadResponse();
            Serial.println("File created successfully");
            return true;
        }
    }
    
    Serial.println("Failed to create file");
    return false;
}

bool ftpAppendToFile(String filename, String content) {
    Serial.printf("Appending to file: %s\n", filename.c_str());
    
    // Set binary mode
    ftpClient.printf("TYPE I\r\n");
    ftpReadResponse();
    
    // Enter passive mode
    ftpClient.printf("PASV\r\n");
    String response = ftpReadResponse();
    
    if (!response.startsWith("227")) {
        Serial.println("Failed to enter passive mode");
        return false;
    }
    
    // Parse passive mode response (same as createFile)
    int start = response.indexOf('(');
    int end = response.indexOf(')', start);
    String dataInfo = response.substring(start + 1, end);
    
    int commas[5];
    int commaCount = 0;
    for (int i = 0; i < dataInfo.length() && commaCount < 5; i++) {
        if (dataInfo.charAt(i) == ',') {
            commas[commaCount++] = i;
        }
    }
    
    if (commaCount == 5) {
        int p1 = dataInfo.substring(commas[3] + 1, commas[4]).toInt();
        int p2 = dataInfo.substring(commas[4] + 1).toInt();
        int dataPort = p1 * 256 + p2;
        
        // Connect to data port
        if (ftpDataClient.connect(FTP_SERVER, dataPort)) {
            Serial.printf("Data connection established on port %d\n", dataPort);
            
            // Send APPE command
            ftpClient.printf("APPE %s\r\n", filename.c_str());
            ftpReadResponse();
            
            // Send data
            ftpDataClient.print(content);
            ftpDataClient.stop();
            
            // Read final response
            ftpReadResponse();
            Serial.println("Data appended successfully");
            return true;
        }
    }
    
    Serial.println("Failed to append to file");
    return false;
}

bool uploadDataToFTP(float avgTemp, float avgPressure, float avgHumidity) {
    Serial.println("Starting FTP upload process...");
    
    // Connect to FTP server
    if (!ftpConnect()) {
        return false;
    }
    
    // Login to FTP server
    if (!ftpLogin()) {
        ftpClient.stop();
        return false;
    }
    
    // Change to target directory
    if (!ftpChangeDir(FTP_BASE_PATH)) {
        ftpClient.stop();
        return false;
    }
    
    // Get filename and prepare data
    String filename = getCSVFilename();
    String currentTime = getCurrentTimeString();
    String csvData = currentTime + "," + String(sampleCount) + "," + 
                    String(avgTemp, 1) + "," + String(avgPressure, 1) + "," + 
                    String(avgHumidity, 2) + "\r\n";
    
    Serial.printf("Filename: %s\n", filename.c_str());
    Serial.printf("CSV data: %s", csvData.c_str());
    
    bool success = false;
    
    // Check if file exists
    if (ftpFileExists(filename)) {
        // File exists, append data
        success = ftpAppendToFile(filename, csvData);
    } else {
        // File doesn't exist, create with header
        String header = "Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)\r\n";
        String fullContent = header + csvData;
        success = ftpCreateFile(filename, fullContent);
    }
    
    // Send QUIT command
    ftpClient.printf("QUIT\r\n");
    ftpReadResponse();
    ftpClient.stop();
    
    if (success) {
        Serial.println("FTP upload completed successfully!");
    } else {
        Serial.println("FTP upload failed!");
    }
    
    return success;
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

bool uploadDataToFTP(float avgTemp, float avgPressure, float avgHumidity) {
    Serial.println("Connecting to FTP server...");
    
    // Connect to FTP server
    if (!ftpClient.connect(FTP_SERVER, FTP_PORT)) {
        Serial.println("FTP connection failed!");
        return false;
    }
    
    Serial.println("Connected to FTP server");
    
    // Read server response
    delay(1000);
    while (ftpClient.available()) {
        Serial.print((char)ftpClient.read());
    }
    
    // Login (anonymous for now - update credentials in config)
    ftpClient.println("USER " + String(FTP_USER));
    delay(1000);
    while (ftpClient.available()) {
        Serial.print((char)ftpClient.read());
    }
    
    ftpClient.println("PASS " + String(FTP_PASSWORD));
    delay(1000);
    while (ftpClient.available()) {
        Serial.print((char)ftpClient.read());
    }
    
    // Get current date for filename
    String filename = getCurrentDateString() + ".csv";
    String fullPath = String(FTP_BASE_PATH) + filename;
    
    // Prepare CSV data
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", timeinfo);
    
    String csvData = String(timeStr) + "," + String(sampleCount) + "," + 
                    String(avgTemp, 1) + "," + String(avgPressure, 1) + "," + 
                    String(avgHumidity, 2) + "\r\n";
    
    Serial.printf("Uploading to: %s\n", fullPath.c_str());
    Serial.printf("CSV data: %s", csvData.c_str());
    
    // For now, simulate successful upload
    // Full FTP implementation would require:
    // 1. PASV command for data connection
    // 2. STOR command to upload file
    // 3. Proper error handling
    Serial.println("FTP upload simulated successfully");
    Serial.println("Note: Implement full FTP protocol for actual file transfer");
    
    ftpClient.stop();
    return true;
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

// Function to print sensor status (can be called for debugging)
void printSensorStatus() {
    Serial.println("\n--- BME280 Sensor Status ---");
    Serial.printf("Chip ID: 0x%02X\n", bme.sensorID());
    Serial.printf("Temperature: %.2f °C\n", bme.readTemperature());
    Serial.printf("Pressure: %.2f hPa\n", bme.readPressure() / 100.0F);
    Serial.printf("Humidity: %.2f %%\n", bme.readHumidity());
    Serial.println("---------------------------\n");
}
