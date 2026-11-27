import {
  generateLicenseKey,
  generateOrderCode,
  maskLicenseKey,
  normalizeLicenseKey,
  sha256Hex,
  signLeaseToken,
  timingSafeEqual,
  verifyHmacSha256Hex,
  type LeasePayload,
} from "./crypto.ts";
import { renderAdminHtml, renderLandingHtml } from "./pages.ts";

export interface D1Like {
  prepare(sql: string): {
    bind(...params: unknown[]): {
      first<T = Record<string, unknown>>(): Promise<T | null>;
      all<T = Record<string, unknown>>(): Promise<{ results: T[] }>;
      run(): Promise<{ success: boolean; meta?: { changes?: number } }>;
    };
    first<T = Record<string, unknown>>(): Promise<T | null>;
    all<T = Record<string, unknown>>(): Promise<{ results: T[] }>;
    run(): Promise<{ success: boolean; meta?: { changes?: number } }>;
  };
}

export interface Env {
  DB: D1Like;
  ED25519_PRIVATE_SEED_HEX: string;
  ED25519_PUBLIC_KEY_HEX?: string;
  ADMIN_SECRET: string;
  SEPAY_WEBHOOK_SECRET?: string;
  PAYOS_CHECKSUM_KEY?: string;
  RESEND_API_KEY?: string;
  RESEND_FROM?: string;
  APP_LATEST_VERSION?: string;
  APP_DOWNLOAD_URL?: string;
  APP_UPDATE_NOTES?: string;
  OFFLINE_LEASE_DAYS?: string;
  MAX_SWAPS_PER_180_DAYS?: string;
  WELCOME_OFFER_HOURS?: string;
  WELCOME_PRICE_1_SEAT?: string;
  VIETQR_BANK_BIN?: string;
  VIETQR_ACCOUNT_NO?: string;
  VIETQR_ACCOUNT_NAME?: string;
}

interface CampaignRow {
  id: string;
  name: string;
  sort_order: number;
  seat_quota: number;
  price_1_seat: number;
  price_3_seats: number;
  price_5_seats: number;
  price_10_seats: number;
}

interface LicenseRow {
  id: string;
  key_hash: string;
  key_masked: string;
  tier: string;
  major_version: number;
  max_seats: number;
  swap_count: number;
  swap_window_start: number;
  status: string;
  buyer_email: string;
  buyer_name: string;
  order_code: string;
  note: string;
  created_at: number;
}

interface ActivationRow {
  id: string;
  license_id: string;
  machine_hash: string;
  machine_name: string;
  app_version: string;
  activated_at: number;
  last_seen_at: number;
  revoked_at: number | null;
}

interface OrderRow {
  order_code: string;
  poll_token: string;
  buyer_email: string;
  buyer_name: string;
  machine_hash: string;
  seats: number;
  amount_vnd: number;
  batch_id: string;
  status: string;
  provider: string;
  provider_tx_id: string | null;
  issued_key: string;
  created_at: number;
  paid_at: number | null;
}

const SECURITY_HEADERS: Record<string, string> = {
  "X-Frame-Options": "DENY",
  "X-Content-Type-Options": "nosniff",
  "Referrer-Policy": "no-referrer",
  "Permissions-Policy": "camera=(), microphone=(), geolocation=()",
  "Strict-Transport-Security": "max-age=31536000; includeSubDomains",
  "X-Robots-Tag": "noindex, nofollow, noai, noimageai",
};

const HTML_CSP =
  "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' https://img.vietqr.io data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";

const BLOCKED_AI_SCRAPER_PATTERNS = [
  "gptbot",
  "chatgpt-user",
  "oai-searchbot",
  "claudebot",
  "claude-web",
  "anthropic-ai",
  "perplexitybot",
  "ccbot",
  "bytespider",
  "firecrawl",
  "apify",
  "scrapy",
  "httrack",
  "webcopier",
  "archiver",
  "diffbot",
  "omgili",
  "meta-externalagent",
  "amazonbot",
  "applebot-extended",
  "google-extended",
];

function isBlockedScraperUa(req: Request): boolean {
  const ua = (req.headers.get("User-Agent") || "").toLowerCase();
  if (!ua) return false;
  return BLOCKED_AI_SCRAPER_PATTERNS.some((p) => ua.includes(p));
}

function corsHeadersFor(request: Request): Record<string, string> {
  const origin = request.headers.get("Origin");
  if (!origin) return {};
  try {
    const u = new URL(request.url);
    const reqOrigin = u.origin;
    const p = u.pathname.replace(/\/+$/, "") || "/";
    if (origin === reqOrigin) {
      return {
        "Access-Control-Allow-Origin": reqOrigin,
        "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
        "Access-Control-Allow-Headers": "Content-Type, Authorization, X-SePay-Secret, X-Order-Token",
        Vary: "Origin",
      };
    }
    // Allow our legacy GroundAtlas Web app to fetch public Early-Bird slot counts on GET /v1/pricing only
    if (origin === "https://groundatlas.groundatlas.workers.dev" && p === "/v1/pricing" && (request.method === "GET" || request.method === "OPTIONS")) {
      return {
        "Access-Control-Allow-Origin": origin,
        "Access-Control-Allow-Methods": "GET, OPTIONS",
        Vary: "Origin",
      };
    }
  } catch {
    // ignore malformed origin
  }
  return { Vary: "Origin" };
}

function json(data: unknown, status = 200, extraHeaders: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      ...SECURITY_HEADERS,
      ...extraHeaders,
    },
  });
}

function html(body: string, status = 200, extraHeaders: Record<string, string> = {}): Response {
  return new Response(body, {
    status,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Cache-Control": "no-store",
      "Content-Security-Policy": HTML_CSP,
      ...SECURITY_HEADERS,
      ...extraHeaders,
    },
  });
}

