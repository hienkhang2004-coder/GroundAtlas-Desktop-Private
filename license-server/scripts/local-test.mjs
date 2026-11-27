// End-to-end automated verification of GroundAtlas License Server (Hardened v2.3.1)
// Covers all 12 core licensing flows + 6 security/anti-clone/anti-scraping defenses (18 suites total)
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");

const { handleRequest } = await import("../src/worker.ts");
const { fromBase64Url } = await import("../src/crypto.ts");

class LocalD1 {
  constructor() {
    this.db = new DatabaseSync(":memory:");
    this.db.exec(fs.readFileSync(path.join(root, "migrations/0001_init.sql"), "utf8"));
    this.db.exec(fs.readFileSync(path.join(root, "migrations/0002_security_hardening.sql"), "utf8"));
    this.db.exec(fs.readFileSync(path.join(root, "migrations/0003_gpu_telemetry.sql"), "utf8"));
  }
  prepare(sql) {
    const stmt = this.db.prepare(sql);
    const makeRunner = (params) => ({
      async first() {
        const row = stmt.get(...params);
        return row === undefined ? null : row;
      },
      async all() {
        return { results: stmt.all(...params) };
      },
      async run() {
        const info = stmt.run(...params);
        return { success: true, meta: { changes: Number(info.changes ?? 0) } };
      },
    });
    return {
      bind(...params) {
        return makeRunner(params);
      },
      ...makeRunner([]),
    };
  }
}

const { privateKey, publicKey } = crypto.generateKeyPairSync("ed25519");
const pkcs8 = privateKey.export({ type: "pkcs8", format: "der" });
const privSeedHex = pkcs8.subarray(pkcs8.length - 32).toString("hex");

const env = {
  DB: new LocalD1(),
  ED25519_PRIVATE_SEED_HEX: privSeedHex,
  ADMIN_SECRET: "test_admin_secret_999",
  SEPAY_WEBHOOK_SECRET: "test_sepay_secret_777",
  APP_LATEST_VERSION: "2.3.0",
  OFFLINE_LEASE_DAYS: "14",
  MAX_SWAPS_PER_180_DAYS: "2",
  WELCOME_OFFER_HOURS: "48",
  WELCOME_PRICE_1_SEAT: "99000",
  VIETQR_BANK_BIN: "970422",
  VIETQR_ACCOUNT_NO: "0978786930",
  VIETQR_ACCOUNT_NAME: "BACH THAI KHANG",
};

async function call(method, p, body, headers = {}) {
  const req = new Request("https://license.groundatlas.test" + p, {
    method,
    headers: { "Content-Type": "application/json", "CF-Connecting-IP": "203.0.113.10", ...headers },
    body: body ? JSON.stringify(body) : undefined,
  });
  const res = await handleRequest(req, env);
  const text = await res.text();
  let data = text;
  try { data = JSON.parse(text); } catch {}
  return { status: res.status, data, headers: res.headers };
}

const m1 = crypto.createHash("sha256").update("pc-architect-01").digest("hex");
const m2 = crypto.createHash("sha256").update("pc-architect-02").digest("hex");
const m3 = crypto.createHash("sha256").update("pc-architect-03").digest("hex");

// 1. Initial Early-Bird pricing (100/100 at 99k)
let r = await call("GET", "/v1/pricing");
assert.equal(r.status, 200);
assert.equal(r.data.batch_id, "batch_1");
assert.equal(r.data.remaining_in_batch, 100);
assert.equal(r.data.packages[0].price_vnd, 99000);

// 2. Bootstrap records immutable 48h welcome offer per machine
r = await call("POST", "/v1/bootstrap", { machine_hash: m1, app_version: "2.3.0" });
assert.equal(r.status, 200);
assert.equal(r.data.welcome_offer_active, true);
assert.ok(r.data.welcome_offer_remaining_sec > 47 * 3600);

// 3. Create VietQR order -> returns 128-bit poll_token
r = await call("POST", "/v1/orders", { email: "kts@studio.vn", name: "KTS Khang", seats: 1, machine_hash: m1 });
assert.equal(r.status, 200);
const orderCode = r.data.order_code;
const pollToken = r.data.poll_token;
assert.equal(r.data.amount_vnd, 99000);
assert.equal(pollToken.length, 32);
assert.ok(r.data.qr_url.includes(orderCode));

// 4. SePay webhook with wrong secret MUST fail 401
r = await call("POST", "/v1/webhook/sepay", { transferType: "in", transferAmount: 99000, content: orderCode }, { Authorization: "Apikey wrong" });
assert.equal(r.status, 401);

// 5. SePay webhook with valid secret auto-issues Key & decrements slots
r = await call("POST", "/v1/webhook/sepay", { id: 1001, transferType: "in", transferAmount: 99000, content: `CK ${orderCode} MUA APP` }, { Authorization: "Apikey test_sepay_secret_777" });
assert.equal(r.status, 200);
assert.equal(r.data.issued, true);

