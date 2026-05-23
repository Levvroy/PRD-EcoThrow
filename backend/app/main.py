from fastapi import FastAPI, Depends, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, desc, func
from pydantic import BaseModel
from datetime import datetime, timedelta
from typing import Optional
import os

from app.database import engine, Base, get_db
from app.models import User, Bin, ThrowEvent, UserPoints
from app.gamification import calculate_xp, get_level, get_level_progress

app = FastAPI(title="EcoThrow API", version="1.0.0")

# ── In-memory store untuk pending throw ─────────────────────────
# Struktur BARU (v1.2): slot eksklusif per bin.
#
# MASALAH SEBELUMNYA:
#   pending_throws[bin_id] = {"qr_code": ..., "timestamp": ...}
#   → Hanya 1 slot per bin, tanpa penolakan. User B yang POST
#     belakangan langsung menimpa user A → XP masuk ke user salah,
#     atau user yang tidak ada di depan bin ikut mendapat XP.
#
# SOLUSI (v1.2) — Slot Eksklusif:
#   Bin hanya bisa dipakai 1 user sekaligus. POST kedua ditolak
#   dengan 409 Conflict selama sesi pertama masih aktif (< 2 menit).
#   Frontend wajib tampilkan "Bin sedang dipakai, coba beberapa detik lagi."
#
# Value: {"qr_code": str, "nim": str, "timestamp": datetime}
pending_throws: dict = {}

# CORS — izinkan frontend lokal akses API
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.on_event("startup")
async def startup():
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    print("EcoThrow API siap di http://localhost:8000")

# ── Schema ───────────────────────────────────────────────────────
class ThrowReq(BaseModel):
    user_qr_code:     str
    bin_id:           str
    bin_level_before: float
    bin_level_after:  float

class RegisterReq(BaseModel):
    nim:     str
    name:    str
    faculty: Optional[str] = "STEI"

class BinUpdateReq(BaseModel):
    bin_id:       str
    capacity_pct: float

class AddBinReq(BaseModel):
    location: str

class PendingThrowReq(BaseModel):
    user_qr_code: str
    bin_id:       str
    nim:          str   # v1.2: wajib disertakan untuk verifikasi & logging

# ── Root ─────────────────────────────────────────────────────────
@app.get("/")
async def root():
    return {"status": "ok", "message": "EcoThrow API aktif!"}

# ══════════════════════════════════════════════════════════════════
# USER
# ══════════════════════════════════════════════════════════════════
@app.post("/api/user/register")
async def register(req: RegisterReq, db: AsyncSession = Depends(get_db)):
    existing = await db.execute(select(User).where(User.nim == req.nim))
    if existing.scalar_one_or_none():
        raise HTTPException(400, "NIM sudah terdaftar")

    import uuid
    qr = f"ECO-{req.nim}-{uuid.uuid4().hex[:6].upper()}"
    user = User(nim=req.nim, name=req.name, faculty=req.faculty, qr_code=qr)
    db.add(user)
    await db.flush()      # ← flush dulu agar user.id ter-generate
    await db.refresh(user)  # ← refresh untuk pastikan state terbaru

    pts = UserPoints(user_id=user.id)
    db.add(pts)
    await db.commit()
    return {"success": True, "qr_code": qr, "user_id": user.id, "name": user.name}

@app.get("/api/user/{nim}")
async def get_user(nim: str, db: AsyncSession = Depends(get_db)):
    res  = await db.execute(select(User).where(User.nim == nim))
    user = res.scalar_one_or_none()
    if not user:
        raise HTTPException(404, "User tidak ditemukan")

    pts_res = await db.execute(select(UserPoints).where(UserPoints.user_id == user.id))
    pts     = pts_res.scalar_one_or_none()
    if not pts:
        pts = UserPoints(user_id=user.id)
        db.add(pts); await db.commit()

    prog = get_level_progress(pts.total_xp)

    # Ambil riwayat 10 event terakhir
    ev_res = await db.execute(
        select(ThrowEvent)
        .where(ThrowEvent.user_id == user.id)
        .order_by(desc(ThrowEvent.timestamp))
        .limit(10)
    )
    events = ev_res.scalars().all()

    return {
        "id": user.id, "nim": user.nim, "name": user.name,
        "faculty": user.faculty, "qr_code": user.qr_code,
        "total_xp": pts.total_xp, "level": pts.level,
        "streak_days": pts.streak_days, "eco_points": pts.eco_points,
        **prog,
        "recent_events": [
            {"timestamp": str(e.timestamp), "points": e.points_earned,
             "bin_id": e.bin_id, "verified": e.verified}
            for e in events
        ]
    }

