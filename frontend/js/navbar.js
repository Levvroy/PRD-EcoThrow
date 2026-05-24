/* navbar.js - inject navbar, footer, mobile bottom nav, & throw popup */

const IS_ROOT = !window.location.pathname.includes("/pages/");
const ROOT = IS_ROOT ? "" : "../";

function isAdmin() {
  return localStorage.getItem("et_admin") === "true";
}

const NAV_LINKS_USER = [
  { href: `${ROOT}index.html`,             label: "Beranda",     icon: "home"      },
  { href: `${ROOT}pages/leaderboard.html`, label: "Leaderboard", icon: "trophy"    },
  { href: `${ROOT}pages/dashboard.html`,   label: "Dashboard",   icon: "bar-chart" },
  { href: `${ROOT}pages/profile.html`,     label: "Profil",      icon: "user"      },
  { href: `${ROOT}pages/history.html`,     label: "Riwayat",     icon: "history"   },
  { href: `${ROOT}pages/about.html`,       label: "Tentang",     icon: "info"      },
];

function buildNavbar() {
  const userLinks = NAV_LINKS_USER.map(l => `
    <a href="${l.href}" class="et-nav-link">
      ${lucideInline(l.icon, 15)} ${l.label}
    </a>`).join("");

  const adminLink = isAdmin() ? `
    <a href="${ROOT}pages/admin.html" class="et-nav-link nav-admin-link">
      ${lucideInline("settings", 15)} Admin
    </a>
    <button onclick="logoutAdmin()" class="et-nav-link nav-admin-link" style="background:none;border:none;cursor:pointer">
      ${lucideInline("x", 14)} Keluar
    </button>` : `
    <a href="${ROOT}pages/admin-login.html" class="et-nav-link" style="color:var(--slate-300);font-size:.78rem">
      ${lucideInline("lock", 13)} Admin
    </a>`;

  const mobileMegaLinks = NAV_LINKS_USER.map(l => `
    <a href="${l.href}" class="et-nav-link d-flex align-items-center gap-2 py-2 px-3">
      ${lucideInline(l.icon, 16)} ${l.label}
    </a>`).join("");

  return `
<nav class="et-navbar" style="padding-left:env(safe-area-inset-left,0px);padding-right:env(safe-area-inset-right,0px)">
  <div class="container" style="max-width:1140px">
    <div class="d-flex align-items-center justify-content-between py-2">
      <a href="${ROOT}index.html" class="et-brand" style="text-decoration:none">
        <div class="brand-icon">${lucideInline("trash2", 18)}</div>
        Eco<span class="brand-accent">Throw</span>
        ${isAdmin() ? '<span class="admin-badge ms-1">ADMIN</span>' : ""}
      </a>

      <div class="d-none d-md-flex align-items-center gap-1">
        ${userLinks}
        ${adminLink}
      </div>

      <button id="et-nav-toggle" class="d-md-none"
        style="background:none;border:none;padding:8px;cursor:pointer;color:var(--slate-700)"
        aria-label="Menu">
        ${lucideInline("menu", 22)}
      </button>
    </div>

    <!-- Mobile dropdown menu -->
    <div id="et-nav-menu" style="display:none;padding-bottom:.75rem;border-top:1px solid var(--slate-100);padding-top:.5rem">
      <div class="d-flex flex-column gap-1">
        ${mobileMegaLinks}
        ${isAdmin() ? `
        <a href="${ROOT}pages/admin.html" class="et-nav-link nav-admin-link d-flex align-items-center gap-2 py-2 px-3">
          ${lucideInline("settings", 16)} Admin Panel
        </a>
        <button onclick="logoutAdmin()" class="et-nav-link nav-admin-link d-flex align-items-center gap-2 py-2 px-3 w-100" style="background:none;border:none;cursor:pointer;text-align:left">
          ${lucideInline("x", 16)} Keluar Admin
        </button>` : `
        <a href="${ROOT}pages/admin-login.html" class="et-nav-link d-flex align-items-center gap-2 py-2 px-3" style="color:var(--slate-300)">
          ${lucideInline("lock", 16)} Login Admin
        </a>`}
      </div>
    </div>
  </div>
</nav>`;
}