function clientIp(request: Request): string {
  return (
    request.headers.get("CF-Connecting-IP") ||
    request.headers.get("X-Forwarded-For")?.split(",")[0]?.trim() ||
    "local"
  ).slice(0, 64);
}

function randomHex(byteCount = 16): string {
  const buf = new Uint8Array(byteCount);
  crypto.getRandomValues(buf);
  return Array.from(buf, (b) => b.toString(16).padStart(2, "0")).join("");
}

async function checkRateLimit(
  env: Env,
  ip: string,
  scope: string,
  maxHits: number,
  windowSec: number
): Promise<{ allowed: boolean; retryAfter: number }> {
  const ts = nowSec();
  const bucket = `${scope}:${await sha256Hex(ip)}`.slice(0, 80);
  try {
    const row = await env.DB.prepare("SELECT count, reset_at FROM rate_limits WHERE bucket = ?")
      .bind(bucket)
      .first<{ count: number; reset_at: number }>();

    if (!row || ts >= row.reset_at) {
      await env.DB.prepare(
        "INSERT INTO rate_limits (bucket, count, reset_at) VALUES (?, 1, ?) ON CONFLICT(bucket) DO UPDATE SET count = 1, reset_at = excluded.reset_at"
      )
        .bind(bucket, ts + windowSec)
        .run();
      if (ts % 37 === 0) {
        await env.DB.prepare("DELETE FROM rate_limits WHERE reset_at < ?").bind(ts - 3600).run();
      }
      return { allowed: true, retryAfter: 0 };
    }

    if (row.count >= maxHits) {
      return { allowed: false, retryAfter: Math.max(1, row.reset_at - ts) };
    }
    await env.DB.prepare("UPDATE rate_limits SET count = count + 1 WHERE bucket = ?").bind(bucket).run();
    return { allowed: true, retryAfter: 0 };
  } catch {
    return { allowed: true, retryAfter: 0 };
  }
}

function readCookie(req: Request, name: string): string {
  const raw = req.headers.get("Cookie") || "";
  for (const part of raw.split(";")) {
    const eq = part.indexOf("=");
    if (eq > 0 && part.slice(0, eq).trim() === name) {
      return decodeURIComponent(part.slice(eq + 1).trim());
    }
  }
  return "";
}

async function expectedAdminCookie(secret: string): Promise<string> {
  return sha256Hex(`ga_adm_v1:${secret}`);
}

function nowSec(): number {
  return Math.floor(Date.now() / 1000);
}

function isValidMachineHash(h: string): boolean {
  return /^[a-f0-9]{64}$/i.test(h);
}

async function computePricing(env: Env, machineHash = "") {
  const { results: batches } = await env.DB.prepare(
    "SELECT * FROM campaigns ORDER BY sort_order ASC"
  ).all<CampaignRow>();

  // Count REAL paid seats sold from completed orders
  const soldRow = await env.DB.prepare(
    "SELECT COALESCE(SUM(seats), 0) AS total_sold FROM orders WHERE status = 'paid'"
  ).first<{ total_sold: number }>();
  const totalSold = Number(soldRow?.total_sold ?? 0);

  let remainingCursor = totalSold;
  let activeBatch = batches[batches.length - 1];
  let soldInBatch = 0;
  let remainingInBatch = -1;

  for (const b of batches) {
    if (b.seat_quota < 0) {
      activeBatch = b;
      soldInBatch = Math.max(0, remainingCursor);
      remainingInBatch = -1;
      break;
    }
    if (remainingCursor < b.seat_quota) {
      activeBatch = b;
      soldInBatch = remainingCursor;
      remainingInBatch = b.seat_quota - remainingCursor;
      break;
    }
    remainingCursor -= b.seat_quota;
  }

  // Check if this specific machine qualifies for the real 48h personal welcome offer
  let welcomeRemainingSec = 0;
  let welcomeActive = false;
  if (machineHash && isValidMachineHash(machineHash)) {
    const m = await env.DB.prepare(
      "SELECT welcome_offer_expires_at FROM machines WHERE machine_hash = ?"
    )
      .bind(machineHash.toLowerCase())
      .first<{ welcome_offer_expires_at: number }>();
    if (m && m.welcome_offer_expires_at > nowSec()) {
      welcomeRemainingSec = m.welcome_offer_expires_at - nowSec();
      welcomeActive = true;
    }
  }

  const welcomePrice1 = Number(env.WELCOME_PRICE_1_SEAT || 99000);
  const effectivePrice1 = welcomeActive
    ? Math.min(activeBatch.price_1_seat, welcomePrice1)
    : activeBatch.price_1_seat;

  return {
    server_utc: nowSec(),
    batch_id: activeBatch.id,
    batch_name: activeBatch.name,
    seat_quota: activeBatch.seat_quota,
    sold_in_batch: soldInBatch,
    remaining_in_batch: remainingInBatch,
    total_seats_sold: totalSold,
    welcome_offer_active: welcomeActive,
    welcome_offer_remaining_sec: welcomeRemainingSec,
    packages: [
      { seats: 1, label: "Cá nhân · 1 Seat", price_vnd: effectivePrice1, list_price_vnd: 199000 },
      { seats: 3, label: "Nhóm · 3 Seats", price_vnd: activeBatch.price_3_seats, list_price_vnd: 399000 },
      { seats: 5, label: "Studio · 5 Seats", price_vnd: activeBatch.price_5_seats, list_price_vnd: 599000 },
      { seats: 10, label: "Công ty · 10 Seats", price_vnd: activeBatch.price_10_seats, list_price_vnd: 990000 },
    ],
    latest_version: env.APP_LATEST_VERSION || "2.3.1",
    download_url: env.APP_DOWNLOAD_URL || "",
    update_notes: env.APP_UPDATE_NOTES || "",
  };
}