@app.get("/api/users")
async def list_users(db: AsyncSession = Depends(get_db)):
    res   = await db.execute(select(User))
    users = res.scalars().all()
    result = []
    for u in users:
        pts_res = await db.execute(select(UserPoints).where(UserPoints.user_id == u.id))
        pts     = pts_res.scalar_one_or_none()
        result.append({
            "id": u.id, "nim": u.nim, "name": u.name,
            "faculty": u.faculty, "qr_code": u.qr_code,
            "total_xp": pts.total_xp if pts else 0,
            "level": pts.level if pts else "Bronze",
        })
    return result

# ══════════════════════════════════════════════════════════════════
# THROW EVENT — dipanggil dari ESP32
# ══════════════════════════════════════════════════════════════════
@app.post("/api/throw")
async def throw_event(req: ThrowReq, db: AsyncSession = Depends(get_db)):
    # Cari user
    res  = await db.execute(select(User).where(User.qr_code == req.user_qr_code))
    user = res.scalar_one_or_none()
    if not user:
        raise HTTPException(404, "QR code tidak dikenali")

    # Time-gate validasi: level bin harus naik minimal 2%
    diff     = req.bin_level_after - req.bin_level_before
    verified = diff >= 2.0

    # Ambil poin user
    pts_res = await db.execute(select(UserPoints).where(UserPoints.user_id == user.id))
    pts     = pts_res.scalar_one_or_none()
    if not pts:
        pts = UserPoints(user_id=user.id)
        db.add(pts)

    xp, eco = 0, 0
    if verified:
        # Update streak
        now = datetime.utcnow()
        if pts.last_activity and (now - pts.last_activity) < timedelta(hours=48):
            pts.streak_days += 1
        else:
            pts.streak_days = 1

        xp, eco = calculate_xp(req.bin_level_after, pts.streak_days)
        pts.total_xp     += xp
        pts.eco_points   += eco
        pts.level         = get_level(pts.total_xp)
        pts.last_activity = now

    # Catat event
    event = ThrowEvent(
        user_id=user.id, bin_id=req.bin_id,
        points_earned=xp, verified=verified,
        bin_level_before=req.bin_level_before,
        bin_level_after=req.bin_level_after,
    )
    db.add(event)

    # Update kapasitas bin
    bin_res = await db.execute(select(Bin).where(Bin.id == req.bin_id))
    bin_obj = bin_res.scalar_one_or_none()
    if bin_obj:
        bin_obj.capacity_pct = req.bin_level_after
        bin_obj.last_updated = datetime.utcnow()

    await db.commit()

    prog = get_level_progress(pts.total_xp)
    return {
        "success": True,
        "verified": verified,
        "xp_earned": xp,
        "eco_pts_earned": eco,
        "new_total_xp": pts.total_xp,
        "new_level": pts.level,
        "streak_days": pts.streak_days,
        "progress_pct": prog["progress_pct"],
        "xp_to_next": prog["xp_to_next"],
        "message": f"+{xp} XP! Level {pts.level}" if verified else "Tidak terdeteksi sampah masuk",
    }

# ══════════════════════════════════════════════════════════════════
# LEADERBOARD
# ══════════════════════════════════════════════════════════════════
@app.get("/api/leaderboard")
async def leaderboard(limit: int = 20, db: AsyncSession = Depends(get_db)):
    res = await db.execute(
        select(User, UserPoints)
        .join(UserPoints, User.id == UserPoints.user_id)
        .order_by(desc(UserPoints.total_xp))
        .limit(limit)
    )
    rows = res.all()
    return [
        {
            "rank": i + 1,
            "name": u.name, "nim": u.nim,
            "faculty": u.faculty,
            "total_xp": p.total_xp,
            "level": p.level,
            "eco_points": p.eco_points,
            "streak_days": p.streak_days,
        }
        for i, (u, p) in enumerate(rows)
    ]

