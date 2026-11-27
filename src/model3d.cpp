#include "model3d.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QQuaternion>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ga {
namespace {

constexpr int kMaxModelTriangles = 250000;
constexpr double kInchToMetre = 0.0254;

QVector3D safeTriNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c) {
 QVector3D n = QVector3D::crossProduct(b - a, c - a);
 if (n.lengthSquared() < 1e-12f) return {0.0f, 0.0f, 1.0f};
 return n.normalized();
}

void pushTri(QVector<Vertex> &out,
             const QVector3D &a, const QVector3D &na,
             const QVector3D &b, const QVector3D &nb,
             const QVector3D &c, const QVector3D &nc,
             const QColor &col) {
 if (out.size() / 3 >= kMaxModelTriangles) return;
 const float r = float(col.redF()), g = float(col.greenF()), bl = float(col.blueF()), al = float(col.alphaF());
 out.append({a.x(), a.y(), a.z(), na.x(), na.y(), na.z(), r, g, bl, al, 0.0f});
 out.append({b.x(), b.y(), b.z(), nb.x(), nb.y(), nb.z(), r, g, bl, al, 0.0f});
 out.append({c.x(), c.y(), c.z(), nc.x(), nc.y(), nc.z(), r, g, bl, al, 0.0f});
}

void normalizeModel(ImportedModel3D &m) {
 if (m.rawTriangles.isEmpty()) return;
 float minX = std::numeric_limits<float>::max(), minY = minX, minZ = minX;
 float maxX = std::numeric_limits<float>::lowest(), maxY = maxX, maxZ = maxX;
 for (const auto &v : m.rawTriangles) {
  if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) continue;
  minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
  minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
  minZ = std::min(minZ, v.z); maxZ = std::max(maxZ, v.z);
 }
 if (minX > maxX) return;
 const float cx = (minX + maxX) * 0.5f;
 const float cy = (minY + maxY) * 0.5f;
 for (auto &v : m.rawTriangles) {
  v.x -= cx;
  v.y -= cy;
  v.z -= minZ;
 }
 m.rawSizeM = {std::max(0.01f, maxX - minX), std::max(0.01f, maxY - minY), std::max(0.01f, maxZ - minZ)};
 m.rawFootprint2D = computeModelFootprint2D(m.rawTriangles);
}

// ── 1. Wavefront OBJ + MTL Loader (SketchUp Pro / Rhino / Revit / Blender) ──

QHash<QString, QColor> parseMtlFile(const QString &mtlPath) {
 QHash<QString, QColor> mats;
 QFile f(mtlPath);
 if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return mats;
 QString current;
 QColor col(232, 226, 216, 255);
 double alpha = 1.0;
 auto flush = [&]() {
  if (!current.isEmpty()) {
   col.setAlphaF(float(std::clamp(alpha, 0.08, 1.0)));
   mats[current] = col;
  }
 };
 QTextStream in(&f);
 while (!in.atEnd()) {
  const QString line = in.readLine().trimmed();
  if (line.isEmpty() || line.startsWith('#')) continue;
  const auto parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  if (parts.isEmpty()) continue;
  const QString kw = parts[0].toLower();
  if (kw == "newmtl" && parts.size() >= 2) {
   flush();
   current = line.mid(line.indexOf(parts[1])).trimmed();
   col = QColor(232, 226, 216, 255);
   alpha = 1.0;
  } else if (kw == "kd" && parts.size() >= 4) {
   col.setRgbF(float(std::clamp(parts[1].toDouble(), 0.0, 1.0)),
               float(std::clamp(parts[2].toDouble(), 0.0, 1.0)),
               float(std::clamp(parts[3].toDouble(), 0.0, 1.0)));
  } else if (kw == "d" && parts.size() >= 2) {
   alpha = parts[1].toDouble();
  } else if (kw == "tr" && parts.size() >= 2) {
   alpha = 1.0 - parts[1].toDouble();
  }
 }
 flush();
 return mats;
}

bool loadObjModel(const QString &path, ImportedModel3D &out, QString *err) {
 QFile f(path);
 if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
  if (err) *err = "Cannot open OBJ file.";
  return false;
 }
 QVector<QVector3D> positions;
 QVector<QVector3D> normals;
 QHash<QString, QColor> materials;
 QColor currentColor(234, 228, 218, 255);
 const QDir baseDir = QFileInfo(path).absoluteDir();
 bool sawSketchUpHeader = false;
 bool sawGroundAtlasHeader = false;

 QTextStream in(&f);
 while (!in.atEnd()) {
  const QString line = in.readLine().trimmed();
  if (line.isEmpty()) continue;
  if (line.startsWith('#')) {
   if (line.contains("SketchUp", Qt::CaseInsensitive)) sawSketchUpHeader = true;
   if (line.contains("GroundAtlas", Qt::CaseInsensitive)) sawGroundAtlasHeader = true;
   continue;
  }
  const auto parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  if (parts.isEmpty()) continue;
  const QString &kw = parts[0];
  if (kw == "v" && parts.size() >= 4) {
   positions.append({parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()});
  } else if (kw == "vn" && parts.size() >= 4) {
   normals.append({parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()});
  } else if (kw == "mtllib" && parts.size() >= 2) {
   const QString rel = line.mid(6).trimmed();
   const auto loaded = parseMtlFile(baseDir.filePath(rel));
   for (auto it = loaded.cbegin(); it != loaded.cend(); ++it) materials[it.key()] = it.value();
  } else if (kw == "usemtl" && parts.size() >= 2) {
   const QString name = line.mid(6).trimmed();
   currentColor = materials.value(name, QColor(234, 228, 218, 255));
  } else if (kw == "f" && parts.size() >= 4) {
   struct Idx { int v = -1; int n = -1; };
   QVector<Idx> poly;
   poly.reserve(parts.size() - 1);
   for (int i = 1; i < parts.size(); ++i) {
    const auto tok = parts[i].split('/');
    int vi = tok[0].toInt();
    vi = (vi < 0) ? (positions.size() + vi) : (vi - 1);
    int ni = -1;
    if (tok.size() >= 3 && !tok[2].isEmpty()) {
     ni = tok[2].toInt();
     ni = (ni < 0) ? (normals.size() + ni) : (ni - 1);
    }
    if (vi >= 0 && vi < positions.size()) poly.append({vi, ni});
   }
   if (poly.size() < 3) continue;
   for (int i = 1; i + 1 < poly.size(); ++i) {
    const QVector3D &p0 = positions[poly[0].v];
    const QVector3D &p1 = positions[poly[i].v];
    const QVector3D &p2 = positions[poly[i + 1].v];
    const QVector3D fn = safeTriNormal(p0, p1, p2);
    const QVector3D n0 = (poly[0].n >= 0 && poly[0].n < normals.size()) ? normals[poly[0].n].normalized() : fn;
    const QVector3D n1 = (poly[i].n >= 0 && poly[i].n < normals.size()) ? normals[poly[i].n].normalized() : fn;
    const QVector3D n2 = (poly[i + 1].n >= 0 && poly[i + 1].n < normals.size()) ? normals[poly[i + 1].n].normalized() : fn;
    pushTri(out.rawTriangles, p0, n0, p1, n1, p2, n2, currentColor);
   }
  }
 }
 if (out.rawTriangles.isEmpty()) {
  if (err) *err = "OBJ file contains no valid triangles.";
  return false;
 }
 // SketchUp exports OBJ with Y-up by default; auto-orient upright!
 if (sawSketchUpHeader && !sawGroundAtlasHeader) out.flipYUp = true;
 return true;
}

// ── 2. STL Binary & ASCII Loader ──

bool loadStlModel(const QString &path, ImportedModel3D &out, QString *err) {
 QFile f(path);
 if (!f.open(QIODevice::ReadOnly)) {
  if (err) *err = "Cannot open STL file.";
  return false;
 }
 const QByteArray raw = f.readAll();
 if (raw.size() < 15) {
  if (err) *err = "STL file is too small.";
  return false;
 }
 const QColor col(234, 228, 218, 255);

 // Check binary STL exact size: 80-byte header + 4-byte uint32 count + count * 50
 bool isBinary = false;
 uint32_t binCount = 0;
 if (raw.size() >= 84) {
  std::memcpy(&binCount, raw.constData() + 80, 4);
  binCount = qFromLittleEndian(binCount);
  if (qint64(84) + qint64(binCount) * 50 == raw.size()) isBinary = true;
 }

 if (isBinary) {
  const int nTris = int(std::min<uint32_t>(binCount, uint32_t(kMaxModelTriangles)));
  out.rawTriangles.reserve(nTris * 3);
  const char *p = raw.constData() + 84;
  for (int i = 0; i < nTris; ++i, p += 50) {
   float vals[12];
   std::memcpy(vals, p, 48);
   const QVector3D a(vals[3], vals[4], vals[5]);
   const QVector3D b(vals[6], vals[7], vals[8]);
   const QVector3D c(vals[9], vals[10], vals[11]);
   QVector3D n(vals[0], vals[1], vals[2]);
   if (n.lengthSquared() < 1e-8f) n = safeTriNormal(a, b, c);
   else n.normalize();
   pushTri(out.rawTriangles, a, n, b, n, c, n, col);
  }
 } else {
  QTextStream in(raw);
  QVector<QVector3D> triVerts;
  QVector3D curNormal(0, 0, 1);
  while (!in.atEnd()) {
   const QString line = in.readLine().trimmed();
   if (line.startsWith("facet normal", Qt::CaseInsensitive)) {
    const auto p = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (p.size() >= 5) curNormal = QVector3D(p[2].toFloat(), p[3].toFloat(), p[4].toFloat());
    triVerts.clear();
   } else if (line.startsWith("vertex", Qt::CaseInsensitive)) {
    const auto p = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (p.size() >= 4) triVerts.append({p[1].toFloat(), p[2].toFloat(), p[3].toFloat()});
   } else if (line.startsWith("endfacet", Qt::CaseInsensitive) && triVerts.size() >= 3) {
    QVector3D n = curNormal.lengthSquared() > 1e-8f ? curNormal.normalized() : safeTriNormal(triVerts[0], triVerts[1], triVerts[2]);
    for (int i = 1; i + 1 < triVerts.size(); ++i) {
     pushTri(out.rawTriangles, triVerts[0], n, triVerts[i], n, triVerts[i + 1], n, col);
    }
    triVerts.clear();
   }
  }
 }
 if (out.rawTriangles.isEmpty()) {
  if (err) *err = "STL file contains no triangles.";
  return false;
 }
 return true;
}

