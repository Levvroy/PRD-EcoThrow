/* navbar.js - inject navbar, footer & mobile bottom nav */

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
  <a href="${ROOT}pages/profile.html?throw=1" class="mob-nav-item mob-nav-throw" title="Buang Sampah">
    ${lucideInline("trash2", 22)}<span>Throw</span>
  </a>
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

function logoutAdmin() {
  localStorage.removeItem("et_admin");
  window.location.href = ROOT + "index.html";
}

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

  setActiveNav();

  // Mobile toggle
  const toggle = document.getElementById("et-nav-toggle");
  const menu = document.getElementById("et-nav-menu");
  if (toggle && menu) {
    toggle.addEventListener("click", () => {
      const open = menu.style.display === "none" || menu.style.display === "";
      menu.style.display = open ? "block" : "none";
      toggle.innerHTML = open ? lucideInline("x", 22) : lucideInline("menu", 22);
    });
  }
});

function setActiveNav() {
  const path = window.location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll(".et-nav-link").forEach(a => {
    const href = a.getAttribute("href")?.split("/").pop()?.split("?")[0] || "";
    a.classList.toggle("active", href === path);
  });
}