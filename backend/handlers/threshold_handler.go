package handlers

import (
    "encoding/json"
    "fmt"
    "net/http"
)

// GET /api/thresholds - Returns current thresholds
func (a *App) GetThresholdsHandler(w http.ResponseWriter, r *http.Request) {
    if r.Method != http.MethodGet {
        http.Error(w, "only GET allowed", http.StatusMethodNotAllowed)
        return
    }

    var thresholds ControlThresholds
    err := a.DB.QueryRow(`
        SELECT temp_threshold_1, temp_threshold_2, humidity_threshold, light_threshold
        FROM thresholds ORDER BY id DESC LIMIT 1
    `).Scan(&thresholds.TempThreshold1, &thresholds.TempThreshold2, 
            &thresholds.HumidityThreshold, &thresholds.LightThreshold)
    
    if err != nil {
        http.Error(w, "failed to fetch thresholds", http.StatusInternalServerError)
        return
    }

    w.Header().Set("Content-Type", "application/json")
    json.NewEncoder(w).Encode(thresholds)
}

// POST /api/thresholds - Update thresholds
func (a *App) UpdateThresholdsHandler(w http.ResponseWriter, r *http.Request) {
    if r.Method != http.MethodPost {
        http.Error(w, "only POST allowed", http.StatusMethodNotAllowed)
        return
    }

    var thresholds ControlThresholds
    if err := json.NewDecoder(r.Body).Decode(&thresholds); err != nil {
        http.Error(w, "invalid JSON", http.StatusBadRequest)
        return
    }
    defer r.Body.Close()

    _, err := a.DB.Exec(`
        UPDATE thresholds SET
            temp_threshold_1 = ?,
            temp_threshold_2 = ?,
            humidity_threshold = ?,
            light_threshold = ?,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = (SELECT id FROM thresholds ORDER BY id DESC LIMIT 1)
    `, thresholds.TempThreshold1, thresholds.TempThreshold2,
       thresholds.HumidityThreshold, thresholds.LightThreshold)

    if err != nil {
        http.Error(w, "failed to update thresholds", http.StatusInternalServerError)
        fmt.Println("[/api/thresholds] update error:", err)
        return
    }

    w.Header().Set("Content-Type", "application/json")
    w.WriteHeader(http.StatusOK)
    json.NewEncoder(w).Encode(map[string]string{"status": "thresholds updated"})
}