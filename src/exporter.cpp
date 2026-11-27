#include "exporter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ga {
namespace {

DccAppInfo findLatestApp(const QString &rootDir, const QRegularExpression &pattern, const QString &relExe) {
 QDir dir(rootDir);
 if (!dir.exists()) return {};
 auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
 std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) { return a > b; });
 for (const auto &name : entries) {
  if (!pattern.match(name).hasMatch()) continue;
  const QString full = dir.filePath(name + "/" + relExe);
  if (QFileInfo::exists(full)) return {name, QDir::toNativeSeparators(full)};
 }
 return {};
}

int dxfAciColor(const QString &layerName) {
 if (layerName == "GA_BOUNDARY") return 1;  // Red
 if (layerName == "GA_PLOT") return 1;      // Red/Terracotta
 if (layerName == "GA_PROPOSED") return 30; // Architectural Orange
 if (layerName == "GA_IMPORTED_3D") return 40; // Warm Gold for imported SketchUp/3D models
 if (layerName == "GA_CONTOURS") return 252;// Soft topographic gray
 if (layerName == "GA_BUILDINGS") return 7; // White/Black ink
 if (layerName == "GA_ROADS") return 8;     // Dark gray
 if (layerName == "GA_GREEN") return 3;     // Green
 if (layerName == "GA_WATER") return 5;     // Blue
 if (layerName == "GA_PARCELS") return 6;   // Magenta
 if (layerName == "GA_TREES") return 92;    // Olive green
 return 7;
}

QString dxfLayerForFeature(const Feature &f) {
 if (f.layer == "buildings") return "GA_BUILDINGS";
 if (f.layer == "roads") return "GA_ROADS";
 if (f.layer == "green") return "GA_GREEN";
 if (f.layer == "water") return "GA_WATER";
 if (f.layer == "parcels" || f.layer == "planning") return "GA_PARCELS";
 if (f.layer == "trees") return "GA_TREES";
 return "GA_OTHER";
}

QString xmlEsc(QString s) {
 s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
 return s;
}

void writeLwPolyline(QTextStream &s, const QPolygonF &ring, const QString &layer, bool closed, double thickness = 0.0) {
 int n = ring.size();
 if (closed && n > 1 && ring.first() == ring.last()) --n;
 if (n < 2) return;
 s << "0\nLWPOLYLINE\n8\n" << layer << "\n90\n" << n << "\n70\n" << (closed ? 1 : 0) << "\n";
 if (thickness > 0.05) s << "39\n" << QString::number(thickness, 'f', 3) << "\n";
 for (int i = 0; i < n; ++i) {
  s << "10\n" << QString::number(ring[i].x(), 'f', 4) << "\n20\n" << QString::number(ring[i].y(), 'f', 4) << "\n";
 }
}

struct ColorGroup {
 QString id;
 QColor color;
 QVector<Vertex> verts;
};

void write3DFace(QTextStream &s, const QString &layer, const Vertex &a, const Vertex &b, const Vertex &c) {
 s << "0\n3DFACE\n8\n" << layer
   << "\n10\n" << a.x << "\n20\n" << a.y << "\n30\n" << a.z
   << "\n11\n" << b.x << "\n21\n" << b.y << "\n31\n" << b.z
   << "\n12\n" << c.x << "\n22\n" << c.y << "\n32\n" << c.z
   << "\n13\n" << c.x << "\n23\n" << c.y << "\n33\n" << c.z << "\n";
}

QVector<ColorGroup> buildExportMeshGroups(const Project &project,
                                          const QVector<Layer> &layers,
                                          const Palette &palette,
                                          const QString &diagram,
                                          const PlotDesign *plot = nullptr,
                                          const MassingConfig *massing = nullptr,
                                          const QVector<ImportedModel3D> *models3d = nullptr) {
 Camera c;
 c.mode = "3d";
 c.exploded = false;
 auto cleanLayers = layers;
 for (auto &l : cleanLayers) l.presentation.remove("shadow");
 auto mesh = buildMesh(project, cleanLayers, palette, c, diagram, -1, false, nullptr, plot, massing, models3d);
 mesh.triangles.append(mesh.transparent);

 QMap<QString, int> bucket;
 QVector<ColorGroup> groups;
 for (int i = 0; i + 2 < mesh.triangles.size(); i += 3) {
  const auto &v0 = mesh.triangles[i];
  const int r = qRound(std::clamp(double(v0.r), 0.0, 1.0) * 255.0);
  const int g = qRound(std::clamp(double(v0.g), 0.0, 1.0) * 255.0);
  const int b = qRound(std::clamp(double(v0.b), 0.0, 1.0) * 255.0);
  const int a = qRound(std::clamp(double(v0.a), 0.0, 1.0) * 255.0);
  const QString key = QString("mat_%1_%2_%3_%4").arg(r).arg(g).arg(b).arg(a);
  auto it = bucket.find(key);
  int idx = -1;
  if (it == bucket.end()) {
   idx = groups.size();
   bucket.insert(key, idx);
   groups.append({key, QColor(r, g, b, a), {}});
  } else {
   idx = it.value();
  }
  groups[idx].verts.append(mesh.triangles[i]);
  groups[idx].verts.append(mesh.triangles[i + 1]);
  groups[idx].verts.append(mesh.triangles[i + 2]);
 }
 return groups;
}

} // namespace