// 6. SECURITY: Order polling WITHOUT poll_token or with WRONG token MUST FAIL 403!
r = await call("GET", `/v1/orders/${orderCode}`);
assert.equal(r.status, 403);
r = await call("GET", `/v1/orders/${orderCode}?t=00000000000000000000000000000000`);
assert.equal(r.status, 403);

// 7. Order polling WITH valid 128-bit poll_token succeeds & returns issued key
r = await call("GET", `/v1/orders/${orderCode}?t=${pollToken}`);
assert.equal(r.status, 200);
assert.equal(r.data.status, "paid");
const issuedKey = r.data.license_key;
assert.match(issuedKey, /^GA2X-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$/);

// Verify Early-Bird slot decremented 100 -> 99
r = await call("GET", "/v1/pricing");
assert.equal(r.data.sold_in_batch, 1);
assert.equal(r.data.remaining_in_batch, 99);

// 8. Activate Machine 1 -> valid RFC 8032 Ed25519 signed lease
r = await call("POST", "/v1/activate", { license_key: issuedKey, machine_hash: m1, machine_name: "WORKSTATION-RTX3060", app_version: "2.3.0" });
assert.equal(r.status, 200);
const leaseParts = r.data.lease.split(".");
assert.equal(leaseParts.length, 3);
assert.equal(leaseParts[0], "GA2L");
assert.equal(crypto.verify(null, Buffer.from(leaseParts[1], "utf8"), publicKey, Buffer.from(fromBase64Url(leaseParts[2]))), true);

// 9. Machine 2 on same 1-seat key -> 409 SEAT_LIMIT_REACHED
r = await call("POST", "/v1/activate", { license_key: issuedKey, machine_hash: m2, machine_name: "LAPTOP-OFFICE" });
assert.equal(r.status, 409);
assert.equal(r.data.error, "SEAT_LIMIT_REACHED");

// 10. Self-service swaps (max 2 swaps / 180 days)
r = await call("POST", "/v1/deactivate", { machine_hash: m1 });
assert.equal(r.status, 200);
assert.equal(r.data.swaps_used, 1);
r = await call("POST", "/v1/activate", { license_key: issuedKey, machine_hash: m2, machine_name: "LAPTOP-OFFICE" });
assert.equal(r.status, 200);
r = await call("POST", "/v1/deactivate", { machine_hash: m2 });
assert.equal(r.status, 200);
r = await call("POST", "/v1/activate", { license_key: issuedKey, machine_hash: m3, machine_name: "PC-HOME" });
assert.equal(r.status, 200);
r = await call("POST", "/v1/deactivate", { machine_hash: m3 });
assert.equal(r.status, 429);
assert.equal(r.data.error, "SWAP_LIMIT_REACHED");

// 11. Admin Bearer auth + reset-seats frees seat & resets swap counter
r = await call("GET", "/v1/admin/overview", null, { Authorization: "Bearer test_admin_secret_999" });
assert.equal(r.status, 200);
assert.equal(r.data.revenue_vnd, 99000);
const licId = r.data.licenses[0].id;
r = await call("POST", `/v1/admin/licenses/${licId}/reset-seats`, {}, { Authorization: "Bearer test_admin_secret_999" });
assert.equal(r.status, 200);
r = await call("POST", "/v1/activate", { license_key: issuedKey, machine_hash: m1, machine_name: "WORKSTATION-RTX3060" });
assert.equal(r.status, 200);

// 12. SECURITY: Landing HTML returns 200 with strict CSP, X-Frame-Options: DENY, X-Robots-Tag: noai
const homeRes = await handleRequest(new Request("https://license.groundatlas.test/", { headers: { "Origin": "https://evil-phishing.site" } }), env);
assert.equal(homeRes.status, 200);
assert.equal(homeRes.headers.get("X-Frame-Options"), "DENY");
assert.ok((homeRes.headers.get("Content-Security-Policy") || "").includes("frame-ancestors 'none'"));
assert.ok((homeRes.headers.get("X-Robots-Tag") || "").includes("noai"));
assert.equal(homeRes.headers.get("Access-Control-Allow-Origin"), null, "Cross-origin must NOT be allowed!");

// 13. SECURITY: /robots.txt disallows all crawlers
const robotsRes = await handleRequest(new Request("https://license.groundatlas.test/robots.txt"), env);
assert.equal(robotsRes.status, 200);
assert.ok((await robotsRes.text()).includes("Disallow: /"));

// 14. SECURITY: AI scrapers (GPTBot, ClaudeBot, Firecrawl) are blocked with 403
for (const botUa of ["Mozilla/5.0 (compatible; GPTBot/1.0)", "ClaudeBot/1.0", "FirecrawlAgent/1.0"]) {
  const botRes = await handleRequest(new Request("https://license.groundatlas.test/", { headers: { "User-Agent": botUa } }), env);
  assert.equal(botRes.status, 403, `Expected 403 for ${botUa}`);
}

