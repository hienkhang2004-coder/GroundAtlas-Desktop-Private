// Pure WebCrypto RFC 8032 Ed25519 + SHA-256 helpers (runs identically on Cloudflare Workers & Node 22)

const te = new TextEncoder();

export function toBase64Url(bytes: Uint8Array): string {
  let bin = "";
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

export function fromBase64Url(str: string): Uint8Array {
  const pad = str.length % 4 === 0 ? "" : "=".repeat(4 - (str.length % 4));
  const bin = atob(str.replace(/-/g, "+").replace(/_/g, "/") + pad);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

export function hexToBytes(hex: string): Uint8Array {
  const clean = hex.trim().replace(/^0x/i, "");
  const out = new Uint8Array(clean.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

export function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

export async function sha256Hex(text: string): Promise<string> {
  const buf = await crypto.subtle.digest("SHA-256", te.encode(text));
  return bytesToHex(new Uint8Array(buf));
}

export function timingSafeEqual(a: string, b: string): boolean {
  const ba = te.encode(a);
  const bb = te.encode(b);
  if (ba.length !== bb.length) return false;
  let diff = 0;
  for (let i = 0; i < ba.length; i++) diff |= ba[i] ^ bb[i];
  return diff === 0;
}

// Wrap a 32-byte raw Ed25519 seed into PKCS#8 DER for WebCrypto importKey
function seedToPkcs8(seed32: Uint8Array): Uint8Array {
  if (seed32.length !== 32) throw new Error("Ed25519 private seed must be 32 bytes");
  // ASN.1 prefix for Ed25519 PKCS#8 private key (16 bytes)
  const prefix = new Uint8Array([
    0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06,
    0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20,
  ]);
  const der = new Uint8Array(prefix.length + 32);
  der.set(prefix, 0);
  der.set(seed32, prefix.length);
  return der;
}

export interface LeasePayload {
  v: number;
  key: string;       // masked key GA2X-XXXX-****-XXXX
  tier: string;      // "pro_2x"
  major: number;     // 2
  seats: number;     // max_seats
  used: number;      // active seats count
  machine: string;   // 64-char hex machine_hash
  email: string;     // buyer email (display)
  issued: number;    // unix utc seconds
  exp: number;       // unix utc seconds (offline lease expiry)
}

// Produces compact signed token: "GA2L.<base64url(json)>.<base64url(ed25519_sig_over_payload_b64)>"
export async function signLeaseToken(payload: LeasePayload, privateSeedHex: string): Promise<string> {
  const seed = hexToBytes(privateSeedHex);
  const key = await crypto.subtle.importKey(
    "pkcs8",
    seedToPkcs8(seed),
    { name: "Ed25519" },
    false,
    ["sign"]
  );
  const payloadB64 = toBase64Url(te.encode(JSON.stringify(payload)));
  const sigBuf = await crypto.subtle.sign({ name: "Ed25519" }, key, te.encode(payloadB64));
  const sigB64 = toBase64Url(new Uint8Array(sigBuf));
  return `GA2L.${payloadB64}.${sigB64}`;
}

export async function verifyHmacSha256Hex(secret: string, message: string, expectedHex: string): Promise<boolean> {
  const key = await crypto.subtle.importKey(
    "raw",
    te.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const sig = new Uint8Array(await crypto.subtle.sign("HMAC", key, te.encode(message)));
  return timingSafeEqual(bytesToHex(sig), expectedHex.trim().toLowerCase());
}

const KEY_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"; // unambiguous characters (no 0/O/1/I/L)

export function generateLicenseKey(prefix = "GA2X"): string {
  const rand = new Uint8Array(12);
  crypto.getRandomValues(rand);
  let chars = "";
  for (let i = 0; i < 12; i++) chars += KEY_ALPHABET[rand[i] % KEY_ALPHABET.length];
  return `${prefix}-${chars.slice(0, 4)}-${chars.slice(4, 8)}-${chars.slice(8, 12)}`;
}

export function normalizeLicenseKey(raw: string): string {
  return raw.trim().toUpperCase().replace(/\s+/g, "");
}

export function maskLicenseKey(key: string): string {
  const parts = normalizeLicenseKey(key).split("-");
  if (parts.length === 4) return `${parts[0]}-${parts[1]}-****-${parts[3]}`;
  return key.slice(0, 4) + "-****-" + key.slice(-4);
}

export function generateOrderCode(): string {
  const rand = new Uint8Array(4);
  crypto.getRandomValues(rand);
  const num = ((rand[0] << 24) | (rand[1] << 16) | (rand[2] << 8) | rand[3]) >>> 0;
  return "GA" + String(100000 + (num % 900000));
}
