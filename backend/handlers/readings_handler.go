package handlers

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// POST /api/readings
// Body (JSON) from ESP32, e.g.:
// { "temperature": 24.7, "temperature2": 25.1, "humidity": 46.3, "light_intensity": 60.0, "fan_speed": 75.0 }
func (a *App) ReadingsHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "only POST allowed", http.StatusMethodNotAllowed)
		return
	}

	var data Reading
	if err := json.NewDecoder(r.Body).Decode(&data); err != nil {
		http.Error(w, "invalid JSON", http.StatusBadRequest)
		fmt.Println("[/api/readings] invalid JSON:", err)
		return
	}
	defer r.Body.Close()

	// Backend sets the timestamp
	data.Timestamp = time.Now().Unix()

	a.Mu.Lock()

	// update ESP32 last contact time
	a.LastESPContact = time.Now()

	// append new reading
	a.LastReadings = append(a.LastReadings, data)

	// keep only last maxPoints readings (sliding window)
	if len(a.LastReadings) > a.MaxPoints {
		a.LastReadings = a.LastReadings[len(a.LastReadings)-a.MaxPoints:]
	}

	total := len(a.LastReadings)
	a.Mu.Unlock()

	// fmt.Printf("[/api/readings] New reading: T=%.2f °C, H=%.2f %% (total stored: %d)\n",
		// data.Temperature, data.Humidity, total)
	fmt.Printf("[/api/readings] New reading: T1=%.2f °C, T2=%.2f °C, H=%.2f %%, Light=%.2f %%, Fan=%.2f %% (total stored: %d)\n",
		data.Temperature, data.Temperature2, data.Humidity, data.LightIntensity, data.FanSpeed, total)

	w.WriteHeader(http.StatusOK)
}