async function issueSignedLease(env: Env, lic: LicenseRow, machineHash: string, activeCount: number): Promise<string> {
  const leaseDays = Math.max(1, Number(env.OFFLINE_LEASE_DAYS || 14));
  const issued = nowSec();
  const payload: LeasePayload = {
    v: 1,
    key: lic.key_masked,
    tier: lic.tier,
    major: lic.major_version,
    seats: lic.max_seats,
    used: activeCount,
    machine: machineHash.toLowerCase(),
    email: lic.buyer_email,
    issued,
    exp: issued + leaseDays * 86400,
  };
  return signLeaseToken(payload, env.ED25519_PRIVATE_SEED_HEX);
}

async function sendLicenseEmail(env: Env, email: string, key: string, seats: number, orderCode: string): Promise<void> {
  if (!env.RESEND_API_KEY || !email) return;
  try {
    await fetch("https://api.resend.com/emails", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${env.RESEND_API_KEY}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        from: env.RESEND_FROM || "GroundAtlas <license@groundatlas.vn>",
        to: [email],
        subject: `[GroundAtlas Desktop] Mã bản quyền trọn đời 2.x (${seats} Seat) · Đơn ${orderCode}`,
        html: `<div style="font-family:Segoe UI,Arial,sans-serif;max-width:560px;margin:auto;padding:24px;background:#faf8f5;border:1px solid #e5dfd5;border-radius:12px;">
          <h2 style="color:#1a1918;margin-top:0;">Cảm ơn bạn đã sở hữu GroundAtlas Desktop 2.x!</h2>
          <p style="color:#4a4640;">Mã bản quyền trọn đời của bạn (<strong>${seats} Seat · 1 Seat = 1 Máy</strong>):</p>
          <div style="background:#1a1918;color:#faf8f5;padding:14px 18px;border-radius:8px;font-family:monospace;font-size:20px;letter-spacing:1.5px;text-align:center;">${key}</div>
          <p style="color:#6b665e;font-size:13px;margin-top:16px;">Mở GroundAtlas Desktop → bấm nút <b>Bản quyền</b> trên thanh công cụ → dán mã trên để kích hoạt ngay. Khi đổi máy tính mới, bạn có thể bấm <b>Gỡ máy này</b> ngay trong app (tối đa 2 lần / 6 tháng).</p>
        </div>`,
      }),
    });
  } catch {
    // Non-blocking email notification; customer still gets key immediately on checkout screen.
  }
}

async function fulfillPaidOrder(env: Env, order: OrderRow, provider: string, txId: string): Promise<string> {
  if (order.status === "paid" && order.issued_key) return order.issued_key;
  const plainKey = generateLicenseKey("GA2X");
  const keyHash = await sha256Hex(normalizeLicenseKey(plainKey));
  const masked = maskLicenseKey(plainKey);
  const licId = crypto.randomUUID();
  const ts = nowSec();

  await env.DB.prepare(
    `INSERT INTO licenses (id, key_hash, key_masked, tier, major_version, max_seats, status, buyer_email, buyer_name, order_code, note, created_at)
     VALUES (?, ?, ?, 'pro_2x', 2, ?, 'active', ?, ?, ?, ?, ?)`
  )
    .bind(licId, keyHash, masked, order.seats, order.buyer_email, order.buyer_name, order.order_code, `Auto ${provider} ${txId}`, ts)
    .run();

  await env.DB.prepare(
    `UPDATE orders SET status = 'paid', provider = ?, provider_tx_id = ?, issued_key = ?, paid_at = ?
     WHERE order_code = ?`
  )
    .bind(provider, txId, plainKey, ts, order.order_code)
    .run();

  await sendLicenseEmail(env, order.buyer_email, plainKey, order.seats, order.order_code);
  return plainKey;
}

async function isAdmin(req: Request, env: Env): Promise<boolean> {
  if (!env.ADMIN_SECRET) return false;
  const auth = req.headers.get("Authorization") || "";
  const token = auth.startsWith("Bearer ") ? auth.slice(7).trim() : "";
  if (token && timingSafeEqual(token, env.ADMIN_SECRET)) return true;
  const cookieVal = readCookie(req, "ga_adm");
  if (cookieVal) {
    const expected = await expectedAdminCookie(env.ADMIN_SECRET);
    if (timingSafeEqual(cookieVal, expected)) return true;
  }
  return false;
}

