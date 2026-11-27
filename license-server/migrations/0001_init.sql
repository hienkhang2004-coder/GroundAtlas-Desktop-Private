-- GroundAtlas Desktop v2.3.0 Commercial Per-Seat License Schema (Cloudflare D1 / SQLite)

CREATE TABLE IF NOT EXISTS campaigns (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  sort_order INTEGER NOT NULL,
  seat_quota INTEGER NOT NULL,        -- -1 = unlimited (standard pricing)
  price_1_seat INTEGER NOT NULL,      -- VND
  price_3_seats INTEGER NOT NULL,
  price_5_seats INTEGER NOT NULL,
  price_10_seats INTEGER NOT NULL
);

INSERT OR IGNORE INTO campaigns (id, name, sort_order, seat_quota, price_1_seat, price_3_seats, price_5_seats, price_10_seats) VALUES
  ('batch_1',  'Đợt 1 · Early Bird (100 suất đầu)', 1, 100,  99000, 249000, 399000, 690000),
  ('batch_2',  'Đợt 2 · Ưu đãi sớm (200 suất)',     2, 200, 149000, 349000, 499000, 890000),
  ('standard', 'Giá niêm yết chuẩn (Lifetime 2.x)', 3,  -1, 199000, 399000, 599000, 990000);

CREATE TABLE IF NOT EXISTS licenses (
  id TEXT PRIMARY KEY,
  key_hash TEXT NOT NULL UNIQUE,
  key_masked TEXT NOT NULL,
  tier TEXT NOT NULL DEFAULT 'pro_2x',
  major_version INTEGER NOT NULL DEFAULT 2,
  max_seats INTEGER NOT NULL DEFAULT 1,
  swap_count INTEGER NOT NULL DEFAULT 0,
  swap_window_start INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'active', -- active | revoked
  buyer_email TEXT NOT NULL DEFAULT '',
  buyer_name TEXT NOT NULL DEFAULT '',
  order_code TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS activations (
  id TEXT PRIMARY KEY,
  license_id TEXT NOT NULL,
  machine_hash TEXT NOT NULL,
  machine_name TEXT NOT NULL DEFAULT '',
  app_version TEXT NOT NULL DEFAULT '',
  activated_at INTEGER NOT NULL,
  last_seen_at INTEGER NOT NULL,
  revoked_at INTEGER,
  FOREIGN KEY (license_id) REFERENCES licenses(id)
);

CREATE INDEX IF NOT EXISTS idx_activations_license ON activations(license_id, revoked_at);
CREATE INDEX IF NOT EXISTS idx_activations_machine ON activations(machine_hash, revoked_at);

CREATE TABLE IF NOT EXISTS machines (
  machine_hash TEXT PRIMARY KEY,
  first_seen_at INTEGER NOT NULL,
  welcome_offer_expires_at INTEGER NOT NULL,
  last_seen_at INTEGER NOT NULL,
  app_version TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS orders (
  order_code TEXT PRIMARY KEY,
  buyer_email TEXT NOT NULL,
  buyer_name TEXT NOT NULL DEFAULT '',
  machine_hash TEXT NOT NULL DEFAULT '',
  seats INTEGER NOT NULL DEFAULT 1,
  amount_vnd INTEGER NOT NULL,
  batch_id TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending', -- pending | paid | expired
  provider TEXT NOT NULL DEFAULT 'sepay', -- sepay | payos | manual
  provider_tx_id TEXT UNIQUE,
  issued_key TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL,
  paid_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status, created_at);
