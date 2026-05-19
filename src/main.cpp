/**
 * ESP32 Greenhouse Monitoring System v2.0
 * Enhanced with DS18B20, LDR, PWM motor control, and NVS threshold storage
 * 
 * FreeRTOS Tasks (optimized priorities & core assignments):
 * 1. wifiTask        - Manages WiFi connection (Priority 2, Core 1)
 * 2. sensorTask      - Reads DHT11, DS18B20, LDR (Priority 2, Core 1)
 * 3. controlTask     - PWM motor control & threshold logic (Priority 3, Core 1) [NEW]
 * 4. httpTask        - Sends data & receives thresholds (Priority 1, Core 1)
 * 5. ledTask         - Status feedback (Priority 0, Core 0)
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP.h>
#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "secrets.h"

// Pin definitions
#define DHTPIN 4
#define DS18B20_PIN 13
#define LDR_PIN 32
#define FAN_PWM_PIN 18
#define LED_WIFI 0
#define LED_SEND 33
#define LED_HEALTH 2

// PWM configuration
#define PWM_FREQUENCY 5000  // 5 kHz
#define PWM_RESOLUTION 8    // 8-bit (0-255)
#define PWM_CHANNEL 0

// Queues
QueueHandle_t sensorDataQueue = NULL;
QueueHandle_t ledEventQueue = NULL;
QueueHandle_t controlDataQueue = NULL;

// Global variables & Objects
WiFiMulti wifiMulti;
DHT dht(DHTPIN, DHT11);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
Preferences preferences;

volatile bool wifiConnected = false;

// DATA STRUCTURES & HELPERS

// Control thresholds (stored in NVS)
typedef struct {
    float temp_threshold_1;     // Temp warning threshold (°C)
    float temp_threshold_2;     // Temp critical threshold (°C)
    float humidity_threshold;   // Humidity warning threshold (%)
    float light_threshold;      // Light intensity threshold (%)
} ControlThresholds_t;

// Sensor data
typedef struct {
    float temperature_dht;      // DHT11 temperature (°C)
    float temperature_ds;       // DS18B20 temperature (°C)
    float humidity;             // DHT11 humidity (%)
    float light_intensity;      // LDR intensity (0-100%)
    uint8_t fan_speed;          // PWM output (0-255)
    bool valid;
    bool from_control;
} ExtendedSensorData_t;

// LED event enum (for visual feedback)
typedef enum {
    LED_WIFI_CONNECTED,
    LED_WIFI_DISCONNECTED,
    LED_SEND_SUCCESS,
    LED_SEND_FAILURE,
    LED_SENSOR_ERROR
} LedEvent_t;

// Motor control state for hysteresis
typedef enum {
    MOTOR_OFF = 0,
    MOTOR_L1 = 1,  // Warning level - 30% speed
    MOTOR_L2 = 2   // Critical level - 50% speed
} MotorState_t;

volatile MotorState_t currentMotorState = MOTOR_OFF;

// NVS Threshold Management
void initializeNVS() {
    preferences.begin("thresholds", false);  // namespace = "thresholds", readOnly = false
    
    // Set defaults if not already set
    if (!preferences.isKey("temp_th1")) {
        preferences.putFloat("temp_th1", 28.0);
        preferences.putFloat("temp_th2", 32.0);
        preferences.putFloat("hum_th", 70.0);
        preferences.putFloat("light_th", 50.0);
        Serial.println("[NVS] Initialized default thresholds");
    }
}

ControlThresholds_t loadThresholds() {
    ControlThresholds_t thresholds;
    thresholds.temp_threshold_1 = preferences.getFloat("temp_th1", 28.0);
    thresholds.temp_threshold_2 = preferences.getFloat("temp_th2", 32.0);
    thresholds.humidity_threshold = preferences.getFloat("hum_th", 70.0);
    thresholds.light_threshold = preferences.getFloat("light_th", 50.0);
    return thresholds;
}

void updateThreshold(const char* key, float value) {
    preferences.putFloat(key, value);
    Serial.printf("[NVS] Updated %s = %.2f\n", key, value);
}

void fetchThresholdsFromBackend() {
    if (!wifiConnected) {
        Serial.println("[Thresholds] WiFi not connected, skipping fetch");
        return;
    }
    
    HTTPClient http;
    String serverURL = SERVER_URL;
    serverURL = serverURL.substring(0, serverURL.indexOf("/api/readings"));
    serverURL += "/api/thresholds";
    
    Serial.print("[Thresholds] Fetching from: ");
    Serial.println(serverURL);
    
    WiFiClient client;
    http.begin(client, serverURL);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        Serial.println("[Thresholds] Response: " + payload);
        
        // Properly parse JSON: find key, then colon, then extract value between colon and comma
        int pos;
        int colonPos, commaPos;
        
        // Parse temp_threshold_1
        pos = payload.indexOf("temp_threshold_1");
        if (pos > 0) {
            colonPos = payload.indexOf(":", pos);
            commaPos = payload.indexOf(",", colonPos);
            if (colonPos > 0 && commaPos > colonPos) {
                float val = payload.substring(colonPos + 1, commaPos).toFloat();
                Serial.printf("[Thresholds] Parsed temp_th1 = %.2f\n", val);
                updateThreshold("temp_th1", val);
            }
        }
        
        // Parse temp_threshold_2
        pos = payload.indexOf("temp_threshold_2");
        if (pos > 0) {
            colonPos = payload.indexOf(":", pos);
            commaPos = payload.indexOf(",", colonPos);
            if (colonPos > 0 && commaPos > colonPos) {
                float val = payload.substring(colonPos + 1, commaPos).toFloat();
                Serial.printf("[Thresholds] Parsed temp_th2 = %.2f\n", val);
                updateThreshold("temp_th2", val);
            }
        }
        
        // Parse humidity_threshold
        pos = payload.indexOf("humidity_threshold");
        if (pos > 0) {
            colonPos = payload.indexOf(":", pos);
            commaPos = payload.indexOf(",", colonPos);
            if (colonPos > 0 && commaPos > colonPos) {
                float val = payload.substring(colonPos + 1, commaPos).toFloat();
                Serial.printf("[Thresholds] Parsed humidity_threshold = %.2f\n", val);
                updateThreshold("hum_th", val);
            }
        }
        
        // Parse light_threshold (last one - might not have comma after)
        pos = payload.indexOf("light_threshold");
        if (pos > 0) {
            colonPos = payload.indexOf(":", pos);
            commaPos = payload.indexOf("}", colonPos);  // Find closing brace instead of comma
            if (colonPos > 0 && commaPos > colonPos) {
                float val = payload.substring(colonPos + 1, commaPos).toFloat();
                Serial.printf("[Thresholds] Parsed light_threshold = %.2f\n", val);
                updateThreshold("light_th", val);
            }
        }
        
        Serial.println("[Thresholds] Updated from backend ✓");
    } else {
        Serial.printf("[Thresholds] GET failed: %d\n", httpCode);
    }
    
    http.end();
}

// LDR Helper - Convert ADC to 0-100%
float readLightIntensity() {
    // Read LDR (ADC4)
    // Typical LDR: ~500 (dark) to ~4000 (bright) in 12-bit ADC
    // We'll use 0-4095 range and convert to percentage
    int rawValue = analogRead(LDR_PIN);
    
    // Map 0-4095 to 0-100%
    float percentage = (rawValue / 4095.0) * 100.0;
    percentage = constrain(percentage, 0.0, 100.0);
    
    return percentage;
}

// Fan PWM Control Logic
uint8_t calculateFanSpeed(float temp_dht, float humidity, ControlThresholds_t thresholds) {
    // Hysteresis buffers to prevent rapid on/off switching
    const float TEMP_BUFFER = 0.5;  // 0.5°C buffer zone
    const float HUM_BUFFER = 2.0;   // 2% buffer zone
    
    // L2 (50% speed) - Critical threshold
    // Turn ON at threshold + buffer, turn OFF at threshold - buffer
    if (currentMotorState == MOTOR_L2) {
        // Already at L2, stay there until we drop significantly below threshold
        if (temp_dht >= (thresholds.temp_threshold_2 - TEMP_BUFFER) || 
            humidity >= (thresholds.humidity_threshold + 5 - HUM_BUFFER)) {
            return 128;  // 50% (~128/255)
        }
        // Drop to L1 if conditions improve
        if (temp_dht >= (thresholds.temp_threshold_1 + TEMP_BUFFER) || 
            humidity >= (thresholds.humidity_threshold + HUM_BUFFER)) {
            currentMotorState = MOTOR_L1;
            return 77;   // 30% (~77/255)
        }
        // Drop to OFF
        currentMotorState = MOTOR_OFF;
        return 0;
    }
    
    // L1 (30% speed) - Warning threshold
    else if (currentMotorState == MOTOR_L1) {
        // Check if we need to escalate to L2
        if (temp_dht >= (thresholds.temp_threshold_2 + TEMP_BUFFER) || 
            humidity >= (thresholds.humidity_threshold + 5 + HUM_BUFFER)) {
            currentMotorState = MOTOR_L2;
            return 128;  // 50%
        }
        // Stay at L1 unless we drop significantly below threshold
        if (temp_dht >= (thresholds.temp_threshold_1 - TEMP_BUFFER) || 
            humidity >= (thresholds.humidity_threshold - HUM_BUFFER)) {
            return 77;   // 30%
        }
        // Drop to OFF
        currentMotorState = MOTOR_OFF;
        return 0;
    }
    
    // MOTOR_OFF state
    else {
        // Check if we should turn on at L1
        if (temp_dht >= (thresholds.temp_threshold_1 + TEMP_BUFFER) || 
            humidity >= (thresholds.humidity_threshold + HUM_BUFFER)) {
            currentMotorState = MOTOR_L1;
            return 77;   // 30%
        }
        // Check if we should jump directly to L2
        if (temp_dht >= (thresholds.temp_threshold_2 + TEMP_BUFFER) || 
            humidity >= (thresholds.humidity_threshold + 5 + HUM_BUFFER)) {
            currentMotorState = MOTOR_L2;
            return 128;  // 50%
        }
        return 0;
    }
}

void setPWM(uint8_t speed) {
    // speed: 0-255 (0% to 100%)
    ledcWrite(PWM_CHANNEL, speed);
}

// TASKS

// WiFi Task - Manages WiFi connectivity
void wifiTask(void *parameter) {
    wifiMulti.addAP(SSID, PASSWORD);

    while (1) {
        bool connected = (wifiMulti.run() == WL_CONNECTED);
        
        if (connected != wifiConnected) {
            wifiConnected = connected;
            
            LedEvent_t ledEvent;
            if (connected) {
                Serial.print("[WiFi] Connected, IP: ");
                Serial.println(WiFi.localIP());
                ledEvent = LED_WIFI_CONNECTED;
            } else {
                Serial.println("[WiFi] Disconnected");
                ledEvent = LED_WIFI_DISCONNECTED;
            }
            
            xQueueSend(ledEventQueue, &ledEvent, 0);
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Sensor Task - Reads DHT11, DS18B20, LDR independently
void sensorTask(void *parameter) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(5000);  // 5 seconds
    
    while (1) {
        ExtendedSensorData_t sensorData = {0};
        int sensorErrorCount = 0;
        
        // ===== Read DHT11 (Temperature & Humidity) =====
        float dht_humidity = dht.readHumidity();
        float dht_temperature = dht.readTemperature();
        
        if (isnan(dht_humidity) || isnan(dht_temperature)) {
            // DHT read failed
            sensorData.temperature_dht = -99.0;
            sensorData.humidity = 0.0;
            sensorErrorCount++;
            Serial.println("[Sensors] DHT11 read failed!");
        } else {
            sensorData.temperature_dht = dht_temperature;
            sensorData.humidity = dht_humidity;
        }
        
        // ===== Read DS18B20 (Temperature 2) =====
        ds18b20.requestTemperatures();
        float ds_temperature = ds18b20.getTempCByIndex(0);
        
        if (isnan(ds_temperature) || ds_temperature < -50.0 || ds_temperature > 125.0) {
            // DS18B20 read failed or out of valid range
            sensorData.temperature_ds = -99.0;
            sensorErrorCount++;
            Serial.println("[Sensors] DS18B20 read failed!");
        } else {
            sensorData.temperature_ds = ds_temperature;
        }
        
        // ===== Read LDR (Light Intensity) =====
        int rawValue = analogRead(LDR_PIN);
        if (rawValue < 0 || rawValue > 4095) {
            // LDR read out of range
            sensorData.light_intensity = 0.0;
            sensorErrorCount++;
            Serial.println("[Sensors] LDR read failed!");
        } else {
            float percentage = (rawValue / 4095.0) * 100.0;
            sensorData.light_intensity = constrain(percentage, 0.0, 100.0);
        }
        
        // ===== Summary =====
        sensorData.valid = (sensorErrorCount < 3);  // Valid if at least some sensors work
        
        if (sensorErrorCount == 0) {
            // All sensors OK
            Serial.printf("[Sensors] DHT-T=%.1f°C, DS18B20-T=%.1f°C, H=%.1f%%, Light=%.1f%% ✓\n",
                         sensorData.temperature_dht, sensorData.temperature_ds,
                         sensorData.humidity, sensorData.light_intensity);
        } else if (sensorErrorCount < 3) {
            // Some sensors failed, but we have data from others
            Serial.printf("[Sensors] Partial read (errors: %d) - DHT-T=%.1f°C, DS18B20-T=%.1f°C, H=%.1f%%, Light=%.1f%%\n",
                         sensorErrorCount, sensorData.temperature_dht, sensorData.temperature_ds,
                         sensorData.humidity, sensorData.light_intensity);
            LedEvent_t ledEvent = LED_SENSOR_ERROR;
            xQueueSend(ledEventQueue, &ledEvent, 0);
        } else {
            // All sensors failed
            Serial.println("[Sensors] All sensors failed!");
            sensorData.valid = false;
            LedEvent_t ledEvent = LED_SENSOR_ERROR;
            xQueueSend(ledEventQueue, &ledEvent, 0);
        }
        
        // Send to control task (always, for PWM updates)
        xQueueSend(controlDataQueue, &sensorData, 0);

        // Send to HTTP task if WiFi connected (for data upload)
        // This ensures HTTP gets the raw sensor data even if control task is busy
        if (wifiConnected && sensorData.valid) {
            sensorData.from_control = false;
            xQueueSend(sensorDataQueue, &sensorData, 0);
        }
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

// Control Task - Processes sensor data and controls PWM
void controlTask(void *parameter) {
    TickType_t lastThresholdFetch = xTaskGetTickCount();
    const TickType_t fetchFrequency = pdMS_TO_TICKS(60000);  // Fetch every 60 seconds
    
    while (1) {
        ExtendedSensorData_t sensorData;
        
        if (xQueueReceive(controlDataQueue, &sensorData, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (sensorData.valid) {
                ControlThresholds_t thresholds = loadThresholds();
                
                // Calculate fan speed based on thresholds
                sensorData.fan_speed = calculateFanSpeed(sensorData.temperature_dht, 
                                                         sensorData.humidity, 
                                                         thresholds);
                
                // Apply PWM
                setPWM(sensorData.fan_speed);
                
                Serial.printf("[Control] Motor State: %d, Fan Speed: %d/255 (%.1f%%)\n", 
                             currentMotorState,
                             sensorData.fan_speed, 
                             (sensorData.fan_speed / 255.0) * 100.0);
                
                // Re-send to HTTP task with PWM data
                sensorData.from_control = true;
                xQueueSend(sensorDataQueue, &sensorData, 0);
            } else {
                // Queue timeout - add brief delay to prevent spin-loop
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        
        // Periodically fetch thresholds from backend (every 60 seconds)
        if (xTaskGetTickCount() - lastThresholdFetch >= fetchFrequency) {
            fetchThresholdsFromBackend();
            lastThresholdFetch = xTaskGetTickCount();
        }
    }
}

// HTTP Task - Sends sensor data to backend
void httpTask(void *parameter) {
    while (1) {
        ExtendedSensorData_t sensorData;
        
        if (xQueueReceive(sensorDataQueue, &sensorData, portMAX_DELAY) == pdTRUE) {
            if (!sensorData.valid || !wifiConnected) {
                continue;
            }
            
            // Only send data that has been processed by controlTask (has fan_speed)
            if (!sensorData.from_control) {
                continue;  // Skip raw sensor data, wait for controlTask version
            }
            
            // Format extended JSON payload
            String payloadJson = R"JSON({"temperature":)JSON";
            payloadJson += String(sensorData.temperature_dht, 1);
            payloadJson += R"JSON(,"temperature2":)JSON";
            payloadJson += String(sensorData.temperature_ds, 1);
            payloadJson += R"JSON(,"humidity":)JSON";
            payloadJson += String(sensorData.humidity, 1);
            payloadJson += R"JSON(,"light_intensity":)JSON";
            payloadJson += String(sensorData.light_intensity, 1);
            payloadJson += R"JSON(,"fan_speed":)JSON";
            payloadJson += String((sensorData.fan_speed / 255.0) * 100.0, 1);
            payloadJson += R"JSON(})JSON";
            
            String serverURL = SERVER_URL;
            int httpCode = -1;
            LedEvent_t ledEvent = LED_SEND_FAILURE;

            Serial.print("[HTTP] Sending to: ");
            Serial.println(serverURL);

            // Handle HTTPS and HTTP separately with proper scope management
            if (serverURL.startsWith("https://")) {
                WiFiClientSecure client;
                client.setInsecure();
                HTTPClient http;
                http.begin(client, serverURL);
                http.addHeader("Content-Type", "application/json");
                httpCode = http.POST(payloadJson);
                
                if (httpCode > 0) {
                    Serial.printf("[HTTP] POST success: %d ✅\n", httpCode);
                    ledEvent = LED_SEND_SUCCESS;
                } else {
                    Serial.printf("[HTTP] POST failed: %s ❌\n", http.errorToString(httpCode).c_str());
                }
                http.end();
            } else {
                WiFiClient client;
                HTTPClient http;
                http.begin(client, serverURL);
                http.addHeader("Content-Type", "application/json");
                httpCode = http.POST(payloadJson);
                
                if (httpCode > 0) {
                    Serial.printf("[HTTP] POST success: %d ✅\n", httpCode);
                    ledEvent = LED_SEND_SUCCESS;
                } else {
                    Serial.printf("[HTTP] POST failed: %s ❌\n", http.errorToString(httpCode).c_str());
                }
                http.end();
            }
            
            // Send LED feedback event
            xQueueSend(ledEventQueue, &ledEvent, 0);
        }
    }
}

// LED Task - Visual status feedback
// Priority 0 (lowest) - non-critical feedback
void ledTask(void *parameter) {
    pinMode(LED_WIFI, OUTPUT);
    pinMode(LED_SEND, OUTPUT);
    pinMode(LED_HEALTH, OUTPUT);
    
    digitalWrite(LED_WIFI, LOW);
    digitalWrite(LED_SEND, LOW);
    digitalWrite(LED_HEALTH, LOW);
    
    while (1) {
        LedEvent_t event;
        
        if (xQueueReceive(ledEventQueue, &event, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (event) {
                case LED_WIFI_CONNECTED:
                    digitalWrite(LED_WIFI, HIGH);
                    break;
                    
                case LED_WIFI_DISCONNECTED:
                    for (int i = 0; i < 3; i++) {
                        digitalWrite(LED_WIFI, HIGH);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        digitalWrite(LED_WIFI, LOW);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    break;
                    
                case LED_SEND_SUCCESS:
                    digitalWrite(LED_SEND, HIGH);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    digitalWrite(LED_SEND, LOW);
                    break;
                    
                case LED_SEND_FAILURE:
                    for (int i = 0; i < 5; i++) {
                        digitalWrite(LED_SEND, HIGH);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        digitalWrite(LED_SEND, LOW);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    break;
                    
                case LED_SENSOR_ERROR:
                    digitalWrite(LED_HEALTH, HIGH);
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    digitalWrite(LED_HEALTH, LOW);
                    break;
            }
        } else {
            // Keep health LED slowly pulsing when system is OK
            if (wifiConnected) {
                digitalWrite(LED_HEALTH, HIGH);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                digitalWrite(LED_HEALTH, LOW);
                vTaskDelay(900 / portTICK_PERIOD_MS);
            }
        }
    }
}

// SETUP

void setup() {
    Serial.begin(115200);
    
    // Initial delay for serial monitor
    for (uint8_t t = 4; t > 0; t--) {
        Serial.printf("[SETUP] Starting in %d seconds...\n", t);
        delay(1000);
    }
    
    Serial.println("\n=== ESP32 Greenhouse Monitoring v2.0 ===");
    
    // Initialize sensors
    dht.begin();
    ds18b20.begin();
    
    // Initialize PWM for fan
    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);  // Start with fan off
    Serial.println("[Setup] Fan PWM initialized");
    
    // Initialize NVS for thresholds
    initializeNVS();
    ControlThresholds_t thresholds = loadThresholds();
    Serial.printf("[Setup] Thresholds - T1:%.1f T2:%.1f H:%.1f L:%.1f\n",
                 thresholds.temp_threshold_1, thresholds.temp_threshold_2,
                 thresholds.humidity_threshold, thresholds.light_threshold);
    
    // Create queues
    sensorDataQueue = xQueueCreate(5, sizeof(ExtendedSensorData_t));
    ledEventQueue = xQueueCreate(5, sizeof(LedEvent_t));
    controlDataQueue = xQueueCreate(5, sizeof(ExtendedSensorData_t));
    
    // Create tasks with optimized priorities
    // Priority 2 - WiFi management (important for connectivity)
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 4096, NULL, 2, NULL, 1);
    
    // Priority 2 - Sensor reading (important for data quality)
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, NULL, 1);
    
    // Priority 3 - Control (real-time PWM control)
    xTaskCreatePinnedToCore(controlTask, "ControlTask", 4096, NULL, 3, NULL, 1);
    
    // Priority 1 - HTTP (can tolerate delays, doesn't block others)
    xTaskCreatePinnedToCore(httpTask, "HTTPTask", 8192, NULL, 1, NULL, 1);
    
    // Priority 0 - LEDs (lowest, just visual feedback)
    xTaskCreatePinnedToCore(ledTask, "LEDTask", 2048, NULL, 0, NULL, 0);
    
    Serial.println("[Setup] All tasks created. System ready!\n");
}

void loop() {}