DccApps detectDccApps() {
 DccApps apps;
#ifdef Q_OS_WIN
 const QString pf = qEnvironmentVariable("ProgramFiles", "C:/Program Files");
 apps.sketchup = findLatestApp(pf + "/SketchUp", QRegularExpression("^SketchUp\\s+\\d{4}", QRegularExpression::CaseInsensitiveOption), "SketchUp.exe");
 apps.blender  = findLatestApp(pf + "/Blender Foundation", QRegularExpression("^Blender", QRegularExpression::CaseInsensitiveOption), "blender.exe");
 apps.autocad  = findLatestApp(pf + "/Autodesk", QRegularExpression("^AutoCAD\\s+\\d{4}", QRegularExpression::CaseInsensitiveOption), "acad.exe");
 apps.rhino    = findLatestApp(pf, QRegularExpression("^Rhino\\s+\\d+", QRegularExpression::CaseInsensitiveOption), "System/Rhino.exe");
#elif defined(Q_OS_MACOS)
 const QString appRoot = QStringLiteral("/Applications");
 apps.sketchup = findLatestApp(appRoot, QRegularExpression("^SketchUp\\s+\\d{4}", QRegularExpression::CaseInsensitiveOption), "SketchUp.app/Contents/MacOS/SketchUp");
 if (!apps.sketchup.found() && QFileInfo::exists(appRoot + "/SketchUp.app/Contents/MacOS/SketchUp")) {
  apps.sketchup = {QStringLiteral("SketchUp"), appRoot + "/SketchUp.app/Contents/MacOS/SketchUp"};
 }
 if (QFileInfo::exists(appRoot + "/Blender.app/Contents/MacOS/Blender")) {
  apps.blender = {QStringLiteral("Blender"), appRoot + "/Blender.app/Contents/MacOS/Blender"};
 } else {
  apps.blender = findLatestApp(appRoot, QRegularExpression("^Blender", QRegularExpression::CaseInsensitiveOption), "Blender.app/Contents/MacOS/Blender");
 }
 {
  QDir acadDir(appRoot + "/Autodesk");
  if (acadDir.exists()) {
   auto entries = acadDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
   std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) { return a > b; });
   const QRegularExpression pat("^AutoCAD\\s+\\d{4}", QRegularExpression::CaseInsensitiveOption);
   for (const auto &name : entries) {
    if (!pat.match(name).hasMatch()) continue;
    const QString bundle = acadDir.filePath(name + "/" + name + ".app");
    if (QFileInfo::exists(bundle)) { apps.autocad = {name, bundle}; break; }
   }
  }
 }
 for (const QString &rhinoName : {QStringLiteral("Rhino 8.app"), QStringLiteral("Rhino 7.app"), QStringLiteral("Rhinoceros.app")}) {
  const QString bundle = appRoot + "/" + rhinoName;
  if (QFileInfo::exists(bundle)) {
   apps.rhino = {rhinoName.chopped(4), bundle};
   break;
  }
 }
#endif
 return apps;
}