function buildMobileNav() {
  const curPage = window.location.pathname.split("/").pop() || "index.html";
  const isActive = (page) => curPage === page ? "active" : "";
  return `
<nav class="mob-nav" id="mob-nav" style="padding-bottom:env(safe-area-inset-bottom,0px);padding-left:env(safe-area-inset-left,0px);padding-right:env(safe-area-inset-right,0px)">
  <a href="${ROOT}index.html" class="mob-nav-item ${isActive("index.html")}">
    ${lucideInline("home", 20)}<span>Beranda</span>
  </a>
  <a href="${ROOT}pages/leaderboard.html" class="mob-nav-item ${isActive("leaderboard.html")}">
    ${lucideInline("trophy", 20)}<span>Ranking</span>
  </a>
  <button type="button" class="mob-nav-item mob-nav-throw" onclick="openThrowPopup()" title="Buang Sampah" aria-label="Buang Sampah">
    ${lucideInline("trash2", 22)}<span>Throw</span>
  </button>
  <a href="${ROOT}pages/profile.html" class="mob-nav-item ${isActive("profile.html")}">
    ${lucideInline("user", 20)}<span>Profil</span>
  </a>
  <a href="${ROOT}pages/history.html" class="mob-nav-item ${isActive("history.html")}">
    ${lucideInline("history", 20)}<span>Riwayat</span>
  </a>
</nav>`;
}

function buildFooter() {
  return `
<footer class="et-footer">
  <div class="container">
    <p>
      ${lucideInline("trash2", 13)} &nbsp;
      <strong style="color:var(--slate-700)">EcoThrow</strong>
      &nbsp;·&nbsp;
      <span class="foot-accent">Kelompok 9 · PRD K51 · STEI-K ITB 2025</span>
    </p>
  </div>
</footer>`;
}

/* ══════════════════════════════════════════════════════════════════
   THROW POPUP — bottom sheet, inject sekali, aktif di semua halaman
   Alur:
     1. User ketik NIM → tekan Buang Sekarang
     2. Validasi NIM via GET /api/user/{nim}
     3. Pilih bin terbaik (tidak penuh)
     4. Cek slot via GET /api/pending_throw/status
     5. POST /api/pending_throw  (daftarkan sesi)
     6. Polling GET /api/user/{nim} setiap 5 detik — tunggu XP naik
     7. Tampilkan hasil / timeout 120 detik
   
   State machine sederhana:
     idle → loading → waiting (hardware aktif) → done | timeout | error
══════════════════════════════════════════════════════════════════ */

let _throwPoller    = null;
let _throwLastXP    = 0;
let _throwNIM       = null;
let _throwBin       = null;

const POPUP_API = window.location.hostname === "localhost"
  ? "http://localhost:8000"
  : "http://localhost:8000"; // ganti jika server beda IP

async function _popupFetch(path, opts = {}) {
  const res = await fetch(POPUP_API + path, {
    headers: { "Content-Type": "application/json" },
    ...opts,
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: "Error tidak diketahui" }));
    throw new Error(err.detail || "Request gagal");
  }
  return res.json();
}

