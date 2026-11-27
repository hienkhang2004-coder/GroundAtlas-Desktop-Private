#!/usr/bin/env node
// GroundAtlas Desktop 2.x — Offline Admin / Founder / VIP Ed25519 Key Signer
// Reads ED25519_PRIVATE_SEED_HEX from license-server/.dev.vars (never committed)
// Usage:
//   node --experimental-strip-types scripts/issue-admin-key.mjs --machine <64-hex-hash> [--email admin@groundatlas.vn] [--days 3650] [--label GA2X-ADMN-****-V230] [--tier admin_2x]
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { signLeaseToken } from "../src/crypto.ts";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const devVarsPath = path.resolve(__dirname, "..", ".dev.vars");

function parseArgs(argv) {
  const out = {};
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith("--") && i + 1 < argv.length) {
      out[a.slice(2)] = argv[++i];
    }
  }
  return out;
}

function loadDevVars(file) {
  if (!fs.existsSync(file)) {
    throw new Error(`Missing ${file}. Run scripts/keygen.mjs first.`);
  }
  const map = {};
  for (const raw of fs.readFileSync(file, "utf8").split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith("#")) continue;
    const eq = line.indexOf("=");
    if (eq > 0) map[line.slice(0, eq).trim()] = line.slice(eq + 1).trim();
  }
  return map;
}

const args = parseArgs(process.argv);
const machine = (args.machine || "").trim().toLowerCase();
if (!/^[a-f0-9]{64}$/.test(machine)) {
  console.error("Error: --machine must be a 64-char hex machine_hash (run GroundAtlas.exe --print-machine-id or click 'Chép mã máy' in License dialog).");
  process.exit(1);
}

const vars = loadDevVars(devVarsPath);
const seedHex = vars.ED25519_PRIVATE_SEED_HEX;
if (!seedHex || seedHex.length !== 64) {
  console.error("Error: ED25519_PRIVATE_SEED_HEX missing or invalid in .dev.vars");
  process.exit(1);
}

const days = Math.max(1, Number(args.days || 3650));
const tier = (args.tier || "admin_2x").trim();
const email = (args.email || "founder@groundatlas.vn").trim();
const label = (args.label || "GA2X-ADMN-****-V230").trim();
const seats = Math.max(1, Number(args.seats || 1));
const now = Math.floor(Date.now() / 1000);

const token = await signLeaseToken({
  v: 1,
  key: label,
  tier,
  major: 2,
  seats,
  used: 1,
  machine,
  email,
  issued: now,
  exp: now + days * 86400,
}, seedHex);

console.log(token);