bool exportDxf(const Project &project,
               const QVector<Layer> &layers,
               const Palette &palette,
               const QString &diagram,
               const QString &filePath,
               const PlotDesign *plot,
               const MassingConfig *massing,
               const TerrainGrid *dem,
               const QVector<ImportedModel3D> *models3d) {
 Q_UNUSED(palette);
 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
 QTextStream s(&out);
 s.setRealNumberPrecision(6);

 const QStringList cadLayers = {"GA_BOUNDARY", "GA_PLOT", "GA_PROPOSED", "GA_IMPORTED_3D", "GA_CONTOURS", "GA_BUILDINGS", "GA_ROADS", "GA_GREEN", "GA_WATER", "GA_PARCELS", "GA_TREES", "GA_OTHER"};
 s << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n6\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n";
 s << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n" << cadLayers.size() << "\n";
 for (const auto &ln : cadLayers) {
  s << "0\nLAYER\n2\n" << ln << "\n70\n0\n62\n" << dxfAciColor(ln) << "\n6\nCONTINUOUS\n";
 }
 s << "0\nENDTAB\n0\nENDSEC\n";
 s << "0\nSECTION\n2\nENTITIES\n";

 // Site boundary
 writeLwPolyline(s, project.site.boundary, "GA_BOUNDARY", true, 0.0);

 // Design Plot boundary (GA_PLOT) & Proposed Concept Massing 3D solid faces + footprints (GA_PROPOSED)
 if (plot && plot->active) {
  writeLwPolyline(s, plot->polygon(), "GA_PLOT", true, 0.0);
  if (massing && massing->enabled) {
   const auto mm = evaluateMassing(*plot, *massing);
   if (mm.valid) {
    if (massing->podiumFloors > 0) writeLwPolyline(s, mm.podiumRing, "GA_PROPOSED", true, mm.podiumHeightM);
    if (massing->towerFloors > 0) writeLwPolyline(s, mm.towerRing, "GA_PROPOSED", true, mm.totalHeightM);
    Mesh propMesh;
    appendMassingToMesh(propMesh, *plot, *massing, 0.0);
    for (int i = 0; i + 2 < propMesh.triangles.size(); i += 3) {
     write3DFace(s, "GA_PROPOSED", propMesh.triangles[i], propMesh.triangles[i + 1], propMesh.triangles[i + 2]);
    }
   }
  }
 }

 // Imported 3D models (SketchUp / DAE / OBJ / GLB / STL -> GA_IMPORTED_3D footprints + 3DFACE up to 24k tris)
 if (models3d && !models3d->isEmpty()) {
  Mesh impMesh;
  appendImportedModelsToMesh(impMesh, *models3d);
  for (const auto &m : *models3d) {
   if (m.visible && m.valid()) writeLwPolyline(s, m.worldFootprint2D(), "GA_IMPORTED_3D", true, 0.0);
  }
  const int maxFaces = std::min<int>(impMesh.triangles.size() / 3, 24000);
  for (int i = 0; i < maxFaces; ++i) {
   write3DFace(s, "GA_IMPORTED_3D", impMesh.triangles[i * 3], impMesh.triangles[i * 3 + 1], impMesh.triangles[i * 3 + 2]);
  }
 }

 // Optional topographic DEM contours (GA_CONTOURS, 2 m interval marching-squares segments)
 if (dem && dem->valid && dem->maxVal > dem->minVal + 0.5) {
  const QRectF b = project.site.boundary.boundingRect();
  const int N = 36;
  const double dx = b.width() / N, dy = b.height() / N;
  const double step = (dem->maxVal - dem->minVal) > 40.0 ? 5.0 : 2.0;
  const double zStart = std::ceil(dem->minVal / step) * step;
  for (int gy = 0; gy < N; ++gy) {
   for (int gx = 0; gx < N; ++gx) {
    const QPointF p00(b.left() + gx * dx, b.top() + gy * dy);
    const QPointF p10(b.left() + (gx + 1) * dx, b.top() + gy * dy);
    const QPointF p11(b.left() + (gx + 1) * dx, b.top() + (gy + 1) * dy);
    const QPointF p01(b.left() + gx * dx, b.top() + (gy + 1) * dy);
    if (!project.site.boundary.containsPoint((p00 + p11) * 0.5, Qt::OddEvenFill)) continue;
    auto elevAt = [&](const QPointF &pt) { const auto ll = geographicPoint(pt, project.site); return dem->sample(ll.x(), ll.y(), 0.0); };
    const double v[4] = {elevAt(p00), elevAt(p10), elevAt(p11), elevAt(p01)};
    const QPointF c[4] = {p00, p10, p11, p01};
    const double lo = std::min({v[0], v[1], v[2], v[3]}), hi = std::max({v[0], v[1], v[2], v[3]});
    for (double lv = zStart; lv <= dem->maxVal; lv += step) {
     if (lv < lo || lv > hi) continue;
     QPointF hits[4]; int hc = 0;
     for (int e = 0; e < 4; ++e) {
      const int e2 = (e + 1) & 3;
      if ((v[e] <= lv && v[e2] > lv) || (v[e2] <= lv && v[e] > lv)) {
       const double t = (lv - v[e]) / (v[e2] - v[e]);
       hits[hc++] = c[e] + (c[e2] - c[e]) * t;
      }
     }
     if (hc >= 2) {
      s << "0\nLINE\n8\nGA_CONTOURS\n10\n" << hits[0].x() << "\n20\n" << hits[0].y() << "\n30\n" << lv
        << "\n11\n" << hits[1].x() << "\n21\n" << hits[1].y() << "\n31\n" << lv << "\n";
     }
    }
   }
  }
 }

 FeaturePaintCache classifier(layers, diagram);
 for (const auto &f : project.features) {
  if (f.rings.isEmpty()) continue;
  if (classifier.paints(f).isEmpty()) continue;
  const QString ln = dxfLayerForFeature(f);
  if (f.geometry == Geometry::Polygon) {
   const double thick = (f.building() && f.knownHeight()) ? f.height : 0.0;
   for (const auto &ring : f.rings) writeLwPolyline(s, ring, ln, true, thick);
  } else if (f.geometry == Geometry::Line) {
   for (const auto &ring : f.rings) writeLwPolyline(s, ring, ln, false, 0.0);
  } else if (f.geometry == Geometry::Point) {
   const auto pt = f.rings[0][0];
   s << "0\nCIRCLE\n8\n" << ln << "\n10\n" << pt.x() << "\n20\n" << pt.y() << "\n30\n0\n40\n2.0\n";
  }
 }

 s << "0\nENDSEC\n0\nEOF\n";
 s.flush();
 return out.commit();
}