// ── 3. COLLADA (.dae) Loader (SketchUp Free/Make/Pro #1 Native Export Format!) ──

QVector<double> parseDoubleList(const QString &text) {
 QVector<double> vals;
 const QByteArray utf = text.toLatin1();
 const char *s = utf.constData();
 const char *end = s + utf.size();
 while (s < end) {
  while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) ++s;
  if (s >= end) break;
  char *next = nullptr;
  double v = std::strtod(s, &next);
  if (next == s) { ++s; continue; }
  vals.append(v);
  s = next;
 }
 return vals;
}

QVector<int> parseIntList(const QString &text) {
 QVector<int> vals;
 const QByteArray utf = text.toLatin1();
 const char *s = utf.constData();
 const char *end = s + utf.size();
 while (s < end) {
  while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) ++s;
  if (s >= end) break;
  char *next = nullptr;
  long v = std::strtol(s, &next, 10);
  if (next == s) { ++s; continue; }
  vals.append(int(v));
  s = next;
 }
 return vals;
}

QString stripHash(QString s) {
 if (s.startsWith('#')) s.remove(0, 1);
 return s;
}

struct DaePrim {
 QString materialSymbol;
 QVector<QVector3D> pos;
 QVector<QVector3D> norm;
};

struct DaeGeo {
 QVector<DaePrim> prims;
};

struct DaeInstanceGeo {
 QString url;
 QHash<QString, QString> matBind; // symbol -> target mat id
};

struct DaeNode {
 QString id;
 QMatrix4x4 local;
 QVector<DaeInstanceGeo> geos;
 QStringList instanceNodes;
 QVector<DaeNode> children;
};

DaeNode parseDaeNode(QXmlStreamReader &xml) {
 DaeNode node;
 node.id = xml.attributes().value("id").toString();
 node.local.setToIdentity();

 while (!xml.atEnd()) {
  const auto tok = xml.readNext();
  if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("node")) break;
  if (tok != QXmlStreamReader::StartElement) continue;
  const auto tag = xml.name();
  if (tag == QLatin1String("matrix")) {
   const auto vals = parseDoubleList(xml.readElementText());
   if (vals.size() >= 16) {
    QMatrix4x4 m;
    for (int r = 0; r < 4; ++r)
     for (int c = 0; c < 4; ++c) m(r, c) = float(vals[r * 4 + c]);
    node.local = node.local * m;
   }
  } else if (tag == QLatin1String("translate")) {
   const auto vals = parseDoubleList(xml.readElementText());
   if (vals.size() >= 3) node.local.translate(float(vals[0]), float(vals[1]), float(vals[2]));
  } else if (tag == QLatin1String("rotate")) {
   const auto vals = parseDoubleList(xml.readElementText());
   if (vals.size() >= 4) node.local.rotate(float(vals[3]), float(vals[0]), float(vals[1]), float(vals[2]));
  } else if (tag == QLatin1String("scale")) {
   const auto vals = parseDoubleList(xml.readElementText());
   if (vals.size() >= 3) node.local.scale(float(vals[0]), float(vals[1]), float(vals[2]));
  } else if (tag == QLatin1String("instance_node")) {
   const QString u = stripHash(xml.attributes().value("url").toString());
   if (!u.isEmpty()) node.instanceNodes.append(u);
   xml.skipCurrentElement();
  } else if (tag == QLatin1String("instance_geometry")) {
   DaeInstanceGeo ig;
   ig.url = stripHash(xml.attributes().value("url").toString());
   while (!xml.atEnd()) {
    const auto itok = xml.readNext();
    if (itok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("instance_geometry")) break;
    if (itok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("instance_material")) {
     const QString sym = xml.attributes().value("symbol").toString();
     const QString tgt = stripHash(xml.attributes().value("target").toString());
     if (!sym.isEmpty() && !tgt.isEmpty()) ig.matBind[sym] = tgt;
    }
   }
   if (!ig.url.isEmpty()) node.geos.append(ig);
  } else if (tag == QLatin1String("node")) {
   node.children.append(parseDaeNode(xml));
  } else {
   xml.skipCurrentElement();
  }
 }
 return node;
}

