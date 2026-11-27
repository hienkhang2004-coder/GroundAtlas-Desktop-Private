# GroundAtlas Desktop v2.3.0 — Cloudflare Workers + D1 License Server

Backend quản lý bản quyền **Per-Seat (1 Seat = 1 Máy · Lifetime 2.x)**, tự động hóa VietQR (**SePay / PayOS**), đếm suất Early-Bird **thật**, và ưu đãi chào mừng **48 giờ thật** theo `Machine ID`. Chi phí vận hành: **\$0/tháng** trên Cloudflare Workers + D1 Free Tier.

## Triển khai trong 5 phút

1. **Cài dependencies & đăng nhập Cloudflare:**
   ```bash
   cd license-server
   npm install
   npx wrangler login
   ```
2. **Tạo cặp khóa Ed25519 của riêng bạn:**
   ```bash
   npm run keygen
   ```
   - Dán mảng `kDefaultPublicKey[32]` vào `src/license.cpp` (và build lại app Desktop).
   - Lưu `ED25519_PRIVATE_SEED_HEX`, `ADMIN_SECRET`, `SEPAY_WEBHOOK_SECRET` vào Cloudflare Secrets ở bước 4.
3. **Tạo D1 Database & chạy migration:**
   ```bash
   npx wrangler d1 create groundatlas-licenses
   # Dán database_id vào wrangler.toml
   npm run migrate:remote
   ```
4. **Đẩy Secrets & Deploy:**
   ```bash
   npx wrangler secret put ED25519_PRIVATE_SEED_HEX
   npx wrangler secret put ADMIN_SECRET
   npx wrangler secret put SEPAY_WEBHOOK_SECRET
   # (Tùy chọn) npx wrangler secret put PAYOS_CHECKSUM_KEY
   # (Tùy chọn) npx wrangler secret put RESEND_API_KEY
   npm run deploy
   ```
5. **Cấu hình số tài khoản VietQR**: sửa `VIETQR_BANK_BIN`, `VIETQR_ACCOUNT_NO`, `VIETQR_ACCOUNT_NAME` trong `wrangler.toml`, sau đó dán webhook URL `https://<worker-domain>/v1/webhook/sepay` (kèm header `Authorization: Apikey <SEPAY_WEBHOOK_SECRET>`) vào trang quản trị SePay.
6. **Vào Trang bán hàng & Quản trị**:
   - Trang mua hàng VietQR + thanh đếm suất thật: `https://<worker-domain>/`
   - Trang Admin tạo/khóa/reset Seat: `https://<worker-domain>/admin`

## Chạy kiểm thử tự động tại máy (không cần Cloudflare account)
```bash
npm test
```
