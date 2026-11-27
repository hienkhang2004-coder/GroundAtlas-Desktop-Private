-- GroundAtlas License Server — Anonymous GPU Compatibility Telemetry (v2.3.3)
-- Stores zero personal data: only hashed machine fingerprint + GPU renderer + OpenGL error / fallback state
CREATE TABLE IF NOT EXISTS gpu_telemetry (
  machine_hash TEXT PRIMARY KEY,
  app_version TEXT NOT NULL,
  gpu_renderer TEXT NOT NULL,
  gl_version TEXT NOT NULL DEFAULT '',
  compat_mode TEXT NOT NULL DEFAULT 'auto',
  effective_tier TEXT NOT NULL DEFAULT 'medium',
  fallback_reason TEXT NOT NULL DEFAULT '',
  gl_error INTEGER NOT NULL DEFAULT 0,
  dpi_percent INTEGER NOT NULL DEFAULT 100,
  display_hz INTEGER NOT NULL DEFAULT 60,
  ram_gb INTEGER NOT NULL DEFAULT 8,
  updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_gpu_telemetry_updated ON gpu_telemetry(updated_at DESC);
