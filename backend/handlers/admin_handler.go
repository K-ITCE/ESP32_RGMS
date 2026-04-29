package handlers

import (
	"encoding/json"
	"net/http"
	"strconv"
)

func (a *App) AdminUsersHandler(w http.ResponseWriter, r *http.Request) {
	if _, ok := a.requireAdmin(w, r); !ok {
		return
	}

	if r.Method != http.MethodGet {
		http.Error(w, "only GET allowed", http.StatusMethodNotAllowed)
		return
	}

	rows, err := a.DB.Query("SELECT id, email, role, status, created_at FROM users WHERE role != 'admin' ORDER BY created_at DESC")
	if err != nil {
		http.Error(w, "failed to load users", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	users := []User{}
	for rows.Next() {
		var user User
		if err := rows.Scan(&user.ID, &user.Email, &user.Role, &user.Status, &user.CreatedAt); err != nil {
			http.Error(w, "failed to read users", http.StatusInternalServerError)
			return
		}
		users = append(users, user)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(users)
}

func (a *App) ApproveUserHandler(w http.ResponseWriter, r *http.Request) {
	if _, ok := a.requireAdmin(w, r); !ok {
		return
	}

	if r.Method != http.MethodPost {
		http.Error(w, "only POST allowed", http.StatusMethodNotAllowed)
		return
	}

	id, err := strconv.Atoi(r.URL.Query().Get("id"))
	if err != nil || id <= 0 {
		http.Error(w, "invalid user id", http.StatusBadRequest)
		return
	}

	_, err = a.DB.Exec("UPDATE users SET status = 'approved' WHERE id = ? AND role != 'admin'", id)
	if err != nil {
		http.Error(w, "failed to approve user", http.StatusInternalServerError)
		return
	}

	w.WriteHeader(http.StatusOK)
}

func (a *App) DisapproveUserHandler(w http.ResponseWriter, r *http.Request) {
	if _, ok := a.requireAdmin(w, r); !ok {
		return
	}

	if r.Method != http.MethodPost {
		http.Error(w, "only POST allowed", http.StatusMethodNotAllowed)
		return
	}

	id, err := strconv.Atoi(r.URL.Query().Get("id"))
	if err != nil || id <= 0 {
		http.Error(w, "invalid user id", http.StatusBadRequest)
		return
	}

	_, err = a.DB.Exec("DELETE FROM users WHERE id = ? AND role != 'admin'", id)
	if err != nil {
		http.Error(w, "failed to disapprove user", http.StatusInternalServerError)
		return
	}

	w.WriteHeader(http.StatusOK)
}
