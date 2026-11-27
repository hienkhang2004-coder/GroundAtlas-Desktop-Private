-- GroundAtlas License Server — Security Hardening Migration (v2.3.1)
-- 1. Cryptographic 128-bit per-order poll_token (prevents 6-digit order enumeration & key theft)
ALTER TABLE orders ADD COLUMN poll_token TEXT NOT NULL DEFAULT '';

-- 2. Persistent Cloudflare D1 edge rate-limit buckets per IP & route
CREATE TABLE IF NOT EXISTS rate_limits (
  bucket TEXT PRIMARY KEY,
  count INTEGER NOT NULL,
  reset_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_rate_limits_reset ON rate_limits(reset_at);
