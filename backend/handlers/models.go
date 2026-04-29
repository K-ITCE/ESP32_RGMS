package handlers

// One sensor reading (temp + humidity only)
type Reading struct {
	Timestamp   int64   `json:"timestamp"`   // Unix time (seconds)
	Temperature float64 `json:"temperature"` // e.g. 24.7
	Humidity    float64 `json:"humidity"`    // e.g. 46.3
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
