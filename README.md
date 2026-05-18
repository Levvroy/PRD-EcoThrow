# EcoThrow - Smart Waste Bin with Gamification
### Kelompok 9 · PRD K51 · STEI-K ITB 2025

> **Buang sampah bukan kewajiban. Ini kompetisi.**
> EcoThrow menggabungkan IoT (ESP32 + ultrasonik HC-SR04), gamifikasi RPG 9-tier, dan data riset real-time untuk mengubah kebiasaan buang sampah mahasiswa menjadi aktivitas yang menyenangkan dan terukur.

---

## Daftar Isi

1. [Arsitektur Sistem](#arsitektur-sistem)
2. [Struktur Project](#struktur-project)
3. [Setup & Menjalankan](#setup--menjalankan)
4. [Halaman Website](#halaman-website)
5. [API Endpoints](#api-endpoints)
6. [Integrasi ESP32](#integrasi-esp32)
7. [Sistem Gamifikasi & Tier RPG](#sistem-gamifikasi--tier-rpg)
8. [Autentikasi Admin](#autentikasi-admin)
9. [Tech Stack](#tech-stack)

---

## Arsitektur Sistem

```
 [Pengguna]           [Admin]
     │                  │
     ▼                  ▼
 Browser (HTML/CSS/JS)  ←──────────────────────────────────────┐
     │                                                          │
     │  fetch() ke localhost:8000                               │
     ▼                                                          │
 [FastAPI Backend]  ←──── POST /api/throw  ←──── [ESP32 + HC-SR04]
     │                    POST /api/bin/update
     ▼
 [SQLite Database]
  (ecothrow.db)
```

**Alur utama:**
1. Pengguna buka website → masukkan NIM → tekan "Trigger Throw"
2. Website kirim sinyal → hardware ESP32 standby
3. Sampah dimasukkan → sensor ultrasonik membaca perubahan level
4. ESP32 kirim data ke API → backend hitung XP + update tier
5. Website polling setiap 5 detik → tampilkan hasil otomatis

---

## Struktur Project

```
ecothrow/
├── backend/
│   ├── app/
│   │   ├── main.py           ← Semua API endpoint (FastAPI)
│   │   ├── database.py       ← Koneksi async SQLite (aiosqlite)
│   │   ├── models.py         ← ORM models: User, Bin, ThrowEvent, UserPoints
│   │   └── gamification.py   ← Logika XP, 9-tier, streak multiplier
│   ├── requirements.txt
│   ├── ecothrow.db           ← Database SQLite (auto-created)
│   └── venv/                 ← Virtual environment Python
│
├── frontend/
│   ├── index.html            ← Beranda + Trigger Throw IoT
│   ├── css/
│   │   └── style.css         ← Design system global (custom)
│   ├── js/
│   │   ├── api.js            ← apiFetch helper, TIERS, lucideInline, utils
│   │   └── navbar.js         ← Auto-inject navbar, footer, mobile bottom nav
│   └── pages/
│       ├── about.html        ← Tentang EcoThrow + staircase tier RPG
│       ├── leaderboard.html  ← Ranking individu & per-fakultas
│       ├── dashboard.html    ← Data riset, grafik bar, export CSV
│       ├── profile.html      ← Profil pengguna + registrasi
│       ├── history.html      ← Riwayat semua pengguna + filter
│       ├── admin.html        ← Panel admin (protected, butuh login)
│       └── admin-login.html  ← Halaman login admin
│
└── start.bat                 ← One-click jalankan backend (Windows)
```

---

## Setup & Menjalankan

### Prasyarat
- Python 3.10+
- Browser modern (Chrome/Firefox/Edge)
- (Opsional) VS Code + Live Server extension

### 1. Setup Virtual Environment

```bash
cd ecothrow/backend
python -m venv venv
venv\Scripts\activate          # Windows
pip install -r requirements.txt
```

### 2. Jalankan Backend

```bash
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

| URL | Keterangan |
|-----|------------|
| `http://localhost:8000` | Backend API |
| `http://localhost:8000/docs` | Swagger UI (dokumentasi interaktif) |

> **Catatan:** Gunakan `--host 0.0.0.0` agar ESP32 bisa mengakses API dari jaringan lokal yang sama.

### 3. Buka Frontend

Buka `frontend/index.html` langsung di browser, atau gunakan **Live Server** di VS Code.

### Cara Cepat (Windows)

Klik dua kali `start.bat` - script otomatis aktifkan venv, install dependencies, dan jalankan server.

---

## Halaman Website

| Halaman | File | Deskripsi |
|---------|------|-----------|
| **Beranda** | `index.html` | Hero + Trigger Throw inline, statistik real-time, status bin, Top 3 leaderboard |
| **Tentang** | `pages/about.html` | Penjelasan sistem, staircase tier RPG interaktif, cara kerja IoT, formula XP |
| **Leaderboard** | `pages/leaderboard.html` | Ranking individu (XP) + ranking per-fakultas (total XP gabungan) |
| **Dashboard** | `pages/dashboard.html` | Grafik aktivitas harian, distribusi tier, data riset, export CSV |
| **Profil** | `pages/profile.html` | Cari profil by NIM, registrasi pengguna baru, progress bar tier |
| **Riwayat** | `pages/history.html` | Daftar semua pengguna, filter nama/NIM/fakultas/tier, detail riwayat throw per user |
| **Admin** | `pages/admin.html` | Kelola bin, kelola user, simulasi throw hardware, manual throw - **butuh login** |
| **Admin Login** | `pages/admin-login.html` | Login admin dengan password, lockout 5 menit setelah 5× salah |

### Navigasi Mobile

Website menggunakan **Bottom Navigation Bar** 5-tab untuk perangkat mobile:

```
[🏠 Beranda] [🏆 Ranking] [🗑️ Throw] [👤 Profil] [📋 Riwayat]
```

---

## API Endpoints

### User

| Method | Endpoint | Body / Param | Respons |
|--------|----------|--------------|---------|
| `POST` | `/api/user/register` | `{nim, name, faculty}` | `{success, qr_code, user_id, name}` |
| `GET`  | `/api/user/{nim}` | - | Profil lengkap + riwayat 10 throw terakhir |
| `GET`  | `/api/users` | - | Array semua pengguna + XP + tier |

### Throw Event (dipanggil dari ESP32)

| Method | Endpoint | Body | Respons |
|--------|----------|------|---------|
| `POST` | `/api/throw` | `{user_qr_code, bin_id, bin_level_before, bin_level_after}` | `{success, verified, xp_earned, new_total_xp, new_level, streak_days}` |

> Throw otomatis **tidak diverifikasi** jika `bin_level_after - bin_level_before < 2.0` (anti-cheat).

### Leaderboard

| Method | Endpoint | Query | Respons |
|--------|----------|-------|---------|
| `GET`  | `/api/leaderboard` | `?limit=20` | Array rank + nama + XP + tier |
| `GET`  | `/api/leaderboard/faculty` | - | Ranking per fakultas (total XP gabungan) |

### Bin

| Method | Endpoint | Body | Respons |
|--------|----------|------|---------|
| `GET`  | `/api/bins` | - | Status semua bin (kapasitas, lokasi, status NORMAL/SEDANG/PENUH) |
| `POST` | `/api/bin/add` | `{location}` | `{success, bin_id}` |
| `POST` | `/api/bin/update` | `{bin_id, capacity_pct}` | `{success, capacity_pct}` |

### Statistik

| Method | Endpoint | Respons |
|--------|----------|---------|
| `GET`  | `/api/stats` | `{total_users, total_throws, total_xp, total_bins, daily_throws[]}` |

---

## Integrasi ESP32

Pastikan ESP32 dan laptop terhubung ke **WiFi yang sama**. Ganti `IP_LAPTOP` dengan IP lokal laptop (cek dengan `ipconfig`).

### Update Kapasitas Bin (dari sensor ultrasonik)

Kirim setiap kali level bin berubah signifikan:

```http
POST http://IP_LAPTOP:8000/api/bin/update
Content-Type: application/json

{
  "bin_id": "ID_BIN_DARI_DATABASE",
  "capacity_pct": 75.5
}
```

### Event Buang Sampah

Kirim setelah pengguna trigger dari website dan sampah terdeteksi:

```http
POST http://IP_LAPTOP:8000/api/throw
Content-Type: application/json

{
  "user_qr_code": "ECO-13525136-AB12CD",
  "bin_id": "ID_BIN",
  "bin_level_before": 30.0,
  "bin_level_after": 38.5
}
```

**Respons sukses:**

```json
{
  "success": true,
  "verified": true,
  "xp_earned": 14,
  "new_total_xp": 314,
  "new_level": "Bronze",
  "streak_days": 3,
  "message": "+14 XP! Level Bronze"
}
```

### Alur Trigger dari Website ke ESP32

```
[Website] → POST /api/user/{nim} → simpan XP awal
[Website] → polling /api/user/{nim} tiap 5 detik
[ESP32]   → POST /api/throw (saat sampah masuk)
[Website] → deteksi XP naik → tampilkan hasil
```

---

## Sistem Gamifikasi & Tier RPG

### 9 Tier Material

| # | Tier | XP Dibutuhkan | Material |
|---|------|---------------|----------|
| 1 | **Copper** | 0 – 299 XP | Tembaga (pemula) |
| 2 | **Bronze** | 300 – 799 XP | Perunggu |
| 3 | **Silver** | 800 – 1.999 XP | Perak |
| 4 | **Gold** | 2.000 – 4.999 XP | Emas |
| 5 | **Platinum** | 5.000 – 9.999 XP | Platinum |
| 6 | **Diamond** | 10.000 – 19.999 XP | Berlian |
| 7 | **Mithril** | 20.000 – 34.999 XP | Mithril (mitologi) |
| 8 | **Orichalcum** | 35.000 – 59.999 XP | Orichalcum (legenda) |
| 9 | **Adamantium** | 60.000+ XP | Adamantium (terkeras) |

### Formula XP

```
XP = base_xp × bin_bonus × streak_multiplier

base_xp         = 10 XP per throw
bin_bonus       = 1.0× (normal) | 1.5× (bin 60–80%) | 2.0× (bin >80%)
streak_multiplier = min(1.0 + streak_days × 0.1, 2.0)
```

**Contoh:** Streak 7 hari + bin 85% penuh → 10 × 2.0 × 1.7 = **34 XP** per buang.

### Validasi Anti-Cheat

Throw hanya dihitung jika `bin_level_after − bin_level_before ≥ 2.0`. Perubahan di bawah threshold dianggap noise sensor dan tidak memberikan XP.

---

## Autentikasi Admin

Akses admin dipisah sepenuhnya dari pengguna biasa:

- **URL login:** `pages/admin-login.html`
- **Password default:** `etrosuksesjaya` *(ubah di `admin-login.html` baris `ADMIN_PASSWORD`)*
- **Lockout:** 5 menit setelah 5× percobaan salah
- **Session:** Disimpan di `localStorage` (`et_admin = "true"`)
- **Guard:** `admin.html` redirect otomatis ke login jika session tidak ada

Fitur admin panel:
- Tambah & lihat semua bin
- Lihat & hapus pengguna
- Simulasi throw hardware (tanpa QR scan)
- Manual throw untuk testing

---

## Tech Stack

### Frontend
| Komponen | Teknologi |
|----------|-----------|
| Markup | HTML5 Semantik |
| Styling | Custom CSS (design system, no framework CSS) |
| UI Framework | Bootstrap 5.3 (grid & utilities saja) |
| Icons | Lucide Icons (SVG inline, satu tone) |
| Fonts | Inter (UI) + Libre Baskerville (brand) via Google Fonts |
| JavaScript | Vanilla JS (ES2020+), no bundler |

### Backend
| Komponen | Teknologi |
|----------|-----------|
| Framework | FastAPI |
| Database | SQLite via aiosqlite (async) |
| ORM | SQLAlchemy 2.0 (async) |
| Server | Uvicorn |
| Validasi | Pydantic v2 |

### Hardware
| Komponen | Keterangan |
|----------|------------|
| Mikrokontroler | ESP32 |
| Sensor | HC-SR04 (ultrasonik jarak/level) |
| Koneksi | WiFi HTTP POST ke FastAPI |

---

## Kontributor

**Kelompok 9 - PRD K51 - STEI-K ITB 2025**

> Project ini dibuat sebagai tugas besar mata kuliah Perancangan dan Rekayasa Desain (PRD) Semester 2 Tahun 2025.
