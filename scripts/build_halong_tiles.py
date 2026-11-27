import json
import math
import os
import shutil
import time
import cv2
import numpy as np
import pymupdf
from PIL import Image

PDF_PATH = r"C:\Users\khang\Downloads\4. QH04. Phương án SDD CVDD 2025.5.20-Model.pdf"
OUT_TILES = r"C:\Users\khang\Documents\Codex\2026-10-24\https-groundatlas-groundatlas-workers-dev-https\outputs\groundatlas-desktop\data\planning-tiles\halong-cvdd"
VERIFY_DIR = r"C:\Users\khang\.gemini\antigravity\brain\0c3c9270-837a-478a-a51c-627fb83a79be\scratch\gcp"

R = 6378137.0
WORLD_M = 2.0 * math.pi * R


def sat_px_to_merc(x0, y0, z, sx, sy):
    n = 2**z
    mx = ((x0 + sx / 256.0) / n - 0.5) * WORLD_M
    my = (0.5 - (y0 + sy / 256.0) / n) * WORLD_M
    return mx, my


def merc_to_lonlat(mx, my):
    lon = math.degrees(mx / R)
    lat = math.degrees(2.0 * math.atan(math.exp(my / R)) - math.pi / 2.0)
    return lon, lat


def lonlat_to_tile_xy(lon, lat, z):
    n = 2**z
    x = int(math.floor((lon + 180.0) / 360.0 * n))
    rad = math.radians(lat)
    y = int(math.floor((1.0 - math.asinh(math.tan(rad)) / math.pi) * 0.5 * n))
    return max(0, min(n - 1, x)), max(0, min(n - 1, y))


