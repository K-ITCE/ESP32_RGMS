package handlers

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// GET /api/summary
// Returns: last readings, average, latest
func (a *App) SummaryHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "only GET allowed", http.StatusMethodNotAllowed)
		return
	}

	if _, ok := a.requireApprovedUser(w, r); !ok {
		return
	}

	a.Mu.Lock()

	readingsCopy := make([]Reading, len(a.LastReadings))
	for i := 0; i < len(a.LastReadings); i++ {
		readingsCopy[i] = a.LastReadings[i]
	}

	espConnected := false
	var lastContact int64 = 0

	if !a.LastESPContact.IsZero() {
		lastContact = a.LastESPContact.Unix()
		espConnected = time.Since(a.LastESPContact).Seconds() < a.ESPTimeoutSecond
	}

	resp := SummaryResponse{
		Readings:       readingsCopy,
		ESPConnected:   espConnected,
		LastESPContact: lastContact,
	}

	if len(a.LastReadings) > 0 {
		latest := a.LastReadings[len(a.LastReadings)-1]
		resp.Latest = &latest

		sumTemp := 0.0
        sumHum := 0.0

        for i := 0; i < len(a.LastReadings); i++ {
            sumTemp += a.LastReadings[i].Temperature
            sumHum += a.LastReadings[i].Humidity
        }

        n := float64(len(a.LastReadings))
        resp.Average = AverageReading{
            Temperature: sumTemp / n,
            Humidity:    sumHum / n,
        }
	}

	count := len(a.LastReadings)
	a.Mu.Unlock()

	fmt.Printf("[/api/summary] Request OK (readings count: %d)\n", count)

	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(resp); err != nil {
		http.Error(w, "failed to encode JSON", http.StatusInternalServerError)
		fmt.Println("[/api/summary] JSON encode error:", err)
		return
	}
}
