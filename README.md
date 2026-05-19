# ESP32 Greenhouse Monitoring & Control System - Technical Summary

## 1. System Overview

This project implements an autonomous greenhouse environmental monitoring and control system built on the ESP32 microcontroller. The system continuously monitors temperature (dual-sensor), humidity, and light intensity, then autonomously controls a PWM-driven ventilation motor based on configurable thresholds. Thresholds are persisted locally via NVS (non-volatile storage) and can be updated remotely via a web-based dashboard.

**Key Features:**
- Real-time multi-sensor monitoring with individual error handling
- Dual-tier motor control (L1/L2 at 30%/50% PWM duty cycles)
- Hysteresis-based control to prevent rapid switching
- Web-based threshold management without HTTP replacement
- Resilient to individual sensor failures
- Backend persistence and threshold fetching
- Real-time dashboard with sensor visualization

---

## 2. Hardware Configuration

**Microcontroller:** ESP32 (upesy_wroom variant)
- Dual-core processor with FreeRTOS
- Upload port: COM7

**Pin Assignments:**
| Component | GPIO | Type | Purpose |
|-----------|------|------|---------|
| DHT11 | GPIO 4 | Digital | Temperature & Humidity |
| DS18B20 | GPIO 5 | 1-Wire | Secondary Temperature |
| LDR | GPIO 32 | ADC | Light Intensity (0-4095 → 0-100%) |
| Fan Motor PWM | GPIO 18 | LEDC | Motor speed control (5kHz, 8-bit) |
| WiFi LED | GPIO 2 | Digital | Status indicator |
| Send LED | GPIO 0 | Digital | Data transmission indicator |
| Health LED | GPIO 33 | Digital | System health pulse |

**Sensor Specifications:**
- **DHT11:** Digital temperature/humidity, ±2°C accuracy, read interval ≥2s
- **DS18B20:** 1-Wire temperature sensor, ±0.5°C accuracy, requires 4.7kΩ pull-up resistor
- **LDR:** Light sensor via voltage divider, minimum practical reading ~20% (hardware limitation)

**Motor Control:**
- LEDC PWM: 5kHz frequency, 8-bit resolution (0-255)
- L1 speed: 77/255 (~30% duty cycle)
- L2 speed: 128/255 (~50% duty cycle)
- Hysteresis buffers: ±0.5°C temperature, ±2% humidity to prevent rapid switching

---

## 3. Firmware Architecture

### Task Structure (FreeRTOS)

Five concurrent tasks run on the dual-core ESP32, with priority-based scheduling and core assignment:

| Task | Priority | Core | Stack | Frequency | Purpose |
|------|----------|------|-------|-----------|---------|
| wifiTask | 3 | 1 | 4096 | Continuous | WiFi connection monitoring (foundational) |
| sensorTask | 2 | 1 | 4096 | 5s cycle | Sensor data acquisition |
| httpTask | 1 | 1 | 8192 | Event-driven | Backend data transmission (blocking I/O) |
| controlTask | 1 | 0 | 4096 | Event-driven | PWM calculation & threshold fetching (hardware LEDC independent) |
| ledTask | 0 | 0 | 2048 | Event-driven | Visual status feedback |

### Task Priority Rationale

- **Core 1 (Real-time I/O):** WiFi (3), Sensors (2), HTTP (1) handle the demanding networking and data acquisition workloads
- **Core 0 (Lightweight Control):** ControlTask (1), LEDTask (0) perform simple message handling and PWM updates
- **WiFiTask priority boosted to 3:** WiFi connectivity is foundational; all other tasks depend on it
- **ControlTask deprioritized to 1:** Hardware LEDC PWM controller runs independently of CPU; task only needs to update registers when thresholds cross (infrequent in stable greenhouse environments)
- **ControlTask moved to Core 0:** Reduces contention on Core 1 for networking operations

### Data Flow

```
sensorTask (reads sensors)
    ↓
controlDataQueue
    ↓
controlTask (calculates PWM + fan_speed, fetches thresholds every 60s)
    ↓
sensorDataQueue (only from_control=true data)
    ↓
httpTask (sends to backend)
    ↓
Backend API
```

### Inter-Task Communication

**Queues:**
- `controlDataQueue`: sensorTask → controlTask (raw sensor data)
- `sensorDataQueue`: controlTask → httpTask (control-enhanced data with fan_speed)
- `ledEventQueue`: wifiTask/sensorTask/httpTask → ledTask (status events)

All queues use non-blocking sends with zero timeout (`xQueueSend(..., 0)`).