bool loadDaeModel(const QString &path, ImportedModel3D &out, QString *err) {
 QFile f(path);
 if (!f.open(QIODevice::ReadOnly)) {
  if (err) *err = "Cannot open COLLADA (.dae) file.";
  return false;
 }
 QXmlStreamReader xml(&f);
 double unitMeter = 1.0;
 QString upAxis = "Z_UP";

 QHash<QString, QColor> effectColors;
 QHash<QString, QString> matToEffect;
 QHash<QString, DaeGeo> geometries;
 QHash<QString, DaeNode> libraryNodes;
 QVector<DaeNode> sceneRoots;

 while (!xml.atEnd()) {
  if (xml.readNext() != QXmlStreamReader::StartElement) continue;
  const auto tag = xml.name();
  if (tag == QLatin1String("unit")) {
   const double m = xml.attributes().value("meter").toDouble();
   if (m > 0.0) unitMeter = m;
  } else if (tag == QLatin1String("up_axis")) {
   upAxis = xml.readElementText().trimmed().toUpper();
  } else if (tag == QLatin1String("effect")) {
   const QString effId = xml.attributes().value("id").toString();
   QColor col(234, 228, 218, 255);
   bool inDiffuse = false;
   bool inTransparency = false;
   double transparencyVal = -1.0;
   while (!xml.atEnd()) {
    const auto tok = xml.readNext();
    if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("effect")) break;
    if (tok == QXmlStreamReader::StartElement) {
     if (xml.name() == QLatin1String("diffuse")) inDiffuse = true;
     else if (xml.name() == QLatin1String("transparency")) inTransparency = true;
     else if (inDiffuse && xml.name() == QLatin1String("color")) {
      const auto vals = parseDoubleList(xml.readElementText());
      if (vals.size() >= 3) {
       col.setRgbF(float(std::clamp(vals[0], 0.0, 1.0)),
                   float(std::clamp(vals[1], 0.0, 1.0)),
                   float(std::clamp(vals[2], 0.0, 1.0)),
                   vals.size() >= 4 ? float(std::clamp(vals[3], 0.1, 1.0)) : 1.0f);
      }
     } else if (inTransparency && xml.name() == QLatin1String("float")) {
      transparencyVal = xml.readElementText().trimmed().toDouble();
     }
    } else if (tok == QXmlStreamReader::EndElement) {
     if (xml.name() == QLatin1String("diffuse")) inDiffuse = false;
     else if (xml.name() == QLatin1String("transparency")) inTransparency = false;
    }
   }
   if (transparencyVal >= 0.0 && transparencyVal <= 1.0 && transparencyVal < 0.99 && col.alphaF() > 0.99f) {
    // SketchUp exports A_ONE transparency as alpha (0.3 = glass, 1.0 = opaque)
    if (transparencyVal > 0.02) col.setAlphaF(float(std::clamp(transparencyVal, 0.12, 1.0)));
   }
   if (!effId.isEmpty()) effectColors[effId] = col;
  } else if (tag == QLatin1String("material")) {
   const QString matId = xml.attributes().value("id").toString();
   while (!xml.atEnd()) {
    const auto tok = xml.readNext();
    if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("material")) break;
    if (tok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("instance_effect")) {
     const QString url = stripHash(xml.attributes().value("url").toString());
     if (!matId.isEmpty() && !url.isEmpty()) matToEffect[matId] = url;
    }
   }
  } else if (tag == QLatin1String("geometry")) {
   const QString geoId = xml.attributes().value("id").toString();
   QHash<QString, QVector<double>> sources;
   QHash<QString, QString> verticesToPos;
   QHash<QString, QString> verticesToNorm;
   DaeGeo geo;

   while (!xml.atEnd()) {
    const auto tok = xml.readNext();
    if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("geometry")) break;
    if (tok != QXmlStreamReader::StartElement) continue;
    if (xml.name() == QLatin1String("source")) {
     const QString srcId = xml.attributes().value("id").toString();
     while (!xml.atEnd()) {
      const auto stok = xml.readNext();
      if (stok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("source")) break;
      if (stok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("float_array")) {
       sources[srcId] = parseDoubleList(xml.readElementText());
      }
     }
    } else if (xml.name() == QLatin1String("vertices")) {
     const QString vId = xml.attributes().value("id").toString();
     while (!xml.atEnd()) {
      const auto vtok = xml.readNext();
      if (vtok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("vertices")) break;
      if (vtok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("input")) {
       const QString sem = xml.attributes().value("semantic").toString();
       const QString src = stripHash(xml.attributes().value("source").toString());
       if (sem == "POSITION") verticesToPos[vId] = src;
       else if (sem == "NORMAL") verticesToNorm[vId] = src;
      }
     }
    } else if (xml.name() == QLatin1String("triangles") ||
               xml.name() == QLatin1String("polylist") ||
               xml.name() == QLatin1String("polygons")) {
     const QString primTag = xml.name().toString();
     DaePrim prim;
     prim.materialSymbol = xml.attributes().value("material").toString();
     int posOff = -1, normOff = -1, maxOff = 0;
     QString posSrc, normSrc;
     QVector<int> vcount;
     QVector<QVector<int>> pLists;

     while (!xml.atEnd()) {
      const auto ptok = xml.readNext();
      if (ptok == QXmlStreamReader::EndElement && xml.name() == primTag) break;
      if (ptok != QXmlStreamReader::StartElement) continue;
      if (xml.name() == QLatin1String("input")) {
       const QString sem = xml.attributes().value("semantic").toString();
       const QString src = stripHash(xml.attributes().value("source").toString());
       const int off = xml.attributes().value("offset").toInt();
       maxOff = std::max(maxOff, off);
       if (sem == "VERTEX") {
        posOff = off;
        posSrc = verticesToPos.value(src, src);
        if (normSrc.isEmpty() && verticesToNorm.contains(src)) {
         normOff = off;
         normSrc = verticesToNorm.value(src);
        }
       } else if (sem == "POSITION") {
        posOff = off;
        posSrc = src;
       } else if (sem == "NORMAL") {
        normOff = off;
        normSrc = src;
       }
      } else if (xml.name() == QLatin1String("vcount")) {
       vcount = parseIntList(xml.readElementText());
      } else if (xml.name() == QLatin1String("p")) {
       pLists.append(parseIntList(xml.readElementText()));
      }
     }

     const int stride = maxOff + 1;
     const auto &posArr = sources[posSrc];
     const auto &normArr = sources[normSrc];
     auto getVec = [](const QVector<double> &arr, int idx) -> QVector3D {
      const int k = idx * 3;
      if (k < 0 || k + 2 >= arr.size()) return {};
      return {float(arr[k]), float(arr[k + 1]), float(arr[k + 2])};
     };

     if (posOff >= 0 && !posArr.isEmpty()) {
      for (const auto &indices : pLists) {
       auto emitPoly = [&](int startVert, int countVert) {
        if (countVert < 3) return;
        for (int k = 1; k + 1 < countVert; ++k) {
         const int i0 = (startVert + 0) * stride;
         const int i1 = (startVert + k) * stride;
         const int i2 = (startVert + k + 1) * stride;
         if (i2 + posOff >= indices.size()) break;
         const QVector3D p0 = getVec(posArr, indices[i0 + posOff]);
         const QVector3D p1 = getVec(posArr, indices[i1 + posOff]);
         const QVector3D p2 = getVec(posArr, indices[i2 + posOff]);
         const QVector3D fn = safeTriNormal(p0, p1, p2);
         QVector3D n0 = fn, n1 = fn, n2 = fn;
         if (normOff >= 0 && !normArr.isEmpty() && i2 + normOff < indices.size()) {
          n0 = getVec(normArr, indices[i0 + normOff]);
          n1 = getVec(normArr, indices[i1 + normOff]);
          n2 = getVec(normArr, indices[i2 + normOff]);
          if (n0.lengthSquared() < 1e-6f) n0 = fn; else n0.normalize();
          if (n1.lengthSquared() < 1e-6f) n1 = fn; else n1.normalize();
          if (n2.lengthSquared() < 1e-6f) n2 = fn; else n2.normalize();
         }
         prim.pos.append(p0); prim.pos.append(p1); prim.pos.append(p2);
         prim.norm.append(n0); prim.norm.append(n1); prim.norm.append(n2);
        }
       };
       if (primTag == "triangles") {
        const int totalVerts = indices.size() / stride;
        for (int v = 0; v + 2 < totalVerts; v += 3) emitPoly(v, 3);
       } else if (primTag == "polylist" && !vcount.isEmpty()) {
        int cursor = 0;
        for (int vc : vcount) {
         emitPoly(cursor, vc);
         cursor += vc;
        }
       } else {
        emitPoly(0, indices.size() / stride);
       }
      }
      if (!prim.pos.isEmpty()) geo.prims.append(std::move(prim));
     }
    }
   }
   if (!geoId.isEmpty() && !geo.prims.isEmpty()) geometries[geoId] = std::move(geo);
  } else if (tag == QLatin1String("library_nodes")) {
   while (!xml.atEnd()) {
    const auto tok = xml.readNext();
    if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("library_nodes")) break;
    if (tok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("node")) {
     DaeNode n = parseDaeNode(xml);
     if (!n.id.isEmpty()) libraryNodes[n.id] = std::move(n);
    }
   }
  } else if (tag == QLatin1String("visual_scene")) {
   while (!xml.atEnd()) {
    const auto tok = xml.readNext();
    if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("visual_scene")) break;
    if (tok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("node")) {
     sceneRoots.append(parseDaeNode(xml));
    }
   }
  }
 }

 auto resolveColor = [&](const QString &matId) -> QColor {
  if (matId.isEmpty()) return QColor(234, 228, 218, 255);
  const QString eff = matToEffect.value(matId);
  if (!eff.isEmpty() && effectColors.contains(eff)) return effectColors[eff];
  if (effectColors.contains(matId)) return effectColors[matId];
  return QColor(234, 228, 218, 255);
 };

 QMatrix4x4 rootFix;
 rootFix.setToIdentity();
 if (upAxis == "Y_UP") rootFix.rotate(90.0f, 1.0f, 0.0f, 0.0f);
 rootFix.scale(float(unitMeter));

 std::function<void(const DaeNode &, const QMatrix4x4 &, int)> visit = [&](const DaeNode &node, const QMatrix4x4 &parent, int depth) {
  if (depth > 32 || out.rawTriangles.size() / 3 >= kMaxModelTriangles) return;
  const QMatrix4x4 world = parent * node.local;
  const QMatrix3x3 normMat = world.normalMatrix();
  const bool flipWind = (world.determinant() < 0.0);

  for (const auto &ig : node.geos) {
   auto it = geometries.constFind(ig.url);
   if (it == geometries.cend()) continue;
   for (const auto &prim : it->prims) {
    const QString targetMat = ig.matBind.value(prim.materialSymbol, prim.materialSymbol);
    const QColor col = resolveColor(targetMat);
    for (int i = 0; i + 2 < prim.pos.size(); i += 3) {
     const QVector3D a = world.map(prim.pos[i]);
     const QVector3D b = world.map(prim.pos[i + 1]);
     const QVector3D c = world.map(prim.pos[i + 2]);
     auto xformN = [&](const QVector3D &n) {
      QVector3D r(normMat(0, 0) * n.x() + normMat(0, 1) * n.y() + normMat(0, 2) * n.z(),
                  normMat(1, 0) * n.x() + normMat(1, 1) * n.y() + normMat(1, 2) * n.z(),
                  normMat(2, 0) * n.x() + normMat(2, 1) * n.y() + normMat(2, 2) * n.z());
      return r.lengthSquared() > 1e-8f ? r.normalized() : safeTriNormal(a, b, c);
     };
     const QVector3D na = xformN(prim.norm[i]);
     const QVector3D nb = xformN(prim.norm[i + 1]);
     const QVector3D nc = xformN(prim.norm[i + 2]);
     if (flipWind) pushTri(out.rawTriangles, a, -na, c, -nc, b, -nb, col);
     else pushTri(out.rawTriangles, a, na, b, nb, c, nc, col);
    }
   }
  }
  for (const auto &refId : node.instanceNodes) {
   auto it = libraryNodes.constFind(refId);
   if (it != libraryNodes.cend()) visit(*it, world, depth + 1);
  }
  for (const auto &ch : node.children) visit(ch, world, depth + 1);
 };

 if (!sceneRoots.isEmpty()) {
  for (const auto &rn : sceneRoots) visit(rn, rootFix, 0);
 } else {
  // Fallback: render all geometries directly if visual_scene missing
  for (auto it = geometries.cbegin(); it != geometries.cend(); ++it) {
   for (const auto &prim : it->prims) {
    const QColor col = resolveColor(prim.materialSymbol);
    for (int i = 0; i + 2 < prim.pos.size(); i += 3) {
     pushTri(out.rawTriangles,
             rootFix.map(prim.pos[i]), prim.norm[i],
             rootFix.map(prim.pos[i + 1]), prim.norm[i + 1],
             rootFix.map(prim.pos[i + 2]), prim.norm[i + 2], col);
    }
   }
  }
 }

 if (out.rawTriangles.isEmpty()) {
  if (err) *err = "COLLADA (.dae) file contains no readable geometry.";
  return false;
 }
 return true;
}

// ── 4. glTF 2.0 / GLB Loader (SketchUp glTF / Blender / Twinmotion / Rhino) ──

