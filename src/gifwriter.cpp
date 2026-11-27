#include "gifwriter.h"
#include <QSaveFile>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ga {
namespace {

// Build a clean 256-color architectural palette:
// 216-color 6x6x6 RGB web-safe cube + 40 smooth warm/neutral grays for crisp architectural shading
void buildArchitecturalPalette(std::array<uint8_t, 768> &pal) {
 int idx = 0;
 for (int r = 0; r < 6; ++r) {
  for (int g = 0; g < 6; ++g) {
   for (int b = 0; b < 6; ++b) {
    pal[idx * 3 + 0] = uint8_t(r * 51);
    pal[idx * 3 + 1] = uint8_t(g * 51);
    pal[idx * 3 + 2] = uint8_t(b * 51);
    ++idx;
   }
  }
 }
 for (int i = 0; i < 40; ++i) {
  const uint8_t v = uint8_t(std::clamp(int(std::round((i + 0.5) * 255.0 / 40.0)), 0, 255));
  pal[idx * 3 + 0] = v;
  pal[idx * 3 + 1] = v;
  pal[idx * 3 + 2] = v;
  ++idx;
 }
}

// Fast Bayer 4x4 ordered dithering to 256-color architectural palette (zero banding on skies & shadows!)
inline uint8_t quantizePixel(int r, int g, int b, int x, int y, const std::array<uint8_t, 768> &pal) {
 static constexpr int kBayer4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
 };
 const int d = (kBayer4[y & 3][x & 3] - 8) * 2;
 const int dr = std::clamp(r + d, 0, 255);
 const int dg = std::clamp(g + d, 0, 255);
 const int db = std::clamp(b + d, 0, 255);

 const int ri = (dr * 5 + 127) / 255;
 const int gi = (dg * 5 + 127) / 255;
 const int bi = (db * 5 + 127) / 255;
 const int cubeIdx = ri * 36 + gi * 6 + bi;
 const int cr = pal[cubeIdx * 3 + 0] - dr;
 const int cg = pal[cubeIdx * 3 + 1] - dg;
 const int cb = pal[cubeIdx * 3 + 2] - db;
 const int cubeDist = cr * cr + cg * cg + cb * cb;

 // Check neutral gray ramp (entries 216..255) for smooth clay buildings & shadows
 const int lum = (dr * 77 + dg * 150 + db * 29) >> 8;
 const int graySlot = std::clamp((lum * 39 + 127) / 255, 0, 39);
 const int grayIdx = 216 + graySlot;
 const int gr = pal[grayIdx * 3 + 0] - dr;
 const int gg = pal[grayIdx * 3 + 1] - dg;
 const int gb = pal[grayIdx * 3 + 2] - db;
 const int grayDist = gr * gr + gg * gg + gb * gb;
 return uint8_t(grayDist < cubeDist ? grayIdx : cubeIdx);
}

// Standard GIF LZW variable-width bit-stream encoder (minCodeSize = 8)
void encodeLzw(const std::vector<uint8_t> &indices, QByteArray &out) {
 constexpr int kMinCode = 8;
 constexpr int kClear = 256;
 constexpr int kEoi = 257;
 out.append(char(kMinCode));

 uint8_t subBlock[255];
 int subLen = 0;
 uint32_t bitBuf = 0;
 int bitCount = 0;

 auto flushBlock = [&] {
  if (subLen > 0) {
   out.append(char(subLen));
   out.append(reinterpret_cast<const char *>(subBlock), subLen);
   subLen = 0;
  }
 };
 auto emitCode = [&](int code, int bits) {
  bitBuf |= (uint32_t(code) << bitCount);
  bitCount += bits;
  while (bitCount >= 8) {
   subBlock[subLen++] = uint8_t(bitBuf & 0xFF);
   bitBuf >>= 8;
   bitCount -= 8;
   if (subLen == 255) flushBlock();
  }
 };

 // Hash table for (prefix, byte) -> code (size 8192 open addressing, super fast!)
 constexpr int kHashSize = 8192;
 std::vector<int32_t> keys(kHashSize, -1);
 std::vector<int16_t> vals(kHashSize, -1);

 int nextCode = 258;
 int codeSize = 9;
 auto resetDict = [&] {
  std::fill(keys.begin(), keys.end(), -1);
  nextCode = 258;
  codeSize = 9;
 };

 emitCode(kClear, codeSize);
 if (indices.empty()) {
  emitCode(kEoi, codeSize);
 } else {
  int prefix = indices[0];
  for (size_t i = 1; i < indices.size(); ++i) {
   const int sym = indices[i];
   const int32_t key = (prefix << 8) | sym;
   int slot = ((prefix << 5) ^ sym ^ (prefix >> 3)) & (kHashSize - 1);
   int found = -1;
   while (keys[slot] != -1) {
    if (keys[slot] == key) { found = vals[slot]; break; }
    slot = (slot + 1) & (kHashSize - 1);
   }
   if (found != -1) {
    prefix = found;
   } else {
    emitCode(prefix, codeSize);
    if (nextCode < 4096) {
     keys[slot] = key;
     vals[slot] = int16_t(nextCode++);
     if (nextCode > (1 << codeSize) && codeSize < 12) ++codeSize;
    } else {
     emitCode(kClear, codeSize);
     resetDict();
    }
    prefix = sym;
   }
  }
  emitCode(prefix, codeSize);
  emitCode(kEoi, codeSize);
 }

 if (bitCount > 0) {
  subBlock[subLen++] = uint8_t(bitBuf & 0xFF);
  if (subLen == 255) flushBlock();
 }
 flushBlock();
 out.append('\0'); // Block terminator
}