### Critical Data Structures

```cpp
typedef struct {
    float temperature_dht;
    float temperature_ds;
    float humidity;
    float light_intensity;
    float fan_speed;
    bool valid;
    bool from_control;  // Tracks data source for deduplication
} ExtendedSensorData_t;

typedef struct {
    float temp_threshold_1;
    float temp_threshold_2;
    float humidity_threshold;
    float light_threshold;
} ControlThresholds_t;

typedef enum {
    MOTOR_OFF = 0,
    MOTOR_L1 = 1,
    MOTOR_L2 = 2
} MotorState_t;

typedef enum {
    LED_WIFI_CONNECTED,
    LED_WIFI_DISCONNECTED,
    LED_SEND_SUCCESS,
    LED_SEND_FAILURE,
    LED_SENSOR_ERROR
} LedEvent_t;
```

---

## 4. Sensor Integration

### DHT11 (Temperature & Humidity)

- Reads temperature and humidity every 5 seconds
- Error handling: Returns -99°C for temperature and 0% for humidity on read failure
- Logs error count for resilience tracking

### DS18B20 (Secondary Temperature)

- 1-Wire protocol; single sensor on GPIO 5
- Provides secondary temperature verification
- Valid range: -50°C to +125°C; values outside this range treated as read failure
- Error handling: Returns -99°C on failure

### LDR (Light Intensity)

- ADC reading on GPIO 32 (0-4095 range)
- Converted to percentage (0-100%)
- Current hardware yields minimum ~20% in near-darkness (calibration opportunity)
- Error handling: Returns 0% on out-of-range readings

### Error Handling Strategy

Each sensor independently validates its reading:
- If any sensor fails: `sensorErrorCount++` and default value used
- If ≥1 sensor succeeds: `valid = true`
- If all 3 sensors fail: `valid = false`, LED_SENSOR_ERROR event sent
- Partial reads logged with error count

---

## 5. Motor Control Logic (L1/L2 with Hysteresis)

### Control Algorithm

```
IF temperature >= temp_threshold_2:
    → MOTOR_L2 (50% PWM = 128/255)
ELSE IF temperature >= temp_threshold_1 AND humidity >= humidity_threshold:
    → MOTOR_L1 (30% PWM = 77/255)
ELSE IF humidity >= humidity_threshold:
    → MOTOR_L1 (30% PWM)
ELSE:
    → MOTOR_OFF (0% PWM)
```

### Hysteresis Implementation

To prevent rapid switching between states, buffers are applied:
- Temperature: ±0.5°C hysteresis band
- Humidity: ±2% hysteresis band

Example: If threshold is 25°C, motor turns on at 25°C but doesn't turn off until 24.5°C.

### PWM Application

```cpp
void setPWM(uint8_t speed) {
    ledcWrite(PWM_CHANNEL, speed);  // 0-255 directly maps to duty cycle
    currentMotorState = (speed == 0) ? MOTOR_OFF : (speed == 77) ? MOTOR_L1 : MOTOR_L2;
}
```

---

## 6. Threshold Persistence & Remote Updates

### NVS Storage

Thresholds are stored in ESP32's non-volatile storage under namespace `"thresholds"`:

| Key | Default | Type |
|-----|---------|------|
| temp_th1 | 25.0°C | REAL |
| temp_th2 | 28.0°C | REAL |
| hum_th | 70% | REAL |
| light_th | 50% | REAL |

Functions:
- `initializeNVS()`: Creates namespace with defaults on first run
- `loadThresholds()`: Retrieves current values from NVS
- `updateThreshold(key, value)`: Writes single value to NVS

### Backend Synchronization

Every 60 seconds, `controlTask` calls `fetchThresholdsFromBackend()`:

1. Sends GET request to `/api/thresholds`
2. Parses JSON response: `{"temp_threshold_1":25,...}`
3. Extracts each threshold by finding colon → comma/brace → `toFloat()`
4. Updates NVS and logs parsed values
5. Next sensor cycle uses new thresholds

---

## 7. Backend System

### Technology Stack
- **Language:** Go
- **Database:** SQLite
- **Authentication:** bcrypt password hashing

### API Endpoints

**GET /api/thresholds**
- Returns current threshold values as JSON
- Example: `{"temp_threshold_1":25,"temp_threshold_2":28,"humidity_threshold":70,"light_threshold":50}`
- Used by ESP32 for 60-second sync

**POST /api/thresholds/update**
- Updates thresholds in database
- Payload: Same JSON structure as GET response