bool loadGltfModel(const QString &path, ImportedModel3D &out, QString *err) {
 QFile f(path);
 if (!f.open(QIODevice::ReadOnly)) {
  if (err) *err = "Cannot open glTF/GLB file.";
  return false;
 }
 const QByteArray raw = f.readAll();
 QJsonObject doc;
 QVector<QByteArray> buffers;
 const QDir baseDir = QFileInfo(path).absoluteDir();

 if (raw.size() >= 12 && qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData())) == 0x46546C67u) {
  // Binary GLB
  int off = 12;
  QByteArray jsonBytes, binBytes;
  while (off + 8 <= raw.size()) {
   const quint32 chunkLen = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData() + off));
   const quint32 chunkType = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData() + off + 4));
   off += 8;
   if (off + qint64(chunkLen) > raw.size()) break;
   if (chunkType == 0x4E4F534Au) jsonBytes = raw.mid(off, int(chunkLen));
   else if (chunkType == 0x004E4942u) binBytes = raw.mid(off, int(chunkLen));
   off += int(chunkLen);
  }
  doc = QJsonDocument::fromJson(jsonBytes).object();
  buffers.append(binBytes);
 } else {
  doc = QJsonDocument::fromJson(raw).object();
  for (const auto &bv : doc["buffers"].toArray()) {
   const QString uri = bv.toObject()["uri"].toString();
   if (uri.startsWith("data:")) {
    const int comma = uri.indexOf(',');
    buffers.append(comma >= 0 ? QByteArray::fromBase64(uri.mid(comma + 1).toLatin1()) : QByteArray());
   } else if (!uri.isEmpty()) {
    QFile bf(baseDir.filePath(uri));
    buffers.append(bf.open(QIODevice::ReadOnly) ? bf.readAll() : QByteArray());
   } else {
    buffers.append(QByteArray());
   }
  }
 }

 const auto bufferViews = doc["bufferViews"].toArray();
 const auto accessors = doc["accessors"].toArray();
 const auto materials = doc["materials"].toArray();
 const auto meshes = doc["meshes"].toArray();
 const auto nodes = doc["nodes"].toArray();

 auto readVec3Accessor = [&](int accIdx) -> QVector<QVector3D> {
  QVector<QVector3D> res;
  if (accIdx < 0 || accIdx >= accessors.size()) return res;
  const auto acc = accessors[accIdx].toObject();
  const int bvIdx = acc["bufferView"].toInt(-1);
  if (bvIdx < 0 || bvIdx >= bufferViews.size()) return res;
  const auto bv = bufferViews[bvIdx].toObject();
  const int bufIdx = bv["buffer"].toInt(0);
  if (bufIdx < 0 || bufIdx >= buffers.size()) return res;
  const auto &buf = buffers[bufIdx];
  const int count = acc["count"].toInt(0);
  const int offset = bv["byteOffset"].toInt(0) + acc["byteOffset"].toInt(0);
  const int stride = std::max(12, bv["byteStride"].toInt(12));
  res.reserve(count);
  for (int i = 0; i < count; ++i) {
   const int p = offset + i * stride;
   if (p + 12 > buf.size()) break;
   float xyz[3];
   std::memcpy(xyz, buf.constData() + p, 12);
   res.append({xyz[0], xyz[1], xyz[2]});
  }
  return res;
 };

 auto readIndicesAccessor = [&](int accIdx) -> QVector<uint32_t> {
  QVector<uint32_t> res;
  if (accIdx < 0 || accIdx >= accessors.size()) return res;
  const auto acc = accessors[accIdx].toObject();
  const int bvIdx = acc["bufferView"].toInt(-1);
  if (bvIdx < 0 || bvIdx >= bufferViews.size()) return res;
  const auto bv = bufferViews[bvIdx].toObject();
  const int bufIdx = bv["buffer"].toInt(0);
  if (bufIdx < 0 || bufIdx >= buffers.size()) return res;
  const auto &buf = buffers[bufIdx];
  const int count = acc["count"].toInt(0);
  const int compType = acc["componentType"].toInt(5123);
  const int compSize = (compType == 5121) ? 1 : (compType == 5123 ? 2 : 4);
  const int offset = bv["byteOffset"].toInt(0) + acc["byteOffset"].toInt(0);
  const int stride = std::max(compSize, bv["byteStride"].toInt(compSize));
  res.reserve(count);
  for (int i = 0; i < count; ++i) {
   const int p = offset + i * stride;
   if (p + compSize > buf.size()) break;
   if (compType == 5121) res.append(uint8_t(buf[p]));
   else if (compType == 5123) res.append(qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(buf.constData() + p)));
   else res.append(qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(buf.constData() + p)));
  }
  return res;
 };

 auto nodeLocalMatrix = [](const QJsonObject &n) -> QMatrix4x4 {
  QMatrix4x4 m;
  m.setToIdentity();
  if (n.contains("matrix")) {
   const auto arr = n["matrix"].toArray();
   if (arr.size() >= 16) {
    for (int c = 0; c < 4; ++c)
     for (int r = 0; r < 4; ++r) m(r, c) = float(arr[c * 4 + r].toDouble());
   }
   return m;
  }
  if (n.contains("translation")) {
   const auto t = n["translation"].toArray();
   if (t.size() >= 3) m.translate(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
  }
  if (n.contains("rotation")) {
   const auto r = n["rotation"].toArray();
   if (r.size() >= 4) {
    QQuaternion q(float(r[3].toDouble()), float(r[0].toDouble()), float(r[1].toDouble()), float(r[2].toDouble()));
    m.rotate(q);
   }
  }
  if (n.contains("scale")) {
   const auto s = n["scale"].toArray();
   if (s.size() >= 3) m.scale(float(s[0].toDouble()), float(s[1].toDouble()), float(s[2].toDouble()));
  }
  return m;
 };

 auto materialColor = [&](int matIdx) -> QColor {
  QColor col(234, 228, 218, 255);
  if (matIdx < 0 || matIdx >= materials.size()) return col;
  const auto pbr = materials[matIdx].toObject()["pbrMetallicRoughness"].toObject();
  const auto bcf = pbr["baseColorFactor"].toArray();
  if (bcf.size() >= 3) {
   col.setRgbF(float(std::clamp(bcf[0].toDouble(), 0.0, 1.0)),
               float(std::clamp(bcf[1].toDouble(), 0.0, 1.0)),
               float(std::clamp(bcf[2].toDouble(), 0.0, 1.0)),
               bcf.size() >= 4 ? float(std::clamp(bcf[3].toDouble(), 0.1, 1.0)) : 1.0f);
  }
  return col;
 };

 // glTF 2.0 specification is always Y-up -> rotate +90° around X into GroundAtlas Z-up!
 QMatrix4x4 yUpToZUp;
 yUpToZUp.setToIdentity();
 yUpToZUp.rotate(90.0f, 1.0f, 0.0f, 0.0f);

 std::function<void(int, const QMatrix4x4 &, int)> visitNode = [&](int idx, const QMatrix4x4 &parent, int depth) {
  if (depth > 32 || idx < 0 || idx >= nodes.size() || out.rawTriangles.size() / 3 >= kMaxModelTriangles) return;
  const auto n = nodes[idx].toObject();
  const QMatrix4x4 world = parent * nodeLocalMatrix(n);
  const QMatrix3x3 normMat = world.normalMatrix();
  const bool flipWind = (world.determinant() < 0.0);

  if (n.contains("mesh")) {
   const int mIdx = n["mesh"].toInt(-1);
   if (mIdx >= 0 && mIdx < meshes.size()) {
    for (const auto &pv : meshes[mIdx].toObject()["primitives"].toArray()) {
     const auto prim = pv.toObject();
     const int mode = prim["mode"].toInt(4);
     if (mode != 4) continue; // TRIANGLES
     const auto attrs = prim["attributes"].toObject();
     if (!attrs.contains("POSITION")) continue;
     const auto pos = readVec3Accessor(attrs["POSITION"].toInt(-1));
     const auto norm = attrs.contains("NORMAL") ? readVec3Accessor(attrs["NORMAL"].toInt(-1)) : QVector<QVector3D>{};
     QVector<uint32_t> inds;
     if (prim.contains("indices")) inds = readIndicesAccessor(prim["indices"].toInt(-1));
     else {
      inds.resize(pos.size());
      for (int i = 0; i < pos.size(); ++i) inds[i] = uint32_t(i);
     }
     const QColor col = materialColor(prim["material"].toInt(-1));
     auto xformN = [&](const QVector3D &nv, const QVector3D &fb) {
      QVector3D r(normMat(0, 0) * nv.x() + normMat(0, 1) * nv.y() + normMat(0, 2) * nv.z(),
                  normMat(1, 0) * nv.x() + normMat(1, 1) * nv.y() + normMat(1, 2) * nv.z(),
                  normMat(2, 0) * nv.x() + normMat(2, 1) * nv.y() + normMat(2, 2) * nv.z());
      return r.lengthSquared() > 1e-8f ? r.normalized() : fb;
     };
     for (int i = 0; i + 2 < inds.size(); i += 3) {
      const int i0 = int(inds[i]), i1 = int(inds[i + 1]), i2 = int(inds[i + 2]);
      if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size()) continue;
      const QVector3D a = world.map(pos[i0]);
      const QVector3D b = world.map(pos[i1]);
      const QVector3D c = world.map(pos[i2]);
      const QVector3D fn = safeTriNormal(a, b, c);
      const QVector3D na = (i0 < norm.size()) ? xformN(norm[i0], fn) : fn;
      const QVector3D nb = (i1 < norm.size()) ? xformN(norm[i1], fn) : fn;
      const QVector3D nc = (i2 < norm.size()) ? xformN(norm[i2], fn) : fn;
      if (flipWind) pushTri(out.rawTriangles, a, -na, c, -nc, b, -nb, col);
      else pushTri(out.rawTriangles, a, na, b, nb, c, nc, col);
     }
    }
   }
  }
  for (const auto &cv : n["children"].toArray()) visitNode(cv.toInt(-1), world, depth + 1);
 };

 const auto scenes = doc["scenes"].toArray();
 const int defaultScene = doc["scene"].toInt(0);
 if (defaultScene >= 0 && defaultScene < scenes.size()) {
  for (const auto &rv : scenes[defaultScene].toObject()["nodes"].toArray()) visitNode(rv.toInt(-1), yUpToZUp, 0);
 } else {
  for (int i = 0; i < nodes.size(); ++i) visitNode(i, yUpToZUp, 0);
 }

 if (out.rawTriangles.isEmpty()) {
  if (err) *err = "glTF/GLB file contains no triangle meshes.";
  return false;
 }
 return true;
}

