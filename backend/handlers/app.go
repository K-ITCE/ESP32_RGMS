package handlers

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"net/http"
	"sync"
	"time"
)

type App struct {
	DB               *sql.DB
	Mu               sync.Mutex
	LastReadings     []Reading
	LastESPContact   time.Time
	MaxPoints        int
	ESPTimeoutSecond float64

	Sessions   map[string]int
	SessionsMu sync.Mutex
}

func NewApp(db *sql.DB) *App {
	return &App{
		DB:               db,
		LastReadings:     []Reading{},
		MaxPoints:        20,
		ESPTimeoutSecond: 10,
		Sessions:         make(map[string]int),
	}
}

func (a *App) createSession(w http.ResponseWriter, userID int) error {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return err
	}

	token := hex.EncodeToString(bytes)

	a.SessionsMu.Lock()
	a.Sessions[token] = userID
	a.SessionsMu.Unlock()

	http.SetCookie(w, &http.Cookie{
		Name:     "session_token",
		Value:    token,
		Path:     "/",
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		MaxAge:   60 * 60 * 24,
	})

	return nil
}

func (a *App) clearSession(w http.ResponseWriter, r *http.Request) {
	cookie, err := r.Cookie("session_token")
	if err == nil {
		a.SessionsMu.Lock()
		delete(a.Sessions, cookie.Value)
		a.SessionsMu.Unlock()
	}

	http.SetCookie(w, &http.Cookie{
		Name:     "session_token",
		Value:    "",
		Path:     "/",
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		MaxAge:   -1,
	})
}

func (a *App) currentUser(r *http.Request) (*User, bool) {
	cookie, err := r.Cookie("session_token")
	if err != nil || cookie.Value == "" {
		return nil, false
	}

	a.SessionsMu.Lock()
	userID, ok := a.Sessions[cookie.Value]
	a.SessionsMu.Unlock()
	if !ok {
		return nil, false
	}

	var user User
	err = a.DB.QueryRow(
		"SELECT id, email, role, status, created_at FROM users WHERE id = ?",
		userID,
	).Scan(&user.ID, &user.Email, &user.Role, &user.Status, &user.CreatedAt)
	if err != nil {
		return nil, false
	}

	return &user, true
}

func (a *App) requireLogin(w http.ResponseWriter, r *http.Request) (*User, bool) {
	user, ok := a.currentUser(r)
	if !ok {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return nil, false
	}

	return user, true
}

func (a *App) requireApprovedUser(w http.ResponseWriter, r *http.Request) (*User, bool) {
	user, ok := a.requireLogin(w, r)
	if !ok {
		return nil, false
	}

	if user.Status != "approved" {
		a.clearSession(w, r)
		http.Redirect(w, r, "/login?error=pending", http.StatusSeeOther)
		return nil, false
	}

	return user, true
}

func (a *App) requireAdmin(w http.ResponseWriter, r *http.Request) (*User, bool) {
	user, ok := a.requireApprovedUser(w, r)
	if !ok {
		return nil, false
	}

	if user.Role != "admin" {
		http.Error(w, "admin only", http.StatusForbidden)
		return nil, false
	}

	return user, true
}
