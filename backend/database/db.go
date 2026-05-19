package database

import (
	"database/sql"
	"fmt"
	"os"

	"golang.org/x/crypto/bcrypt"
	_ "modernc.org/sqlite"
)

func Open() (*sql.DB, error) {
	path := os.Getenv("DB_PATH")
	if path == "" {
		path = "app.db"
	}

	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}

	if _, err := db.Exec("PRAGMA foreign_keys = ON"); err != nil {
		return nil, err
	}

	return db, nil
}

func Migrate(db *sql.DB) error {
	query := `
	CREATE TABLE IF NOT EXISTS users (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		email TEXT UNIQUE NOT NULL,
		password_hash TEXT NOT NULL,
		role TEXT NOT NULL DEFAULT 'user',
		status TEXT NOT NULL DEFAULT 'pending',
		created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
	);
	CREATE TABLE IF NOT EXISTS thresholds (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		temp_threshold_1 REAL NOT NULL DEFAULT 25.0,
		temp_threshold_2 REAL NOT NULL DEFAULT 28.0,
		humidity_threshold REAL NOT NULL DEFAULT 70.0,
		light_threshold REAL NOT NULL DEFAULT 50.0,
		updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
	);
	`

	_, err := db.Exec(query)
	return err
}

func SeedAdmin(db *sql.DB) error {
	email := os.Getenv("ADMIN_EMAIL")
	if email == "" {
		email = "admin@gmail.com"
	}

	password := os.Getenv("ADMIN_PASSWORD")
	if password == "" {
		password = "admin-1610"
	}

	var exists int
	err := db.QueryRow("SELECT COUNT(*) FROM users WHERE email = ?", email).Scan(&exists)
	if err != nil {
		return err
	}

	if exists > 0 {
		_, err = db.Exec("UPDATE users SET role = 'admin', status = 'approved' WHERE email = ?", email)
		return err
	}

	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	if err != nil {
		return err
	}

	_, err = db.Exec(
		"INSERT INTO users (email, password_hash, role, status) VALUES (?, ?, 'admin', 'approved')",
		email,
		string(hash),
	)
	if err != nil {
		return fmt.Errorf("seed admin: %w", err)
	}

	return nil
}

func SeedThresholds(db *sql.DB) error {
    var exists int
    err := db.QueryRow("SELECT COUNT(*) FROM thresholds").Scan(&exists)
    if err != nil {
        return err
    }
    
    if exists == 0 {
        _, err := db.Exec(`
            INSERT INTO thresholds (temp_threshold_1, temp_threshold_2, humidity_threshold, light_threshold)
            VALUES (25.0, 28.0, 70.0, 50.0)
        `)
        return err
    }
    return nil
}