// ── 5. Direct Native SketchUp (.skp) Loader via Runtime QLibrary(SketchUpAPI.dll) ──

struct SUModelRef { void *ptr = nullptr; };
struct SUEntitiesRef { void *ptr = nullptr; };
struct SUFaceRef { void *ptr = nullptr; };
struct SUGroupRef { void *ptr = nullptr; };
struct SUComponentInstanceRef { void *ptr = nullptr; };
struct SUComponentDefinitionRef { void *ptr = nullptr; };
struct SUDrawingElementRef { void *ptr = nullptr; };
struct SUMaterialRef { void *ptr = nullptr; };
struct SUMeshHelperRef { void *ptr = nullptr; };
struct SUPoint3D { double x, y, z; };
struct SUVector3D { double x, y, z; };
struct SUTransformation { double values[16]; };
struct SUColor { uint8_t red, green, blue, alpha; };

struct SketchUpApiRuntime {
#ifdef Q_OS_WIN
 HMODULE hMod = nullptr;
#else
 QLibrary lib;
#endif
 bool ready = false;
 QString dllPath;

 void (*SUInitialize)() = nullptr;
 void (*SUTerminate)() = nullptr;
 int (*SUModelCreateFromFile)(SUModelRef *, const char *) = nullptr;
 int (*SUModelRelease)(SUModelRef *) = nullptr;
 int (*SUModelGetEntities)(SUModelRef, SUEntitiesRef *) = nullptr;
 int (*SUEntitiesGetNumFaces)(SUEntitiesRef, size_t *) = nullptr;
 int (*SUEntitiesGetFaces)(SUEntitiesRef, size_t, SUFaceRef *, size_t *) = nullptr;
 int (*SUEntitiesGetNumGroups)(SUEntitiesRef, size_t *) = nullptr;
 int (*SUEntitiesGetGroups)(SUEntitiesRef, size_t, SUGroupRef *, size_t *) = nullptr;
 int (*SUEntitiesGetNumInstances)(SUEntitiesRef, size_t *) = nullptr;
 int (*SUEntitiesGetInstances)(SUEntitiesRef, size_t, SUComponentInstanceRef *, size_t *) = nullptr;
 int (*SUGroupGetEntities)(SUGroupRef, SUEntitiesRef *) = nullptr;
 int (*SUGroupGetTransform)(SUGroupRef, SUTransformation *) = nullptr;
 SUDrawingElementRef (*SUGroupToDrawingElement)(SUGroupRef) = nullptr;
 int (*SUComponentInstanceGetDefinition)(SUComponentInstanceRef, SUComponentDefinitionRef *) = nullptr;
 int (*SUComponentInstanceGetTransform)(SUComponentInstanceRef, SUTransformation *) = nullptr;
 SUDrawingElementRef (*SUComponentInstanceToDrawingElement)(SUComponentInstanceRef) = nullptr;
 int (*SUComponentDefinitionGetEntities)(SUComponentDefinitionRef, SUEntitiesRef *) = nullptr;
 int (*SUDrawingElementGetMaterial)(SUDrawingElementRef, SUMaterialRef *) = nullptr;
 int (*SUFaceGetFrontMaterial)(SUFaceRef, SUMaterialRef *) = nullptr;
 int (*SUFaceGetBackMaterial)(SUFaceRef, SUMaterialRef *) = nullptr;
 int (*SUMaterialGetColor)(SUMaterialRef, SUColor *) = nullptr;
 int (*SUMaterialGetOpacity)(SUMaterialRef, double *) = nullptr;
 int (*SUMaterialGetUseOpacity)(SUMaterialRef, bool *) = nullptr;
 int (*SUMeshHelperCreate)(SUMeshHelperRef *, SUFaceRef) = nullptr;
 int (*SUMeshHelperRelease)(SUMeshHelperRef *) = nullptr;
 int (*SUMeshHelperGetNumTriangles)(SUMeshHelperRef, size_t *) = nullptr;
 int (*SUMeshHelperGetNumVertices)(SUMeshHelperRef, size_t *) = nullptr;
 int (*SUMeshHelperGetVertices)(SUMeshHelperRef, size_t, SUPoint3D *, size_t *) = nullptr;
 int (*SUMeshHelperGetNormals)(SUMeshHelperRef, size_t, SUVector3D *, size_t *) = nullptr;
 int (*SUMeshHelperGetVertexIndices)(SUMeshHelperRef, size_t, size_t *, size_t *) = nullptr;

 static SketchUpApiRuntime &instance() {
  static SketchUpApiRuntime api;
  if (!api.ready) api.init();
  return api;
 }

 bool init() {
  if (ready) return true;
  dllPath = findSketchUpApiDll();
  if (dllPath.isEmpty()) return false;
#ifdef Q_OS_WIN
  // LOAD_WITH_ALTERED_SEARCH_PATH lets Windows resolve SketchUpAPI.dll's sibling DLLs inside Program Files\SketchUp\*!
  const std::wstring wpath = dllPath.toStdWString();
  hMod = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!hMod) return false;
  auto sym = [&](const char *name) -> void * { return reinterpret_cast<void *>(GetProcAddress(hMod, name)); };
#else
  lib.setFileName(dllPath);
  if (!lib.load()) return false;
  auto sym = [&](const char *name) -> void * { return reinterpret_cast<void *>(lib.resolve(name)); };
#endif
  SUInitialize = reinterpret_cast<decltype(SUInitialize)>(sym("SUInitialize"));
  SUTerminate = reinterpret_cast<decltype(SUTerminate)>(sym("SUTerminate"));
  SUModelCreateFromFile = reinterpret_cast<decltype(SUModelCreateFromFile)>(sym("SUModelCreateFromFile"));
  SUModelRelease = reinterpret_cast<decltype(SUModelRelease)>(sym("SUModelRelease"));
  SUModelGetEntities = reinterpret_cast<decltype(SUModelGetEntities)>(sym("SUModelGetEntities"));
  SUEntitiesGetNumFaces = reinterpret_cast<decltype(SUEntitiesGetNumFaces)>(sym("SUEntitiesGetNumFaces"));
  SUEntitiesGetFaces = reinterpret_cast<decltype(SUEntitiesGetFaces)>(sym("SUEntitiesGetFaces"));
  SUEntitiesGetNumGroups = reinterpret_cast<decltype(SUEntitiesGetNumGroups)>(sym("SUEntitiesGetNumGroups"));
  SUEntitiesGetGroups = reinterpret_cast<decltype(SUEntitiesGetGroups)>(sym("SUEntitiesGetGroups"));
  SUEntitiesGetNumInstances = reinterpret_cast<decltype(SUEntitiesGetNumInstances)>(sym("SUEntitiesGetNumInstances"));
  SUEntitiesGetInstances = reinterpret_cast<decltype(SUEntitiesGetInstances)>(sym("SUEntitiesGetInstances"));
  SUGroupGetEntities = reinterpret_cast<decltype(SUGroupGetEntities)>(sym("SUGroupGetEntities"));
  SUGroupGetTransform = reinterpret_cast<decltype(SUGroupGetTransform)>(sym("SUGroupGetTransform"));
  SUGroupToDrawingElement = reinterpret_cast<decltype(SUGroupToDrawingElement)>(sym("SUGroupToDrawingElement"));
  SUComponentInstanceGetDefinition = reinterpret_cast<decltype(SUComponentInstanceGetDefinition)>(sym("SUComponentInstanceGetDefinition"));
  SUComponentInstanceGetTransform = reinterpret_cast<decltype(SUComponentInstanceGetTransform)>(sym("SUComponentInstanceGetTransform"));
  SUComponentInstanceToDrawingElement = reinterpret_cast<decltype(SUComponentInstanceToDrawingElement)>(sym("SUComponentInstanceToDrawingElement"));
  SUComponentDefinitionGetEntities = reinterpret_cast<decltype(SUComponentDefinitionGetEntities)>(sym("SUComponentDefinitionGetEntities"));
  SUDrawingElementGetMaterial = reinterpret_cast<decltype(SUDrawingElementGetMaterial)>(sym("SUDrawingElementGetMaterial"));
  SUFaceGetFrontMaterial = reinterpret_cast<decltype(SUFaceGetFrontMaterial)>(sym("SUFaceGetFrontMaterial"));
  SUFaceGetBackMaterial = reinterpret_cast<decltype(SUFaceGetBackMaterial)>(sym("SUFaceGetBackMaterial"));
  SUMaterialGetColor = reinterpret_cast<decltype(SUMaterialGetColor)>(sym("SUMaterialGetColor"));
  SUMaterialGetOpacity = reinterpret_cast<decltype(SUMaterialGetOpacity)>(sym("SUMaterialGetOpacity"));
  SUMaterialGetUseOpacity = reinterpret_cast<decltype(SUMaterialGetUseOpacity)>(sym("SUMaterialGetUseOpacity"));
  SUMeshHelperCreate = reinterpret_cast<decltype(SUMeshHelperCreate)>(sym("SUMeshHelperCreate"));
  SUMeshHelperRelease = reinterpret_cast<decltype(SUMeshHelperRelease)>(sym("SUMeshHelperRelease"));
  SUMeshHelperGetNumTriangles = reinterpret_cast<decltype(SUMeshHelperGetNumTriangles)>(sym("SUMeshHelperGetNumTriangles"));
  SUMeshHelperGetNumVertices = reinterpret_cast<decltype(SUMeshHelperGetNumVertices)>(sym("SUMeshHelperGetNumVertices"));
  SUMeshHelperGetVertices = reinterpret_cast<decltype(SUMeshHelperGetVertices)>(sym("SUMeshHelperGetVertices"));
  SUMeshHelperGetNormals = reinterpret_cast<decltype(SUMeshHelperGetNormals)>(sym("SUMeshHelperGetNormals"));
  SUMeshHelperGetVertexIndices = reinterpret_cast<decltype(SUMeshHelperGetVertexIndices)>(sym("SUMeshHelperGetVertexIndices"));