function buildThrowPopup() {
  return `
<!-- ── Throw Popup Overlay ──────────────────────────────────── -->
<div id="throw-popup-overlay"
     onclick="_overlayClose(event)"
     style="
       display:none;
       position:fixed;
       inset:0;
       z-index:9990;
       background:rgba(15,23,42,.48);
       backdrop-filter:blur(3px);
       -webkit-backdrop-filter:blur(3px);
       align-items:flex-end;
       justify-content:center;
     ">

  <div id="throw-popup-sheet"
       style="
         background:#fff;
         border-radius:20px 20px 0 0;
         width:100%;
         max-width:480px;
         padding:0 0 env(safe-area-inset-bottom,12px);
         box-shadow:0 -8px 40px rgba(0,0,0,.18);
         transform:translateY(100%);
         transition:transform .32s cubic-bezier(.32,0,.15,1);
         will-change:transform;
         overflow:hidden;
       ">

    <!-- Drag handle -->
    <div style="
      width:36px;height:4px;border-radius:2px;
      background:var(--slate-200,#e2e8f0);
      margin:12px auto 0;
    "></div>

    <!-- Header -->
    <div style="
      display:flex;align-items:center;justify-content:space-between;
      padding:1.1rem 1.25rem .75rem;
      border-bottom:1px solid var(--slate-100,#f1f5f9);
    ">
      <div>
        <div style="font-weight:700;font-size:.95rem;color:var(--slate-900,#0f172a);display:flex;align-items:center;gap:6px">
          <span id="tp-title-icon"></span> Buang Sampah
        </div>
        <div style="font-size:.75rem;color:var(--slate-400,#94a3b8);margin-top:2px">
          Masukkan NIM lalu tekan tombol
        </div>
      </div>
      <button onclick="closeThrowPopup()"
              style="background:none;border:none;cursor:pointer;padding:6px;color:var(--slate-400,#94a3b8);border-radius:8px;line-height:1"
              aria-label="Tutup">
        <span id="tp-close-icon"></span>
      </button>
    </div>

    <!-- Body -->
    <div style="padding:1.25rem">

      <!-- NIM input -->
      <div id="tp-input-wrap">
        <label style="display:block;font-size:.78rem;font-weight:600;color:var(--slate-700,#334155);margin-bottom:6px">
          NIM Mahasiswa
        </label>
        <input
          id="tp-nim"
          type="tel"
          inputmode="numeric"
          pattern="[0-9]*"
          autocomplete="off"
          placeholder="Contoh: 13525136"
          style="
            width:100%;padding:11px 14px;
            border:1.5px solid var(--slate-200,#e2e8f0);
            border-radius:10px;font-size:1.05rem;
            font-family:inherit;background:#fff;
            color:var(--slate-900,#0f172a);
            outline:none;transition:border-color .2s;
            box-sizing:border-box;
          "
          oninput="this.value=this.value.replace(/\D/g,'')"
          onkeyup="_tpKeyEnter(event)"
          onfocus="this.style.borderColor='var(--emerald,#10b981)'"
          onblur="this.style.borderColor='var(--slate-200,#e2e8f0)'">
        <!-- Simpan NIM terakhir yang pernah dipakai -->
        <div style="font-size:.72rem;color:var(--slate-400,#94a3b8);margin-top:5px" id="tp-nim-hint"></div>
      </div>

      <!-- Status area -->
      <div id="tp-status" style="display:none;margin-top:.85rem"></div>

      <!-- CTA button -->
      <button
        type="button"
        id="tp-btn"
        onclick="doThrowPopup()"
        style="
          width:100%;margin-top:1rem;
          background:var(--emerald,#10b981);color:#fff;
          border:none;border-radius:12px;
          padding:13px 20px;font-size:.95rem;
          font-weight:700;font-family:inherit;
          display:flex;align-items:center;justify-content:center;gap:8px;
          cursor:pointer;transition:all .2s;
          box-shadow:0 4px 14px rgba(16,185,129,.28);
        "
        onmouseover="if(!this.disabled)this.style.background='var(--emerald-dark,#059669)'"
        onmouseout="if(!this.disabled)this.style.background='var(--emerald,#10b981)'">
        <span id="tp-btn-icon"></span>
        <span id="tp-btn-text">Buang Sekarang</span>
      </button>

    </div>
  </div>
</div>`;
}