bool exportSectionDxf(const SectionData &section, const QString &siteName, const QString &filePath) {
 if (section.length < 1.0) return false;
 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
 QTextStream s(&out);
 s.setRealNumberPrecision(6);

 const QVector<QPair<QString, int>> cadLayers = {
  {"SEC_GROUND", 8}, {"SEC_CUT_BUILDINGS", 7}, {"SEC_PROPOSED", 30}, {"SEC_SKYLINE", 9},
  {"SEC_WATER", 5}, {"SEC_ROADS", 30}, {"SEC_GREEN", 3}, {"SEC_ANNOTATIONS", 2}
 };
 s << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n6\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n";
 s << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n" << cadLayers.size() << "\n";
 for (const auto &p : cadLayers) {
  s << "0\nLAYER\n2\n" << p.first << "\n70\n0\n62\n" << p.second << "\n6\nCONTINUOUS\n";
 }
 s << "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";

 // Ground profile polyline + subterranean cut box
 QPolygonF groundPoly = section.groundProfile;
 const double baseDepth = section.minGround - std::max(8.0, section.length * 0.025);
 groundPoly.append({section.length, baseDepth});
 groundPoly.append({0.0, baseDepth});
 writeLwPolyline(s, groundPoly, "SEC_GROUND", true);

 // Background skyline
 for (const auto &bg : section.skyline) {
  QPolygonF rect{{bg.s0, bg.groundElev}, {bg.s1, bg.groundElev}, {bg.s1, bg.groundElev + bg.height}, {bg.s0, bg.groundElev + bg.height}};
  writeLwPolyline(s, rect, "SEC_SKYLINE", true);
 }

 // Cut buildings & Proposed Concept Massing
 for (const auto &b : section.buildings) {
  const double y0 = b.groundElev + b.baseOffset;
  const double y1 = y0 + b.height;
  QPolygonF rect{{b.s0, y0}, {b.s1, y0}, {b.s1, y1}, {b.s0, y1}};
  writeLwPolyline(s, rect, b.kind == "proposed" ? "SEC_PROPOSED" : "SEC_CUT_BUILDINGS", true);
 }

 // Water & roads
 for (const auto &w : section.waters) {
  QPolygonF rect{{w.s0, w.groundElev}, {w.s1, w.groundElev}, {w.s1, w.groundElev - 2.0}, {w.s0, w.groundElev - 2.0}};
  writeLwPolyline(s, rect, "SEC_WATER", true);
 }
 for (const auto &r : section.roads) {
  QPolygonF line{{r.s0, r.groundElev + 0.15}, {r.s1, r.groundElev + 0.15}};
  writeLwPolyline(s, line, "SEC_ROADS", false);
 }

 // Title & callouts
 s << "0\nTEXT\n8\nSEC_ANNOTATIONS\n10\n0\n20\n" << (section.maxTop + 8.0) << "\n40\n4.5\n1\n"
   << QString("SECTION A-A' | %1 | L=%2m").arg(siteName).arg(qRound(section.length)) << "\n";
 for (const auto &c : section.callouts) {
  const double ty = (c.kind == "building") ? (c.elev + 2.0) : (baseDepth - 5.0);
  s << "0\nTEXT\n8\nSEC_ANNOTATIONS\n10\n" << c.s << "\n20\n" << ty << "\n40\n2.2\n1\n" << c.label << "\n";
 }

 s << "0\nENDSEC\n0\nEOF\n";
 s.flush();
 return out.commit();
}