  if (!SUInitialize || !SUModelCreateFromFile || !SUModelRelease || !SUModelGetEntities ||
      !SUEntitiesGetNumFaces || !SUEntitiesGetFaces || !SUMeshHelperCreate || !SUMeshHelperRelease ||
      !SUMeshHelperGetNumTriangles || !SUMeshHelperGetNumVertices || !SUMeshHelperGetVertices ||
      !SUMeshHelperGetVertexIndices) {
   return false;
  }
  SUInitialize();
  ready = true;
  return true;
 }
};

QColor readSuMaterialColor(const SketchUpApiRuntime &api, SUMaterialRef mat, const QColor &fallback) {
 if (!mat.ptr || !api.SUMaterialGetColor) return fallback;
 SUColor sc{235, 230, 220, 255};
 if (api.SUMaterialGetColor(mat, &sc) != 0) return fallback;
 double alpha = sc.alpha / 255.0;
 if (api.SUMaterialGetUseOpacity && api.SUMaterialGetOpacity) {
  bool useOp = false;
  if (api.SUMaterialGetUseOpacity(mat, &useOp) == 0 && useOp) {
   double op = 1.0;
   if (api.SUMaterialGetOpacity(mat, &op) == 0) alpha = op;
  }
 }
 QColor c(sc.red, sc.green, sc.blue);
 c.setAlphaF(float(std::clamp(alpha, 0.12, 1.0)));
 return c;
}

QMatrix4x4 suTransformToMat(const SUTransformation &t) {
 QMatrix4x4 m;
 for (int c = 0; c < 4; ++c)
  for (int r = 0; r < 4; ++r) m(r, c) = float(t.values[c * 4 + r]);
 return m;
}

bool loadSkpModel(const QString &path, ImportedModel3D &out, QString *err) {
 auto &api = SketchUpApiRuntime::instance();
 if (!api.ready) {
  if (err) {
#ifdef Q_OS_MACOS
   *err = QString::fromUtf8(
    "Máy Mac này chưa cài SketchUp (không tìm thấy SketchUpAPI.framework để đọc trực tiếp .skp).\n"
    "Hãy xuất từ SketchUp sang .dae (COLLADA), .obj, .glb hoặc .stl rồi nhập trực tiếp (hỗ trợ 100% không cần cài SketchUp).");
#else
   *err = QString::fromUtf8(
    "Máy này chưa cài SketchUp (không tìm thấy SketchUpAPI.dll để đọc trực tiếp .skp).\n"
    "Hãy xuất từ SketchUp sang .dae (COLLADA), .obj, .glb hoặc .stl rồi nhập trực tiếp (hỗ trợ 100% không cần cài SketchUp).");
#endif
  }
  return false;
 }

 SUModelRef model{};
 const QByteArray utf8Path = QDir::toNativeSeparators(path).toUtf8();
 const int res = api.SUModelCreateFromFile(&model, utf8Path.constData());
 if (res != 0 || !model.ptr) {
  if (err) *err = QString::fromUtf8("Không thể đọc file SketchUp (.skp) này (mã lỗi %1). Thử lưu bản SketchUp thấp hơn hoặc xuất .dae.").arg(res);
  return false;
 }

 SUEntitiesRef rootEnt{};
 if (api.SUModelGetEntities(model, &rootEnt) != 0 || !rootEnt.ptr) {
  api.SUModelRelease(&model);
  if (err) *err = "SketchUp model has no root entities.";
  return false;
 }

 std::function<void(SUEntitiesRef, const QMatrix4x4 &, const QColor &, int)> traverse =
  [&](SUEntitiesRef entities, const QMatrix4x4 &parentMat, const QColor &inheritedColor, int depth) {
   if (!entities.ptr || depth > 24 || out.rawTriangles.size() / 3 >= kMaxModelTriangles) return;
   const QMatrix3x3 normMat = parentMat.normalMatrix();
   const bool flipWind = (parentMat.determinant() < 0.0);

   // 1. Direct faces
   size_t numFaces = 0;
   if (api.SUEntitiesGetNumFaces(entities, &numFaces) == 0 && numFaces > 0) {
    QVector<SUFaceRef> faces(static_cast<qsizetype>(numFaces));
    size_t got = 0;
    if (api.SUEntitiesGetFaces(entities, numFaces, faces.data(), &got) == 0) {
     for (size_t fi = 0; fi < got && out.rawTriangles.size() / 3 < kMaxModelTriangles; ++fi) {
      QColor col = inheritedColor;
      SUMaterialRef fMat{};
      if (api.SUFaceGetFrontMaterial && api.SUFaceGetFrontMaterial(faces[int(fi)], &fMat) == 0 && fMat.ptr) {
       col = readSuMaterialColor(api, fMat, inheritedColor);
      } else if (api.SUFaceGetBackMaterial && api.SUFaceGetBackMaterial(faces[int(fi)], &fMat) == 0 && fMat.ptr) {
       col = readSuMaterialColor(api, fMat, inheritedColor);
      }

      SUMeshHelperRef mh{};
      if (api.SUMeshHelperCreate(&mh, faces[int(fi)]) != 0 || !mh.ptr) continue;
      size_t nTris = 0, nVerts = 0;
      api.SUMeshHelperGetNumTriangles(mh, &nTris);
      api.SUMeshHelperGetNumVertices(mh, &nVerts);
      if (nTris > 0 && nVerts > 0) {
       QVector<SUPoint3D> pts(static_cast<qsizetype>(nVerts));
       QVector<SUVector3D> nrms(static_cast<qsizetype>(nVerts));
       QVector<size_t> inds(static_cast<qsizetype>(nTris * 3));
       size_t gotV = 0, gotN = 0, gotI = 0;
       api.SUMeshHelperGetVertices(mh, nVerts, pts.data(), &gotV);
       if (api.SUMeshHelperGetNormals) api.SUMeshHelperGetNormals(mh, nVerts, nrms.data(), &gotN);
       api.SUMeshHelperGetVertexIndices(mh, nTris * 3, inds.data(), &gotI);

       auto toWorldM = [&](const SUPoint3D &p) {
        const QVector3D v = parentMat.map(QVector3D(float(p.x), float(p.y), float(p.z)));
        return v * float(kInchToMetre);
       };
       auto toNorm = [&](size_t idx, const QVector3D &fb) {
        if (idx >= gotN) return fb;
        const auto &vn = nrms[int(idx)];
        QVector3D r(normMat(0, 0) * float(vn.x) + normMat(0, 1) * float(vn.y) + normMat(0, 2) * float(vn.z),
                    normMat(1, 0) * float(vn.x) + normMat(1, 1) * float(vn.y) + normMat(1, 2) * float(vn.z),
                    normMat(2, 0) * float(vn.x) + normMat(2, 1) * float(vn.y) + normMat(2, 2) * float(vn.z));
        return r.lengthSquared() > 1e-8f ? r.normalized() : fb;
       };

       for (size_t t = 0; t + 2 < gotI; t += 3) {
        const size_t i0 = inds[int(t)], i1 = inds[int(t + 1)], i2 = inds[int(t + 2)];
        if (i0 >= gotV || i1 >= gotV || i2 >= gotV) continue;
        const QVector3D a = toWorldM(pts[int(i0)]);
        const QVector3D b = toWorldM(pts[int(i1)]);
        const QVector3D c = toWorldM(pts[int(i2)]);
        const QVector3D fn = safeTriNormal(a, b, c);
        const QVector3D na = toNorm(i0, fn), nb = toNorm(i1, fn), nc = toNorm(i2, fn);
        if (flipWind) pushTri(out.rawTriangles, a, -na, c, -nc, b, -nb, col);
        else pushTri(out.rawTriangles, a, na, b, nb, c, nc, col);
       }
      }
      api.SUMeshHelperRelease(&mh);
     }
    }
   }

   // 2. Nested Groups
   size_t numGroups = 0;
   if (api.SUEntitiesGetNumGroups && api.SUEntitiesGetGroups && api.SUGroupGetEntities && api.SUGroupGetTransform &&
       api.SUEntitiesGetNumGroups(entities, &numGroups) == 0 && numGroups > 0) {
    QVector<SUGroupRef> groups(static_cast<qsizetype>(numGroups));
    size_t got = 0;
    if (api.SUEntitiesGetGroups(entities, numGroups, groups.data(), &got) == 0) {
     for (size_t gi = 0; gi < got; ++gi) {
      SUTransformation tr{};
      SUEntitiesRef gEnt{};
      if (api.SUGroupGetTransform(groups[int(gi)], &tr) != 0) continue;
      if (api.SUGroupGetEntities(groups[int(gi)], &gEnt) != 0) continue;
      QColor gCol = inheritedColor;
      if (api.SUGroupToDrawingElement && api.SUDrawingElementGetMaterial) {
       SUMaterialRef gMat{};
       if (api.SUDrawingElementGetMaterial(api.SUGroupToDrawingElement(groups[int(gi)]), &gMat) == 0 && gMat.ptr) {
        gCol = readSuMaterialColor(api, gMat, inheritedColor);
       }
      }
      traverse(gEnt, parentMat * suTransformToMat(tr), gCol, depth + 1);
     }
    }
   }

   // 3. Component Instances
   size_t numInst = 0;
   if (api.SUEntitiesGetNumInstances && api.SUEntitiesGetInstances && api.SUComponentInstanceGetDefinition &&
       api.SUComponentInstanceGetTransform && api.SUComponentDefinitionGetEntities &&
       api.SUEntitiesGetNumInstances(entities, &numInst) == 0 && numInst > 0) {
    QVector<SUComponentInstanceRef> insts(static_cast<qsizetype>(numInst));
    size_t got = 0;
    if (api.SUEntitiesGetInstances(entities, numInst, insts.data(), &got) == 0) {
     for (size_t ii = 0; ii < got; ++ii) {
      SUTransformation tr{};
      SUComponentDefinitionRef def{};
      SUEntitiesRef cEnt{};
      if (api.SUComponentInstanceGetTransform(insts[int(ii)], &tr) != 0) continue;
      if (api.SUComponentInstanceGetDefinition(insts[int(ii)], &def) != 0) continue;
      if (api.SUComponentDefinitionGetEntities(def, &cEnt) != 0) continue;
      QColor iCol = inheritedColor;
      if (api.SUComponentInstanceToDrawingElement && api.SUDrawingElementGetMaterial) {
       SUMaterialRef iMat{};
       if (api.SUDrawingElementGetMaterial(api.SUComponentInstanceToDrawingElement(insts[int(ii)]), &iMat) == 0 && iMat.ptr) {
        iCol = readSuMaterialColor(api, iMat, inheritedColor);
       }
      }
      traverse(cEnt, parentMat * suTransformToMat(tr), iCol, depth + 1);
     }
    }
   }
  };

 QMatrix4x4 ident;
 ident.setToIdentity();
 traverse(rootEnt, ident, QColor(235, 230, 220, 255), 0);
 api.SUModelRelease(&model);

 if (out.rawTriangles.isEmpty()) {
  if (err) *err = QString::fromUtf8("File SketchUp (.skp) không có bề mặt 3D nào để hiển thị.");
  return false;
 }
 return true;
}

} // namespace

