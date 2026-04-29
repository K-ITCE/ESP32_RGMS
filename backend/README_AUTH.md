# Authentication + Admin Approval Setup

## Run locally

From the backend folder:

```bash
cd backend
go mod tidy
go run .
```

Open:

```text
http://localhost:8000/login
```

## Default admin login

```text
Email: admin@gmail.com
Password: admin-1610
```

The admin account is created automatically on startup if it does not exist.
The password is stored as a hash in the database, not plain text.

## Optional environment variables

You can override the default admin login:

```bash
ADMIN_EMAIL=admin@gmail.com
ADMIN_PASSWORD=admin-1610
```

You can also control the SQLite database file path:

```bash
DB_PATH=app.db
```

## Flow

1. User registers with email and password.
2. Password is hashed.
3. User is saved with status `pending`.
4. Admin logs in and opens `/admin`.
5. Admin approves or disapproves users.
6. Approved users can log in and access `/dashboard`.
7. Disapproved users are deleted from the database.

## Notes

- The SQLite database is created automatically.
- The migration runs automatically from `database.Migrate()`.
- For Render deployment, use a persistent disk for SQLite or switch to hosted PostgreSQL later.
