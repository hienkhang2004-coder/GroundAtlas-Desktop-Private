// Self-contained Adobe Spectrum Monochrome Landing + VietQR Checkout (/) & Dark Monochrome Admin (/admin)

const MONO_LOGO_SVG = `<svg width="20" height="20" viewBox="0 0 48 48" fill="none" aria-hidden="true"><rect width="48" height="48" rx="12" fill="#131313"/><path transform="translate(6.4 6.4) scale(1.1)" d="m4 11 12-7 12 7-12 7-12-7Zm0 6 12 7 12-7M4 23l12 7 12-7" stroke="#ffffff" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
const MONO_LOGO_DARK_SVG = `<svg width="20" height="20" viewBox="0 0 48 48" fill="none" aria-hidden="true"><rect width="48" height="48" rx="12" fill="#f5f5f5"/><path transform="translate(6.4 6.4) scale(1.1)" d="m4 11 12-7 12 7-12 7-12-7Zm0 6 12 7 12-7M4 23l12 7 12-7" stroke="#131313" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>`;

export function renderLandingHtml(): string {
  return `<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<meta name="robots" content="noindex,nofollow,noai,noimageai" />
<meta name="theme-color" content="#f7f7f7" />
<title>GroundAtlas Desktop 2.3 — Bản quyền trọn đời theo Seat</title>
<style>
  :root {
    --bg: #f7f7f7;
    --surface: #ffffff;
    --subtle: #f2f2f2;
    --ink: #131313;
    --ink-soft: #424242;
    --muted: #6e6e6e;
    --line: #e6e6e6;
    --line-strong: #d4d4d4;
    --focus: #3b63fb;
    --pill-tint: #eef2ff;
    --pill-border: #dce4ff;
    --ok: #007a4d;
    --ok-bg: #e7f6ee;
    --shadow-sm: 0 2px 8px rgba(0, 0, 0, 0.035);
    --shadow-md: 0 12px 32px rgba(0, 0, 0, 0.055), 0 1px 2px rgba(0, 0, 0, 0.03);
    --shadow-hover: 0 14px 30px rgba(0, 0, 0, 0.085);
  }
  *, *::before, *::after { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: "Inter", "Segoe UI Variable Text", "Segoe UI", -apple-system, BlinkMacSystemFont, system-ui, sans-serif;
    background: var(--bg);
    color: var(--ink);
    line-height: 1.55;
    -webkit-font-smoothing: antialiased;
  }
  a { color: inherit; }
  button, input { font-family: inherit; }
  :focus-visible { outline: 2px solid var(--focus); outline-offset: 2px; }

  .wrap { max-width: 1000px; margin: 0 auto; padding: 24px 20px 72px; }

  /* Floating Spectrum Pill Header */
  .nav {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    background: rgba(255, 255, 255, 0.92);
    backdrop-filter: blur(14px);
    -webkit-backdrop-filter: blur(14px);
    border: 1px solid var(--line);
    border-radius: 999px;
    padding: 9px 10px 9px 18px;
    margin-bottom: 28px;
    box-shadow: var(--shadow-sm);
  }
  .brand {
    font-size: 16px;
    font-weight: 700;
    letter-spacing: -0.4px;
    display: inline-flex;
    align-items: center;
    gap: 10px;
    text-decoration: none;
  }
  .brand svg { width: 26px; height: 26px; flex: none; }
  .tag {
    background: var(--subtle);
    color: var(--ink-soft);
    border: 1px solid var(--line);
    font-size: 11px;
    padding: 3px 9px;
    border-radius: 999px;
    font-weight: 600;
    letter-spacing: 0.01em;
  }
  .nav-actions { display: flex; align-items: center; gap: 8px; }
  .nav-link {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 8px 15px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: var(--surface);
    color: var(--ink);
    font-weight: 600;
    text-decoration: none;
    font-size: 13px;
    transition: background .15s ease, border-color .15s ease;
  }
  .nav-link:hover { background: var(--subtle); border-color: var(--line-strong); }
  .nav-link-dark {
    background: var(--ink);
    border-color: var(--ink);
    color: #fff;
  }
  .nav-link-dark:hover { background: #2c2c2c; border-color: #2c2c2c; }

  /* Hero Card — Spectrum Rounded Surface */
  .hero {
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 24px;
    padding: 36px 36px 32px;
    margin-bottom: 22px;
    box-shadow: var(--shadow-md);
  }

  /* Signature Spectrum Announcement Pill (single restrained accent) */
  .batch-pill {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    flex-wrap: wrap;
    background: var(--pill-tint);
    border: 1px solid var(--pill-border);
    border-radius: 999px;
    padding: 6px 7px 6px 16px;
    margin-bottom: 22px;
  }
  .batch-info {
    display: inline-flex;
    align-items: center;
    gap: 9px;
    font-size: 13px;
    font-weight: 500;
    color: #1e2952;
  }
  .batch-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--focus);
    flex: none;
  }
  .batch-badge {
    display: inline-flex;
    align-items: center;
    background: var(--focus);
    color: #ffffff;
    font-size: 12px;
    font-weight: 600;
    padding: 5px 13px;
    border-radius: 999px;
    white-space: nowrap;
  }

  h1 {
    margin: 0 0 12px;
    font-size: clamp(28px, 4vw, 40px);
    font-weight: 700;
    line-height: 1.12;
    letter-spacing: -0.04em;
    color: var(--ink);
  }
  .sub {
    color: var(--muted);
    margin: 0 0 22px;
    font-size: 15px;
    line-height: 1.65;
    max-width: 740px;
  }
  .sub code {
    background: var(--subtle);
    border: 1px solid var(--line);
    padding: 1px 6px;
    border-radius: 6px;
    font-size: 13px;
    color: var(--ink);
    font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
  }
  .sub strong { color: var(--ink); font-weight: 600; }

  .progress-wrap { margin: 0 0 24px; }
  .bar { height: 6px; background: #ececec; border-radius: 999px; overflow: hidden; }
  .bar-fill { height: 100%; background: var(--ink); width: 0%; border-radius: 999px; transition: width .4s ease; }

  /* Seat Package Cards — Crisp Monochrome Rounded Borders */
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(205px, 1fr));
    gap: 14px;
    margin: 0;
  }
  .pkg {
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 18px;
    padding: 20px 18px;
    cursor: pointer;
    transition: border-color .15s ease, transform .15s ease, box-shadow .15s ease, background .15s ease;
    position: relative;
  }
  .pkg:hover {
    border-color: #b8b8b8;
    transform: translateY(-2px);
    box-shadow: var(--shadow-sm);
  }
  .pkg.active {
    border: 2px solid var(--ink);
    padding: 19px 17px;
    background: #fafafa;
    box-shadow: var(--shadow-hover);
  }
  .pkg-head { display: flex; align-items: center; justify-content: space-between; gap: 6px; }
  .pkg-title { font-weight: 600; font-size: 13.5px; color: var(--ink); white-space: nowrap; }
  .pkg-check {
    display: none;
    font-size: 10px;
    font-weight: 600;
    background: var(--ink);
    color: #fff;
    padding: 2px 7px;
    border-radius: 999px;
    white-space: nowrap;
    flex: none;
  }
  .pkg.active .pkg-check { display: inline-flex; }
  .pkg-price {
    font-size: 27px;
    font-weight: 700;
    letter-spacing: -0.03em;
    color: var(--ink);
    margin: 10px 0 2px;
    font-variant-numeric: tabular-nums;
  }
  .pkg-old { font-size: 12px; color: #8c8c8c; text-decoration: line-through; font-variant-numeric: tabular-nums; }
  .pkg-note { font-size: 12px; color: var(--muted); margin-top: 10px; padding-top: 10px; border-top: 1px solid #efefef; }

  /* Checkout Surface */
  .checkout {
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 24px;
    padding: 32px;
    display: grid;
    grid-template-columns: 1.05fr 0.95fr;
    gap: 28px;
    box-shadow: var(--shadow-md);
  }
  .checkout h3 {
    margin: 0 0 18px;
    font-size: 20px;
    font-weight: 700;
    letter-spacing: -0.025em;
    color: var(--ink);
  }
  label {
    display: block;
    font-size: 12px;
    font-weight: 600;
    color: var(--ink-soft);
    margin-bottom: 6px;
  }
  input {
    width: 100%;
    padding: 12px 14px;
    border: 1px solid var(--line-strong);
    border-radius: 12px;
    font-size: 14px;
    margin-bottom: 16px;
    background: var(--surface);
    color: var(--ink);
    transition: border-color .15s ease, box-shadow .15s ease;
  }
  input::placeholder { color: #9a9a9a; }
  input:focus {
    outline: none;
    border-color: var(--ink);
    box-shadow: 0 0 0 3px rgba(59, 99, 251, 0.16);
  }
  button.primary {
    width: 100%;
    background: var(--ink);
    color: #ffffff;
    border: 1px solid var(--ink);
    border-radius: 999px;
    padding: 13px 20px;
    font-size: 14px;
    font-weight: 600;
    cursor: pointer;
    transition: background .15s ease, transform .12s ease;
  }
  button.primary:hover { background: #2c2c2c; border-color: #2c2c2c; }
  button.primary:active { transform: scale(0.99); }

  .notes {
    margin: 18px 0 0;
    padding: 16px 18px;
    background: var(--bg);
    border: 1px solid var(--line);
    border-radius: 16px;
    font-size: 12.5px;
    line-height: 1.65;
    color: var(--muted);
  }
  .notes strong, .notes b { color: var(--ink); font-weight: 600; }

  .qr-panel {
    text-align: center;
    background: var(--bg);
    border: 1px solid var(--line);
    border-radius: 18px;
    padding: 24px 20px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 300px;
  }
  .qr-panel img {
    max-width: 224px;
    border-radius: 14px;
    border: 1px solid var(--line);
    background: #fff;
    padding: 8px;
    box-shadow: var(--shadow-sm);
  }
  .qr-empty-icon {
    width: 44px;
    height: 44px;
    border-radius: 14px;
    background: var(--surface);
    border: 1px solid var(--line);
    display: grid;
    place-items: center;
    margin-bottom: 12px;
    color: var(--ink);
    font-size: 18px;
  }
  .key-box {
    background: var(--ink);
    color: #ffffff;
    font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
    font-size: 19px;
    padding: 14px 20px;
    border-radius: 14px;
    letter-spacing: 1.4px;
    margin: 12px 0;
    user-select: all;
  }
  .copy-btn {
    border: 1px solid var(--line-strong);
    background: var(--surface);
    color: var(--ink);
    border-radius: 999px;
    padding: 7px 14px;
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
    margin-bottom: 8px;
  }
  .copy-btn:hover { background: var(--subtle); }
  .muted { color: var(--muted); font-size: 13px; }
  .pill-ok {
    background: var(--ok-bg);
    color: var(--ok);
    border: 1px solid #bce8d1;
    padding: 5px 12px;
    border-radius: 999px;
    font-size: 12px;
    font-weight: 600;
  }
  .site-foot {
    margin-top: 26px;
    padding: 0 8px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 12px;
    font-size: 12px;
    color: var(--muted);
  }
  .site-foot a { text-decoration: none; border-bottom: 1px solid var(--line-strong); padding-bottom: 1px; }
  .site-foot a:hover { color: var(--ink); border-color: var(--ink); }

  @media (max-width: 760px) {
    .wrap { padding: 14px 14px 48px; }
    .nav { border-radius: 18px; padding: 10px 12px; flex-wrap: wrap; }
    .hero, .checkout { padding: 22px 18px; border-radius: 20px; }
    .checkout { grid-template-columns: 1fr; }
    .batch-pill { border-radius: 16px; padding: 10px 12px; }
  }
</style>
</head>
<body>
<div class="wrap">
  <header class="nav">
    <a class="brand" href="https://groundatlas.groundatlas.workers.dev/products.html">${MONO_LOGO_SVG}<span>GroundAtlas Desktop</span><span class="tag">v2.3 Native</span></a>
    <div class="nav-actions">
      <a class="nav-link" href="https://groundatlas.groundatlas.workers.dev/">Bản Web</a>
      <a class="nav-link nav-link-dark" id="dlLink" href="#">Tải bản Free ↗</a>
    </div>
  </header>

  <section class="hero">
    <div class="batch-pill">
      <span class="batch-info"><span class="batch-dot" aria-hidden="true"></span><span id="batchName">Đang tải thông tin đợt mở bán…</span></span>
      <span class="batch-badge" id="batchSlot">—</span>
    </div>
    <h1>Mua đứt 1 lần · Dùng trọn đời bản 2.x</h1>
    <p class="sub">Thuần mã máy C++20 / OpenGL 3.3 / 180Hz cho <strong>Windows &amp; macOS (Apple Silicon)</strong>. Nạp hiện trạng OSM trong 2 giây, đè lớp quy hoạch, import trực tiếp SketchUp <code>.skp</code>, kéo mặt cắt A–A' sống và xuất trọn Bộ hồ sơ 8 tệp (4K/8K PNG, CAD DXF 3D, SketchUp DAE, PDF A3, GIF). <strong>1 Seat = 1 Máy</strong>, hỗ trợ tự chuyển máy &amp; chạy offline 14 ngày.</p>
    <div class="progress-wrap"><div class="bar"><div class="bar-fill" id="batchBar"></div></div></div>
    <div class="grid" id="pkgGrid"></div>
  </section>

  <section class="checkout">
    <div>
      <h3>Nhận mã bản quyền tự động trong 5 giây</h3>
      <label for="email">Email nhận mã bản quyền</label>
      <input id="email" type="email" placeholder="kts@domain.com" required />
      <label for="name">Họ tên / Văn phòng (tùy chọn)</label>
      <input id="name" type="text" placeholder="Nguyễn Văn A · Studio" />
      <button class="primary" id="createOrderBtn">Tạo mã VietQR thanh toán</button>
      <div class="notes">
        • Thanh toán chuyển khoản ngân hàng qua VietQR (không tự động gia hạn).<br>
        • Hệ thống đối soát tự động sau 3–5 giây, hiển thị Key ngay bên phải và gửi bản sao qua email.<br>
        • Bảo lưu bản quyền vĩnh viễn dòng 2.x, hỗ trợ tự chuyển máy (2 lần / 6 tháng).<br>
        • <strong>Windows SmartScreen:</strong> nếu hiện cảnh báo lần mở đầu, chọn <b>More info → Run anyway</b> (hoặc chuột phải → <b>Properties → Unblock</b>).
      </div>
    </div>
    <div class="qr-panel" id="qrPanel">
      <div class="qr-empty-icon" aria-hidden="true">⊞</div>
      <p class="muted" style="margin:0;max-width:260px;">Chọn gói Seat phía trên và nhập email để tạo mã VietQR thanh toán tự động.</p>
    </div>
  </section>

  <footer class="site-foot">
    <span>© 2026 GroundAtlas · Pure Native Architecture Suite</span>
    <span><a href="https://groundatlas.groundatlas.workers.dev/releases.html">Nhật ký bản dựng</a> · <a href="https://groundatlas.groundatlas.workers.dev/terms.html">Điều khoản</a></span>
  </footer>
</div>

<script>
const fmt = n => new Intl.NumberFormat('vi-VN').format(n) + ' đ';
let selectedSeats = 1, pricingData = null, pollTimer = null;
const urlParams = new URLSearchParams(location.search);
const machineHash = urlParams.get('m') || '';

async function loadPricing() {
  const res = await fetch('/v1/pricing' + (machineHash ? ('?machine_hash=' + encodeURIComponent(machineHash)) : ''));
  pricingData = await res.json();
  if (pricingData.download_url) document.getElementById('dlLink').href = pricingData.download_url;
  document.getElementById('batchName').textContent = pricingData.batch_name + (pricingData.welcome_offer_active ? ' · Ưu đãi 48h đang bật' : '');
  if (pricingData.seat_quota > 0) {
    document.getElementById('batchSlot').textContent = 'Còn ' + pricingData.remaining_in_batch + '/' + pricingData.seat_quota + ' suất';
    const pct = Math.min(100, Math.max(4, Math.round((pricingData.sold_in_batch / pricingData.seat_quota) * 100)));
    document.getElementById('batchBar').style.width = pct + '%';
  } else {
    document.getElementById('batchSlot').textContent = 'Đã cấp ' + pricingData.total_seats_sold + ' seats';
    document.getElementById('batchBar').style.width = '100%';
  }
  const grid = document.getElementById('pkgGrid');
  grid.innerHTML = '';
  for (const p of pricingData.packages) {
    const el = document.createElement('div');
    el.className = 'pkg' + (p.seats === selectedSeats ? ' active' : '');
    const perSeat = Math.round(p.price_vnd / p.seats);
    el.innerHTML = '<div class="pkg-head"><span class="pkg-title">' + p.label + '</span><span class="pkg-check">✓ Chọn</span></div>'
      + '<div class="pkg-price">' + fmt(p.price_vnd) + '</div>'
      + (p.list_price_vnd > p.price_vnd ? '<div class="pkg-old">' + fmt(p.list_price_vnd) + '</div>' : '<div class="pkg-old" style="visibility:hidden">0</div>')
      + '<div class="pkg-note">~' + fmt(perSeat) + '/máy · Trọn đời 2.x</div>';
    el.onclick = () => { selectedSeats = p.seats; loadPricing(); };
    grid.appendChild(el);
  }
}

document.getElementById('createOrderBtn').onclick = async () => {
  const email = document.getElementById('email').value.trim();
  const name = document.getElementById('name').value.trim();
  if (!email) { alert('Vui lòng nhập email!'); return; }
  const panel = document.getElementById('qrPanel');
  panel.innerHTML = '<p class="muted">Đang tạo mã VietQR…</p>';
  const res = await fetch('/v1/orders', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ email, name, seats: selectedSeats, machine_hash: machineHash })
  });
  const data = await res.json();
  if (!res.ok) { panel.innerHTML = '<p style="color:#c62828;font-weight:600;">' + (data.message || data.error) + '</p>'; return; }
  const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  panel.innerHTML = '<img src="' + esc(data.qr_url) + '" alt="VietQR" />'
    + '<p style="margin:12px 0 4px;font-weight:700;font-size:15px;">' + esc(fmt(data.amount_vnd)) + '</p>'
    + '<p class="muted" style="margin:0 0 8px;">Nội dung: <code style="background:#fff;border:1px solid #e6e6e6;padding:2px 7px;border-radius:6px;color:#131313;">' + esc(data.transfer_content) + '</code></p>'
    + '<p class="muted" id="pollMsg" style="margin:0;">Đang chờ chuyển khoản (tự kiểm tra mỗi 3 giây)…</p>';
  if (pollTimer) clearInterval(pollTimer);
  const pollTok = data.poll_token || '';
  pollTimer = setInterval(async () => {
    const r = await fetch('/v1/orders/' + encodeURIComponent(data.order_code) + '?t=' + encodeURIComponent(pollTok));
    if (!r.ok) return;
    const st = await r.json();
    if (st.status === 'paid' && st.license_key) {
      clearInterval(pollTimer);
      const safeKey = esc(st.license_key);
      panel.innerHTML = '<span class="pill-ok">✓ Thanh toán thành công</span>'
        + '<p style="margin:12px 0 4px;font-weight:600;">Mã bản quyền của bạn (' + Number(st.seats) + ' Seat):</p>'
        + '<div class="key-box" id="issuedKeyBox">' + safeKey + '</div>'
        + '<button type="button" class="copy-btn" id="copyKeyBtn">Sao chép mã</button>'
        + '<p class="muted" style="margin:4px 0 0;">Dán mã này vào mục <b>Bản quyền</b> trong GroundAtlas Desktop.</p>';
      const btn = document.getElementById('copyKeyBtn');
      if (btn) btn.onclick = () => { navigator.clipboard?.writeText(st.license_key); btn.textContent = 'Đã sao chép ✓'; };
      loadPricing();
    }
  }, 3000);
};
loadPricing();
</script>
</body>
</html>`;
}

export function renderAdminHtml(): string {
  return `<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<meta name="robots" content="noindex,nofollow,noai,noimageai" />
<meta name="theme-color" content="#141414" />
<title>GroundAtlas Admin</title>
<style>
  :root {
    --bg: #141414;
    --surface: #1f1f1f;
    --subtle: #282828;
    --ink: #f5f5f5;
    --muted: #969696;
    --line: #2e2e2e;
    --line-strong: #3d3d3d;
    --focus: #3b63fb;
    --ok: #34d399;
  }
  *, *::before, *::after { box-sizing: border-box; }
  body {
    font-family: "Inter", "Segoe UI Variable Text", "Segoe UI", system-ui, sans-serif;
    background: var(--bg);
    color: var(--ink);
    margin: 0;
    padding: 24px 20px 64px;
    -webkit-font-smoothing: antialiased;
  }
  .wrap { max-width: 1120px; margin: 0 auto; }
  .top {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 14px;
    flex-wrap: wrap;
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 999px;
    padding: 10px 14px 10px 20px;
    margin-bottom: 20px;
  }
  .brand { display: inline-flex; align-items: center; gap: 10px; font-size: 16px; font-weight: 700; letter-spacing: -0.3px; }
  .brand svg { width: 24px; height: 24px; }
  .card {
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 20px;
    padding: 22px 24px;
    margin-bottom: 16px;
  }
  .card h3 { margin: 0 0 14px; font-size: 16px; font-weight: 600; letter-spacing: -0.02em; }
  .row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
  input, select {
    background: var(--bg);
    color: var(--ink);
    border: 1px solid var(--line-strong);
    border-radius: 10px;
    padding: 9px 12px;
    font-size: 13px;
  }
  input:focus, select:focus { outline: 2px solid var(--focus); outline-offset: 1px; }
  button {
    background: #f5f5f5;
    color: #131313;
    border: 1px solid #f5f5f5;
    border-radius: 999px;
    padding: 8px 16px;
    font-size: 13px;
    font-weight: 600;
    cursor: pointer;
    transition: background .15s ease;
  }
  button:hover { background: #e0e0e0; }
  button.sec {
    background: var(--subtle);
    color: var(--ink);
    border-color: var(--line-strong);
  }
  button.sec:hover { background: #333333; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; margin-top: 8px; }
  th, td { text-align: left; padding: 11px 10px; border-bottom: 1px solid var(--line); }
  th { color: var(--muted); font-size: 11px; text-transform: uppercase; letter-spacing: 0.05em; font-weight: 600; }
  .kpi { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 14px; margin-bottom: 16px; }
  .kpi .card { margin: 0; padding: 18px 20px; }
  .kpi-label { font-size: 12px; color: var(--muted); margin-bottom: 6px; }
  .val { font-size: 24px; font-weight: 700; color: var(--ink); letter-spacing: -0.03em; font-variant-numeric: tabular-nums; }
  code { background: var(--bg); border: 1px solid var(--line); padding: 3px 8px; border-radius: 7px; font-family: ui-monospace, Consolas, monospace; font-size: 12px; }
  .status-pill { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; color: var(--ok); background: rgba(52, 211, 153, 0.1); border: 1px solid rgba(52, 211, 153, 0.25); padding: 4px 10px; border-radius: 999px; }
  @media (max-width: 800px) { .kpi { grid-template-columns: repeat(2, 1fr); } .top { border-radius: 18px; } }
</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div class="brand">${MONO_LOGO_DARK_SVG}<span>GroundAtlas · Quản trị Bản quyền Seat</span></div>
    <div class="row">
      <span id="status" class="status-pill">Đang tải…</span>
      <button class="sec" onclick="loadOverview()">Làm mới</button>
      <button class="sec" onclick="logout()">Đăng xuất</button>
    </div>
  </div>

  <div class="kpi" id="kpiRow">
    <div class="card"><div class="kpi-label">Tổng doanh thu</div><div class="val" id="kRev">0 đ</div></div>
    <div class="card"><div class="kpi-label">Đơn đã thanh toán</div><div class="val" id="kOrd">0</div></div>
    <div class="card"><div class="kpi-label">Tổng Seat đã bán</div><div class="val" id="kSeats">0</div></div>
    <div class="card"><div class="kpi-label">Đợt mở bán hiện tại</div><div class="val" id="kBatch" style="font-size:15px;">—</div></div>
  </div>

  <div class="card" id="createCard">
    <h3>Tạo nhanh License Key mới (Tặng / Bán tay / B2B)</h3>
    <div class="row">
      <input id="cEmail" placeholder="Email khách" />
      <input id="cName" placeholder="Tên / Studio" />
      <select id="cSeats"><option value="1">1 Seat</option><option value="3">3 Seats</option><option value="5">5 Seats</option><option value="10">10 Seats</option></select>
      <input id="cAmt" type="number" placeholder="Số tiền VND" value="99000" />
      <label style="font-size:13px;color:var(--muted);display:inline-flex;align-items:center;gap:6px;"><input id="cPaid" type="checkbox" checked /> Tính vào slot thật</label>
      <button onclick="createKey()">+ Sinh Key ngay</button>
    </div>
    <div id="newKeyOut" style="margin-top:12px;"></div>
  </div>

  <div class="card" id="licCard">
    <h3>Danh sách License Keys</h3>
    <table><thead><tr><th>Key</th><th>Email</th><th>Seats</th><th>Swap</th><th>Trạng thái</th><th>Thao tác</th></tr></thead><tbody id="licBody"></tbody></table>
  </div>

  <div class="card" id="gpuCard">
    <div style="display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:8px;">
      <h3 style="margin:0;">Sức khỏe GPU &amp; Tương thích phần cứng (Telemetry ẩn danh)</h3>
      <span id="gpuSummary" class="status-pill">0 thiết bị</span>
    </div>
    <table><thead><tr><th>GPU · OpenGL</th><th>Bản</th><th>Chế độ</th><th>Màn hình / RAM</th><th>gl_error</th><th>Watchdog</th><th>Cập nhật</th></tr></thead><tbody id="gpuBody"></tbody></table>
  </div>
</div>

<script>
const fmt = n => new Intl.NumberFormat('vi-VN').format(n) + ' đ';
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const api = (url, opts = {}) => fetch(url, { credentials: 'same-origin', headers: { 'Content-Type': 'application/json' }, ...opts });

async function loadOverview() {
  const res = await api('/v1/admin/overview');
  if (!res.ok) { document.getElementById('status').textContent = 'Phiên hết hạn'; return; }
  const d = await res.json();
  document.getElementById('status').textContent = 'Phiên bảo mật (HttpOnly)';
  document.getElementById('kRev').textContent = fmt(d.revenue_vnd);
  document.getElementById('kOrd').textContent = d.paid_orders;
  document.getElementById('kSeats').textContent = d.pricing.total_seats_sold;
  document.getElementById('kBatch').textContent = d.pricing.batch_name + (d.pricing.seat_quota > 0 ? (' · Còn ' + d.pricing.remaining_in_batch) : '');
  const tb = document.getElementById('licBody');
  tb.innerHTML = '';
  for (const l of d.licenses) {
    const tr = document.createElement('tr');
    const nextSt = l.status === 'active' ? 'revoked' : 'active';
    tr.innerHTML = '<td><code>' + esc(l.key_masked) + '</code></td><td>' + esc(l.buyer_email || '—') + '</td>'
      + '<td>' + Number(l.active_seats) + ' / ' + Number(l.max_seats) + '</td>'
      + '<td>' + Number(l.swap_count) + '/2</td>'
      + '<td>' + esc(l.status) + '</td>'
      + '<td><button class="sec" data-act="reset">Reset máy</button> <button class="sec" data-act="toggle">' + (l.status === 'active' ? 'Khóa' : 'Mở lại') + '</button></td>';
    tr.querySelector('[data-act="reset"]').onclick = () => resetSeats(l.id);
    tr.querySelector('[data-act="toggle"]').onclick = () => toggleRevoke(l.id, nextSt);
    tb.appendChild(tr);
  }
  const gh = d.gpu_health || { total_devices: 0, healthy_devices: 0, fallback_devices: 0, error_devices: 0, devices: [] };
  document.getElementById('gpuSummary').textContent = gh.healthy_devices + ' ổn định · ' + gh.fallback_devices + ' tự hạ chế độ · ' + gh.error_devices + ' lỗi GL';
  const gb = document.getElementById('gpuBody');
  gb.innerHTML = '';
  if (!gh.devices || gh.devices.length === 0) {
    gb.innerHTML = '<tr><td colspan="7" style="color:var(--muted);">Chưa có dữ liệu telemetry GPU từ bản v2.3.3 trở lên.</td></tr>';
  } else {
    for (const g of gh.devices) {
      const tr = document.createElement('tr');
      const dt = g.updated_at ? new Date(g.updated_at * 1000).toLocaleString('vi-VN') : '—';
      const errColor = Number(g.gl_error || 0) !== 0 ? '#f87171' : (g.fallback_reason ? '#fbbf24' : '#34d399');
      tr.innerHTML = '<td><strong>' + esc(g.gpu_renderer || 'Unknown') + '</strong><div style="font-size:11px;color:var(--muted);">' + esc(g.gl_version || '') + '</div></td>'
        + '<td><code>' + esc(g.app_version || '') + '</code></td>'
        + '<td>' + esc(g.compat_mode || 'auto') + ' · ' + esc(g.effective_tier || '') + '</td>'
        + '<td>' + Number(g.dpi_percent || 100) + '% · ' + Number(g.display_hz || 60) + 'Hz · ' + Number(g.ram_gb || 0) + 'GB</td>'
        + '<td style="color:' + errColor + ';font-weight:600;">' + Number(g.gl_error || 0) + '</td>'
        + '<td>' + esc(g.fallback_reason || 'OK') + '</td>'
        + '<td style="color:var(--muted);font-size:12px;">' + esc(dt) + '</td>';
      gb.appendChild(tr);
    }
  }
}

async function createKey() {
  const res = await api('/v1/admin/licenses', {
    method: 'POST',
    body: JSON.stringify({
      buyer_email: document.getElementById('cEmail').value.trim(),
      buyer_name: document.getElementById('cName').value.trim(),
      max_seats: Number(document.getElementById('cSeats').value),
      amount_vnd: Number(document.getElementById('cAmt').value || 0),
      count_as_paid_order: document.getElementById('cPaid').checked
    })
  });
  const d = await res.json();
  if (d.ok) {
    document.getElementById('newKeyOut').innerHTML = 'Key vừa tạo (chỉ hiện 1 lần): <code style="font-size:17px;color:#34d399;">' + esc(d.license_key) + '</code> (' + Number(d.max_seats) + ' seats)';
    loadOverview();
  }
}

async function resetSeats(id) {
  await api('/v1/admin/licenses/' + encodeURIComponent(id) + '/reset-seats', { method: 'POST' });
  loadOverview();
}
async function toggleRevoke(id, status) {
  await api('/v1/admin/licenses/' + encodeURIComponent(id) + '/revoke', { method: 'POST', body: JSON.stringify({ status }) });
  loadOverview();
}
async function logout() {
  await api('/v1/admin/logout', { method: 'POST' });
  location.href = '/';
}
loadOverview();
</script>
</body>
</html>`;
}