@app.get("/api/leaderboard/faculty")
async def leaderboard_faculty(db: AsyncSession = Depends(get_db)):
    res = await db.execute(
        select(User.faculty, func.sum(UserPoints.total_xp).label("total"))
        .join(UserPoints, User.id == UserPoints.user_id)
        .group_by(User.faculty)
        .order_by(desc("total"))
    )
    rows = res.all()
    return [
        {"rank": i+1, "faculty": r.faculty, "total_xp": r.total or 0}
        for i, r in enumerate(rows)
    ]

# ══════════════════════════════════════════════════════════════════
# PENDING THROW — jembatan website ↔ ESP32
# ══════════════════════════════════════════════════════════════════

@app.post("/api/pending_throw")
async def set_pending_throw(req: PendingThrowReq, db: AsyncSession = Depends(get_db)):
    """
    Dipanggil dari website saat user menekan tombol 'Buang Sampah'.

    v1.2 — Slot Eksklusif:
      Setiap bin hanya bisa menampung 1 pending throw aktif sekaligus.
      Jika bin sedang dipakai user lain dan sesi belum expire, request
      baru ditolak dengan 409 Conflict. Frontend wajib tampilkan pesan
      "Bin sedang dipakai, coba beberapa detik lagi."

    Ini mencegah race condition dimana user B menimpa pendaftaran user A
    sehingga XP masuk ke user yang salah.
    """
    # Validasi NIM wajib diisi (double-check selain frontend)
    if not req.nim or not req.nim.strip():
        raise HTTPException(400, "NIM wajib diisi")

    # Validasi user ada & NIM cocok dengan QR code yang dikirim
    res  = await db.execute(select(User).where(User.qr_code == req.user_qr_code))
    user = res.scalar_one_or_none()
    if not user:
        raise HTTPException(404, "QR code tidak dikenali")

    # Verifikasi NIM cocok dengan pemilik QR code
    # Mencegah seseorang mengirim QR orang lain dengan NIM palsu
    if str(user.nim) != str(req.nim.strip()):
        raise HTTPException(403, "NIM tidak cocok dengan QR code")

    # Validasi bin ada
    bin_res = await db.execute(select(Bin).where(Bin.id == req.bin_id))
    if not bin_res.scalar_one_or_none():
        raise HTTPException(404, "Bin tidak ditemukan")

    # Cek apakah bin sedang dalam sesi aktif user lain (slot eksklusif)
    existing = pending_throws.get(req.bin_id)
    if existing:
        age = (datetime.utcnow() - existing["timestamp"]).total_seconds()
        if age <= 120:
            # Sesi aktif — cek apakah ini user yang sama (re-trigger diizinkan)
            if existing["qr_code"] != req.user_qr_code:
                sisa = int(120 - age)
                raise HTTPException(
                    409,
                    f"Bin sedang dipakai user lain. Sisa waktu: {sisa} detik. "
                    f"Coba lagi setelah sesi sebelumnya selesai."
                )
            # User yang sama re-trigger → perbarui timestamp (perpanjang sesi)
            pending_throws[req.bin_id]["timestamp"] = datetime.utcnow()
            return {"success": True, "message": "Sesi diperpanjang. Silakan buang sampah ke bin.", "renewed": True}

    # Slot kosong / sudah expire → daftarkan sesi baru
    pending_throws[req.bin_id] = {
        "qr_code":   req.user_qr_code,
        "nim":       req.nim.strip(),
        "timestamp": datetime.utcnow(),
    }
    return {"success": True, "message": "Pending throw terdaftar. Silakan buang sampah ke bin.", "renewed": False}


@app.get("/api/pending_throw/status")
async def get_pending_throw_status(bin_id: str):
    """
    Dicek oleh frontend sebelum user menekan tombol 'Buang Sampah'.
    Mengembalikan apakah bin sedang dalam sesi aktif user lain.
    Frontend bisa tampilkan peringatan jika bin sedang dipakai.
    """
    entry = pending_throws.get(bin_id)
    if not entry:
        return {"busy": False, "sisa_detik": 0}

    age = (datetime.utcnow() - entry["timestamp"]).total_seconds()
    if age > 120:
        # Sudah expire — hapus & laporkan kosong
        pending_throws.pop(bin_id, None)
        return {"busy": False, "sisa_detik": 0}

    return {"busy": True, "sisa_detik": int(120 - age)}


