package handlers

import (
	"net/http"
	"os"
	"path/filepath"
)

func frontendDir() string {
	dir := os.Getenv("FRONTEND_DIR")
	if dir == "" {
		dir = "../frontend"
	}
	return dir
}

func serveHTML(w http.ResponseWriter, r *http.Request, filename string) {
	path := filepath.Join(frontendDir(), "templates", filename)
	http.ServeFile(w, r, path)
}

func (a *App) HomeHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	http.Redirect(w, r, "/login", http.StatusSeeOther)
}

func (a *App) LoginPageHandler(w http.ResponseWriter, r *http.Request) {
	if _, ok := a.currentUser(r); ok {
		user, _ := a.currentUser(r)
		if user.Role == "admin" {
			http.Redirect(w, r, "/admin", http.StatusSeeOther)
			return
		}
		if user.Status == "approved" {
			http.Redirect(w, r, "/dashboard", http.StatusSeeOther)
			return
		}
	}

	serveHTML(w, r, "login.html")
}

func (a *App) RegisterPageHandler(w http.ResponseWriter, r *http.Request) {
	serveHTML(w, r, "register.html")
}

func (a *App) DashboardPageHandler(w http.ResponseWriter, r *http.Request) {
	user, ok := a.requireApprovedUser(w, r)
	if !ok {
		return
	}

	if user.Role == "admin" {
		http.Redirect(w, r, "/admin", http.StatusSeeOther)
		return
	}

	serveHTML(w, r, "index.html")
}

func (a *App) AdminPageHandler(w http.ResponseWriter, r *http.Request) {
	if _, ok := a.requireAdmin(w, r); !ok {
		return
	}

	serveHTML(w, r, "admin.html")
}
