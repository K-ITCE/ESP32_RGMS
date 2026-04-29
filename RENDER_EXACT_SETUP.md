# Exact Render Setup for ESP32_RGMS

Use these settings exactly.

## 1) Repository layout

Keep the project like this:

```text
ESP32_RGMS/
├── backend/
│   ├── main.go
│   ├── go.mod
│   ├── handlers/
│   └── database/
├── frontend/
│   ├── templates/
│   │   ├── login.html
│   │   ├── register.html
│   │   ├── index.html
│   │   └── admin.html
│   └── style/
├── src/
│   ├── main.cpp
│   └── secrets.h
└── render.yaml
```

Do not move the frontend inside backend.

## 2) Render manual settings

Create a new Web Service from GitHub.

Use:

```text
Root Directory: leave empty
Runtime: Go
Build Command: cd backend && go build -o app .
Start Command: cd backend && ./app
```

Important: Root Directory must be empty. Do NOT set it to backend.

## 3) Environment variables

Add these in Render > Environment:

```text
FRONTEND_DIR=../frontend
ADMIN_EMAIL=admin@gmail.com
ADMIN_PASSWORD=admin-1610
SESSION_SECRET=change-this-to-a-long-random-secret
DB_PATH=app.db
```

Render automatically provides PORT. Do not add PORT manually.

## 4) After deployment

Open:

```text
https://esp32-rgms.onrender.com/login
```

Admin login:

```text
Email: admin@gmail.com
Password: admin-1610
```

## 5) ESP32 online URL

After Render deploys successfully, open:

```text
src/secrets.h
```

Set:

```cpp
#define SERVER_URL "https://esp32-rgms.onrender.com/api/readings"
```

For local testing, use:

```cpp
#define SERVER_URL "http://YOUR_COMPUTER_IP:8000/api/readings"
```

## 6) Common errors

### 404 on /login

Usually means one of these:

- Root Directory was set to backend.
- The latest code was not pushed to GitHub.
- The deploy used an old commit.

Fix:

```text
Root Directory: leave empty
Build Command: cd backend && go build -o app .
Start Command: cd backend && ./app
```

Then click:

```text
Manual Deploy > Clear build cache & deploy
```

### Build says ./app not found

You put `./app` in Build Command by mistake.

Correct:

```text
Build Command: cd backend && go build -o app .
Start Command: cd backend && ./app
```

## 7) Data warning

This version uses SQLite. It is fine for local testing and demos.

On Render, SQLite data may be lost after redeploy/restart unless you configure persistent storage. For a serious always-on version, use PostgreSQL.

## 8) Availability warning

Free Render services can sleep after inactivity. For continuous ESP32 monitoring, use a paid instance or another always-on server.