@app.get("/api/pending_throw")
async def get_pending_throw(bin_id: str):
    """
    Di-poll oleh ESP32 setiap 5 detik.
    Mengembalikan QR code user yang sedang menunggu di bin ini.
    Pending throw kadaluarsa setelah 2 menit.
    """
    entry = pending_throws.get(bin_id)
    if not entry:
        raise HTTPException(404, "Tidak ada pending throw")

    # Expire setelah 2 menit
    age = (datetime.utcnow() - entry["timestamp"]).total_seconds()
    if age > 120:
        del pending_throws[bin_id]
        raise HTTPException(404, "Pending throw sudah kadaluarsa")

    return {"qr_code": entry["qr_code"]}


@app.delete("/api/pending_throw")
async def clear_pending_throw(bin_id: str):
    """Hapus pending throw setelah ESP32 berhasil kirim event throw."""
    pending_throws.pop(bin_id, None)
    return {"success": True}


# ══════════════════════════════════════════════════════════════════
# BIN
# ══════════════════════════════════════════════════════════════════
@app.get("/api/bins")
async def all_bins(db: AsyncSession = Depends(get_db)):
    res  = await db.execute(select(Bin))
    bins = res.scalars().all()
    return [
        {
            "id": b.id, "location": b.location,
            "capacity_pct": b.capacity_pct,
            "status": "PENUH" if b.capacity_pct >= 80
                      else "SEDANG" if b.capacity_pct >= 50 else "NORMAL",
            "last_updated": str(b.last_updated),
        }
        for b in bins
    ]

@app.post("/api/bin/add")
async def add_bin(req: AddBinReq, db: AsyncSession = Depends(get_db)):
    b = Bin(location=req.location)
    db.add(b); await db.commit()
    return {"success": True, "bin_id": b.id, "location": b.location}

@app.post("/api/bin/update")
async def update_bin(req: BinUpdateReq, db: AsyncSession = Depends(get_db)):
    res     = await db.execute(select(Bin).where(Bin.id == req.bin_id))
    bin_obj = res.scalar_one_or_none()
    if not bin_obj:
        raise HTTPException(404, "Bin tidak ditemukan")
    bin_obj.capacity_pct = req.capacity_pct
    bin_obj.last_updated = datetime.utcnow()
    await db.commit()
    return {"success": True, "capacity_pct": req.capacity_pct}

# ══════════════════════════════════════════════════════════════════
# STATISTIK — untuk halaman riset/data
# ══════════════════════════════════════════════════════════════════
@app.get("/api/stats")
async def stats(db: AsyncSession = Depends(get_db)):
    total_users  = (await db.execute(select(func.count(User.id)))).scalar()
    total_throws = (await db.execute(select(func.count(ThrowEvent.id)))).scalar()
    total_xp     = (await db.execute(select(func.sum(UserPoints.total_xp)))).scalar() or 0
    total_eco    = (await db.execute(select(func.sum(UserPoints.eco_points)))).scalar() or 0
    total_bins   = (await db.execute(select(func.count(Bin.id)))).scalar()

    # Event per hari (7 hari terakhir)
    seven_days_ago = datetime.utcnow() - timedelta(days=7)
    events_res = await db.execute(
        select(ThrowEvent).where(ThrowEvent.timestamp >= seven_days_ago)
        .order_by(ThrowEvent.timestamp)
    )
    events = events_res.scalars().all()

    daily = {}
    for e in events:
        day = e.timestamp.strftime("%Y-%m-%d") if e.timestamp else "unknown"
        daily[day] = daily.get(day, 0) + 1

    return {
        "total_users":  total_users,
        "total_throws": total_throws,
        "total_xp":     total_xp,
        "total_eco":    total_eco,
        "total_bins":   total_bins,
        "daily_throws": [{"date": k, "count": v} for k, v in sorted(daily.items())],
    }