/* ── Popup lifecycle ──────────────────────────────────────────── */
function openThrowPopup() {
  const overlay = document.getElementById("throw-popup-overlay");
  const sheet   = document.getElementById("throw-popup-sheet");
  if (!overlay || !sheet) return;

  // Reset ke idle
  _tpSetIdle();

  // Prefill NIM dari localStorage kalau ada
  const saved = localStorage.getItem("et_saved_nim");
  const nimEl  = document.getElementById("tp-nim");
  const hint   = document.getElementById("tp-nim-hint");
  if (saved && nimEl) {
    nimEl.value = saved;
    if (hint) hint.textContent = "NIM terakhir digunakan";
  } else {
    if (nimEl) nimEl.value = "";
    if (hint) hint.textContent = "";
  }

  overlay.style.display = "flex";
  // Trigger reflow sebelum animasi
  sheet.offsetHeight;
  sheet.style.transform = "translateY(0)";

  // Auto-focus input setelah animasi selesai
  setTimeout(() => {
    const inp = document.getElementById("tp-nim");
    if (inp) {
      inp.focus();
      // Pada iOS, buka keyboard numerik dengan cara geser scroll ke input
      inp.scrollIntoView({ behavior: "smooth", block: "nearest" });
    }
  }, 340);
}

function closeThrowPopup() {
  const overlay = document.getElementById("throw-popup-overlay");
  const sheet   = document.getElementById("throw-popup-sheet");
  if (!sheet) return;

  sheet.style.transform = "translateY(100%)";

  // Bersihkan poller aktif
  if (_throwPoller) { clearInterval(_throwPoller); _throwPoller = null; }

  setTimeout(() => {
    if (overlay) overlay.style.display = "none";
    _tpSetIdle();
  }, 340);
}

function _overlayClose(e) {
  // Tutup hanya kalau klik di luar sheet
  if (e.target === document.getElementById("throw-popup-overlay")) {
    closeThrowPopup();
  }
}

function _tpKeyEnter(e) {
  if (e.key === "Enter") doThrowPopup();
}

/* ── State helpers ────────────────────────────────────────────── */
function _tpSetIdle() {
  const btn     = document.getElementById("tp-btn");
  const btnIcon = document.getElementById("tp-btn-icon");
  const btnText = document.getElementById("tp-btn-text");
  const status  = document.getElementById("tp-status");
  const inputWp = document.getElementById("tp-input-wrap");

  if (btn)     { btn.disabled = false; btn.style.background = "var(--emerald,#10b981)"; btn.style.boxShadow = "0 4px 14px rgba(16,185,129,.28)"; }
  if (btnIcon) btnIcon.innerHTML = lucideInline("zap", 17);
  if (btnText) btnText.textContent = "Buang Sekarang";
  if (status)  status.style.display = "none";
  if (inputWp) inputWp.style.display = "block";
}

function _tpSetLoading(msg) {
  const btn     = document.getElementById("tp-btn");
  const btnText = document.getElementById("tp-btn-text");
  const btnIcon = document.getElementById("tp-btn-icon");
  if (btn)     { btn.disabled = true; btn.style.background = "var(--slate-400,#94a3b8)"; btn.style.boxShadow = "none"; }
  if (btnText) btnText.textContent = msg || "Menghubungi server...";
  if (btnIcon) btnIcon.innerHTML = `<div style="width:16px;height:16px;border:2px solid rgba(255,255,255,.4);border-top-color:#fff;border-radius:50%;animation:_tp-spin .7s linear infinite;flex-shrink:0"></div>`;
}

