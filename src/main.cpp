#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>

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

// Measurement Timing
const unsigned long READING_INTERVAL = 1000;    // Time between readings (ms) - 1 second
const unsigned long PRINT_INTERVAL = 30000;     // Time between prints (ms) - 30 seconds
const unsigned long WARMUP_TIME = 2000;         // Sensor warmup time (ms)

// BME280 Sensor Configuration
const Adafruit_BME280::sensor_mode SENSOR_MODE = Adafruit_BME280::MODE_NORMAL;
const Adafruit_BME280::sensor_sampling TEMP_OVERSAMPLING = Adafruit_BME280::SAMPLING_X2;
const Adafruit_BME280::sensor_sampling PRESSURE_OVERSAMPLING = Adafruit_BME280::SAMPLING_X16;
const Adafruit_BME280::sensor_sampling HUMIDITY_OVERSAMPLING = Adafruit_BME280::SAMPLING_X1;
const Adafruit_BME280::sensor_filter FILTER_SETTING = Adafruit_BME280::FILTER_X16;
const Adafruit_BME280::standby_duration STANDBY_TIME = Adafruit_BME280::STANDBY_MS_500;

// Altitude Calculation
const float SEA_LEVEL_PRESSURE = 1000; //1013.25;  // Standard sea level pressure (hPa) - adjusted for Digha

// Loop Delay
const int LOOP_DELAY = 10;  // Small delay in main loop (ms)

// =============================================================================
// GLOBAL VARIABLES - Do not modify unless you know what you're doing
// =============================================================================

// BME280 sensor object
Adafruit_BME280 bme;

// Timing variables
unsigned long lastReading = 0;
unsigned long lastPrint = 0;

// Data collection variables
float tempSum = 0;
float pressureSum = 0;
float humiditySum = 0;
int sampleCount = 0;

void setup() {
    // Initialize serial communication
    Serial.begin(SERIAL_BAUD);
    while (!Serial) {
        delay(10); // Wait for serial port to connect
    }
    
    Serial.println("\n=== BME280 Sensor Test ===");
    Serial.println("Testing Temperature, Pressure, and Humidity");
    Serial.printf("SDA Pin: %d, SCL Pin: %d\n", SDA_PIN, SCL_PIN);
    
    // Initialize I2C with custom pins
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_CLOCK);
    
    Serial.println("Initializing BME280 sensor...");
    
    // Try to initialize BME280 with primary address
    if (!bme.begin(BME280_ADDR_PRIMARY, &Wire)) {
        Serial.printf("Could not find BME280 sensor at 0x%02X, trying 0x%02X...\n", 
                     BME280_ADDR_PRIMARY, BME280_ADDR_SECONDARY);
        
        // Try alternative address
        if (!bme.begin(BME280_ADDR_SECONDARY, &Wire)) {
            Serial.printf("Could not find BME280 sensor at 0x%02X either!\n", BME280_ADDR_SECONDARY);
            Serial.println("Check wiring and I2C address:");
            Serial.println("- VCC to 3.3V");
            Serial.println("- GND to GND");
            Serial.printf("- SDA to GPIO %d\n", SDA_PIN);
            Serial.printf("- SCL to GPIO %d\n", SCL_PIN);
            
            // Scan for I2C devices
            Serial.println("\nScanning for I2C devices...");
            for (byte address = 1; address < 127; address++) {
                Wire.beginTransmission(address);
                if (Wire.endTransmission() == 0) {
                    Serial.printf("I2C device found at address 0x%02X\n", address);
                }
            }
            
            while (1) {
                delay(1000);
            }
        } else {
            Serial.printf("BME280 found at address 0x%02X!\n", BME280_ADDR_SECONDARY);
        }
    } else {
        Serial.printf("BME280 found at address 0x%02X!\n", BME280_ADDR_PRIMARY);
    }
    
    // Configure BME280 settings
    Serial.println("Configuring BME280 settings...");
    bme.setSampling(SENSOR_MODE,           // Operating mode
                    TEMP_OVERSAMPLING,     // Temperature oversampling
                    PRESSURE_OVERSAMPLING, // Pressure oversampling
                    HUMIDITY_OVERSAMPLING, // Humidity oversampling
                    FILTER_SETTING,        // Filtering
                    STANDBY_TIME);         // Standby time
    
    Serial.println("BME280 initialized successfully!");
    
    // Allow sensor to warm up and stabilize
    Serial.printf("Allowing sensor to warm up (%lu ms)...\n", WARMUP_TIME);
    delay(WARMUP_TIME);
    
    Serial.println("Starting measurements...");
}

void loop() {
    unsigned long currentTime = millis();
    
    // Read sensor data every second
    if (currentTime - lastReading >= READING_INTERVAL) {
        lastReading = currentTime;
        
        // Read sensor values
        float temperature = bme.readTemperature();
        float pressure = bme.readPressure() / 100.0F; // Convert Pa to hPa
        float humidity = bme.readHumidity();
        
        // Check if readings are valid
        if (!isnan(temperature) && !isnan(pressure) && !isnan(humidity)) {
            // Accumulate readings for averaging
            tempSum += temperature;
            pressureSum += pressure;
            humiditySum += humidity;
            sampleCount++;
        }
    }
    
    // Print averaged data every 30 seconds
    if (currentTime - lastPrint >= PRINT_INTERVAL && sampleCount > 0) {
        lastPrint = currentTime;
        
        // Calculate averages
        float avgTemp = tempSum / sampleCount;
        float avgPressure = pressureSum / sampleCount;
        float avgHumidity = humiditySum / sampleCount;
        
        // Get current date and time (simulated - you may want to use RTC)
        // For now, using millis() to create a timestamp
        unsigned long totalMinutes = currentTime / 60000; // Convert ms to minutes
        int hours = (totalMinutes / 60) % 24;
        int minutes = totalMinutes % 60;
        
        // Format: DDMMYYYY_HH_MM SampleSize Temperature Pressure Humidity
        // Using a fixed date for demo - you can integrate RTC for real date/time
        Serial.printf("29072025_%02d_%02d %d %.1f %.1f %.2f\n", 
                     hours, minutes, sampleCount, avgTemp, avgPressure, avgHumidity);
        
        // Reset accumulators
        tempSum = 0;
        pressureSum = 0;
        humiditySum = 0;
        sampleCount = 0;
    }
    
    // Small delay to prevent overwhelming the system
    delay(LOOP_DELAY);
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
