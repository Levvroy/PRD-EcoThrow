# EcoThrow - Smart Waste Bin with Gamification

<div align="center">

![Python](https://img.shields.io/badge/Python-3.10+-3776AB?style=flat-square&logo=python&logoColor=white)
![FastAPI](https://img.shields.io/badge/FastAPI-0.100+-009688?style=flat-square&logo=fastapi&logoColor=white)
![SQLite](https://img.shields.io/badge/SQLite-aiosqlite-003B57?style=flat-square&logo=sqlite&logoColor=white)
![ESP32](https://img.shields.io/badge/Hardware-ESP32-E7352C?style=flat-square&logo=espressif&logoColor=white)
![Vanilla JS](https://img.shields.io/badge/JavaScript-Vanilla_ES2020-F7DF1E?style=flat-square&logo=javascript&logoColor=black)
![Bootstrap](https://img.shields.io/badge/Bootstrap-5.3-7952B3?style=flat-square&logo=bootstrap&logoColor=white)
![License](https://img.shields.io/badge/Lisensi-MIT-green?style=flat-square)

**Kelompok 9 · PRD K51 · STEI-K ITB 2025**

EcoThrow menggabungkan **IoT** (ESP32 + sensor ultrasonik HC-SR04), **gamifikasi RPG 9-tier**, dan **data riset real-time** untuk mengubah kebiasaan membuang sampah mahasiswa menjadi aktivitas yang menyenangkan dan terukur.

</div>

---

## 📑 Daftar Isi

1. [Gambaran Umum](#-gambaran-umum)
2. [Arsitektur Sistem](#-arsitektur-sistem)
3. [Struktur Project](#-struktur-project)
4. [Setup & Menjalankan](#-setup--menjalankan)
5. [Halaman Website](#-halaman-website)
6. [API Endpoints](#-api-endpoints)
7. [Integrasi ESP32 & Hardware](#-integrasi-esp32--hardware)
8. [Sistem Gamifikasi & Tier RPG](#-sistem-gamifikasi--tier-rpg)
9. [Autentikasi Admin](#-autentikasi-admin)
10. [Tech Stack](#-tech-stack)
11. [Kontributor](#-kontributor)

---

## 🌿 Gambaran Umum

EcoThrow adalah sistem smart waste bin berbasis IoT yang dirancang untuk kampus ITB. Sistem ini menggabungkan tiga komponen utama:

| Komponen | Deskripsi |
|----------|-----------|
| **Hardware IoT** | Tempat sampah pintar dengan ESP32 + sensor ultrasonik HC-SR04 untuk mendeteksi setiap lemparan sampah secara otomatis |
| **Backend API** | Server FastAPI + SQLite yang memproses event throw, menghitung XP, mengelola tier RPG, dan menyimpan data riset |
| **Frontend Web** | Antarmuka responsif berbasis HTML/CSS/JS murni - beranda, leaderboard, profil, dashboard riset, riwayat, dan panel admin |

**Masalah yang diselesaikan:** Mahasiswa sering malas membuang sampah pada tempatnya karena tidak ada insentif langsung. EcoThrow mengubah perilaku ini dengan sistem reward XP, kompetisi tier RPG, leaderboard antar-fakultas, dan streak harian - semua terhubung langsung ke hardware fisik.

---

## 🏗️ Arsitektur Sistem

```
 ┌─────────────┐          ┌─────────────┐
 │  Pengguna   │          │    Admin    │
 └──────┬──────┘          └──────┬──────┘
        │                        │
        ▼                        ▼
 ┌──────────────────────────────────────┐
 │        Browser (HTML/CSS/JS)         │
 │  fetch() → http://localhost:8000     │
 └──────────────────┬───────────────────┘
                    │
                    ▼
 ┌──────────────────────────────────────┐
 │         FastAPI Backend              │◄──── POST /api/throw ◄──── ESP32
 │         (app/main.py)                │◄──── POST /api/bin/update ◄── ESP32
 └──────────────────┬───────────────────┘
                    │
                    ▼
 ┌──────────────────────────────────────┐
 │        SQLite Database               │
 │        (ecothrow.db)                 │
 └──────────────────────────────────────┘
```

### Alur Utama (End-to-End)

```
1. Pengguna buka website → masukkan NIM → tekan "Trigger Throw"
2. Website catat XP awal → mulai polling /api/user/{nim} tiap 5 detik
3. Pengguna masukkan sampah ke bin fisik
4. Sensor HC-SR04 membaca perubahan level → ESP32 deteksi throw
5. ESP32 kirim POST /api/throw ke backend
6. Backend: validasi anti-cheat → hitung XP → update tier → simpan ke DB
7. Website: polling deteksi kenaikan XP → tampilkan animasi hasil otomatis
```

---

## 📁 Struktur Project

```
PRD-EcoThrow/
│
├── backend/
│   └── app/
│       ├── __init__.py
│       ├── main.py           ← Semua API endpoint (FastAPI)
│       ├── database.py       ← Koneksi async SQLite (aiosqlite)
│       ├── models.py         ← ORM models: User, Bin, ThrowEvent, UserPoints
│       └── gamification.py   ← Logika XP, 9-tier RPG, streak multiplier
│   ├── requirements.txt      ← Daftar dependensi Python
│   └── ecothrow.db           ← Database SQLite (auto-created saat pertama run)
│
├── esp32/
│   └── hardware/
│       └── hardware.ino      ← Sketch Arduino untuk ESP32
│
├── frontend/
│   ├── index.html            ← Beranda + Trigger Throw IoT + statistik real-time
│   ├── css/
│   │   └── style.css         ← Design system global (custom, no framework CSS)
│   ├── js/
│   │   ├── api.js            ← apiFetch helper, konstanta TIERS, lucideInline, utils
│   │   └── navbar.js         ← Auto-inject navbar, footer, dan mobile bottom nav
│   └── pages/
│       ├── about.html        ← Tentang EcoThrow + staircase tier RPG interaktif
│       ├── leaderboard.html  ← Ranking individu (XP) + ranking per-fakultas
│       ├── dashboard.html    ← Grafik aktivitas, distribusi tier, export CSV
│       ├── profile.html      ← Profil pengguna by NIM + registrasi + progress bar
│       ├── history.html      ← Riwayat semua throw + filter nama/NIM/fakultas/tier
│       ├── admin.html        ← Panel admin (protected) - kelola bin & user
│       └── admin-login.html  ← Halaman login admin + lockout protection
│
├── .gitignore
├── README.md
└── start.bat                 ← One-click jalankan backend (Windows)
```

> **Catatan:** `ecothrow.db` di-generate otomatis saat backend pertama kali dijalankan. File ini ada di `.gitignore` dan tidak perlu dibuat manual.

---

## ⚙️ Setup & Menjalankan

### Prasyarat

- **Python 3.10+**
- **Browser modern** (Chrome / Firefox / Edge)
- *(Opsional)* VS Code + ekstensi **Live Server**
- *(Untuk hardware)* Board ESP32 + sensor HC-SR04 + Arduino IDE

---

### Cara Cepat - Windows

Cukup klik dua kali `start.bat` di root project. Script ini otomatis:
1. Masuk ke folder `backend/`
2. Mengaktifkan virtual environment
3. Install semua dependensi dari `requirements.txt`
4. Menjalankan server di port `8000`

---

### Setup Manual

#### 1. Clone Repository

```bash
git clone https://github.com/Levvroy/PRD-EcoThrow.git
cd PRD-EcoThrow
```

#### 2. Buat & Aktifkan Virtual Environment

```bash
cd backend
python -m venv venv

# Windows
venv\Scripts\activate

# macOS / Linux
source venv/bin/activate
```

#### 3. Install Dependensi

```bash
pip install -r requirements.txt
```

#### 4. Jalankan Backend

```bash
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

> **Penting:** Gunakan `--host 0.0.0.0` agar ESP32 bisa mengakses API dari jaringan WiFi yang sama.

| URL | Keterangan |
|-----|------------|
| `http://localhost:8000` | Backend API |
| `http://localhost:8000/docs` | Swagger UI - dokumentasi & uji endpoint interaktif |
| `http://localhost:8000/redoc` | ReDoc - dokumentasi API alternatif |

#### 5. Buka Frontend

Buka `frontend/index.html` langsung di browser, **atau** gunakan **Live Server** di VS Code (klik kanan → *Open with Live Server*) agar hot-reload berfungsi.

---

### Upload Sketch ke ESP32

1. Buka `esp32/hardware/hardware.ino` di **Arduino IDE**
2. Install board **ESP32** via Board Manager (jika belum)
3. Install library yang dibutuhkan:
   - `WiFi.h` (bawaan ESP32)
   - `HTTPClient.h` (bawaan ESP32)
4. Ubah variabel berikut di sketch sesuai jaringan lokal kamu:

```cpp
const char* ssid     = "NAMA_WIFI_KAMU";
const char* password = "PASSWORD_WIFI_KAMU";
const char* serverIP = "IP_LAPTOP_KAMU";  // cek dengan `ipconfig`
const char* binID    = "ID_BIN_DARI_DATABASE";
```

5. Upload ke board ESP32, buka Serial Monitor (115200 baud) untuk debug

---

## 🖥️ Halaman Website

| Halaman | File | Deskripsi |
|---------|------|-----------|
| **Beranda** | `index.html` | Hero section, Trigger Throw inline, statistik real-time (total user/throw/XP), status semua bin, Top 3 leaderboard |
| **Tentang** | `pages/about.html` | Penjelasan sistem EcoThrow, visualisasi staircase tier RPG interaktif, cara kerja IoT, dan formula XP lengkap |
| **Leaderboard** | `pages/leaderboard.html` | Ranking individu berdasarkan XP + ranking per-fakultas (total XP gabungan seluruh anggota) |
| **Dashboard** | `pages/dashboard.html` | Grafik aktivitas harian, distribusi tier pengguna, data riset ringkasan, fitur export data ke CSV |
| **Profil** | `pages/profile.html` | Cari profil pengguna berdasarkan NIM, form registrasi pengguna baru, progress bar menuju tier berikutnya |
| **Riwayat** | `pages/history.html` | Daftar seluruh throw events, filter berdasarkan nama/NIM/fakultas/tier, detail riwayat per pengguna |
| **Admin** | `pages/admin.html` | Kelola bin (tambah/lihat), kelola user (lihat/hapus), simulasi throw hardware, manual throw untuk testing - **butuh login** |
| **Admin Login** | `pages/admin-login.html` | Form login admin + sistem lockout 5 menit setelah 5× percobaan salah |

### Navigasi Mobile

Website menggunakan **Bottom Navigation Bar 5-tab** yang diinjeksi otomatis oleh `navbar.js` pada perangkat mobile:

```
[ Beranda ]  [ Ranking ]  [ Throw ]  [ Profil ]  [ Riwayat ]
```

Navbar dan footer desktop juga diinjeksi otomatis - tidak perlu duplikasi HTML di setiap halaman.

---

## 📡 API Endpoints

Base URL: `http://localhost:8000`  
Dokumentasi interaktif: `http://localhost:8000/docs`

---

### 👤 User

| Method | Endpoint | Body / Param | Deskripsi |
|--------|----------|--------------|-----------|
| `POST` | `/api/user/register` | `{ nim, name, faculty }` | Daftarkan pengguna baru, generate QR code unik |
| `GET`  | `/api/user/{nim}` | - | Ambil profil lengkap + 10 riwayat throw terakhir |
| `GET`  | `/api/users` | - | Array semua pengguna beserta XP dan tier saat ini |

**Contoh register:**
```json
POST /api/user/register
{
  "nim": "13525167",
  "name": "Joel Angga",
  "faculty": "STEI"
}
```

**Respons:**
```json
{
  "success": true,
  "user_id": 1,
  "name": "Joel Angga",
  "qr_code": "ECO-13525167-AB12CD"
}
```

---

### 🗑️ Throw Event

> Endpoint ini dipanggil oleh **ESP32**, bukan pengguna secara langsung.

| Method | Endpoint | Body | Deskripsi |
|--------|----------|------|-----------|
| `POST` | `/api/throw` | `{ user_qr_code, bin_id, bin_level_before, bin_level_after }` | Catat event throw, validasi anti-cheat, hitung & tambahkan XP |

**Contoh request:**
```json
POST /api/throw
{
  "user_qr_code": "ECO-13525167-AB12CD",
  "bin_id": "BIN-001",
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

> **Anti-cheat:** Throw **tidak diverifikasi** dan tidak memberikan XP jika `bin_level_after − bin_level_before < 2.0` (dianggap noise sensor).

---

### 🏆 Leaderboard

| Method | Endpoint | Query | Deskripsi |
|--------|----------|-------|-----------|
| `GET`  | `/api/leaderboard` | `?limit=20` | Array ranking individu: rank, nama, XP, tier |
| `GET`  | `/api/leaderboard/faculty` | - | Ranking per-fakultas berdasarkan total XP gabungan |

---

### 🗑️ Bin

| Method | Endpoint | Body | Deskripsi |
|--------|----------|------|-----------|
| `GET`  | `/api/bins` | - | Status semua bin: ID, lokasi, kapasitas (%), status NORMAL/SEDANG/PENUH |
| `POST` | `/api/bin/add` | `{ location }` | Tambah bin baru ke database |
| `POST` | `/api/bin/update` | `{ bin_id, capacity_pct }` | Update kapasitas bin (dipanggil ESP32 secara periodik) |

**Status bin:**
| Kapasitas | Status |
|-----------|--------|
| 0 – 59% | `NORMAL` |
| 60 – 80% | `SEDANG` |
| > 80% | `PENUH` |

---

### 📊 Statistik

| Method | Endpoint | Deskripsi |
|--------|----------|-----------|
| `GET`  | `/api/stats` | `{ total_users, total_throws, total_xp, total_bins, daily_throws[] }` |

---

## 🔌 Integrasi ESP32 & Hardware

### Komponen Hardware

| Komponen | Spesifikasi |
|----------|-------------|
| Mikrokontroler | ESP32 (WiFi built-in) |
| Sensor jarak | HC-SR04 (ultrasonik, range 2cm – 400cm) |
| Koneksi | WiFi 2.4GHz → HTTP POST ke FastAPI |

### Diagram Wiring HC-SR04 ↔ ESP32

```
HC-SR04     ESP32
-------     -----
VCC    →    3.3V / 5V
GND    →    GND
TRIG   →    GPIO 5
ECHO   →    GPIO 18
```

> Sesuaikan pin TRIG dan ECHO dengan definisi di `hardware.ino` jika berbeda.

---

### Konfigurasi Sketch (`esp32/hardware/hardware.ino`)

Sebelum upload, edit bagian ini:

```cpp
// WiFi
const char* ssid     = "NAMA_WIFI";
const char* password = "PASSWORD_WIFI";

// Backend
const char* serverIP = "192.168.X.X";   // IP laptop di jaringan lokal (ipconfig)
const int   serverPort = 8000;

// Bin ID (dari database - lihat GET /api/bins)
const char* binID = "BIN-001";
```

---

### Alur Komunikasi ESP32 ↔ Backend

#### 1. Update kapasitas bin (periodik)

Dikirim setiap beberapa detik atau saat level berubah signifikan:

```http
POST http://{IP_LAPTOP}:8000/api/bin/update
Content-Type: application/json

{
  "bin_id": "BIN-001",
  "capacity_pct": 75.5
}
```

#### 2. Event buang sampah (saat throw terdeteksi)

Dikirim setelah sensor mendeteksi perubahan level:

```http
POST http://{IP_LAPTOP}:8000/api/throw
Content-Type: application/json

{
  "user_qr_code": "ECO-13525167-AB12CD",
  "bin_id": "BIN-001",
  "bin_level_before": 30.0,
  "bin_level_after": 38.5
}
```

---

### Alur Trigger dari Website ke ESP32

```
[Website]  → GET /api/user/{nim}       → catat XP awal
[Website]  → polling GET /api/user/{nim} setiap 5 detik
[Pengguna] → masukkan sampah ke bin fisik
[ESP32]    → sensor HC-SR04 deteksi perubahan level
[ESP32]    → POST /api/throw
[Backend]  → validasi → hitung XP → update DB
[Website]  → polling deteksi XP naik → tampilkan animasi hasil
```

---

## 🎮 Sistem Gamifikasi & Tier RPG

### 9 Tier Material

| # | Tier | Rentang XP | Tema |
|---|------|------------|------|
| 1 | **Copper** | 0 – 299 XP | Tembaga (pemula) |
| 2 | **Bronze** | 300 – 799 XP | Perunggu |
| 3 | **Silver** | 800 – 1.999 XP | Perak |
| 4 | **Gold** | 2.000 – 4.999 XP | Emas |
| 5 | **Platinum** | 5.000 – 9.999 XP | Platinum |
| 6 | **Diamond** | 10.000 – 19.999 XP | Berlian |
| 7 | **Mithril** | 20.000 – 34.999 XP | Mithril (mitologi) |
| 8 | **Orichalcum** | 35.000 – 59.999 XP | Orichalcum (legenda) |
| 9 | **Adamantium** | 60.000+ XP | Adamantium (terkeras) |

---

### Formula XP

```
XP = base_xp × bin_bonus × streak_multiplier
```

| Variabel | Nilai |
|----------|-------|
| `base_xp` | 10 XP per throw |
| `bin_bonus` (kapasitas < 60%) | 1.0× |
| `bin_bonus` (kapasitas 60–80%) | 1.5× |
| `bin_bonus` (kapasitas > 80%) | 2.0× |
| `streak_multiplier` | `min(1.0 + streak_days × 0.1, 2.0)` |

**Contoh kalkulasi:**
- Streak 7 hari + bin 85% penuh:
  ```
  10 × 2.0 × min(1.0 + 7×0.1, 2.0)
  = 10 × 2.0 × 1.7
  = 34 XP
  ```
- Streak 0 hari + bin normal:
  ```
  10 × 1.0 × 1.0 = 10 XP
  ```

---

### Sistem Streak

Streak dihitung berdasarkan **hari berturut-turut** pengguna membuang sampah. Streak tidak terputus selama pengguna melakukan minimal 1 throw per hari. Streak multiplier maksimal adalah **2.0×** (tercapai di streak ke-10).

---

### Validasi Anti-Cheat

Throw hanya dihitung jika:

```
bin_level_after − bin_level_before ≥ 2.0
```

Perubahan di bawah threshold `2.0` dianggap **noise sensor** dan tidak menghasilkan XP. Ini mencegah pengguna "fake throw" dengan menggoyang tempat sampah.

---

## 🔐 Autentikasi Admin

Akses admin terpisah sepenuhnya dari sistem pengguna biasa.

| Parameter | Detail |
|-----------|--------|
| URL Login | `pages/admin-login.html` |
| Password Default | `etrosuksesjaya` |
| Lokasi config password | `admin-login.html` → variabel `ADMIN_PASSWORD` |
| Lockout | 5 menit setelah **5× percobaan salah** |
| Session | Disimpan di `localStorage` (`et_admin = "true"`) |
| Guard | `admin.html` otomatis redirect ke login jika session tidak ada |

> ⚠️ **Penting:** Ubah `ADMIN_PASSWORD` sebelum deploy ke produksi.

### Fitur Admin Panel

- ➕ Tambah bin baru (isi lokasi)
- 📋 Lihat semua bin beserta status kapasitas
- 👥 Lihat semua pengguna terdaftar
- 🗑️ Hapus pengguna dari database
- ⚡ **Simulasi throw hardware** - tanpa perlu scan QR fisik
- 🔧 **Manual throw** - untuk keperluan testing & debugging

---

## 🛠️ Tech Stack

### Frontend

| Komponen | Teknologi | Keterangan |
|----------|-----------|------------|
| Markup | HTML5 Semantik | Struktur aksesibel, semantic tags |
| Styling | Custom CSS | Design system sendiri, tanpa Tailwind/SASS |
| UI Framework | Bootstrap 5.3 | Digunakan untuk grid & utilities saja |
| Icons | Lucide Icons | SVG inline, satu warna (monotone) |
| Fonts | Inter + Libre Baskerville | Via Google Fonts - UI + brand typeface |
| JavaScript | Vanilla JS ES2020+ | Tanpa bundler, tanpa framework |

### Backend

| Komponen | Teknologi | Keterangan |
|----------|-----------|------------|
| Framework | FastAPI | Async, auto-docs Swagger/ReDoc |
| Database | SQLite | File-based, cocok untuk skala kampus |
| Driver DB | aiosqlite | Async I/O untuk SQLite |
| ORM | SQLAlchemy 2.0 | Async mode |
| Server | Uvicorn | ASGI server untuk FastAPI |
| Validasi | Pydantic v2 | Schema validation request/response |

### Hardware

| Komponen | Spesifikasi | Keterangan |
|----------|-------------|------------|
| Mikrokontroler | ESP32 (Wroom-32) | Dual-core, WiFi built-in |
| Sensor | HC-SR04 | Ultrasonik, deteksi level sampah |
| Bahasa | C++ / Arduino | Sketch di `esp32/hardware/hardware.ino` |
| Koneksi | WiFi 2.4GHz | HTTP POST langsung ke FastAPI |

---

## 👨‍💻 Kontributor

**Kelompok 9 - PRD K51 - STEI-K ITB 2025**

Project ini merupakan tugas besar mata kuliah **Perancangan dan Rekayasa Desain (PRD)** Semester 2 Tahun 2025, Institut Teknologi Bandung.

---

<div align="center">

*EcoThrow, masuk pak eko.*

</div>