def main():
    t0 = time.time()
    # 5 high-precision visually verified GCPs across the 4.8 km span (adjusted from v2 satellite overlay inspection)
    gcps_raw = [
        ("West_RB",       525.8,  596.3,  52250, 28865, 16, 395.0, 318.0),
        ("Lower_RB",     1265.2,  853.0, 104511, 57729, 17, 466.0, 696.0),
        ("Pier_NW_Bend", 1480.8, 1011.2, 104515, 57731, 17, 286.0, 374.0),
        ("Pier_SE_Join", 1490.2, 1028.0, 104515, 57731, 17, 344.0, 412.0),
        ("Pylon_HonGai", 1714.0,  891.5, 104517, 57729, 17, 306.0, 224.0),
    ]

    A, B = [], []
    for _, px, py, x0, y0, z, sx, sy in gcps_raw:
        mx, my = sat_px_to_merc(x0, y0, z, sx, sy)
        A.append([px, py, 1.0, 0.0])
        B.append(mx)
        A.append([-py, px, 0.0, 1.0])
        B.append(my)

    a, b, tx, ty = np.linalg.lstsq(np.array(A), np.array(B), rcond=None)[0]
    cos_lat = math.cos(math.radians(20.95))
    errs = []
    print(f"4-DOF Fit: a={a:.8f}, b={b:.8f}, tx={tx:.4f}, ty={ty:.4f}, rot={math.degrees(math.atan2(b, a)):.4f} deg")
    for name, px, py, x0, y0, z, sx, sy in gcps_raw:
        mx_t, my_t = sat_px_to_merc(x0, y0, z, sx, sy)
        dx = (a * px + b * py + tx - mx_t) * cos_lat
        dy = (b * px - a * py + ty - my_t) * cos_lat
        e = math.hypot(dx, dy)
        errs.append(e)
        print(f"  {name:14s}: dx={dx:+5.2f}m, dy={dy:+5.2f}m, err={e:4.2f}m")
    rms = math.sqrt(sum(e * e for e in errs) / len(errs))
    print(f"Final RMS across 4.8 km = {rms:.2f} m")

    # Exact drawing clip in PDF points (excludes far-left floating tilted legend box at x<415!)
    CLIP_X0, CLIP_Y0, CLIP_X1, CLIP_Y1 = 420.0, 520.0, 2365.0, 1215.0
    # Compute Mercator & WGS84 bounds of the 4 clipped PDF corners
    corners_pdf = [(CLIP_X0, CLIP_Y0), (CLIP_X1, CLIP_Y0), (CLIP_X1, CLIP_Y1), (CLIP_X0, CLIP_Y1)]
    corners_ll = [merc_to_lonlat(a * x + b * y + tx, b * x - a * y + ty) for x, y in corners_pdf]
    west = min(c[0] for c in corners_ll)
    east = max(c[0] for c in corners_ll)
    south = min(c[1] for c in corners_ll)
    north = max(c[1] for c in corners_ll)
    print(f"Geographic bounds: W={west:.6f}, S={south:.6f}, E={east:.6f}, N={north:.6f}")

    # Render the full vector drawing cleanly with alpha=True in 4 vertical bands at scale=6.4 (~12448 x 4448 px, ~0.55m/px!)
    PDF_SCALE = 6.4
    full_w = int(round((CLIP_X1 - CLIP_X0) * PDF_SCALE))
    full_h = int(round((CLIP_Y1 - CLIP_Y0) * PDF_SCALE))
    print(f"Rendering transparent vector PDF at {full_w} x {full_h} px (4 bands)...")
    src_rgba = np.zeros((full_h, full_w, 4), dtype=np.uint8)

    doc = pymupdf.open(PDF_PATH)
    page = doc[0]
    n_bands = 4
    band_pt = (CLIP_X1 - CLIP_X0) / n_bands
    for bi in range(n_bands):
        bx0 = CLIP_X0 + bi * band_pt
        bx1 = CLIP_X0 + (bi + 1) * band_pt
        pix = page.get_pixmap(
            matrix=pymupdf.Matrix(PDF_SCALE, PDF_SCALE),
            clip=pymupdf.Rect(bx0, CLIP_Y0, bx1, CLIP_Y1),
            alpha=True,
        )
        band = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width, 4)
        ux0 = int(round((bx0 - CLIP_X0) * PDF_SCALE))
        cw = min(pix.width, full_w - ux0)
        ch = min(pix.height, full_h)
        src_rgba[:ch, ux0 : ux0 + cw, :] = band[:ch, :cw, :]
        print(f"  Band {bi+1}/{n_bands}: {pix.width}x{pix.height} done ({time.time()-t0:.1f}s)")
    doc.close()

    # Clean up any residual pure-white opaque pixels if any (only 0.1%, e.g., white paper masks) while preserving parcel colors & crisp black outlines
    # Note: alpha=True already leaves 74%+ paper completely alpha=0; leave interior white text labels readable or strip bright opaque neutral white only if connected to background?
    # Actually alpha=True ALREADY stripped 100% of the white background! No aggressive color keying needed that could eat white parcel numbers/labels!

    # Prepare clean output directory
    if os.path.exists(OUT_TILES):
        shutil.rmtree(OUT_TILES)
    os.makedirs(OUT_TILES, exist_ok=True)

    # Generate MAX_ZOOM = 18 tiles directly from crisp 12448x4448 transparent RGBA via cv2.warpAffine
    MAX_Z = 18
    MIN_Z = 12
    n18 = 2**MAX_Z
    m_per_px18 = WORLD_M / (n18 * 256.0)
    tx0, ty0 = lonlat_to_tile_xy(west, north, MAX_Z)
    tx1, ty1 = lonlat_to_tile_xy(east, south, MAX_Z)
    print(f"Slicing Z{MAX_Z}: x={tx0}..{tx1} ({tx1-tx0+1}), y={ty0}..{ty1} ({ty1-ty0+1})")

    # Inverse transform helper: tile pixel (sx,sy) -> src_rgba pixel (u,v)
    # Forward: mx = a*(CLIP_X0 + u/S) + b*(CLIP_Y0 + v/S) + tx
    #          my = b*(CLIP_X0 + u/S) - a*(CLIP_Y0 + v/S) + ty
    # Forward matrix F (2x3) mapping (u,v) -> (mx, my):
    #   [ a/S,  b/S, a*CLIP_X0 + b*CLIP_Y0 + tx ]
    #   [ b/S, -a/S, b*CLIP_X0 - a*CLIP_Y0 + ty ]
    # Notice det = -(a^2+b^2)/S^2. Exact inverse mapping (mx,my) -> (u,v):
    det_ab = a * a + b * b
    c_mx = a * CLIP_X0 + b * CLIP_Y0 + tx
    c_my = b * CLIP_X0 - a * CLIP_Y0 + ty

    # Pre-multiply RGB by Alpha for artifact-free anti-aliased interpolation & mipmapping!
    src_f = src_rgba.astype(np.float32)
    a_norm = src_f[:, :, 3:4] / 255.0
    src_pm = np.empty_like(src_f)
    src_pm[:, :, :3] = src_f[:, :, :3] * a_norm
    src_pm[:, :, 3:4] = src_f[:, :, 3:4]
    del src_rgba, src_f

    tile_cache = {}  # (z, x, y) -> uint8 RGBA 256x256 (non-empty)
    saved_counts = {}
    total_bytes = 0

    def save_tile_png(z, tx_i, ty_i, rgba_u8):
        nonlocal total_bytes
        d = os.path.join(OUT_TILES, str(z), str(tx_i))
        os.makedirs(d, exist_ok=True)
        fp = os.path.join(d, f"{ty_i}.png")
        Image.fromarray(rgba_u8, "RGBA").save(fp, format="PNG", optimize=True, compress_level=9)
        total_bytes += os.path.getsize(fp)

    # Render Z18 in chunks of 4x4 tiles (1024x1024) for lightning speed!
    BLOCK = 4
    z18_saved = 0
    for by in range(ty0, ty1 + 1, BLOCK):
        for bx in range(tx0, tx1 + 1, BLOCK):
            bw = min(BLOCK, tx1 - bx + 1) * 256
            bh = min(BLOCK, ty1 - by + 1) * 256
            tile_x0_m = (bx / n18 - 0.5) * WORLD_M
            tile_y0_m = (0.5 - by / n18) * WORLD_M

            # Forward matrix mapping src_pm (u,v) -> block pixel (sx,sy):
            M_fwd = np.array(
                [
                    [a / (PDF_SCALE * m_per_px18), b / (PDF_SCALE * m_per_px18), (c_mx - tile_x0_m) / m_per_px18],
                    [-b / (PDF_SCALE * m_per_px18), a / (PDF_SCALE * m_per_px18), (tile_y0_m - c_my) / m_per_px18],
                ],
                dtype=np.float64,
            )
            block_pm = cv2.warpAffine(
                src_pm,
                M_fwd,
                (bw, bh),
                flags=cv2.INTER_AREA,
                borderMode=cv2.BORDER_CONSTANT,
                borderValue=(0.0, 0.0, 0.0, 0.0),
            )
            if block_pm[:, :, 3].max() < 6.0:
                continue

            # Un-premultiply cleanly
            ba = block_pm[:, :, 3:4]
            safe_a = np.maximum(ba, 1e-4)
            out_rgb = np.where(ba > 1.0, block_pm[:, :, :3] * (255.0 / safe_a), 0.0)
            block_u8 = np.zeros((bh, bw, 4), dtype=np.uint8)
            block_u8[:, :, :3] = np.clip(out_rgb, 0, 255).astype(np.uint8)
            block_u8[:, :, 3] = np.clip(ba[:, :, 0], 0, 255).astype(np.uint8)

            for dy in range(bh // 256):
                for dx in range(bw // 256):
                    sub = block_u8[dy * 256 : (dy + 1) * 256, dx * 256 : (dx + 1) * 256, :]
                    if sub[:, :, 3].max() < 6:
                        continue
                    tx_i, ty_i = bx + dx, by + dy
                    save_tile_png(MAX_Z, tx_i, ty_i, sub)
                    tile_cache[(MAX_Z, tx_i, ty_i)] = sub
                    z18_saved += 1

    saved_counts[MAX_Z] = z18_saved
    print(f"  Z{MAX_Z}: {z18_saved} non-empty tiles saved ({time.time()-t0:.1f}s)")
    del src_pm

    # Hierarchical 2x2 premultiplied mipmap pyramid downsampling: Z17 -> Z12 (ZERO moiré, <2 seconds!)
    for z in range(MAX_Z - 1, MIN_Z - 1, -1):
        child_z = z + 1
        parents = set((cx // 2, cy // 2) for (cz, cx, cy) in tile_cache.keys() if cz == child_z)
        cnt = 0
        for px_i, py_i in sorted(parents):
            mosaic = np.zeros((512, 512, 4), dtype=np.float32)
            for oy in (0, 1):
                for ox in (0, 1):
                    ch = tile_cache.get((child_z, px_i * 2 + ox, py_i * 2 + oy))
                    if ch is not None:
                        cf = ch.astype(np.float32)
                        ca = cf[:, :, 3:4] / 255.0
                        mosaic[oy * 256 : (oy + 1) * 256, ox * 256 : (ox + 1) * 256, :3] = cf[:, :, :3] * ca
                        mosaic[oy * 256 : (oy + 1) * 256, ox * 256 : (ox + 1) * 256, 3:4] = cf[:, :, 3:4]
            down = cv2.resize(mosaic, (256, 256), interpolation=cv2.INTER_AREA)
            da = down[:, :, 3:4]
            if da.max() < 4.0:
                continue
            safe_a = np.maximum(da, 1e-4)
            rgb = np.where(da > 1.0, down[:, :, :3] * (255.0 / safe_a), 0.0)
            tile_u8 = np.zeros((256, 256, 4), dtype=np.uint8)
            tile_u8[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
            tile_u8[:, :, 3] = np.clip(da[:, :, 0], 0, 255).astype(np.uint8)
            save_tile_png(z, px_i, py_i, tile_u8)
            tile_cache[(z, px_i, py_i)] = tile_u8
            cnt += 1
        # Free child level from RAM once parent built
        for k in [k for k in tile_cache if k[0] == child_z]:
            del tile_cache[k]
        saved_counts[z] = cnt
        print(f"  Z{z}: {cnt} non-empty tiles saved ({time.time()-t0:.1f}s)")

    # Write clean tilejson / metadata.json alongside tiles for self-documenting bounds & center
    # Exact center of user's red-circled magenta plot 1 (Cảng tàu QT Hạ Long): PDF (1358, 845)
    port_lon, port_lat = merc_to_lonlat(a * 1358.0 + b * 845.0 + tx, b * 1358.0 - a * 845.0 + ty)
    meta = {
        "id": "halong_cvdd",
        "name_vi": "QH Hạ Long · CV Đại Dương & Cảng tàu QT (2025)",
        "name_en": "Ha Long · Ocean Park & Int'l Cruise Port Plan (2025)",
        "source_pdf": "4. QH04. Phương án SDD CVDD 2025.5.20-Model.pdf",
        "minzoom": MIN_Z,
        "maxzoom": MAX_Z,
        "tileSize": 256,
        "bounds": [round(west, 6), round(south, 6), round(east, 6), round(north, 6)],
        "focus_center": [round(port_lon, 6), round(port_lat, 6)],
        "rms_error_m": round(rms, 2),
        "tiles_per_zoom": saved_counts,
        "total_tiles": sum(saved_counts.values()),
        "total_bytes": total_bytes,
    }
    with open(os.path.join(OUT_TILES, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)
    print("DONE!", json.dumps(meta, indent=2, ensure_ascii=False))
    print(f"Total size: {total_bytes/1024/1024:.2f} MB in {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()
