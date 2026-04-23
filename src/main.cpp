/**
 * ESP32 DHT11 -> Go Backend (JSON HTTP POST) - FreeRTOS Version
 * 
 * FreeRTOS Tasks:
 * 1. wifiTask        - Manages WiFi connection
 * 2. sensorTask      - Reads DHT11 sensor data
 * 3. httpTask        - Sends data to backend server
 * 4. ledTask         - Handles LED feedback
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ESP.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "secrets.h"

// Pin Definitions
#define DHTPIN 4
#define LED_WIFI 2
#define LED_SEND 0

// Queues
QueueHandle_t sensorDataQueue = NULL;
QueueHandle_t ledEventQueue = NULL;

// Global Variables
WiFiMulti wifiMulti;
DHT dht(DHTPIN, DHT11);
volatile bool wifiConnected = false;

// Data Structures
typedef struct {
    float temperature;
    float humidity;
    bool valid;
} SensorData_t;

typedef enum {
    LED_WIFI_CONNECTED,
    LED_WIFI_DISCONNECTED,
    LED_SEND_SUCCESS,
    LED_SEND_FAILURE
} LedEvent_t;

// WiFi Task
void wifiTask(void *parameter) {
    wifiMulti.addAP(SSID, PASSWORD);

    while (1) {
        bool connected = (wifiMulti.run() == WL_CONNECTED);
        if (!connected) {
            LedEvent_t ledEvent = LED_WIFI_DISCONNECTED;
            xQueueSend(ledEventQueue, &ledEvent, portMAX_DELAY);
        }
        
        if (connected != wifiConnected) {
            wifiConnected = connected;
            
            if (connected) {
                Serial.print("[WiFi] Connected, IP: ");
                Serial.println(WiFi.localIP());

                LedEvent_t ledEvent = LED_WIFI_CONNECTED;
                xQueueSend(ledEventQueue, &ledEvent, portMAX_DELAY);
            } else {
                Serial.println("[WiFi] Disconnected");
                
                LedEvent_t ledEvent = LED_WIFI_DISCONNECTED;
                xQueueSend(ledEventQueue, &ledEvent, portMAX_DELAY);
            }
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Check WiFi every second
    }
}

// Sensor Task
void sensorTask(void *parameter) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(5000);  // 5 seconds
    
    while (1) {
        SensorData_t sensorData;
        
        // Read sensor data
        sensorData.humidity = dht.readHumidity();
        sensorData.temperature = dht.readTemperature();
        sensorData.valid = !(isnan(sensorData.humidity) || isnan(sensorData.temperature));
        
        if (sensorData.valid) {
            Serial.print("[DHT] T = ");
            Serial.print(sensorData.temperature, 1);
            Serial.print(" °C, H = ");
            Serial.print(sensorData.humidity, 1);
            Serial.println(" %");
        } else {
            Serial.println("[DHT] Read failed");
        }
        
        // Send data to HTTP task if WiFi is connected
        if (wifiConnected) {
            xQueueSend(sensorDataQueue, &sensorData, portMAX_DELAY);
        }
        
        // Wait for next cycle (5 seconds)
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

// HTTP Task
void httpTask(void *parameter) {
    while (1) {
        SensorData_t sensorData;
        
        // Wait for sensor data
        if (xQueueReceive(sensorDataQueue, &sensorData, portMAX_DELAY) == pdTRUE) {
            if (!sensorData.valid || !wifiConnected) {
                continue;
            }
            
            // Format JSON payload
            String payloadJson = R"JSON({"temperature":)JSON";
            payloadJson += String(sensorData.temperature, 1);
            payloadJson += R"JSON(,"humidity":)JSON";
            payloadJson += String(sensorData.humidity, 1);
            payloadJson += R"JSON(})JSON";
            
            // Create HTTP client
            HTTPClient http;
            String serverURL = "http://" SERVER_IP ":8000/api/readings";
            
            http.begin(serverURL);
            http.addHeader("Content-Type", "application/json");
            
            // Send POST request
            int httpCode = http.POST(payloadJson);
            
            LedEvent_t ledEvent;
            if (httpCode > 0) {
                Serial.printf("[HTTP] POST... code: %d ✅\n", httpCode);
                ledEvent = LED_SEND_SUCCESS;
            } else {
                Serial.printf("[HTTP] POST... failed, error: %s ❌\n", 
                             http.errorToString(httpCode).c_str());
                ledEvent = LED_SEND_FAILURE;
            }
            
            http.end();
            Serial.println();
            
            // Send LED feedback event
            xQueueSend(ledEventQueue, &ledEvent, portMAX_DELAY);
        }
    }
}

// LED Task
void ledTask(void *parameter) {
    pinMode(LED_WIFI, OUTPUT);
    pinMode(LED_SEND, OUTPUT);
    digitalWrite(LED_WIFI, LOW);
    digitalWrite(LED_SEND, LOW);
    
    while (1) {
        LedEvent_t event;
        
        // Wait for LED event
        if (xQueueReceive(ledEventQueue, &event, portMAX_DELAY) == pdTRUE) {
            switch (event) {
                case LED_WIFI_CONNECTED:
                    digitalWrite(LED_WIFI, HIGH);
                    break;
                    
                case LED_WIFI_DISCONNECTED:
                    for (int i = 0; i < 5; i++) {
                        digitalWrite(LED_WIFI, LOW);
                    }
                    break;
                    
                case LED_SEND_SUCCESS:
                    digitalWrite(LED_SEND, HIGH);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    digitalWrite(LED_SEND, LOW);
                    break;
                    
                case LED_SEND_FAILURE:
                    for (int i = 0; i < 10; i++) {
                        digitalWrite(LED_SEND, HIGH);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        digitalWrite(LED_SEND, LOW);
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    break;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initial delay for serial monitor
    for (uint8_t t = 4; t > 0; t--) {
        Serial.printf("[SETUP] WAIT %d...\n", t);
        delay(1000);
    }
    
    dht.begin();
    
    // Queues and semaphores
    sensorDataQueue = xQueueCreate(5, sizeof(SensorData_t));
    ledEventQueue = xQueueCreate(5, sizeof(LedEvent_t));
    
    // Tasks
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 4096, NULL, 2, NULL, 1);
    
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 1, NULL, 1);
    
    xTaskCreatePinnedToCore(httpTask, "HTTPTask", 8192, NULL, 2, NULL, 1);
    
    xTaskCreatePinnedToCore(ledTask, "LEDTask", 2048, NULL, 3, NULL, 0);
}

void loop() {}