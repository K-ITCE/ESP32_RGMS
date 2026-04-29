# Render Deployment

Read `RENDER_EXACT_SETUP.md`.

Most important settings:

```text
Root Directory: leave empty
Build Command: cd backend && go build -o app .
Start Command: cd backend && ./app
```

Environment variables:

```text
FRONTEND_DIR=../frontend
ADMIN_EMAIL=admin@gmail.com
ADMIN_PASSWORD=admin-1610
SESSION_SECRET=change-this-to-a-long-random-secret
DB_PATH=app.db
```