export async function handleRequest(request: Request, env: Env): Promise<Response> {
  const cors = corsHeadersFor(request);
  if (request.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: { ...SECURITY_HEADERS, ...cors } });
  }

  const url = new URL(request.url);
  const path = url.pathname.replace(/\/+$/, "") || "/";
  const ip = clientIp(request);

  // ── Block AI crawlers & bulk scrapers ──
  if (path === "/robots.txt") {
    return new Response("User-agent: *\nDisallow: /\n", {
      status: 200,
      headers: { "Content-Type": "text/plain; charset=utf-8", ...SECURITY_HEADERS },
    });
  }
  if (isBlockedScraperUa(request)) {
    return new Response("Forbidden", { status: 403, headers: SECURITY_HEADERS });
  }

  // ── Web Pages (Landing VietQR Checkout + Hidden Admin Dashboard) ──
  if (request.method === "GET" && path === "/") {
    return html(renderLandingHtml());
  }
  if (request.method === "GET" && path === "/admin") {
    const suppliedKey = (url.searchParams.get("key") || "").trim();
    if (suppliedKey) {
      const rl = await checkRateLimit(env, ip, "admin_login", 8, 900);
      if (!rl.allowed) return new Response("Too Many Requests", { status: 429, headers: { "Retry-After": String(rl.retryAfter), ...SECURITY_HEADERS } });
      if (env.ADMIN_SECRET && timingSafeEqual(suppliedKey, env.ADMIN_SECRET)) {
        const cookieVal = await expectedAdminCookie(env.ADMIN_SECRET);
        const secureFlag = url.protocol === "https:" ? "; Secure" : "";
        return new Response(null, {
          status: 302,
          headers: {
            Location: "/admin",
            "Set-Cookie": `ga_adm=${cookieVal}; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200${secureFlag}`,
            ...SECURITY_HEADERS,
          },
        });
      }
      return new Response("Not Found", { status: 404, headers: SECURITY_HEADERS });
    }
    if (!(await isAdmin(request, env))) {
      // Return plain 404 so bots/scanners cannot even detect or scrape the Admin HTML
      return new Response("Not Found", { status: 404, headers: SECURITY_HEADERS });
    }
    return html(renderAdminHtml());
  }

  // ── Public Health & Live Early-Bird Pricing ──
  if (request.method === "GET" && path === "/v1/health") {
    return json({ ok: true, server_utc: nowSec() }, 200, cors);
  }
  if (request.method === "GET" && path === "/v1/pricing") {
    const mh = url.searchParams.get("machine_hash") || "";
    return json(await computePricing(env, mh), 200, cors);
  }

  // ── POST /v1/bootstrap (Real immutable 48h welcome offer per machine + live pricing) ──
  if (request.method === "POST" && path === "/v1/bootstrap") {
    const rl = await checkRateLimit(env, ip, "bootstrap", 60, 600);
    if (!rl.allowed) return json({ error: "RATE_LIMITED" }, 429, { "Retry-After": String(rl.retryAfter), ...cors });
    const body = (await request.json().catch(() => ({}))) as { machine_hash?: string; app_version?: string };
    const mh = (body.machine_hash || "").trim().toLowerCase();
    const ver = (body.app_version || "").slice(0, 32);
    if (!isValidMachineHash(mh)) return json({ error: "INVALID_MACHINE_HASH" }, 400, cors);

    const ts = nowSec();
    const offerHours = Math.max(1, Number(env.WELCOME_OFFER_HOURS || 48));
    const existing = await env.DB.prepare("SELECT * FROM machines WHERE machine_hash = ?").bind(mh).first();
    if (!existing) {
      await env.DB.prepare(
        "INSERT INTO machines (machine_hash, first_seen_at, welcome_offer_expires_at, last_seen_at, app_version) VALUES (?, ?, ?, ?, ?)"
      )
        .bind(mh, ts, ts + offerHours * 3600, ts, ver)
        .run();
    } else {
      await env.DB.prepare("UPDATE machines SET last_seen_at = ?, app_version = ? WHERE machine_hash = ?")
        .bind(ts, ver, mh)
        .run();
    }
    return json(await computePricing(env, mh), 200, cors);
  }

  // ── POST /v1/telemetry/compat (Anonymous GPU / OpenGL compatibility health beacon) ──
  if (request.method === "POST" && path === "/v1/telemetry/compat") {
    const rl = await checkRateLimit(env, ip, "telemetry_compat", 30, 600);
    if (!rl.allowed) return json({ error: "RATE_LIMITED" }, 429, { "Retry-After": String(rl.retryAfter), ...cors });
    const body = (await request.json().catch(() => ({}))) as {
      machine_hash?: string;
      app_version?: string;
      gpu_renderer?: string;
      gl_version?: string;
      compat_mode?: string;
      effective_tier?: string;
      fallback_reason?: string;
      gl_error?: number;
      dpi_percent?: number;
      display_hz?: number;
      ram_gb?: number;
    };
    const mh = String(body.machine_hash || "").trim().toLowerCase();
    if (!isValidMachineHash(mh)) return json({ error: "INVALID_MACHINE_HASH" }, 400, cors);
    const ver = String(body.app_version || "2.3.3").trim().slice(0, 32);
    const gpu = String(body.gpu_renderer || "Unknown").trim().slice(0, 140);
    const glVer = String(body.gl_version || "").trim().slice(0, 80);
    const cMode = String(body.compat_mode || "auto").trim().slice(0, 24);
    const tier = String(body.effective_tier || "medium").trim().slice(0, 24);
    const reason = String(body.fallback_reason || "").trim().slice(0, 100);
    const glErr = Math.max(0, Math.min(99999, Math.trunc(Number(body.gl_error || 0))));
    const dpiPct = Math.max(50, Math.min(400, Math.trunc(Number(body.dpi_percent || 100))));
    const hz = Math.max(30, Math.min(500, Math.trunc(Number(body.display_hz || 60))));
    const ram = Math.max(1, Math.min(1024, Math.trunc(Number(body.ram_gb || 8))));
    const ts = nowSec();
    try {
      await env.DB.prepare(
        `CREATE TABLE IF NOT EXISTS gpu_telemetry (
          machine_hash TEXT PRIMARY KEY, app_version TEXT NOT NULL, gpu_renderer TEXT NOT NULL,
          gl_version TEXT NOT NULL DEFAULT '', compat_mode TEXT NOT NULL DEFAULT 'auto',
          effective_tier TEXT NOT NULL DEFAULT 'medium', fallback_reason TEXT NOT NULL DEFAULT '',
          gl_error INTEGER NOT NULL DEFAULT 0, dpi_percent INTEGER NOT NULL DEFAULT 100,
          display_hz INTEGER NOT NULL DEFAULT 60, ram_gb INTEGER NOT NULL DEFAULT 8, updated_at INTEGER NOT NULL
        )`
      ).run();
      await env.DB.prepare(
        `INSERT INTO gpu_telemetry
          (machine_hash, app_version, gpu_renderer, gl_version, compat_mode, effective_tier, fallback_reason, gl_error, dpi_percent, display_hz, ram_gb, updated_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
         ON CONFLICT(machine_hash) DO UPDATE SET
          app_version=excluded.app_version, gpu_renderer=excluded.gpu_renderer, gl_version=excluded.gl_version,
          compat_mode=excluded.compat_mode, effective_tier=excluded.effective_tier, fallback_reason=excluded.fallback_reason,
          gl_error=excluded.gl_error, dpi_percent=excluded.dpi_percent, display_hz=excluded.display_hz,
          ram_gb=excluded.ram_gb, updated_at=excluded.updated_at`
      )
        .bind(mh, ver, gpu, glVer, cMode, tier, reason, glErr, dpiPct, hz, ram, ts)
        .run();
    } catch {
      // Never fail client telemetry calls
    }
    return json({ ok: true, recorded_at: ts }, 200, cors);
  }

  // ── POST /v1/activate (1 Seat = 1 Machine enforcement + Ed25519 Signed Lease Token) ──
  if (request.method === "POST" && path === "/v1/activate") {
    const rl = await checkRateLimit(env, ip, "activate", 15, 600);
    if (!rl.allowed) return json({ error: "RATE_LIMITED", message: "Thử quá nhiều lần. Vui lòng đợi vài phút." }, 429, { "Retry-After": String(rl.retryAfter), ...cors });
    const body = (await request.json().catch(() => ({}))) as {
      license_key?: string;
      machine_hash?: string;
      machine_name?: string;
      app_version?: string;
    };
    const normKey = normalizeLicenseKey(body.license_key || "");
    const mh = (body.machine_hash || "").trim().toLowerCase();
    const mName = (body.machine_name || "Windows PC").trim().slice(0, 80);
    const ver = (body.app_version || "2.3.0").trim().slice(0, 32);
    if (normKey.length < 10) return json({ error: "INVALID_KEY_FORMAT", message: "Định dạng mã bản quyền không hợp lệ." }, 400);
    if (!isValidMachineHash(mh)) return json({ error: "INVALID_MACHINE_HASH" }, 400);

    const keyHash = await sha256Hex(normKey);
    const lic = await env.DB.prepare("SELECT * FROM licenses WHERE key_hash = ?").bind(keyHash).first<LicenseRow>();
    if (!lic) return json({ error: "KEY_NOT_FOUND", message: "Mã bản quyền không tồn tại. Vui lòng kiểm tra lại." }, 404);
    if (lic.status !== "active") {
      return json({ error: "KEY_REVOKED", message: "Mã bản quyền này đã bị thu hồi hoặc tạm khóa." }, 403);
    }

    const { results: activeList } = await env.DB.prepare(
      "SELECT * FROM activations WHERE license_id = ? AND revoked_at IS NULL ORDER BY activated_at ASC"
    )
      .bind(lic.id)
      .all<ActivationRow>();

    const ts = nowSec();
    const sameMachine = activeList.find((a) => a.machine_hash === mh);
    let usedSeats = activeList.length;

    if (sameMachine) {
      await env.DB.prepare("UPDATE activations SET last_seen_at = ?, machine_name = ?, app_version = ? WHERE id = ?")
        .bind(ts, mName, ver, sameMachine.id)
        .run();
    } else {
      if (activeList.length >= lic.max_seats) {
        return json(
          {
            error: "SEAT_LIMIT_REACHED",
            message: `Mã bản quyền đã kích hoạt đủ ${activeList.length}/${lic.max_seats} máy. Hãy bấm 'Gỡ máy này' trên máy cũ trước khi chuyển sang máy mới.`,
            max_seats: lic.max_seats,
            used_seats: activeList.length,
            active_machines: activeList.map((a) => a.machine_name || "PC"),
          },
          409
        );
      }
      await env.DB.prepare(
        "INSERT INTO activations (id, license_id, machine_hash, machine_name, app_version, activated_at, last_seen_at) VALUES (?, ?, ?, ?, ?, ?, ?)"
      )
        .bind(crypto.randomUUID(), lic.id, mh, mName, ver, ts, ts)
        .run();
      usedSeats += 1;
    }

    const lease = await issueSignedLease(env, lic, mh, usedSeats);
    return json({
      ok: true,
      lease,
      key_masked: lic.key_masked,
      tier: lic.tier,
      max_seats: lic.max_seats,
      used_seats: usedSeats,
      buyer_email: lic.buyer_email,
      server_utc: ts,
    });
  }

  // ── POST /v1/refresh (Silent lease renewal for active machine) ──
  if (request.method === "POST" && path === "/v1/refresh") {
    const rl = await checkRateLimit(env, ip, "refresh", 30, 600);
    if (!rl.allowed) return json({ error: "RATE_LIMITED" }, 429, { "Retry-After": String(rl.retryAfter), ...cors });
    const body = (await request.json().catch(() => ({}))) as { machine_hash?: string; key_masked?: string; app_version?: string };
    const mh = (body.machine_hash || "").trim().toLowerCase();
    const km = (body.key_masked || "").trim();
    const ver = (body.app_version || "2.3.0").trim().slice(0, 32);
    if (!isValidMachineHash(mh)) return json({ error: "INVALID_MACHINE_HASH" }, 400);

    const row = await env.DB.prepare(
      `SELECT l.*, a.id AS act_id
       FROM activations a JOIN licenses l ON l.id = a.license_id
       WHERE a.machine_hash = ? AND a.revoked_at IS NULL AND (? = '' OR l.key_masked = ?)
       ORDER BY a.last_seen_at DESC LIMIT 1`
    )
      .bind(mh, km, km)
      .first<LicenseRow & { act_id: string }>();

    if (!row || row.status !== "active") {
      return json({ error: "ACTIVATION_REVOKED", message: "Bản quyền trên máy này đã bị gỡ hoặc thu hồi." }, 403);
    }

    const ts = nowSec();
    await env.DB.prepare("UPDATE activations SET last_seen_at = ?, app_version = ? WHERE id = ?")
      .bind(ts, ver, row.act_id)
      .run();

    const cnt = await env.DB.prepare(
      "SELECT COUNT(*) AS c FROM activations WHERE license_id = ? AND revoked_at IS NULL"
    )
      .bind(row.id)
      .first<{ c: number }>();

    const usedSeats = Number(cnt?.c ?? 1);
    const lease = await issueSignedLease(env, row, mh, usedSeats);
    return json({ ok: true, lease, max_seats: row.max_seats, used_seats: usedSeats, server_utc: ts });
  }

  // ── POST /v1/deactivate (Self-service Seat Swap — max 2 swaps / 180 days) ──
  if (request.method === "POST" && path === "/v1/deactivate") {
    const rl = await checkRateLimit(env, ip, "deactivate", 30, 600);
    if (!rl.allowed) return json({ error: "RATE_LIMITED" }, 429, { "Retry-After": String(rl.retryAfter), ...cors });
    const body = (await request.json().catch(() => ({}))) as { machine_hash?: string; key_masked?: string };
    const mh = (body.machine_hash || "").trim().toLowerCase();
    const km = (body.key_masked || "").trim();
    if (!isValidMachineHash(mh)) return json({ error: "INVALID_MACHINE_HASH" }, 400);

    const row = await env.DB.prepare(
      `SELECT l.*, a.id AS act_id
       FROM activations a JOIN licenses l ON l.id = a.license_id
       WHERE a.machine_hash = ? AND a.revoked_at IS NULL AND (? = '' OR l.key_masked = ?)
       ORDER BY a.last_seen_at DESC LIMIT 1`
    )
      .bind(mh, km, km)
      .first<LicenseRow & { act_id: string }>();

    if (!row) return json({ error: "NOT_ACTIVATED", message: "Máy này hiện chưa kích hoạt seat nào." }, 404);

    const ts = nowSec();
    const windowSec = 180 * 86400;
    const maxSwaps = Math.max(1, Number(env.MAX_SWAPS_PER_180_DAYS || 2));
    let swapCount = Number(row.swap_count || 0);
    let swapStart = Number(row.swap_window_start || 0);

    if (swapStart === 0 || ts - swapStart >= windowSec) {
      swapCount = 0;
      swapStart = ts;
    }
    if (swapCount >= maxSwaps) {
      const nextReset = swapStart + windowSec;
      return json(
        {
          error: "SWAP_LIMIT_REACHED",
          message: `Key này đã đạt giới hạn chuyển máy (${maxSwaps} lần / 6 tháng). Liên hệ hỗ trợ nếu bạn bị hỏng máy đột xuất.`,
          next_reset_utc: nextReset,
        },
        429
      );
    }

    await env.DB.prepare("UPDATE activations SET revoked_at = ? WHERE id = ?").bind(ts, row.act_id).run();
    await env.DB.prepare("UPDATE licenses SET swap_count = ?, swap_window_start = ? WHERE id = ?")
      .bind(swapCount + 1, swapStart, row.id)
      .run();

    return json({
      ok: true,
      swaps_used: swapCount + 1,
      swaps_limit: maxSwaps,
      message: "Đã gỡ kích hoạt máy này và trả lại 1 seat. Bạn có thể kích hoạt trên máy mới ngay bây giờ.",
    });
  }

  // ── POST /v1/orders (Create VietQR Checkout Order with 128-bit poll_token) ──
  if (request.method === "POST" && path === "/v1/orders") {
    const rl = await checkRateLimit(env, ip, "orders", 6, 600);
    if (!rl.allowed) {
      return json(
        { error: "RATE_LIMITED", message: "Bạn tạo quá nhiều đơn trong thời gian ngắn. Vui lòng đợi vài phút rồi thử lại." },
        429,
        { "Retry-After": String(rl.retryAfter), ...cors }
      );
    }
    const body = (await request.json().catch(() => ({}))) as {
      email?: string;
      name?: string;
      seats?: number;
      machine_hash?: string;
    };
    const email = (body.email || "").trim().toLowerCase();
    const name = (body.name || "").trim().slice(0, 80);
    const seats = [1, 3, 5, 10].includes(Number(body.seats)) ? Number(body.seats) : 1;
    const mh = (body.machine_hash || "").trim().toLowerCase();
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
      return json({ error: "INVALID_EMAIL", message: "Vui lòng nhập email hợp lệ để nhận mã bản quyền." }, 400, cors);
    }

    const pricing = await computePricing(env, mh);
    const pkg = pricing.packages.find((p) => p.seats === seats) || pricing.packages[0];
    const orderCode = generateOrderCode();
    const pollToken = randomHex(16); // 128-bit secret bound to checkout session
    const ts = nowSec();

    await env.DB.prepare(
      `INSERT INTO orders (order_code, poll_token, buyer_email, buyer_name, machine_hash, seats, amount_vnd, batch_id, status, provider, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'pending', 'sepay', ?)`
    )
      .bind(orderCode, pollToken, email, name, isValidMachineHash(mh) ? mh : "", seats, pkg.price_vnd, pricing.batch_id, ts)
      .run();

    const bankBin = env.VIETQR_BANK_BIN || "970422";
    const acctNo = env.VIETQR_ACCOUNT_NO || "0000000000";
    const acctName = env.VIETQR_ACCOUNT_NAME || "GROUNDATLAS";
    const qrUrl = `https://img.vietqr.io/image/${encodeURIComponent(bankBin)}-${encodeURIComponent(acctNo)}-compact2.png?amount=${pkg.price_vnd}&addInfo=${encodeURIComponent(orderCode)}&accountName=${encodeURIComponent(acctName)}`;

    return json(
      {
        ok: true,
        order_code: orderCode,
        poll_token: pollToken,
        seats,
        amount_vnd: pkg.price_vnd,
        batch_name: pricing.batch_name,
        bank_bin: bankBin,
        account_no: acctNo,
        account_name: acctName,
        transfer_content: orderCode,
        qr_url: qrUrl,
      },
      200,
      cors
    );
  }

  // ── GET /v1/orders/:code (Protected by 128-bit poll_token — prevents order enumeration / key theft!) ──
  if (request.method === "GET" && path.startsWith("/v1/orders/")) {
    const rl = await checkRateLimit(env, ip, "order_poll", 120, 600);
    if (!rl.allowed) return json({ error: "RATE_LIMITED" }, 429, { "Retry-After": String(rl.retryAfter), ...cors });
    const code = path.slice("/v1/orders/".length).trim().toUpperCase();
    const suppliedToken = (url.searchParams.get("t") || request.headers.get("X-Order-Token") || "").trim();
    if (suppliedToken.length < 16) return json({ error: "INVALID_ORDER_TOKEN" }, 403, cors);
    const order = await env.DB.prepare("SELECT * FROM orders WHERE order_code = ?").bind(code).first<OrderRow>();
    if (!order || !order.poll_token || !timingSafeEqual(suppliedToken, order.poll_token)) {
      return json({ error: "INVALID_ORDER_TOKEN" }, 403, cors);
    }
    return json(
      {
        order_code: order.order_code,
        status: order.status,
        seats: order.seats,
        amount_vnd: order.amount_vnd,
        license_key: order.status === "paid" ? order.issued_key : "",
        paid_at: order.paid_at,
      },
      200,
      cors
    );
  }

  // ── POST /v1/webhook/sepay (Automatic VietQR SePay Bank Transfer Webhook) ──
  if (request.method === "POST" && path === "/v1/webhook/sepay") {
    const authHeader = request.headers.get("Authorization") || request.headers.get("X-SePay-Secret") || "";
    const provided = authHeader.replace(/^Apikey\s+/i, "").replace(/^Bearer\s+/i, "").trim();
    if (!env.SEPAY_WEBHOOK_SECRET || !timingSafeEqual(provided, env.SEPAY_WEBHOOK_SECRET)) {
      return json({ error: "UNAUTHORIZED_WEBHOOK" }, 401);
    }

    const body = (await request.json().catch(() => ({}))) as {
      id?: string | number;
      transferType?: string;
      transferAmount?: number;
      content?: string;
      referenceCode?: string;
    };
    if (body.transferType && body.transferType !== "in") return json({ ok: true, ignored: "outgoing" });
    const content = String(body.content || "").toUpperCase();
    const match = content.match(/GA\d{6}/);
    if (!match) return json({ ok: true, ignored: "no_order_code" });

    const orderCode = match[0];
    const order = await env.DB.prepare("SELECT * FROM orders WHERE order_code = ?").bind(orderCode).first<OrderRow>();
    if (!order) return json({ ok: true, ignored: "order_not_found" });
    if (Number(body.transferAmount || 0) < order.amount_vnd) {
      return json({ ok: false, error: "UNDERPAID", expected: order.amount_vnd, got: body.transferAmount }, 400);
    }

    const txId = String(body.referenceCode || body.id || `sepay_${nowSec()}`);
    const key = await fulfillPaidOrder(env, order, "sepay", txId);
    return json({ ok: true, order_code: orderCode, issued: true, key_masked: maskLicenseKey(key) });
  }

  // ── POST /v1/webhook/payos (Automatic PayOS Webhook with HMAC-SHA256 verification) ──
  if (request.method === "POST" && path === "/v1/webhook/payos") {
    const body = (await request.json().catch(() => ({}))) as {
      data?: Record<string, unknown>;
      signature?: string;
    };
    if (!env.PAYOS_CHECKSUM_KEY || !body.data || !body.signature) {
      return json({ error: "UNAUTHORIZED_WEBHOOK" }, 401);
    }
    const sorted = Object.keys(body.data)
      .sort()
      .map((k) => `${k}=${body.data![k] ?? ""}`)
      .join("&");
    const validSig = await verifyHmacSha256Hex(env.PAYOS_CHECKSUM_KEY, sorted, body.signature);
    if (!validSig) return json({ error: "INVALID_SIGNATURE" }, 401);

    const desc = String(body.data.description || "").toUpperCase();
    const match = desc.match(/GA\d{6}/);
    if (!match) return json({ ok: true, ignored: "no_order_code" });

    const order = await env.DB.prepare("SELECT * FROM orders WHERE order_code = ?").bind(match[0]).first<OrderRow>();
    if (!order) return json({ ok: true, ignored: "order_not_found" });
    if (Number(body.data.amount || 0) < order.amount_vnd) return json({ ok: false, error: "UNDERPAID" }, 400);

    const txId = String(body.data.reference || body.data.paymentLinkId || `payos_${nowSec()}`);
    const key = await fulfillPaidOrder(env, order, "payos", txId);
    return json({ ok: true, order_code: order.order_code, issued: true, key_masked: maskLicenseKey(key) });
  }

  // ── Admin Endpoints (Protected by Bearer ADMIN_SECRET or HttpOnly cookie + brute-force rate limit) ──
  if (path.startsWith("/v1/admin/")) {
    if (!(await isAdmin(request, env))) {
      const rl = await checkRateLimit(env, ip, "admin_auth_fail", 8, 900);
      if (!rl.allowed) return json({ error: "RATE_LIMITED" }, 429, { "Retry-After": String(rl.retryAfter) });
      return json({ error: "UNAUTHORIZED" }, 401);
    }

    if (request.method === "POST" && path === "/v1/admin/logout") {
      const secureFlag = url.protocol === "https:" ? "; Secure" : "";
      return json(
        { ok: true },
        200,
        { "Set-Cookie": `ga_adm=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0${secureFlag}` }
      );
    }

    if (request.method === "GET" && path === "/v1/admin/overview") {
      const pricing = await computePricing(env);
      const revRow = await env.DB.prepare(
        "SELECT COALESCE(SUM(amount_vnd), 0) AS revenue, COUNT(*) AS paid_orders FROM orders WHERE status = 'paid'"
      ).first<{ revenue: number; paid_orders: number }>();
      const { results: licenses } = await env.DB.prepare(
        `SELECT l.*,
          (SELECT COUNT(*) FROM activations a WHERE a.license_id = l.id AND a.revoked_at IS NULL) AS active_seats
         FROM licenses l ORDER BY l.created_at DESC LIMIT 100`
      ).all();
      const { results: recentOrders } = await env.DB.prepare(
        "SELECT * FROM orders ORDER BY created_at DESC LIMIT 50"
      ).all();
      let gpuHealth: {
        total_devices: number;
        healthy_devices: number;
        fallback_devices: number;
        error_devices: number;
        devices: unknown[];
      } = { total_devices: 0, healthy_devices: 0, fallback_devices: 0, error_devices: 0, devices: [] };
      try {
        const { results: gpus } = await env.DB.prepare(
          "SELECT machine_hash, app_version, gpu_renderer, gl_version, compat_mode, effective_tier, fallback_reason, gl_error, dpi_percent, display_hz, ram_gb, updated_at FROM gpu_telemetry ORDER BY updated_at DESC LIMIT 60"
        ).all<{
          fallback_reason?: string;
          gl_error?: number;
          compat_mode?: string;
        }>();
        let healthy = 0, fallback = 0, errors = 0;
        for (const g of gpus) {
          const hasErr = Number(g.gl_error || 0) !== 0;
          const hasFb = Boolean(g.fallback_reason) || g.compat_mode === "raster" || g.compat_mode === "software";
          if (hasErr) errors += 1;
          else if (hasFb) fallback += 1;
          else healthy += 1;
        }
        gpuHealth = {
          total_devices: gpus.length,
          healthy_devices: healthy,
          fallback_devices: fallback,
          error_devices: errors,
          devices: gpus,
        };
      } catch {
        // Table not yet created on older DBs — return empty summary safely
      }
      return json({
        pricing,
        revenue_vnd: Number(revRow?.revenue ?? 0),
        paid_orders: Number(revRow?.paid_orders ?? 0),
        licenses,
        orders: recentOrders,
        gpu_health: gpuHealth,
      });
    }

    // Manual key creation (gifts / bank transfer verified manually / B2B invoices)
    if (request.method === "POST" && path === "/v1/admin/licenses") {
      const body = (await request.json().catch(() => ({}))) as {
        max_seats?: number;
        buyer_email?: string;
        buyer_name?: string;
        note?: string;
        count_as_paid_order?: boolean;
        amount_vnd?: number;
      };
      const seats = Math.max(1, Math.min(200, Number(body.max_seats || 1)));
      const plainKey = generateLicenseKey("GA2X");
      const keyHash = await sha256Hex(normalizeLicenseKey(plainKey));
      const masked = maskLicenseKey(plainKey);
      const licId = crypto.randomUUID();
      const ts = nowSec();
      const orderCode = body.count_as_paid_order ? generateOrderCode() : "";

      await env.DB.prepare(
        `INSERT INTO licenses (id, key_hash, key_masked, tier, major_version, max_seats, status, buyer_email, buyer_name, order_code, note, created_at)
         VALUES (?, ?, ?, 'pro_2x', 2, ?, 'active', ?, ?, ?, ?, ?)`
      )
        .bind(licId, keyHash, masked, seats, body.buyer_email || "", body.buyer_name || "", orderCode, body.note || "Manual admin key", ts)
        .run();

      if (body.count_as_paid_order) {
        const pricing = await computePricing(env);
        await env.DB.prepare(
          `INSERT INTO orders (order_code, buyer_email, buyer_name, seats, amount_vnd, batch_id, status, provider, provider_tx_id, issued_key, created_at, paid_at)
           VALUES (?, ?, ?, ?, ?, ?, 'paid', 'manual', ?, ?, ?, ?)`
        )
          .bind(orderCode, body.buyer_email || "manual@local", body.buyer_name || "", seats, Number(body.amount_vnd || 0), pricing.batch_id, `man_${ts}_${licId.slice(0, 6)}`, plainKey, ts, ts)
          .run();
      }
      return json({ ok: true, id: licId, license_key: plainKey, key_masked: masked, max_seats: seats });
    }

    // Revoke / Reactivate a license key
    if (request.method === "POST" && path.endsWith("/revoke")) {
      const id = path.slice("/v1/admin/licenses/".length, -"/revoke".length);
      const body = (await request.json().catch(() => ({}))) as { status?: string };
      const nextStatus = body.status === "active" ? "active" : "revoked";
      await env.DB.prepare("UPDATE licenses SET status = ? WHERE id = ?").bind(nextStatus, id).run();
      if (nextStatus === "revoked") {
        await env.DB.prepare("UPDATE activations SET revoked_at = ? WHERE license_id = ? AND revoked_at IS NULL")
          .bind(nowSec(), id)
          .run();
      }
      return json({ ok: true, id, status: nextStatus });
    }

    // Reset all seats on a license (when customer's PC broke/reinstalled Windows)
    if (request.method === "POST" && path.endsWith("/reset-seats")) {
      const id = path.slice("/v1/admin/licenses/".length, -"/reset-seats".length);
      await env.DB.prepare("UPDATE activations SET revoked_at = ? WHERE license_id = ? AND revoked_at IS NULL")
        .bind(nowSec(), id)
        .run();
      await env.DB.prepare("UPDATE licenses SET swap_count = 0 WHERE id = ?").bind(id).run();
      return json({ ok: true, id, message: "All seats released and swap counter reset." });
    }
  }

  return json({ error: "NOT_FOUND" }, 404);
}

export default {
  fetch: handleRequest,
};