bool exportColladaDae(const Project &project,
                      const QVector<Layer> &layers,
                      const Palette &palette,
                      const QString &diagram,
                      const QString &filePath,
                      const PlotDesign *plot,
                      const MassingConfig *massing,
                      const QVector<ImportedModel3D> *models3d) {
 const auto groups = buildExportMeshGroups(project, layers, palette, diagram, plot, massing, models3d);
 if (groups.isEmpty()) return false;
 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
 QTextStream s(&out);
 s.setRealNumberPrecision(5);

 s << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
   << "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
   << "  <asset>\n"
   << "    <contributor><authoring_tool>GroundAtlas Desktop 2.2.0 (Pure C++/Qt)</authoring_tool></contributor>\n"
   << "    <unit name=\"meter\" meter=\"1\"/>\n"
   << "    <up_axis>Z_UP</up_axis>\n"
   << "  </asset>\n";

 // Effects
 s << "  <library_effects>\n";
 for (const auto &g : groups) {
  s << "    <effect id=\"" << g.id << "_fx\"><profile_COMMON><technique sid=\"common\"><lambert>"
    << "<diffuse><color>" << g.color.redF() << " " << g.color.greenF() << " " << g.color.blueF() << " " << g.color.alphaF() << "</color></diffuse>"
    << "</lambert></technique></profile_COMMON></effect>\n";
 }
 s << "  </library_effects>\n";

 // Materials
 s << "  <library_materials>\n";
 for (const auto &g : groups) {
  s << "    <material id=\"" << g.id << "\" name=\"" << g.id << "\"><instance_effect url=\"#" << g.id << "_fx\"/></material>\n";
 }
 s << "  </library_materials>\n";

 // Geometries
 s << "  <library_geometries>\n";
 for (int gi = 0; gi < groups.size(); ++gi) {
  const auto &g = groups[gi];
  const int vCount = g.verts.size();
  const int tCount = vCount / 3;
  const QString gid = QString("geom_%1").arg(gi);
  s << "    <geometry id=\"" << gid << "\" name=\"" << xmlEsc(g.id) << "\"><mesh>\n";
  s << "      <source id=\"" << gid << "_pos\"><float_array id=\"" << gid << "_pos_arr\" count=\"" << (vCount * 3) << "\">";
  for (const auto &v : g.verts) s << v.x << ' ' << v.y << ' ' << v.z << ' ';
  s << "</float_array><technique_common><accessor source=\"#" << gid << "_pos_arr\" count=\"" << vCount << "\" stride=\"3\">"
    << "<param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n";

  s << "      <source id=\"" << gid << "_nrm\"><float_array id=\"" << gid << "_nrm_arr\" count=\"" << (vCount * 3) << "\">";
  for (const auto &v : g.verts) s << v.nx << ' ' << v.ny << ' ' << v.nz << ' ';
  s << "</float_array><technique_common><accessor source=\"#" << gid << "_nrm_arr\" count=\"" << vCount << "\" stride=\"3\">"
    << "<param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n";

  s << "      <vertices id=\"" << gid << "_verts\"><input semantic=\"POSITION\" source=\"#" << gid << "_pos\"/></vertices>\n";
  s << "      <triangles material=\"" << g.id << "\" count=\"" << tCount << "\">"
    << "<input semantic=\"VERTEX\" source=\"#" << gid << "_verts\" offset=\"0\"/>"
    << "<input semantic=\"NORMAL\" source=\"#" << gid << "_nrm\" offset=\"0\"/>"
    << "<p>";
  for (int i = 0; i < vCount; ++i) s << i << ' ';
  s << "</p></triangles>\n    </mesh></geometry>\n";
 }
 s << "  </library_geometries>\n";

 // Visual scene
 s << "  <library_visual_scenes><visual_scene id=\"GroundAtlasScene\" name=\"" << xmlEsc(project.site.name) << "\">\n";
 for (int gi = 0; gi < groups.size(); ++gi) {
  const auto &g = groups[gi];
  const QString gid = QString("geom_%1").arg(gi);
  s << "    <node id=\"node_" << gi << "\" name=\"" << xmlEsc(g.id) << "\">"
    << "<instance_geometry url=\"#" << gid << "\"><bind_material><technique_common>"
    << "<instance_material symbol=\"" << g.id << "\" target=\"#" << g.id << "\"/>"
    << "</technique_common></bind_material></instance_geometry></node>\n";
 }
 s << "  </visual_scene></library_visual_scenes>\n"
   << "  <scene><instance_visual_scene url=\"#GroundAtlasScene\"/></scene>\n"
   << "</COLLADA>\n";

 s.flush();
 return out.commit();
}

