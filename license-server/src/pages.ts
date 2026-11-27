// Self-contained Warm Editorial Landing + VietQR Checkout page (/) & Admin Dashboard (/admin)

export function renderLandingHtml(): string {
  return `<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<meta name="robots" content="noindex,nofollow,noai,noimageai" />
<title>GroundAtlas Desktop 2.3 — Bản quyền trọn đời theo Seat</title>
<style>
  :root { --bg:#faf8f5; --card:#ffffff; --ink:#1a1918; --muted:#6b665e; --accent:#c45632; --line:#e6e0d6; --ok:#16a34a; }
  * { box-sizing:border-box; }
  body { margin:0; font-family:"Segoe UI Variable Text","Segoe UI",system-ui,sans-serif; background:var(--bg); color:var(--ink); line-height:1.55; }
  .wrap { max-width:980px; margin:0 auto; padding:28px 20px 64px; }
  .nav { display:flex; align-items:center; justify-content:space-between; margin-bottom:28px; }
  .brand { font-size:20px; font-weight:700; letter-spacing:-0.3px; display:flex; align-items:center; gap:8px; }
  .tag { background:#1a1918; color:#fff; font-size:11px; padding:2px 8px; border-radius:99px; font-weight:600; }
  .hero { background:var(--card); border:1px solid var(--line); border-radius:18px; padding:32px; margin-bottom:24px; }
  h1 { margin:0 0 10px; font-size:32px; line-height:1.2; letter-spacing:-0.6px; }
  .sub { color:var(--muted); margin:0 0 20px; font-size:15px; max-width:680px; }
  .batch-box { background:#fff7f3; border:1px solid #f1cfc0; border-radius:12px; padding:14px 16px; margin-bottom:20px; }
  .batch-top { display:flex; justify-content:space-between; align-items:center; font-weight:600; font-size:14px; margin-bottom:8px; }
  .bar { height:9px; background:#efe5dc; border-radius:99px; overflow:hidden; }
  .bar-fill { height:100%; background:var(--accent); width:0%; transition:width .4s ease; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(210px,1fr)); gap:14px; margin:20px 0; }
  .pkg { background:var(--card); border:1.5px solid var(--line); border-radius:14px; padding:18px; cursor:pointer; transition:all .15s ease; }
  .pkg:hover { border-color:var(--accent); transform:translateY(-2px); }
  .pkg.active { border-color:var(--accent); background:#fffaf7; box-shadow:0 6px 20px rgba(196,86,50,.12); }
  .pkg-title { font-weight:700; font-size:15px; }
  .pkg-price { font-size:26px; font-weight:800; color:var(--accent); margin:8px 0 2px; }
  .pkg-old { font-size:12px; color:var(--muted); text-decoration:line-through; }
  .pkg-note { font-size:12px; color:var(--muted); margin-top:8px; }
  .checkout { background:var(--card); border:1px solid var(--line); border-radius:16px; padding:24px; display:grid; grid-template-columns:1fr 1fr; gap:24px; }
  @media(max-width:740px){ .checkout { grid-template-columns:1fr; } }
  label { display:block; font-size:12px; font-weight:600; color:var(--muted); margin-bottom:5px; text-transform:uppercase; }
  input { width:100%; padding:11px 13px; border:1px solid var(--line); border-radius:10px; font-size:14px; margin-bottom:14px; background:#fff; }
  button.primary { width:100%; background:var(--accent); color:#fff; border:none; border-radius:10px; padding:13px; font-size:15px; font-weight:700; cursor:pointer; }
  button.primary:hover { filter:brightness(1.06); }
  .qr-panel { text-align:center; background:#faf8f5; border:1px dashed var(--line); border-radius:14px; padding:20px; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:260px; }
  .qr-panel img { max-width:220px; border-radius:10px; border:1px solid var(--line); background:#fff; padding:6px; }
  .key-box { background:#1a1918; color:#faf8f5; font-family:Consolas,monospace; font-size:20px; padding:14px 18px; border-radius:10px; letter-spacing:1.4px; margin:12px 0; user-select:all; }
  .muted { color:var(--muted); font-size:13px; }
  .pill-ok { background:#dcfce7; color:#166534; padding:4px 10px; border-radius:99px; font-size:12px; font-weight:700; }
</style>
</head>
<body>
<div class="wrap">
  <div class="nav">
    <div class="brand">GroundAtlas Desktop <span class="tag">v2.3 Native C++</span></div>
    <a id="dlLink" href="#" style="color:var(--accent);font-weight:600;text-decoration:none;font-size:14px;">Tải bản Free dùng thử ↗</a>
  </div>

  <div class="hero">
    <h1>Mua đứt 1 lần · Dùng trọn đời bản 2.x</h1>
    <p class="sub">100% Native C++20 / OpenGL 3.3 / 180Hz. Nạp hiện trạng OSM 2 giây, đè lớp quy hoạch, import trực tiếp SketchUp <code>.skp</code>, cắt mặt cắt A–A', xuất trọn Bộ hồ sơ 8 tệp (4K PNG, CAD DXF 3D, SketchUp DAE, PDF A3, GIF). <strong>1 Seat = 1 Máy</strong>, hỗ trợ tự chuyển máy & chạy offline 14 ngày.</p>
    <div class="batch-box">
      <div class="batch-top">
        <span id="batchName">Đang tải thông tin đợt mở bán…</span>
        <span id="batchSlot" style="color:var(--accent);">—</span>
      </div>
      <div class="bar"><div class="bar-fill" id="batchBar"></div></div>
    </div>
    <div class="grid" id="pkgGrid"></div>
  </div>

  <div class="checkout">
    <div>
      <h3 style="margin-top:0;">Nhận mã bản quyền tự động trong 5 giây</h3>
      <label>Email nhận mã bản quyền</label>
      <input id="email" type="email" placeholder="kts@domain.com" required />
      <label>Họ tên / Văn phòng (tùy chọn)</label>
      <input id="name" type="text" placeholder="Nguyễn Văn A · Studio" />
      <button class="primary" id="createOrderBtn">Tạo mã VietQR thanh toán</button>
      <p class="muted" style="margin-top:12px;">• Thanh toán chuyển khoản ngân hàng qua VietQR.<br>• Hệ thống đối soát tự động sau 3–5 giây, hiện Key ngay bên phải và gửi bản sao qua email.<br>• Bảo lưu bản quyền vĩnh viễn dòng 2.x, hỗ trợ chuyển máy (2 lần / 6 tháng).<br>• <strong>Mẹo cài Windows:</strong> nếu hiện bảng xanh SmartScreen ở lần mở đầu, bấm <b>More info → Run anyway</b> (hoặc chuột phải file → <b>Properties → Unblock</b>).</p>
    </div>
    <div class="qr-panel" id="qrPanel">
      <p class="muted">Chọn gói Seat ở trên và nhập email để hiển thị mã VietQR.</p>
    </div>
  </div>
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
  document.getElementById('batchName').textContent = pricingData.batch_name + (pricingData.welcome_offer_active ? ' · Ưu đãi 48h đang bật!' : '');
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
    el.innerHTML = '<div class="pkg-title">' + p.label + '</div>'
      + '<div class="pkg-price">' + fmt(p.price_vnd) + '</div>'
      + (p.list_price_vnd > p.price_vnd ? '<div class="pkg-old">' + fmt(p.list_price_vnd) + '</div>' : '')
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
  if (!res.ok) { panel.innerHTML = '<p style="color:#dc2626;">' + (data.message || data.error) + '</p>'; return; }
  const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  panel.innerHTML = '<img src="' + esc(data.qr_url) + '" alt="VietQR" />'
    + '<p style="margin:10px 0 4px;font-weight:700;">' + esc(fmt(data.amount_vnd)) + ' · Nội dung: <code>' + esc(data.transfer_content) + '</code></p>'
    + '<p class="muted" id="pollMsg">Đang chờ chuyển khoản (tự kiểm tra mỗi 3 giây)…</p>';
  if (pollTimer) clearInterval(pollTimer);
  const pollTok = data.poll_token || '';
  pollTimer = setInterval(async () => {
    const r = await fetch('/v1/orders/' + encodeURIComponent(data.order_code) + '?t=' + encodeURIComponent(pollTok));
    if (!r.ok) return;
    const st = await r.json();
    if (st.status === 'paid' && st.license_key) {
      clearInterval(pollTimer);
      panel.innerHTML = '<span class="pill-ok">✓ Thanh toán thành công!</span>'
        + '<p style="margin:10px 0 4px;">Mã bản quyền của bạn (' + Number(st.seats) + ' Seat):</p>'
        + '<div class="key-box">' + esc(st.license_key) + '</div>'
        + '<p class="muted">Dán mã này vào mục <b>Bản quyền</b> trong GroundAtlas Desktop.</p>';
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
<title>GroundAtlas Admin</title>
<style>
  body { font-family:"Segoe UI",system-ui,sans-serif; background:#141312; color:#f4f2ee; margin:0; padding:24px; }
  .wrap { max-width:1100px; margin:0 auto; }
  .top { display:flex; justify-content:space-between; align-items:center; margin-bottom:14px; }
  .card { background:#1e1c1a; border:1px solid #33302c; border-radius:12px; padding:18px; margin-bottom:16px; }
  .row { display:flex; gap:10px; flex-wrap:wrap; align-items:center; }
  input, select { background:#121110; color:#f4f2ee; border:1px solid #3c3833; border-radius:8px; padding:8px 10px; }
  button { background:#c45632; color:#fff; border:none; border-radius:8px; padding:8px 14px; font-weight:600; cursor:pointer; }
  button.sec { background:#2d2a26; }
  table { width:100%; border-collapse:collapse; font-size:13px; margin-top:10px; }
  th, td { text-align:left; padding:8px; border-bottom:1px solid #2c2926; }
  .kpi { display:grid; grid-template-columns:repeat(4,1fr); gap:12px; margin-bottom:16px; }
  .kpi .card { margin:0; }
  .val { font-size:24px; font-weight:800; color:#e09173; }
  code { background:#111; padding:2px 6px; border-radius:4px; }
</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <h2 style="margin:0;">GroundAtlas · Quản trị Bản quyền Seat</h2>
    <div class="row">
      <span id="status" style="color:#4ade80;font-size:13px;">Đang tải…</span>
      <button class="sec" onclick="loadOverview()">Làm mới</button>
      <button class="sec" onclick="logout()">Đăng xuất</button>
    </div>
  </div>

  <div class="kpi" id="kpiRow">
    <div class="card"><div>Tổng doanh thu</div><div class="val" id="kRev">0 đ</div></div>
    <div class="card"><div>Đơn đã thanh toán</div><div class="val" id="kOrd">0</div></div>
    <div class="card"><div>Tổng Seat đã bán</div><div class="val" id="kSeats">0</div></div>
    <div class="card"><div>Đợt mở bán hiện tại</div><div class="val" id="kBatch" style="font-size:15px;">—</div></div>
  </div>

  <div class="card" id="createCard">
    <h3 style="margin-top:0;">Tạo nhanh License Key mới (Tặng / Bán tay / B2B)</h3>
    <div class="row">
      <input id="cEmail" placeholder="Email khách" />
      <input id="cName" placeholder="Tên / Studio" />
      <select id="cSeats"><option value="1">1 Seat</option><option value="3">3 Seats</option><option value="5">5 Seats</option><option value="10">10 Seats</option></select>
      <input id="cAmt" type="number" placeholder="Số tiền VND" value="99000" />
      <label><input id="cPaid" type="checkbox" checked /> Tính vào slot thật</label>
      <button onclick="createKey()">+ Sinh Key ngay</button>
    </div>
    <div id="newKeyOut" style="margin-top:12px;"></div>
  </div>

  <div class="card" id="licCard">
    <h3 style="margin-top:0;">Danh sách License Keys</h3>
    <table><thead><tr><th>Key</th><th>Email</th><th>Seats</th><th>Swap</th><th>Trạng thái</th><th>Thao tác</th></tr></thead><tbody id="licBody"></tbody></table>
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
    document.getElementById('newKeyOut').innerHTML = 'Key vừa tạo (chỉ hiện 1 lần): <code style="font-size:17px;color:#4ade80;">' + esc(d.license_key) + '</code> (' + Number(d.max_seats) + ' seats)';
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