// ── Public API Implementation ──

QString findSketchUpApiDll() {
#ifdef Q_OS_WIN
 const QStringList roots = {
  "C:/Program Files/SketchUp",
  "C:/Program Files (x86)/SketchUp",
 };
 const QRegularExpression pat("^SketchUp\\s+\\d{4}", QRegularExpression::CaseInsensitiveOption);
 for (const auto &r : roots) {
  QDir dir(r);
  if (!dir.exists()) continue;
  auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) { return a > b; });
  for (const auto &name : entries) {
   if (!pat.match(name).hasMatch()) continue;
   const QString dll = dir.filePath(name + "/SketchUpAPI.dll");
   if (QFileInfo::exists(dll)) return QDir::toNativeSeparators(dll);
  }
 }
#elif defined(Q_OS_MACOS)
 const QStringList roots = {
  QStringLiteral("/Applications"),
  QDir::homePath() + QStringLiteral("/Applications"),
 };
 const QRegularExpression pat("^SketchUp\\s+\\d{4}", QRegularExpression::CaseInsensitiveOption);
 const QStringList fwRelPaths = {
  QStringLiteral("SketchUp.app/Contents/Frameworks/SketchUpAPI.framework/Versions/A/SketchUpAPI"),
  QStringLiteral("SketchUp.app/Contents/Frameworks/SketchUpAPI.framework/SketchUpAPI"),
 };
 for (const auto &r : roots) {
  QDir dir(r);
  if (!dir.exists()) continue;
  auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) { return a > b; });
  for (const auto &name : entries) {
   if (!pat.match(name).hasMatch()) continue;
   for (const auto &rel : fwRelPaths) {
    const QString candidate = dir.filePath(name + "/" + rel);
    if (QFileInfo::exists(candidate)) return candidate;
   }
  }
  for (const auto &rel : fwRelPaths) {
   const QString direct = dir.filePath(rel);
   if (QFileInfo::exists(direct)) return direct;
  }
 }
#endif
 return {};
}

bool sketchUpDirectImportAvailable() {
 return !findSketchUpApiDll().isEmpty();
}

bool isSupported3DModelFile(const QString &filePath) {
 const QString ext = QFileInfo(filePath).suffix().toLower();
 return ext == "skp" || ext == "dae" || ext == "obj" || ext == "glb" || ext == "gltf" || ext == "stl";
}

QPolygonF computeModelFootprint2D(const QVector<Vertex> &tris) {
 if (tris.isEmpty()) return {};
 QVector<QPointF> pts;
 const qsizetype step = std::max<qsizetype>(1, tris.size() / 6000);
 pts.reserve(std::min<qsizetype>(tris.size(), 6000));
 for (qsizetype i = 0; i < tris.size(); i += step) {
  if (std::isfinite(tris[i].x) && std::isfinite(tris[i].y)) pts.append({double(tris[i].x), double(tris[i].y)});
 }
 if (pts.size() < 3) return {};

 std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) {
  return (a.x() != b.x()) ? (a.x() < b.x()) : (a.y() < b.y());
 });
 auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
  return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
 };
 QVector<QPointF> h;
 h.reserve(pts.size() * 2);
 for (const auto &p : pts) {
  while (h.size() >= 2 && cross(h[h.size() - 2], h.last(), p) <= 1e-6) h.removeLast();
  h.append(p);
 }
 const int lowerSize = h.size();
 for (int i = pts.size() - 2; i >= 0; --i) {
  const auto &p = pts[i];
  while (h.size() > lowerSize && cross(h[h.size() - 2], h.last(), p) <= 1e-6) h.removeLast();
  h.append(p);
 }
 if (h.size() > 1) h.removeLast();
 QPolygonF poly;
 for (const auto &p : h) poly.append(p);
 return poly;
}

QMatrix4x4 ImportedModel3D::worldMatrix(double groundZ) const {
 QMatrix4x4 m;
 m.setToIdentity();
 m.translate(float(x), float(y), float(groundZ + z));
 m.rotate(float(yawDeg), 0.0f, 0.0f, 1.0f);
 const float s = float(std::clamp(scale, 0.0001, 10000.0));
 m.scale(s);
 if (flipYUp) {
  // Keeps XY centered and bottom flush at Z=0 after rotating Y-up -> Z-up
  m.translate(0.0f, rawSizeM.z() * 0.5f, rawSizeM.y() * 0.5f);
  m.rotate(90.0f, 1.0f, 0.0f, 0.0f);
 }
 return m;
}

QVector3D ImportedModel3D::effectiveSizeM() const {
 const float s = float(std::clamp(scale, 0.0001, 10000.0));
 if (flipYUp) return {rawSizeM.x() * s, rawSizeM.z() * s, rawSizeM.y() * s};
 return rawSizeM * s;
}

QPolygonF ImportedModel3D::worldFootprint2D() const {
 const QMatrix4x4 wm = worldMatrix(0.0);
 if (!flipYUp && rawFootprint2D.size() >= 3) {
  QPolygonF out;
  out.reserve(rawFootprint2D.size());
  for (const auto &p : rawFootprint2D) {
   const QVector3D w = wm.map(QVector3D(float(p.x()), float(p.y()), 0.0f));
   out.append({double(w.x()), double(w.y())});
  }
  return out;
 }
 // Oriented bounding box fallback when Y-up flipped
 const QVector3D sz = effectiveSizeM();
 const double hw = sz.x() * 0.5, hd = sz.y() * 0.5;
 const double rad = yawDeg * M_PI / 180.0;
 const double c = std::cos(rad), s = std::sin(rad);
 auto pt = [&](double lx, double ly) { return QPointF(x + lx * c - ly * s, y + lx * s + ly * c); };
 return QPolygonF{pt(-hw, -hd), pt(hw, -hd), pt(hw, hd), pt(-hw, hd)};
}

QJsonObject ImportedModel3D::toJson() const {
 return {
  {"id", id},
  {"name", name},
  {"source_path", sourcePath},
  {"format", format},
  {"visible", visible},
  {"x", x},
  {"y", y},
  {"z", z},
  {"yaw_deg", yawDeg},
  {"scale", scale},
  {"flip_y_up", flipYUp},
  {"override_color", overrideColor},
  {"tint_color", tintColor.name()},
 };
}

ImportedModel3D ImportedModel3D::fromJson(const QJsonObject &v, QString *errorOut) {
 const QString src = v["source_path"].toString();
 if (src.isEmpty() || !QFileInfo::exists(src)) {
  if (errorOut) *errorOut = "Imported 3D source file not found: " + src;
  return {};
 }
 ImportedModel3D m = load3DModelFile(src, errorOut);
 if (!m.valid()) return {};
 if (!v["id"].toString().isEmpty()) m.id = v["id"].toString();
 if (!v["name"].toString().isEmpty()) m.name = v["name"].toString();
 m.visible = v["visible"].toBool(true);
 m.x = v["x"].toDouble(0.0);
 m.y = v["y"].toDouble(0.0);
 m.z = v["z"].toDouble(0.0);
 m.yawDeg = v["yaw_deg"].toDouble(0.0);
 m.scale = std::clamp(v["scale"].toDouble(1.0), 0.0001, 10000.0);
 m.flipYUp = v["flip_y_up"].toBool(m.flipYUp);
 m.overrideColor = v["override_color"].toBool(false);
 const QColor tc(v["tint_color"].toString());
 if (tc.isValid()) m.tintColor = tc;
 return m;
}