bool exportGlb(const Project &project,
               const QVector<Layer> &layers,
               const Palette &palette,
               const QString &diagram,
               const QString &filePath,
               const PlotDesign *plot,
               const MassingConfig *massing,
               const QVector<ImportedModel3D> *models3d) {
 const auto groups = buildExportMeshGroups(project, layers, palette, diagram, plot, massing, models3d);
 if (groups.isEmpty()) return false;

 QByteArray bin;
 QJsonArray bufferViews, accessors, materials, primitives;

 for (int gi = 0; gi < groups.size(); ++gi) {
  const auto &g = groups[gi];
  const int vCount = g.verts.size();
  if (vCount < 3) continue;

  // glTF uses Y-up right-handed coordinates: (x, y, z)_site -> (x, z, -y)_gltf
  const int posOffset = bin.size();
  float minP[3] = {1e30f, 1e30f, 1e30f};
  float maxP[3] = {-1e30f, -1e30f, -1e30f};
  QByteArray posBytes;
  posBytes.resize(vCount * 12);
  QByteArray nrmBytes;
  nrmBytes.resize(vCount * 12);

  for (int i = 0; i < vCount; ++i) {
   const auto &v = g.verts[i];
   const float px = v.x, py = v.z, pz = -v.y;
   const float nx = v.nx, ny = v.nz, nz = -v.ny;
   minP[0] = std::min(minP[0], px); maxP[0] = std::max(maxP[0], px);
   minP[1] = std::min(minP[1], py); maxP[1] = std::max(maxP[1], py);
   minP[2] = std::min(minP[2], pz); maxP[2] = std::max(maxP[2], pz);
   std::memcpy(posBytes.data() + i * 12 + 0, &px, 4);
   std::memcpy(posBytes.data() + i * 12 + 4, &py, 4);
   std::memcpy(posBytes.data() + i * 12 + 8, &pz, 4);
   std::memcpy(nrmBytes.data() + i * 12 + 0, &nx, 4);
   std::memcpy(nrmBytes.data() + i * 12 + 4, &ny, 4);
   std::memcpy(nrmBytes.data() + i * 12 + 8, &nz, 4);
  }
  bin.append(posBytes);
  const int nrmOffset = bin.size();
  bin.append(nrmBytes);

  const int posBv = bufferViews.size();
  bufferViews.append(QJsonObject{{"buffer", 0}, {"byteOffset", posOffset}, {"byteLength", posBytes.size()}, {"target", 34962}});
  const int nrmBv = bufferViews.size();
  bufferViews.append(QJsonObject{{"buffer", 0}, {"byteOffset", nrmOffset}, {"byteLength", nrmBytes.size()}, {"target", 34962}});

  const int posAcc = accessors.size();
  accessors.append(QJsonObject{
   {"bufferView", posBv}, {"componentType", 5126}, {"count", vCount}, {"type", "VEC3"},
   {"min", QJsonArray{double(minP[0]), double(minP[1]), double(minP[2])}},
   {"max", QJsonArray{double(maxP[0]), double(maxP[1]), double(maxP[2])}},
  });
  const int nrmAcc = accessors.size();
  accessors.append(QJsonObject{{"bufferView", nrmBv}, {"componentType", 5126}, {"count", vCount}, {"type", "VEC3"}});

  const int matIdx = materials.size();
  QJsonObject pbr{
   {"baseColorFactor", QJsonArray{g.color.redF(), g.color.greenF(), g.color.blueF(), g.color.alphaF()}},
   {"metallicFactor", 0.05},
   {"roughnessFactor", 0.82},
  };
  QJsonObject mat{{"name", g.id}, {"pbrMetallicRoughness", pbr}, {"doubleSided", true}};
  if (g.color.alphaF() < 0.99) mat["alphaMode"] = "BLEND";
  materials.append(mat);

  primitives.append(QJsonObject{
   {"attributes", QJsonObject{{"POSITION", posAcc}, {"NORMAL", nrmAcc}}},
   {"material", matIdx},
   {"mode", 4},
  });
 }

 // Pad BIN to 4-byte boundary
 while (bin.size() % 4 != 0) bin.append('\0');

 QJsonObject gltf{
  {"asset", QJsonObject{{"version", "2.0"}, {"generator", "GroundAtlas Desktop 0.12.0 (Pure C++/Qt)"}}},
  {"scene", 0},
  {"scenes", QJsonArray{QJsonObject{{"name", project.site.name}, {"nodes", QJsonArray{0}}}}},
  {"nodes", QJsonArray{QJsonObject{{"name", project.site.name}, {"mesh", 0}}}},
  {"meshes", QJsonArray{QJsonObject{{"name", "GroundAtlasSite"}, {"primitives", primitives}}}},
  {"materials", materials},
  {"accessors", accessors},
  {"bufferViews", bufferViews},
  {"buffers", QJsonArray{QJsonObject{{"byteLength", bin.size()}}}},
 };

 QByteArray jsonBytes = QJsonDocument(gltf).toJson(QJsonDocument::Compact);
 while (jsonBytes.size() % 4 != 0) jsonBytes.append(' ');

 const quint32 totalLen = 12 + 8 + quint32(jsonBytes.size()) + 8 + quint32(bin.size());
 QByteArray glb;
 glb.reserve(int(totalLen));
 auto appendU32 = [&](quint32 v) {
  char b[4];
  qToLittleEndian(v, reinterpret_cast<uchar *>(b));
  glb.append(b, 4);
 };
 appendU32(0x46546C67); // "glTF"
 appendU32(2);          // version 2
 appendU32(totalLen);
 appendU32(quint32(jsonBytes.size()));
 appendU32(0x4E4F534A); // "JSON"
 glb.append(jsonBytes);
 appendU32(quint32(bin.size()));
 appendU32(0x004E4942); // "BIN\0"
 glb.append(bin);

 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly)) return false;
 return out.write(glb) == glb.size() && out.commit();
}