function _tpSetStatus(html, type) {
  // type: 'waiting' | 'active' | 'done' | 'error'
  const colors = {
    waiting: { bg: "#fff7ed", color: "#c2410c", border: "#fed7aa" },
    active:  { bg: "var(--emerald-pale,#f0fdf9)", color: "var(--emerald-darker,#047857)", border: "var(--emerald,#10b981)" },
    done:    { bg: "var(--emerald-pale,#f0fdf9)", color: "var(--emerald-darker,#047857)", border: "#6ee7b7" },
    error:   { bg: "#fef2f2", color: "#dc2626", border: "#fecaca" },
  };
  const c   = colors[type] || colors.waiting;
  const el  = document.getElementById("tp-status");
  if (!el) return;
  el.style.display = "block";
  el.innerHTML = `
    <div style="
      background:${c.bg};color:${c.color};
      border:1.5px solid ${c.border};
      border-radius:10px;padding:.75rem 1rem;
      font-size:.82rem;font-weight:500;
      display:flex;align-items:flex-start;gap:8px;line-height:1.4
    ">${html}</div>`;
}

/* ── Main throw action ────────────────────────────────────────── */
async function doThrowPopup() {
  const nimEl = document.getElementById("tp-nim");
  if (!nimEl) return;
  const nim = nimEl.value.trim().replace(/\D/g, "");

  if (!nim) {
    nimEl.style.borderColor = "#f87171";
    setTimeout(() => nimEl.style.borderColor = "var(--slate-200,#e2e8f0)", 1500);
    nimEl.focus();
    return;
  }

  // Stop poller lama kalau ada
  if (_throwPoller) { clearInterval(_throwPoller); _throwPoller = null; }

  _tpSetLoading("Validasi NIM...");

  // ── Step 1: validasi user ──────────────────────────────────────
  let user;
  try {
    user = await _popupFetch(`/api/user/${nim}`);
    _throwLastXP = user.total_xp || 0;
    _throwNIM    = nim;
    localStorage.setItem("et_saved_nim", nim);
    const hint = document.getElementById("tp-nim-hint");
    if (hint) hint.textContent = "✓ " + user.name;
  } catch {
    _tpSetIdle();
    _tpSetStatus(
      lucideInline("alert-triangle", 14) +
      " NIM tidak ditemukan. <a href='" + ROOT + "pages/profile.html' style='color:inherit;font-weight:700'>Daftar di sini</a>",
      "error"
    );
    nimEl.focus();
    return;
  }

  // ── Step 2: ambil bin tersedia ─────────────────────────────────
  _tpSetLoading("Mencari bin...");
  let bins;
  try {
    bins = await _popupFetch("/api/bins");
  } catch {
    bins = [];
  }

  if (!bins || bins.length === 0) {
    _tpSetIdle();
    _tpSetStatus(lucideInline("alert-triangle", 14) + " Tidak ada bin aktif. Hubungi admin.", "error");
    return;
  }

  // Pilih bin tidak penuh, fallback ke bin pertama
  _throwBin = bins.find(b => b.capacity_pct < 100) || bins[0];

  // ── Step 3: cek slot bin (v1.2 exclusive slot) ─────────────────
  try {
    const slot = await _popupFetch(`/api/pending_throw/status?bin_id=${_throwBin.id}`);
    if (slot.busy) {
      _tpSetIdle();
      _tpSetStatus(
        lucideInline("alert-triangle", 14) +
        ` Bin sedang dipakai. Tunggu <strong>${slot.sisa_detik} detik</strong> lagi.`,
        "waiting"
      );
      return;
    }
  } catch { /* endpoint lama — lanjut */ }

  // ── Step 4: daftarkan pending throw ───────────────────────────
  _tpSetLoading("Mendaftarkan sesi...");
  try {
    await _popupFetch("/api/pending_throw", {
      method: "POST",
      body: JSON.stringify({
        user_qr_code: user.qr_code,
        bin_id:       _throwBin.id,
        nim:          nim,
      }),
    });
  } catch (e) {
    _tpSetIdle();
    const msg = e.message || "";
    const isBusy = msg.includes("sedang dipakai") || msg.includes("409");
    _tpSetStatus(
      lucideInline("alert-triangle", 14) +
      (isBusy ? " Bin sedang dipakai. Coba beberapa detik lagi." : " " + msg),
      "error"
    );
    return;
  }

  // ── Step 5: hardware aktif — tampilkan instruksi ───────────────
  const btnText = document.getElementById("tp-btn-text");
  const btnIcon = document.getElementById("tp-btn-icon");
  const btn     = document.getElementById("tp-btn");
  if (btn)     { btn.disabled = true; btn.style.background = "var(--emerald-dark,#059669)"; }
  if (btnIcon) btnIcon.innerHTML = lucideInline("wifi", 17);
  if (btnText) btnText.textContent = "Menunggu hardware...";

  // Sembunyikan input NIM, tampilkan status
  const inputWp = document.getElementById("tp-input-wrap");
  if (inputWp) inputWp.style.display = "none";

  _tpSetStatus(
    lucideInline("wifi", 15) +
    ` <strong>Hardware aktif!</strong> Berdiri di depan bin <strong>${_throwBin.location}</strong>, lalu buang sampah sekarang.`,
    "active"
  );

  // ── Step 6: polling XP ────────────────────────────────────────
  const nimSaat  = _throwNIM;
  const xpAwal   = _throwLastXP;
  let pollCount  = 0;
  const MAX_POLL = 24; // 120 detik

  _throwPoller = setInterval(async () => {
    pollCount++;
    try {
      const u = await _popupFetch(`/api/user/${nimSaat}`);

      if (u.total_xp > xpAwal) {
        // ── Berhasil! ──────────────────────────────────────────
        clearInterval(_throwPoller); _throwPoller = null;

        const gained = u.total_xp - xpAwal;
        _throwLastXP = u.total_xp;

        if (btn)     { btn.disabled = false; btn.style.background = "var(--emerald,#10b981)"; btn.style.boxShadow = "0 4px 14px rgba(16,185,129,.28)"; }
        if (btnIcon) btnIcon.innerHTML = lucideInline("check-circle", 17);
        if (btnText) btnText.textContent = "Selesai!";

        _tpSetStatus(
          lucideInline("check-circle", 15) +
          ` <strong>+${gained} XP diterima!</strong> Total: ${u.total_xp.toLocaleString()} XP &nbsp;·&nbsp; ${u.level}`,
          "done"
        );

        // Notif toast global kalau ada showToast di halaman aktif
        if (typeof showToast === "function") {
          showToast(`+${gained} XP! Level: ${u.level}`, "success");
        }

        // Auto-close setelah 3 detik
        setTimeout(() => {
          closeThrowPopup();
          // Refresh data halaman aktif kalau ada fungsi update
          if (typeof updateStatsInPlace === "function") updateStatsInPlace();
          if (typeof updateBinsInPlace  === "function") updateBinsInPlace();
          if (typeof loadBoard          === "function") loadBoard();
          if (typeof loadProfile        === "function") {
            const savedNim = localStorage.getItem("et_saved_nim");
            const nimInp   = document.getElementById("nim-input");
            if (nimInp && savedNim) {
              nimInp.value = savedNim;
              loadProfile();
            }
          }
        }, 3000);

      } else if (pollCount >= MAX_POLL) {
        // ── Timeout ───────────────────────────────────────────
        clearInterval(_throwPoller); _throwPoller = null;
        try { await _popupFetch(`/api/pending_throw?bin_id=${_throwBin.id}`, { method: "DELETE" }); } catch {}

        if (btn)     { btn.disabled = false; btn.style.background = "var(--emerald,#10b981)"; btn.style.boxShadow = "0 4px 14px rgba(16,185,129,.28)"; }
        if (btnText) btnText.textContent = "Coba Lagi";
        if (btnIcon) btnIcon.innerHTML   = lucideInline("refresh", 17);
        if (inputWp) inputWp.style.display = "block";

        _tpSetStatus(
          lucideInline("alert-triangle", 14) +
          " Timeout 120 detik. Pastikan hardware aktif & sampah sudah masuk ke bin.",
          "waiting"
        );

      } else {
        // ── Masih polling ─────────────────────────────────────
        _tpSetStatus(
          `<span style="display:inline-flex;align-items:center;gap:7px">
            <span style="width:7px;height:7px;border-radius:50%;background:currentColor;display:inline-block;animation:_tp-pulse .9s ease-in-out infinite"></span>
            ${lucideInline("wifi", 14)}
            Sensor mendeteksi... (${pollCount * 5}s / 120s) — Bin: <strong>${_throwBin.location}</strong>
          </span>`,
          "active"
        );
      }
    } catch { /* tetap polling saat network error sementara */ }
  }, 5000);
}