ImportedModel3D load3DModelFile(const QString &filePath, QString *errorOut) {
 ImportedModel3D m;
 const QFileInfo fi(filePath);
 if (!fi.exists() || !fi.isFile()) {
  if (errorOut) *errorOut = "3D model file does not exist.";
  return {};
 }
 m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
 m.name = fi.completeBaseName();
 m.sourcePath = fi.absoluteFilePath();
 m.format = fi.suffix().toLower();

 bool ok = false;
 if (m.format == "skp") ok = loadSkpModel(m.sourcePath, m, errorOut);
 else if (m.format == "dae") ok = loadDaeModel(m.sourcePath, m, errorOut);
 else if (m.format == "obj") ok = loadObjModel(m.sourcePath, m, errorOut);
 else if (m.format == "glb" || m.format == "gltf") ok = loadGltfModel(m.sourcePath, m, errorOut);
 else if (m.format == "stl") ok = loadStlModel(m.sourcePath, m, errorOut);
 else {
  if (errorOut) *errorOut = "Unsupported 3D format: ." + m.format;
  return {};
 }
 if (!ok) return {};
 normalizeModel(m);
 return m;
}

void appendImportedModelsToMesh(Mesh &mesh,
                                const QVector<ImportedModel3D> &models,
                                const std::function<double(const QPointF &)> &terrainZ) {
 for (const auto &m : models) {
  if (!m.visible || !m.valid()) continue;
  const double gz = terrainZ ? terrainZ(QPointF(m.x, m.y)) : 0.0;
  const QMatrix4x4 wm = m.worldMatrix(gz);
  const QMatrix3x3 nm = wm.normalMatrix();
  const float tr = float(m.tintColor.redF()), tg = float(m.tintColor.greenF()), tb = float(m.tintColor.blueF());

  mesh.triangles.reserve(mesh.triangles.size() + m.rawTriangles.size());
  for (int i = 0; i + 2 < m.rawTriangles.size(); i += 3) {
   Vertex outV[3];
   for (int k = 0; k < 3; ++k) {
    const auto &src = m.rawTriangles[i + k];
    const QVector3D wp = wm.map(QVector3D(src.x, src.y, src.z));
    QVector3D wn(nm(0, 0) * src.nx + nm(0, 1) * src.ny + nm(0, 2) * src.nz,
                 nm(1, 0) * src.nx + nm(1, 1) * src.ny + nm(1, 2) * src.nz,
                 nm(2, 0) * src.nx + nm(2, 1) * src.ny + nm(2, 2) * src.nz);
    if (wn.lengthSquared() > 1e-8f) wn.normalize();
    else wn = {0.0f, 0.0f, 1.0f};

    outV[k] = src;
    outV[k].x = wp.x(); outV[k].y = wp.y(); outV[k].z = wp.z();
    outV[k].nx = wn.x(); outV[k].ny = wn.y(); outV[k].nz = wn.z();
    if (m.overrideColor && src.a > 0.95f) {
     outV[k].r = tr; outV[k].g = tg; outV[k].b = tb; outV[k].a = 1.0f;
     outV[k].mat = 3.0f; // Architectural sheen matching Concept Massing
    } else {
     outV[k].mat = (src.a < 0.95f) ? 1.0f : 3.0f;
    }
   }
   mesh.triangles.append(outV[0]);
   mesh.triangles.append(outV[1]);
   mesh.triangles.append(outV[2]);
  }
 }
}

void appendImportedModelsToSection(SectionData &sec,
                                   const QVector<ImportedModel3D> &models,
                                   QPointF a,
                                   QPointF b,
                                   const TerrainGrid *dem,
                                   SectionMode mode,
                                   double &maxTop,
                                   QVector<SectionCallout> &rawCallouts) {
 const QPointF ab = b - a;
 const double len = std::hypot(ab.x(), ab.y());
 if (len < 1e-4) return;
 const QPointF dir(ab.x() / len, ab.y() / len);
 const QPointF perp(-dir.y(), dir.x());

 for (const auto &m : models) {
  if (!m.visible || !m.valid()) continue;
  // Fast bounding reject against section line
  const QVector3D sz = m.effectiveSizeM();
  const double boundR = std::hypot(sz.x(), sz.y()) * 0.6 + 2.0;
  const QPointF relCenter = QPointF(m.x, m.y) - a;
  const double perpDist = std::abs(relCenter.x() * perp.x() + relCenter.y() * perp.y());
  if (perpDist > boundR) continue;

  const double baseGround = (mode == SectionMode::TerrainDem && dem && dem->valid)
                             ? dem->sample(m.x, m.y, sec.minGround)
                             : 0.0;
  const QMatrix4x4 wm = m.worldMatrix(baseGround);

  struct CutSeg { double s0, z0, s1, z1; };
  QVector<CutSeg> segs;
  double sMin = len, sMax = 0.0;

  for (int i = 0; i + 2 < m.rawTriangles.size(); i += 3) {
   QVector3D w[3];
   double d[3];
   for (int k = 0; k < 3; ++k) {
    const auto &v = m.rawTriangles[i + k];
    w[k] = wm.map(QVector3D(v.x, v.y, v.z));
    const double rx = double(w[k].x()) - a.x();
    const double ry = double(w[k].y()) - a.y();
    d[k] = rx * perp.x() + ry * perp.y();
   }
   QVector<QPointF> hits; // (s, z)
   for (int e = 0; e < 3; ++e) {
    const int e2 = (e + 1) % 3;
    const double d0 = d[e], d1 = d[e2];
    if ((d0 <= 0.0 && d1 >= 0.0) || (d0 >= 0.0 && d1 <= 0.0)) {
     const double denom = d1 - d0;
     if (std::abs(denom) < 1e-8) continue;
     const double t = -d0 / denom;
     if (t < -1e-6 || t > 1.0 + 1e-6) continue;
     const QVector3D pt = w[e] + (w[e2] - w[e]) * float(std::clamp(t, 0.0, 1.0));
     const double s = (double(pt.x()) - a.x()) * dir.x() + (double(pt.y()) - a.y()) * dir.y();
     hits.append({s, double(pt.z())});
    }
   }
   if (hits.size() >= 2) {
    const double s0 = std::clamp(hits[0].x(), 0.0, len);
    const double s1 = std::clamp(hits[1].x(), 0.0, len);
    if (std::max(s0, s1) >= 0.0 && std::min(s0, s1) <= len) {
     segs.append({s0, hits[0].y(), s1, hits[1].y()});
     sMin = std::min({sMin, s0, s1});
     sMax = std::max({sMax, s0, s1});
    }
   }
  }
  if (segs.isEmpty() || sMax - sMin < 0.25) continue;

  // Rasterize intersected profile into crisp architectural bins along [sMin..sMax]
  const int nBins = std::clamp(int(std::ceil((sMax - sMin) / 1.5)), 4, 48);
  const double binW = (sMax - sMin) / nBins;
  QVector<double> binMinZ(nBins, std::numeric_limits<double>::max());
  QVector<double> binMaxZ(nBins, std::numeric_limits<double>::lowest());

  for (const auto &sg : segs) {
   const double lo = std::min(sg.s0, sg.s1), hi = std::max(sg.s0, sg.s1);
   const int b0 = std::clamp(int(std::floor((lo - sMin) / binW)), 0, nBins - 1);
   const int b1 = std::clamp(int(std::floor((hi - sMin) / binW)), 0, nBins - 1);
   for (int bi = b0; bi <= b1; ++bi) {
    const double sc = sMin + (bi + 0.5) * binW;
    double zVal = (sg.z0 + sg.z1) * 0.5;
    if (std::abs(sg.s1 - sg.s0) > 1e-4) {
     const double t = std::clamp((sc - sg.s0) / (sg.s1 - sg.s0), 0.0, 1.0);
     zVal = sg.z0 + (sg.z1 - sg.z0) * t;
    }
    binMinZ[bi] = std::min(binMinZ[bi], zVal);
    binMaxZ[bi] = std::max(binMaxZ[bi], zVal);
   }
  }

  double peakZ = baseGround;
  double peakS = (sMin + sMax) * 0.5;
  const QColor secCol = m.overrideColor ? m.tintColor : QColor("#c85a32");

  // Emit merged architectural spans so flat roofs merge cleanly while pitched roofs keep crisp stepped silhouette
  int bi = 0;
  while (bi < nBins) {
   if (binMaxZ[bi] <= binMinZ[bi] + 0.05) { ++bi; continue; }
   int bj = bi + 1;
   while (bj < nBins && binMaxZ[bj] > binMinZ[bj] + 0.05 &&
          std::abs(binMaxZ[bj] - binMaxZ[bi]) < 0.45 &&
          std::abs(binMinZ[bj] - binMinZ[bi]) < 0.45) {
    ++bj;
   }
   double zLo = binMinZ[bi], zHi = binMaxZ[bi];
   for (int k = bi + 1; k < bj; ++k) {
    zLo = std::min(zLo, binMinZ[k]);
    zHi = std::max(zHi, binMaxZ[k]);
   }
   const double spanS0 = sMin + bi * binW;
   const double spanS1 = sMin + bj * binW;
   const double baseOff = std::max(0.0, zLo - baseGround);
   const double h = std::max(0.5, zHi - (baseGround + baseOff));
   sec.buildings.append({spanS0, spanS1, baseGround, h, baseOff, true, "proposed", "imported3d", m.name, secCol});
   if (zHi > peakZ) {
    peakZ = zHi;
    peakS = (spanS0 + spanS1) * 0.5;
   }
   maxTop = std::max(maxTop, zHi);
   bi = bj;
  }

  if (peakZ > baseGround + 0.5) {
   rawCallouts.append({peakS, peakZ, QString::fromUtf8("★ %1 · %2 m").arg(m.name.left(14)).arg(qRound(peakZ - baseGround)), "proposed"});
  }
 }
}

} // namespace ga
