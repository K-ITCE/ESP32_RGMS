package handlers

import (
	"database/sql"
	"net/http"
	"strings"

	"golang.org/x/crypto/bcrypt"
)

func (a *App) RegisterHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "only POST allowed", http.StatusMethodNotAllowed)
		return
	}

	if err := r.ParseForm(); err != nil {
		http.Redirect(w, r, "/register?error=invalid", http.StatusSeeOther)
		return
	}

	email := strings.TrimSpace(strings.ToLower(r.FormValue("email")))
	password := r.FormValue("pwd")

	if email == "" || password == "" {
		http.Redirect(w, r, "/register?error=empty", http.StatusSeeOther)
		return
	}

	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	if err != nil {
		http.Redirect(w, r, "/register?error=server", http.StatusSeeOther)
		return
	}

	_, err = a.DB.Exec(
		"INSERT INTO users (email, password_hash, role, status) VALUES (?, ?, 'user', 'pending')",
		email,
		string(hash),
	)
	if err != nil {
		http.Redirect(w, r, "/register?error=exists", http.StatusSeeOther)
		return
	}

	http.Redirect(w, r, "/login?registered=pending", http.StatusSeeOther)
}

func (a *App) LoginHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "only POST allowed", http.StatusMethodNotAllowed)
		return
	}

	if err := r.ParseForm(); err != nil {
		http.Redirect(w, r, "/login?error=invalid", http.StatusSeeOther)
		return
	}

	email := strings.TrimSpace(strings.ToLower(r.FormValue("email")))
	password := r.FormValue("pwd")

	var userID int
	var passwordHash string
	var role string
	var status string

	err := a.DB.QueryRow(
		"SELECT id, password_hash, role, status FROM users WHERE email = ?",
		email,
	).Scan(&userID, &passwordHash, &role, &status)

	if err == sql.ErrNoRows {
		http.Redirect(w, r, "/login?error=wrong", http.StatusSeeOther)
		return
	}
	if err != nil {
		http.Redirect(w, r, "/login?error=server", http.StatusSeeOther)
		return
	}

	if bcrypt.CompareHashAndPassword([]byte(passwordHash), []byte(password)) != nil {
		http.Redirect(w, r, "/login?error=wrong", http.StatusSeeOther)
		return
	}

	if status != "approved" {
		http.Redirect(w, r, "/login?error=pending", http.StatusSeeOther)
		return
	}

	if err := a.createSession(w, userID); err != nil {
		http.Redirect(w, r, "/login?error=server", http.StatusSeeOther)
		return
	}

	if role == "admin" {
		http.Redirect(w, r, "/admin", http.StatusSeeOther)
		return
	}

	http.Redirect(w, r, "/dashboard", http.StatusSeeOther)
}

func (a *App) LogoutHandler(w http.ResponseWriter, r *http.Request) {
	a.clearSession(w, r)
	http.Redirect(w, r, "/login", http.StatusSeeOther)
}
