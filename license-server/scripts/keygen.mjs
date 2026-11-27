// Generates an RFC 8032 Ed25519 signing keypair for GroundAtlas Desktop v2.3.0 License Engine.
// Run: node scripts/keygen.mjs
import crypto from "node:crypto";

const { privateKey, publicKey } = crypto.generateKeyPairSync("ed25519");
const pkcs8 = privateKey.export({ type: "pkcs8", format: "der" });
const spki = publicKey.export({ type: "spki", format: "der" });

// Last 32 bytes of Ed25519 PKCS#8 DER = raw private seed; last 32 bytes of SPKI DER = raw public key
const privSeedHex = pkcs8.subarray(pkcs8.length - 32).toString("hex");
const pubKeyBytes = spki.subarray(spki.length - 32);
const pubKeyHex = pubKeyBytes.toString("hex");
const adminSecret = "ga_adm_" + crypto.randomBytes(24).toString("hex");
const webhookSecret = "ga_wh_" + crypto.randomBytes(20).toString("hex");

console.log("=== GROUNDATLAS ED25519 KEYPAIR (KEEP PRIVATE SEED SECRET!) ===");
console.log("ED25519_PRIVATE_SEED_HEX=" + privSeedHex);
console.log("ED25519_PUBLIC_KEY_HEX=" + pubKeyHex);
console.log("ADMIN_SECRET=" + adminSecret);
console.log("SEPAY_WEBHOOK_SECRET=" + webhookSecret);
console.log("\n--- Paste into C++ src/license.cpp (kDefaultPublicKey[32]) ---");
const cppBytes = Array.from(pubKeyBytes, (b) => "0x" + b.toString(16).padStart(2, "0")).join(", ");
console.log(`static constexpr uint8_t kDefaultPublicKey[32] = { ${cppBytes} };`);
console.log("\n--- Upload secrets to Cloudflare Worker ---");
console.log("npx wrangler secret put ED25519_PRIVATE_SEED_HEX");
console.log("npx wrangler secret put ADMIN_SECRET");
console.log("npx wrangler secret put SEPAY_WEBHOOK_SECRET");
