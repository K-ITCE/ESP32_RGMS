package handlers

// Sensor readings
type Reading struct {
    Timestamp       int64   `json:"timestamp"`			// Unix time (sec)
    Temperature     float64 `json:"temperature"`		// DHT11 (°C)
    Temperature2    float64 `json:"temperature2"`		// DS18B20 (°C)
    Humidity        float64 `json:"humidity"`			// DHT11 (0-100%)
    LightIntensity  float64 `json:"light_intensity"`	// 0-100%
    FanSpeed        float64 `json:"fan_speed"`			// 0-100%
}

type ControlThresholds struct {
    TempThreshold1    float64 `json:"temp_threshold_1"`
    TempThreshold2    float64 `json:"temp_threshold_2"`
    HumidityThreshold float64 `json:"humidity_threshold"`
    LightThreshold    float64 `json:"light_threshold"`
}

type AverageReading struct {
	Temperature float64 `json:"temperature"`
	Humidity    float64 `json:"humidity"`
}

// What we return to the frontend
type SummaryResponse struct {
	Readings       []Reading      `json:"readings"`         // last N readings (for graph)
	Average        AverageReading `json:"average"`          // average of the current window
	Latest         *Reading       `json:"latest"`           // most recent reading (or null)
	ESPConnected   bool           `json:"esp_connected"`    // true if ESP32 recently sent data
	LastESPContact int64          `json:"last_esp_contact"` // unix timestamp of last ESP32 data
}

type User struct {
	ID        int    `json:"id"`
	Email     string `json:"email"`
	Role      string `json:"role"`
	Status    string `json:"status"`
	CreatedAt string `json:"created_at"`
}