**POST /api/readings**
- Receives sensor data from ESP32 every 5 seconds
- Payload: `{"temperature":27.6,"temperature2":21.4,"humidity":32,"light_intensity":70.5,"fan_speed":30.2}`
- All 7 sensor fields included

### Database Schema

```sql
CREATE TABLE thresholds (
    id INTEGER PRIMARY KEY,
    temp_threshold_1 REAL DEFAULT 25.0,
    temp_threshold_2 REAL DEFAULT 28.0,
    humidity_threshold REAL DEFAULT 70.0,
    light_threshold REAL DEFAULT 50.0,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

---

## 8. Frontend Dashboard

### Technology Stack
- **HTML5** with Chart.js for time-series visualization
- **JavaScript** for dynamic updates (2.5-second polling cycle)
- **Dark mode** toggle

### Display Elements

**Sensor Cards (6):**
1. Temperature 1 (DHT11)
2. Humidity (DHT11)
3. Average Temperature (calculated from both sensors)
4. Temperature 2 (DS18B20)
5. Light Intensity (LDR)
6. Fan Speed (calculated PWM percentage)

**Settings Modal:**
- Input fields for 4 thresholds
- Save button → POST to `/api/thresholds/update`
- Cancel button
- Real-time validation

**Detailed Readings Table (7 columns):**
- Time, Temp1, Temp2, Humidity, Light, Fan Speed
- Color-coded rows based on motor state
- Updates every 2.5 seconds

---

## 9. Task Timing & Synchronization

### Expected Cycle Cadence

```
t=0s:    sensorTask reads, sends to controlDataQueue
t=0+10ms: controlTask receives, calculates PWM, sends to sensorDataQueue
t=0+20ms: httpTask receives, sends POST to backend
t=0+50ms: (idle, waiting for next cycle)

t=5s:    Cycle repeats

t=60s:   controlTask triggers fetchThresholdsFromBackend()
         (while normal sensor cycle continues)
```

### Delay Mechanisms

- **sensorTask:** `vTaskDelayUntil()` every 5 seconds
- **controlTask:** 1-second queue receive timeout with 100ms backoff on empty queue
- **httpTask:** `portMAX_DELAY` (indefinite wait for data)
- **ledTask:** 500ms queue receive timeout

---

## 10. Known Limitations & Future Work

### Current Limitations

1. **LDR Minimum Reading:** Hardware voltage divider yields ~20% minimum even in darkness. Needs recalibration with adjusted resistor values or software mapping.

2. **Single Motor:** Framework supports PWM_CHANNEL 0; second motor would require GPIO configuration and additional LEDC channel setup.

3. **Threshold Update Latency:** Frontend must wait 5-60 seconds for threshold fetch cycle to apply changes.

4. **No Sensor Calibration UI:** All calibration currently requires code changes.

### Future Enhancements

1. **LDR Calibration:** Implement in-app calibration routine (dark/light reference points).

2. **Second Motor:** Add GPIO configuration and duplicate controlTask logic for secondary LEDC channel.

3. **Historical Data Retention:** Extend backend to store readings with timestamps, enable graph generation of multi-day trends.

4. **Alert System:** Email/push notifications when thresholds exceeded or sensor failures detected.

5. **Sensor Diagnostics Dashboard:** Real-time sensor health indicators and error logs.

6. **Automatic Failover:** If WiFi disconnects, system continues operating with last-known-good thresholds and logs data locally for later sync.

7. **Advanced Control Modes:** Scheduled threshold changes, weather-based prediction, machine learning for optimal setpoints.

---

## 11. Build & Deployment

### Compilation
```bash
platformio run --environment upesy_wroom
```

### Upload
```bash
platformio run --target upload --environment upesy_wroom --upload-port COM7
```

### Memory Usage
- RAM: 14.4% (47KB / 327KB available)
- Flash: 72.6% (951KB / 1310KB available)

### Dependencies
- Arduino framework
- WiFi, HTTPClient
- DHT sensor library
- OneWire, DallasTemperature
- FreeRTOS (built-in)
- ESP32 Preferences (NVS)

---

## 12. Conclusion

This system demonstrates a complete IoT greenhouse monitoring solution with:
- **Robust hardware abstraction** through individual sensor error handling
- **Real-time control** using FreeRTOS task prioritization
- **Redundant data flow** (sensorTask + controlTask pathway) for reliability
- **Remote configurability** without code replacement
- **User-friendly dashboard** for monitoring and control

The architecture prioritizes resilience and extensibility, allowing straightforward addition of new sensors, motors, or control logic.

---
