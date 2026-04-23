package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"
)

// One sensor reading (temp + humidity only)
type Reading struct {
	Timestamp   int64   `json:"timestamp"`   // Unix time (seconds)
	Temperature float64 `json:"temperature"` // e.g. 24.7
	Humidity    float64 `json:"humidity"`    // e.g. 46.3
}

// What we return to the frontend
type SummaryResponse struct {
	Readings       []Reading      `json:"readings"`         // last N readings (for graph)
	Average        AverageReading `json:"average"`          // average of the current window
	Latest         *Reading       `json:"latest"`           // most recent reading (or null)
	ESPConnected   bool           `json:"esp_connected"`    // true if ESP32 recently sent data
	LastESPContact int64          `json:"last_esp_contact"` // unix timestamp of last ESP32 data
}

type AverageReading struct {
	Temperature float64 `json:"temperature"`
	Humidity    float64 `json:"humidity"`
}

// Shared state (in memory)
var mu sync.Mutex
var lastReadings []Reading
var lastESPContact time.Time // tracks when ESP32 last sent data

const maxPoints = 20         // window size (last 20 readings)
const espTimeoutSeconds = 10 // ESP32 considered disconnected after this many seconds

func main() {
	http.HandleFunc("/api/readings", readingsHandler) // POST from ESP32
	http.HandleFunc("/api/summary", summaryHandler)   // GET from frontend

	// Optional: serve frontend from ./static
	// fs := http.FileServer(http.Dir("./static"))
	// http.Handle("/", fs)

	fs := http.FileServer(http.Dir("./static"))
	http.Handle("/", fs)
	fmt.Println("Starting server on :8000 ...")
	fmt.Println("POST readings at  /api/readings")
	fmt.Println("GET  summary at   /api/summary")

	if err := http.ListenAndServe(":8000", nil); err != nil {
		log.Fatal(err)
	}
}

// POST /api/readings
// Body (JSON) from ESP32, e.g.:
// { "temperature": 24.7, "humidity": 46.3 }
func readingsHandler(w http.ResponseWriter, r *http.Request) {
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

	mu.Lock()
	// update ESP32 last contact time
	lastESPContact = time.Now()

	// append new reading
	lastReadings = append(lastReadings, data)

	// keep only last maxPoints readings (sliding window)
	if len(lastReadings) > maxPoints {
		lastReadings = lastReadings[len(lastReadings)-maxPoints:]
	}

	total := len(lastReadings)
	mu.Unlock()

	// simple log to confirm connection + values
	fmt.Printf("[/api/readings] New reading: T=%.2f Â°C, H=%.2f %% (total stored: %d)\n",
		data.Temperature, data.Humidity, total)

	w.WriteHeader(http.StatusOK)
}

// GET /api/summary
// Returns: last readings, average, latest
func summaryHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "only GET allowed", http.StatusMethodNotAllowed)
		return
	}

	mu.Lock()

	// copy slice manually with classic for loop
	readingsCopy := make([]Reading, len(lastReadings))
	for i := 0; i < len(lastReadings); i++ {
		readingsCopy[i] = lastReadings[i]
	}

	// Check ESP32 connection status
	espConnected := false
	var lastContact int64 = 0
	if !lastESPContact.IsZero() {
		lastContact = lastESPContact.Unix()
		espConnected = time.Since(lastESPContact).Seconds() < espTimeoutSeconds
	}

	resp := SummaryResponse{
		Readings:       readingsCopy,
		ESPConnected:   espConnected,
		LastESPContact: lastContact,
	}

	if len(lastReadings) > 0 {
		// latest = last element
		latest := lastReadings[len(lastReadings)-1]
		resp.Latest = &latest

		// compute averages over current window
		sum_temp := 0.0
		sum_hum := 0.0

		for i := 0; i < len(lastReadings); i++ {
			sum_temp += lastReadings[i].Temperature
			sum_hum += lastReadings[i].Humidity
		}

		n := float64(len(lastReadings))
		resp.Average = AverageReading{
			Temperature: sum_temp / n,
			Humidity:    sum_hum / n,
		}
	}

	count := len(lastReadings)
	mu.Unlock()

	// log summary calls (optional but useful)
	fmt.Printf("[/api/summary] Request OK (readings count: %d)\n", count)

	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(resp); err != nil {
		http.Error(w, "failed to encode JSON", http.StatusInternalServerError)
		fmt.Println("[/api/summary] JSON encode error:", err)
		return
	}
}