bool exportStl(const Project &project,
               const QVector<Layer> &layers,
               const Palette &palette,
               const QString &diagram,
               const QString &filePath,
               const PlotDesign *plot,
               const MassingConfig *massing,
               const QVector<ImportedModel3D> *models3d) {
 Camera c;
 c.mode = "3d";
 c.exploded = false;
 auto cleanLayers = layers;
 for (auto &l : cleanLayers) l.presentation.remove("shadow");
 auto mesh = buildMesh(project, cleanLayers, palette, c, diagram, -1, false, nullptr, plot, massing, models3d);
 mesh.triangles.append(mesh.transparent);
 const quint32 triCount = quint32(mesh.triangles.size() / 3);
 if (triCount == 0) return false;

 QByteArray stl;
 stl.reserve(84 + int(triCount) * 50);
 QByteArray header("GroundAtlas Desktop 2.2.0 Binary STL (metres)");
 header.resize(80);
 stl.append(header);
 char countBuf[4];
 qToLittleEndian(triCount, reinterpret_cast<uchar *>(countBuf));
 stl.append(countBuf, 4);

 for (quint32 i = 0; i < triCount; ++i) {
  const auto &a = mesh.triangles[int(i * 3 + 0)];
  const auto &b = mesh.triangles[int(i * 3 + 1)];
  const auto &d = mesh.triangles[int(i * 3 + 2)];
  const QVector3D e1(b.x - a.x, b.y - a.y, b.z - a.z);
  const QVector3D e2(d.x - a.x, d.y - a.y, d.z - a.z);
  QVector3D n = QVector3D::crossProduct(e1, e2);
  if (n.lengthSquared() > 1e-10f) n.normalize();
  const float vals[12] = {n.x(), n.y(), n.z(), a.x, a.y, a.z, b.x, b.y, b.z, d.x, d.y, d.z};
  stl.append(reinterpret_cast<const char *>(vals), 48);
  const quint16 attr = 0;
  stl.append(reinterpret_cast<const char *>(&attr), 2);
 }

 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly)) return false;
 return out.write(stl) == stl.size() && out.commit();
}

bool openInSketchUpWithSkpSave(const QString &daePath, const QString &skpOutputPath, QString *errorOut) {
 const auto apps = detectDccApps();
 if (!apps.sketchup.found()) {
  if (errorOut) *errorOut = "SketchUp is not installed on this computer.";
  return false;
 }
 const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/groundatlas-su";
 QDir().mkpath(tmpDir);
 const QString rbFile = tmpDir + "/import_" + QUuid::createUuid().toString(QUuid::Id128) + ".rb";

 auto rbEsc = [](QString p) { return QDir::fromNativeSeparators(p).replace('"', "\\\""); };
 QFile f(rbFile);
 if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
  if (errorOut) *errorOut = "Could not create SketchUp Ruby bridge script.";
  return false;
 }
 QTextStream s(&f);
 s << "UI.start_timer(0.8, false) do\n"
   << "  begin\n"
   << "    model = Sketchup.active_model\n"
   << "    if model\n"
   << "      opts = model.options['UnitsOptions']\n"
   << "      if opts\n"
   << "        opts['LengthFormat'] = Length::Decimal\n"
   << "        opts['LengthUnit'] = Length::Meter\n"
   << "      end\n"
   << "      src = \"" << rbEsc(QFileInfo(daePath).absoluteFilePath()) << "\"\n"
   << "      ok = begin; model.import(src, show_summary: false); rescue ArgumentError; model.import(src, false); end\n"
   << "      if ok\n"
   << "        model.active_view.zoom_extents\n";
 if (!skpOutputPath.isEmpty()) {
  s << "        model.save(\"" << rbEsc(QFileInfo(skpOutputPath).absoluteFilePath()) << "\")\n";
 }
 s << "      end\n"
   << "    end\n"
   << "  rescue => e\n"
   << "    puts e.message\n"
   << "  end\n"
   << "end\n";
 f.close();

 const bool started = QProcess::startDetached(apps.sketchup.executable, {"-RubyStartup", QDir::toNativeSeparators(rbFile)});
 if (!started && errorOut) *errorOut = "Failed to launch " + apps.sketchup.name;
 return started;
}

