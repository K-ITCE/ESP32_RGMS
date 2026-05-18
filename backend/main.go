package main

import (
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"

	"sensor/database"
	"sensor/handlers"
)

func main() {
	db, err := database.Open()
	if err != nil {
		log.Fatal("database open error:", err)
	}
	defer db.Close()

	if err := database.Migrate(db); err != nil {
		log.Fatal("database migration error:", err)
	}

	if err := database.SeedAdmin(db); err != nil {
		log.Fatal("admin seed error:", err)
	}

	if err := database.SeedThresholds(db); err != nil {
		log.Fatal("thresholds seed error:", err)
	}

	app := handlers.NewApp(db)

	http.HandleFunc("/", app.HomeHandler)
	http.HandleFunc("/login", app.LoginPageHandler)
	http.HandleFunc("/register", app.RegisterPageHandler)
	http.HandleFunc("/dashboard", app.DashboardPageHandler)
	http.HandleFunc("/admin", app.AdminPageHandler)

	frontendDir := os.Getenv("FRONTEND_DIR")
	if frontendDir == "" {
		frontendDir = "../frontend"
	}

	styleFS := http.FileServer(http.Dir(filepath.Join(frontendDir, "style")))
	http.Handle("/style/", http.StripPrefix("/style/", styleFS))

	http.HandleFunc("/submit-registration", app.RegisterHandler)
	http.HandleFunc("/submit-login", app.LoginHandler)
	http.HandleFunc("/logout", app.LogoutHandler)

	http.HandleFunc("/api/readings", app.ReadingsHandler)
	http.HandleFunc("/api/summary", app.SummaryHandler)
	http.HandleFunc("/api/thresholds", app.GetThresholdsHandler)
	http.HandleFunc("/api/thresholds/update", app.UpdateThresholdsHandler)

	http.HandleFunc("/api/admin/users", app.AdminUsersHandler)
	http.HandleFunc("/api/admin/users/approve", app.ApproveUserHandler)
	http.HandleFunc("/api/admin/users/disapprove", app.DisapproveUserHandler)

	port := os.Getenv("PORT")
	if port == "" {
		port = "8000"
	}

	fmt.Println("Starting server on :" + port + " ...")
	fmt.Println("Login page:       http://localhost:" + port + "/login")
	fmt.Println("Register page:    http://localhost:" + port + "/register")
	fmt.Println("Dashboard page:   http://localhost:" + port + "/dashboard")
	fmt.Println("Admin page:       http://localhost:" + port + "/admin")
	fmt.Println("POST readings at  /api/readings")
	fmt.Println("GET  summary at   /api/summary")
	fmt.Println("GET  thresholds at /api/thresholds")
	fmt.Println("POST update thresholds at /api/thresholds/update")
	if err := http.ListenAndServe(":"+port, nil); err != nil {
		log.Fatal(err)
	}
}