inline void putU16(QByteArray &out, uint16_t v) {
 out.append(char(v & 0xFF));
 out.append(char((v >> 8) & 0xFF));
}

} // namespace

bool writeAnimatedGif(const QString &filePath, const QVector<QImage> &frames, int delayCentiseconds) {
 if (frames.isEmpty()) return false;
 const int w = frames.first().width();
 const int h = frames.first().height();
 if (w <= 0 || h <= 0 || w > 65535 || h > 65535) return false;
 const uint16_t delay = uint16_t(std::clamp(delayCentiseconds, 2, 500));

 std::array<uint8_t, 768> pal{};
 buildArchitecturalPalette(pal);

 QByteArray data;
 data.reserve(frames.size() * w * h / 3);

 // 1. GIF89a Header + Logical Screen Descriptor
 data.append("GIF89a", 6);
 putU16(data, uint16_t(w));
 putU16(data, uint16_t(h));
 data.append(char(0xF7)); // Global Color Table flag=1, color res=7 (8-bit), sort=0, size=7 (256 colors)
 data.append(char(0x00)); // Background color index
 data.append(char(0x00)); // Pixel aspect ratio

 // 2. Global Color Table (256 * 3 = 768 bytes)
 data.append(reinterpret_cast<const char *>(pal.data()), 768);

 // 3. Netscape 2.0 Application Extension (infinite looping)
 const unsigned char netscapeExt[] = {
  0x21, 0xFF, 0x0B,
  'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
  0x03, 0x01, 0x00, 0x00, 0x00
 };
 data.append(reinterpret_cast<const char *>(netscapeExt), sizeof(netscapeExt));

 // 4. Encode each frame
 std::vector<uint8_t> idxBuf(size_t(w) * size_t(h));
 for (const auto &raw : frames) {
  QImage img = (raw.width() == w && raw.height() == h)
   ? raw.convertToFormat(QImage::Format_RGB32)
   : raw.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
  for (int y = 0; y < h; ++y) {
   const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
   uint8_t *dst = idxBuf.data() + size_t(y) * size_t(w);
   for (int x = 0; x < w; ++x) {
    const QRgb px = line[x];
    dst[x] = quantizePixel(qRed(px), qGreen(px), qBlue(px), x, y, pal);
   }
  }

  // Graphic Control Extension
  data.append(char(0x21));
  data.append(char(0xF9));
  data.append(char(0x04));
  data.append(char(0x04)); // Disposal method 1 (keep)
  putU16(data, delay);
  data.append(char(0x00)); // No transparent color index
  data.append(char(0x00));

  // Image Descriptor
  data.append(char(0x2C));
  putU16(data, 0);
  putU16(data, 0);
  putU16(data, uint16_t(w));
  putU16(data, uint16_t(h));
  data.append(char(0x00)); // No local color table

  encodeLzw(idxBuf, data);
 }

 // 5. GIF Trailer
 data.append(char(0x3B));

 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly)) return false;
 return out.write(data) == data.size() && out.commit();
}

} // namespace ga