// 15. SECURITY: Unauthenticated /admin returns plain 404 (hidden from scanners & scrapers!)
const unauthAdm = await handleRequest(new Request("https://license.groundatlas.test/admin"), env);
assert.equal(unauthAdm.status, 404);
const badKeyAdm = await handleRequest(new Request("https://license.groundatlas.test/admin?key=wrong"), env);
assert.equal(badKeyAdm.status, 404);

// 16. SECURITY: /admin?key=<ADMIN_SECRET> sets HttpOnly Secure SameSite=Strict cookie & unlocks /admin + API!
const loginAdm = await handleRequest(new Request("https://license.groundatlas.test/admin?key=test_admin_secret_999"), env);
assert.equal(loginAdm.status, 302);
const setCookie = loginAdm.headers.get("Set-Cookie") || "";
assert.ok(setCookie.includes("ga_adm="));
assert.ok(setCookie.includes("HttpOnly"));
assert.ok(setCookie.includes("SameSite=Strict"));
const cookiePair = setCookie.split(";")[0];
const authAdmPage = await handleRequest(new Request("https://license.groundatlas.test/admin", { headers: { Cookie: cookiePair } }), env);
assert.equal(authAdmPage.status, 200);
const authAdmApi = await handleRequest(new Request("https://license.groundatlas.test/v1/admin/overview", { headers: { Cookie: cookiePair } }), env);
assert.equal(authAdmApi.status, 200);

// 17. SECURITY: Persistent D1 IP Rate Limiter blocks order spam on 7th order with 429!
for (let i = 0; i < 6; i++) {
  const sp = await call("POST", "/v1/orders", { email: `spam${i}@x.com`, seats: 1 }, { "CF-Connecting-IP": "198.51.100.77" });
  assert.equal(sp.status, 200);
}
const blockedSpam = await call("POST", "/v1/orders", { email: "spam7@x.com", seats: 1 }, { "CF-Connecting-IP": "198.51.100.77" });
assert.equal(blockedSpam.status, 429);
assert.ok(Number(blockedSpam.headers.get("Retry-After")) >= 1);

// 18. SECURITY: /v1/health stays minimal & CORS allows ONLY legacy GroundAtlas Web on GET /v1/pricing
r = await call("GET", "/v1/health");
assert.equal(r.status, 200);
assert.equal(r.data.ok, true);
assert.equal(r.data.service, undefined);
const webCorsRes = await handleRequest(
  new Request("https://license.groundatlas.test/v1/pricing", { headers: { Origin: "https://groundatlas.groundatlas.workers.dev" } }),
  env
);
assert.equal(webCorsRes.headers.get("Access-Control-Allow-Origin"), "https://groundatlas.groundatlas.workers.dev");
const evilCorsRes = await handleRequest(
  new Request("https://license.groundatlas.test/v1/pricing", { headers: { Origin: "https://evil-clone.site" } }),
  env
);
assert.equal(evilCorsRes.headers.get("Access-Control-Allow-Origin"), null);

// 19. GPU COMPATIBILITY TELEMETRY: records anonymous GPU health & surfaces on Admin Hub overview
r = await call("POST", "/v1/telemetry/compat", {
  machine_hash: m1,
  app_version: "2.3.3",
  gpu_renderer: "AMD Radeon(TM) Graphics",
  gl_version: "4.6.0 Compatibility Profile",
  compat_mode: "compatible",
  effective_tier: "low",
  fallback_reason: "mapcanvas-black-frame-detected",
  gl_error: 0,
  dpi_percent: 125,
  display_hz: 165,
  ram_gb: 16,
});
assert.equal(r.status, 200);
assert.equal(r.data.ok, true);
r = await call("POST", "/v1/telemetry/compat", {
  machine_hash: m2,
  app_version: "2.3.3",
  gpu_renderer: "NVIDIA GeForce RTX 3060/PCIe/SSE2",
  gl_version: "4.6.0 NVIDIA",
  compat_mode: "auto",
  effective_tier: "ultra",
  fallback_reason: "",
  gl_error: 0,
  dpi_percent: 100,
  display_hz: 144,
  ram_gb: 32,
});
assert.equal(r.status, 200);
r = await call("GET", "/v1/admin/overview", null, { Authorization: "Bearer test_admin_secret_999" });
assert.equal(r.status, 200);
assert.equal(r.data.gpu_health.total_devices, 2);
assert.equal(r.data.gpu_health.healthy_devices, 1);
assert.equal(r.data.gpu_health.fallback_devices, 1);
assert.equal(r.data.gpu_health.error_devices, 0);

console.log("ALL 19 HARDENED LICENSE-SERVER + GPU TELEMETRY TESTS PASSED 100%!");