/* ── logoutAdmin ──────────────────────────────────────────────── */
function logoutAdmin() {
  localStorage.removeItem("et_admin");
  window.location.href = ROOT + "index.html";
}

/* ── setActiveNav ─────────────────────────────────────────────── */
function setActiveNav() {
  const path = window.location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll(".et-nav-link").forEach(a => {
    const href = a.getAttribute("href")?.split("/").pop()?.split("?")[0] || "";
    a.classList.toggle("active", href === path);
  });
}

/* ── DOMContentLoaded — inject semua elemen ──────────────────── */
document.addEventListener("DOMContentLoaded", () => {

  // Inject navbar
  const navEl = document.createElement("div");
  navEl.innerHTML = buildNavbar();
  document.body.prepend(navEl.firstElementChild);

  // Inject footer
  const footEl = document.createElement("div");
  footEl.innerHTML = buildFooter();
  document.body.appendChild(footEl.firstElementChild);

  // Inject mobile bottom nav
  const mobEl = document.createElement("div");
  mobEl.innerHTML = buildMobileNav();
  document.body.appendChild(mobEl.firstElementChild);

  // ── Inject throw popup (sekali, untuk semua halaman) ──────────
  const popupEl = document.createElement("div");
  popupEl.innerHTML = buildThrowPopup();
  document.body.appendChild(popupEl.firstElementChild);

  // Inject keyframe animations untuk popup
  if (!document.getElementById("_tp-styles")) {
    const style = document.createElement("style");
    style.id = "_tp-styles";
    style.textContent = `
      @keyframes _tp-spin {
        to { transform: rotate(360deg); }
      }
      @keyframes _tp-pulse {
        0%, 100% { opacity: 1; transform: scale(1); }
        50%       { opacity: .35; transform: scale(.65); }
      }
    `;
    document.head.appendChild(style);
  }

  // Init icon di popup
  const tIcon = document.getElementById("tp-title-icon");
  const tClose = document.getElementById("tp-close-icon");
  const tBtnIcon = document.getElementById("tp-btn-icon");
  if (tIcon)    tIcon.innerHTML    = lucideInline("trash2", 18);
  if (tClose)   tClose.innerHTML   = lucideInline("x", 20);
  if (tBtnIcon) tBtnIcon.innerHTML = lucideInline("zap", 17);

  setActiveNav();

  // Mobile toggle
  const toggle = document.getElementById("et-nav-toggle");
  const menu   = document.getElementById("et-nav-menu");
  if (toggle && menu) {
    toggle.addEventListener("click", () => {
      const open = menu.style.display === "none" || menu.style.display === "";
      menu.style.display = open ? "block" : "none";
      toggle.innerHTML = open ? lucideInline("x", 22) : lucideInline("menu", 22);
    });
  }

  // Auto-open popup kalau URL mengandung ?throw=1
  // (dari mobile nav tombol profile lama yang masih pointing ke ?throw=1)
  const params = new URLSearchParams(window.location.search);
  if (params.get("throw") === "1") {
    setTimeout(() => openThrowPopup(), 400);
  }
});