bool convertGlbToBlendNative(const QString &glbPath, const QString &blendOutputPath, bool launchBlenderAfter, QString *errorOut) {
 const auto apps = detectDccApps();
 if (!apps.blender.found()) {
  if (errorOut) *errorOut = "Blender is not installed on this computer.";
  return false;
 }
 const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/groundatlas-blender";
 QDir().mkpath(tmpDir);
 const QString pyFile = tmpDir + "/to_blend_" + QUuid::createUuid().toString(QUuid::Id128) + ".py";

 QFile pf(pyFile);
 if (!pf.open(QIODevice::WriteOnly | QIODevice::Text)) {
  if (errorOut) *errorOut = "Could not create Blender conversion script.";
  return false;
 }
 pf.write(R"(import sys, os, math, bpy
from mathutils import Vector
args = sys.argv[sys.argv.index('--') + 1:]
inp, outp = os.path.abspath(args[0]), os.path.abspath(args[1])
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.unit_settings.system = 'METRIC'
sc.unit_settings.scale_length = 1.0
sc.unit_settings.length_unit = 'METERS'
bpy.ops.import_scene.gltf(filepath=inp)
mn = Vector((1e18, 1e18, 1e18))
mx = Vector((-1e18, -1e18, -1e18))
cnt = 0
for o in sc.objects:
    if o.type != 'MESH': continue
    cnt += 1
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        mn.x, mn.y, mn.z = min(mn.x, w.x), min(mn.y, w.y), min(mn.z, w.z)
        mx.x, mx.y, mx.z = max(mx.x, w.x), max(mx.y, w.y), max(mx.z, w.z)
center = (mn + mx) * 0.5 if cnt else Vector((0, 0, 0))
rad = max((mx - mn).length * 0.5, 40.0) if cnt else 100.0
world = bpy.data.worlds.new('GroundAtlas_Sky')
sc.world = world
world.use_nodes = True
bg = world.node_tree.nodes.get('Background')
if bg:
    bg.inputs[0].default_value = (0.88, 0.91, 0.96, 1.0)
    bg.inputs[1].default_value = 0.85
sd = bpy.data.lights.new('GroundAtlas_Sun', type='SUN')
sd.energy = 3.2
so = bpy.data.objects.new('GroundAtlas_Sun', sd)
so.rotation_euler = (math.radians(48), math.radians(12), math.radians(-38))
sc.collection.objects.link(so)
cd = bpy.data.cameras.new('GroundAtlas_Camera')
cd.lens = 35.0
cd.clip_start = max(0.5, rad * 0.002)
cd.clip_end = max(10000.0, rad * 25.0)
co = bpy.data.objects.new('GroundAtlas_Camera', cd)
d = rad * 1.9
co.location = (center.x + d * 0.72, center.y - d * 0.72, center.z + d * 0.58)
co.rotation_euler = (center - co.location).to_track_quat('-Z', 'Y').to_euler()
sc.collection.objects.link(co)
sc.camera = co
os.makedirs(os.path.dirname(outp), exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=outp, compress=True)
)");
 pf.close();

 QProcess proc;
 proc.start(apps.blender.executable, {
  "--background", "--factory-startup", "--python", QDir::toNativeSeparators(pyFile),
  "--", QFileInfo(glbPath).absoluteFilePath(), QFileInfo(blendOutputPath).absoluteFilePath()
 });
 const bool finished = proc.waitForFinished(120000);
 QFile::remove(pyFile);
 if (!finished || proc.exitCode() != 0 || !QFileInfo::exists(blendOutputPath)) {
  if (errorOut) *errorOut = QString::fromUtf8(proc.readAllStandardError()).left(400);
  return false;
 }
 if (launchBlenderAfter) {
  QProcess::startDetached(apps.blender.executable, {QDir::toNativeSeparators(QFileInfo(blendOutputPath).absoluteFilePath())});
 }
 return true;
}

bool openInAutoCADNative(const QString &dxfPath, QString *errorOut) {
 const auto apps = detectDccApps();
 if (!apps.autocad.found()) {
  if (errorOut) *errorOut = "AutoCAD is not installed on this computer.";
  return false;
 }
 const QString absDxf = QDir::toNativeSeparators(QFileInfo(dxfPath).absoluteFilePath());
#ifdef Q_OS_MACOS
 const bool ok = apps.autocad.executable.endsWith(".app", Qt::CaseInsensitive)
  ? QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), apps.autocad.executable, absDxf})
  : QProcess::startDetached(apps.autocad.executable, {absDxf});
#else
 const bool ok = QProcess::startDetached(apps.autocad.executable, {absDxf});
#endif
 if (!ok && errorOut) *errorOut = "Failed to launch " + apps.autocad.name;
 return ok;
}

} // namespace ga
