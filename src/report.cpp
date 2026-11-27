#include "report.h"
#include "analysis.h"
#include "planpainter.h"
#include <QJsonArray>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace ga {
namespace {

void writeDxfPolyMm(QTextStream &s, const QPolygonF &ring, const QString &layer, double scaleToMm, double ox, double oy, bool closed) {
 int n = ring.size();
 if (closed && n > 1 && ring.first() == ring.last()) --n;
 if (n < 2) return;
 s << "0\nLWPOLYLINE\n8\n" << layer << "\n90\n" << n << "\n70\n" << (closed ? 1 : 0) << "\n";
 for (int i = 0; i < n; ++i) {
  s << "10\n" << QString::number(ox + ring[i].x() * scaleToMm, 'f', 3)
    << "\n20\n" << QString::number(oy + ring[i].y() * scaleToMm, 'f', 3) << "\n";
 }
}

void drawPlanInRect(QPainter &p, const Project &project, const QVector<Layer> &layers, const Palette &pal, const QString &diagram, const QRectF &rect) {
 const auto bounds = project.site.boundary.boundingRect();
 const double sc = std::min(rect.width() / std::max(1.0, bounds.width()), rect.height() / std::max(1.0, bounds.height()));
 const auto ptFn = [&](QPointF v) {
  return rect.center() + QPointF((v.x() - bounds.center().x()) * sc, -(v.y() - bounds.center().y()) * sc);
 };
 p.save();
 p.setClipRect(rect);
 p.fillRect(rect, pal.paper);
 paintPlan(p, project, layers, pal, diagram, ptFn, sc, true, -1, false);
 p.setPen(QPen(QColor("#dad6ce"), 1));
 p.setBrush(Qt::NoBrush);
 p.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
 p.restore();
}

} // namespace

bool exportLaserCutDxf(const Project &project, const LaserCutOptions &opts, const QString &filePath) {
 if (project.features.isEmpty() || opts.scaleDenom <= 1.0) return false;
 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
 QTextStream s(&out);
 s.setRealNumberPrecision(4);

 const double mmPerM = 1000.0 / opts.scaleDenom;
 const double offset = project.site.radius * mmPerM + 15.0;

 s << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n";
 s << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n4\n"
   << "0\nLAYER\n2\nLASER_CUT_BOUNDARY\n70\n0\n62\n1\n6\nCONTINUOUS\n"
   << "0\nLAYER\n2\nLASER_CUT_BUILDINGS\n70\n0\n62\n1\n6\nCONTINUOUS\n"
   << "0\nLAYER\n2\nLASER_ENGRAVE_ROADS\n70\n0\n62\n5\n6\nCONTINUOUS\n"
   << "0\nLAYER\n2\nLASER_ENGRAVE_WATER\n70\n0\n62\n4\n6\nCONTINUOUS\n"
   << "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";

 // Sheet 1: Baseplate boundary + road/water score lines
 writeDxfPolyMm(s, project.site.boundary, "LASER_CUT_BOUNDARY", mmPerM, offset, offset, true);
 for (const auto &f : project.features) {
  if (f.rings.isEmpty()) continue;
  if (opts.engraveRoads && f.layer == "roads" && f.geometry == Geometry::Line) {
   for (const auto &r : f.rings) writeDxfPolyMm(s, r, "LASER_ENGRAVE_ROADS", mmPerM, offset, offset, false);
  } else if (opts.includeWater && f.layer == "water" && f.geometry == Geometry::Polygon) {
   for (const auto &r : f.rings) writeDxfPolyMm(s, r, "LASER_ENGRAVE_WATER", mmPerM, offset, offset, true);
  }
 }

 // Sheet 2 (shifted right if separateBaseplate): Building footprints for cut-out massing
 const double bx = opts.separateBaseplate ? (offset * 3.0 + 20.0) : offset;
 if (opts.separateBaseplate) writeDxfPolyMm(s, project.site.boundary, "LASER_CUT_BOUNDARY", mmPerM, bx, offset, true);
 for (const auto &f : project.features) {
  if (!f.building() || f.geometry != Geometry::Polygon) continue;
  for (const auto &r : f.rings) writeDxfPolyMm(s, r, "LASER_CUT_BUILDINGS", mmPerM, bx, offset, true);
 }

 s << "0\nENDSEC\n0\nEOF\n";
 s.flush();
 return out.commit();
}

bool exportLaserCutSvg(const Project &project, const LaserCutOptions &opts, const QString &filePath) {
 if (project.features.isEmpty() || opts.scaleDenom <= 1.0) return false;
 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly)) return false;
 const double mmPerM = 1000.0 / opts.scaleDenom;
 const int w = std::max(300, qRound(opts.sheetWidthMm * 3.7795));
 const int h = std::max(240, qRound(opts.sheetHeightMm * 3.7795));

 QSvgGenerator svg;
 svg.setOutputDevice(&out);
 svg.setSize({w, h});
 svg.setViewBox(QRect(0, 0, w, h));
 svg.setTitle(project.site.name + QString(" Laser Cut 1:%1").arg(qRound(opts.scaleDenom)));

 QPainter p(&svg);
 p.fillRect(QRect(0, 0, w, h), Qt::white);
 const double pxPerM = mmPerM * 3.7795;
 const QPointF c(w * 0.5, h * 0.5);
 auto mapPt = [&](QPointF q) { return QPointF(c.x() + q.x() * pxPerM, c.y() - q.y() * pxPerM); };

 // Red hairline = CUT, Blue hairline = ENGRAVE
 p.setBrush(Qt::NoBrush);
 if (opts.engraveRoads) {
  p.setPen(QPen(QColor("#0055ff"), 0.5));
  for (const auto &f : project.features) {
   if (f.layer != "roads" || f.geometry != Geometry::Line) continue;
   for (const auto &r : f.rings) {
    QPolygonF poly;
    for (const auto &pt : r) poly.append(mapPt(pt));
    p.drawPolyline(poly);
   }
  }
 }
 if (opts.includeWater) {
  p.setPen(QPen(QColor("#0099cc"), 0.5));
  for (const auto &f : project.features) {
   if (f.layer != "water" || f.geometry != Geometry::Polygon) continue;
   for (const auto &r : f.rings) {
    QPolygonF poly;
    for (const auto &pt : r) poly.append(mapPt(pt));
    p.drawPolygon(poly);
   }
  }
 }
 p.setPen(QPen(QColor("#ff0000"), 0.5));
 {
  QPolygonF bp;
  for (const auto &pt : project.site.boundary) bp.append(mapPt(pt));
  p.drawPolygon(bp);
 }
 for (const auto &f : project.features) {
  if (!f.building() || f.geometry != Geometry::Polygon) continue;
  for (const auto &r : f.rings) {
   QPolygonF poly;
   for (const auto &pt : r) poly.append(mapPt(pt));
   p.drawPolygon(poly);
  }
 }
 p.end();
 return out.commit();
}

bool exportSiteReportPdf(const Project &project,
                         const QVector<Layer> &layers,
                         const Palette &palette,
                         const QString &climateFile,
                         const QString &envDir,
                         bool english,
                         const QString &filePath,
                         const PlotDesign *plot,
                         const MassingConfig *massing) {
 if (project.features.isEmpty()) return false;
 QSaveFile out(filePath);
 if (!out.open(QIODevice::WriteOnly)) return false;

 QPdfWriter writer(&out);
 writer.setPageSize(QPageSize(QPageSize::A3));
 writer.setPageOrientation(QPageLayout::Landscape);
 writer.setResolution(120);
 writer.setTitle(project.site.name + " — Site Intelligence Report");
 writer.setCreator("GroundAtlas Desktop 2.2.0 · Pure C++/Qt");

 QPainter p(&writer);
 if (!p.isActive()) return false;
 p.setRenderHint(QPainter::Antialiasing);

 const double W = writer.width(), H = writer.height();
 const auto tr = [&](const char *vi, const char *en) { return QString::fromUtf8(english ? en : vi); };
 const QLocale loc(english ? "en_US" : "vi_VN");
 const auto stats = project.stats();
 const auto bAnalysis = buildingAnalysis(project);

 auto drawHeader = [&](const QString &kicker, const QString &title, int pageNo) {
  p.fillRect(QRectF(0, 0, W, H), QColor("#faf9f8"));
  p.setPen(QColor("#9e5a40"));
  p.setFont(QFont("Segoe UI", 9, QFont::Bold));
  p.drawText(QRectF(60, 40, W - 200, 20), Qt::AlignLeft, kicker);
  p.setPen(QColor("#1a1918"));
  p.setFont(QFont("Segoe UI", 22, QFont::DemiBold));
  p.drawText(QRectF(60, 62, W - 200, 38), Qt::AlignLeft, title);
  p.setPen(QColor("#75736f"));
  p.setFont(QFont("Segoe UI", 9));
  p.drawText(QRectF(W - 220, 48, 160, 24), Qt::AlignRight, QString("GROUNDATLAS · 0%1 / 03").arg(pageNo));
  p.setPen(QPen(QColor("#dfdcd5"), 1.2));
  p.drawLine(QPointF(60, 112), QPointF(W - 60, 112));
 };

 auto drawFooter = [&]() {
  p.setPen(QColor("#75736f"));
  p.setFont(QFont("Segoe UI", 8));
  p.drawText(QRectF(60, H - 46, W - 120, 28), Qt::AlignLeft | Qt::AlignVCenter,
             project.site.name + " · " + project.source["attribution"].toString("OpenStreetMap / Overture / GroundAtlas Native"));
 };

 // ── PAGE 1: Executive Overview + Hero Master Plan + Metrics ──
 drawHeader(tr("01 / HỒ SƠ PHÂN TÍCH KHU ĐẤT", "01 / SITE INTELLIGENCE REPORT"), project.site.name, 1);
 const QRectF heroRect(60, 136, W * 0.60, H - 210);
 drawPlanInRect(p, project, layers, palette, "context", heroRect);

 const double colX = heroRect.right() + 36;
 const double colW = W - 60 - colX;
 const double siteArea = std::max(1.0, std::abs(ringArea(project.site.boundary)));
 const double bcr = 100.0 * stats.footprintArea / siteArea;

 const QVector<QPair<QString, QString>> cards = {
  {loc.toString(stats.buildings), tr("Tổng công trình", "Total buildings")},
  {loc.toString(bcr, 'f', 1) + "%", tr("Mật độ xây dựng (BCR)", "Building coverage (BCR)")},
  {stats.knownHeights ? loc.toString(stats.meanHeight, 'f', 1) + " m" : "—", tr("Chiều cao trung bình", "Mean building height")},
  {stats.knownHeights ? loc.toString(stats.maxHeight, 'f', 1) + " m" : "—", tr("Công trình cao nhất", "Tallest building")},
 };
 double cy = 136;
 for (int i = 0; i < cards.size(); ++i) {
  const double cx = colX + (i % 2) * (colW * 0.5 + 8);
  const double cw = colW * 0.5 - 8;
  const QRectF cr(cx, cy + (i / 2) * 108, cw, 94);
  p.setPen(QPen(QColor("#e3e0d9"), 1));
  p.setBrush(Qt::white);
  p.drawRoundedRect(cr, 10, 10);
  p.setPen(QColor("#1a1918"));
  p.setFont(QFont("Segoe UI", 22, QFont::Bold));
  p.drawText(cr.adjusted(16, 14, -12, -38), Qt::AlignLeft | Qt::AlignVCenter, cards[i].first);
  p.setPen(QColor("#75736f"));
  p.setFont(QFont("Segoe UI", 9.5));
  p.drawText(cr.adjusted(16, 56, -12, -10), Qt::AlignLeft | Qt::AlignVCenter, cards[i].second);
 }

 // Height distribution bar chart on right side of Page 1
 const QRectF histRect(colX, cy + 236, colW, 250);
 p.setPen(QPen(QColor("#e3e0d9"), 1));
 p.setBrush(Qt::white);
 p.drawRoundedRect(histRect, 10, 10);
 p.setPen(QColor("#1a1918"));
 p.setFont(QFont("Segoe UI", 10.5, QFont::DemiBold));
 p.drawText(histRect.adjusted(16, 12, -16, -210), Qt::AlignLeft, tr("PHÂN BỐ CHIỀU CAO CÔNG TRÌNH (m)", "BUILDING HEIGHT DISTRIBUTION (m)"));
 const auto bins = bAnalysis["height_bins"].toArray();
 const QStringList binLabels = {"<5", "5–10", "10–20", "20–40", "40–80", "≥80"};
 double maxBin = 1.0;
 for (const auto &b : bins) maxBin = std::max(maxBin, b.toDouble());
 const double plotL = histRect.left() + 24, plotR = histRect.right() - 24, plotB = histRect.bottom() - 42, plotH = 148;
 const double slot = (plotR - plotL) / 6.0;
 for (int i = 0; i < 6 && i < bins.size(); ++i) {
  const double v = bins[i].toDouble();
  const double bh = (v / maxBin) * plotH;
  const QRectF bar(plotL + i * slot + 8, plotB - bh, slot - 16, bh);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#2c2a28"));
  p.drawRoundedRect(bar, 3, 3);
  p.setPen(QColor("#5f5c57"));
  p.setFont(QFont("Segoe UI", 8.5));
  p.drawText(QRectF(plotL + i * slot, plotB + 6, slot, 20), Qt::AlignCenter, binLabels[i]);
  if (v > 0) p.drawText(QRectF(plotL + i * slot, plotB - bh - 20, slot, 18), Qt::AlignCenter, QString::number(int(v)));
 }

 // Notes block
 const QRectF notesRect(colX, histRect.bottom() + 20, colW, heroRect.bottom() - histRect.bottom() - 20);
 if (notesRect.height() > 60) {
  p.setPen(QPen(QColor("#e3e0d9"), 1));
  p.setBrush(Qt::white);
  p.drawRoundedRect(notesRect, 10, 10);
  p.setPen(QColor("#1a1918"));
  p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
  p.drawText(notesRect.adjusted(16, 12, -16, -notesRect.height() + 34), Qt::AlignLeft, tr("TỌA ĐỘ & GHI CHÚ KHU ĐẤT", "SITE COORDINATES & NOTES"));
  p.setPen(QColor("#4a4844"));
  p.setFont(QFont("Segoe UI", 9.5));
  QString body = QString("WGS84: %1, %2  ·  R = %3 m\n").arg(project.site.lat, 0, 'f', 5).arg(project.site.lon, 0, 'f', 5).arg(qRound(project.site.radius));
  if (plot && massing && plot->active && massing->enabled) {
   const auto mm = evaluateMassing(*plot, *massing);
   if (mm.valid) {
    body += QString(tr("Khối đề xuất: BCR %1% · FAR %2 · GFA %3 m² · Cao %4 m (%5 tầng)\n",
                       "Proposed Massing: BCR %1% · FAR %2 · GFA %3 m² · H %4 m (%5 fl)\n"))
     .arg(loc.toString(mm.bcrPercent, 'f', 1)).arg(loc.toString(mm.far, 'f', 2))
     .arg(loc.toString(mm.totalGfaM2, 'f', 0)).arg(loc.toString(mm.totalHeightM, 'f', 1)).arg(mm.totalFloors);
   }
  }
  body += (project.notes.trimmed().isEmpty() ? tr("Dữ liệu hiện trạng mở dùng cho giai đoạn nghiên cứu phương án kiến trúc.", "Open-data baseline prepared for architectural site analysis.") : project.notes.trimmed());
  p.drawText(notesRect.adjusted(16, 40, -16, -12), Qt::TextWordWrap, body);
 }
 drawFooter();

 // ── PAGE 2: 2×2 Architectural Analytical Diagrams Matrix ──
 writer.newPage();
 drawHeader(tr("02 / BỘ SƠ ĐỒ PHÂN TÍCH ĐÔ THỊ", "02 / ARCHITECTURAL DIAGRAM MATRIX"), project.site.name, 2);
 const QVector<QPair<QString, QString>> diagPanels = {
  {"figure", tr("A. ĐẶC – RỖNG (FIGURE–GROUND)", "A. FIGURE–GROUND")},
  {"height", tr("B. CHIỀU CAO CÔNG TRÌNH", "B. BUILDING HEIGHTS")},
  {"roads",  tr("C. MẠNG LƯỚI GIAO THÔNG", "C. MOBILITY NETWORK")},
  {"green",  tr("D. MẢNG XANH & MẶT NƯỚC", "D. GREEN & WATER")},
 };
 const double cellW = (W - 150) * 0.5;
 const double cellH = (H - 220) * 0.5;
 for (int i = 0; i < 4; ++i) {
  const double rx = 60 + (i % 2) * (cellW + 30);
  const double ry = 132 + (i / 2) * (cellH + 24);
  p.setPen(QColor("#1a1918"));
  p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
  p.drawText(QRectF(rx, ry, cellW, 20), Qt::AlignLeft, diagPanels[i].second);
  const auto tLayers = thematicLayers(layers, diagPanels[i].first);
  drawPlanInRect(p, project, tLayers, palette, diagPanels[i].first, QRectF(rx, ry + 24, cellW, cellH - 24));
 }
 drawFooter();

 // ── PAGE 3: Architectural Section A–A' + Climate Station Summary ──
 writer.newPage();
 drawHeader(tr("03 / MẶT CẮT KIẾN TRÚC A–A' & KHÍ HẬU", "03 / ARCHITECTURAL SECTION A–A' & CLIMATE"), project.site.name, 3);
 TerrainGrid dem;
 for (const auto &f : environmentFields(envDir, project.site)) {
  if (f["id"].toString() == "gedtm30") { dem = TerrainGrid::fromEnvironmentField(f); break; }
 }
 const double r = std::max(80.0, project.site.radius * 0.92);
 const auto sec = computeSection(project, {-r, 0}, {r, 0}, SectionMode::FlatUrban, dem.valid ? &dem : nullptr, 120.0, plot, massing);

 const QRectF secBox(60, 134, W - 120, H * 0.48);
 p.setPen(QPen(QColor("#e3e0d9"), 1));
 p.setBrush(Qt::white);
 p.drawRoundedRect(secBox, 10, 10);
 if (sec.length > 1.0) {
  const QRectF sp = secBox.adjusted(64, 34, -36, -64);
  const double sxScale = sp.width() / sec.length;
  const double spanY = std::max(28.0, (sec.maxTop - sec.minGround) + 18.0);
  const double syScale = (sp.height() * 0.72) / spanY;
  const double baseY = sp.bottom() - 28.0;
  auto sx = [&](double s) { return sp.left() + s * sxScale; };
  auto sy = [&](double e) { return baseY - (e - sec.minGround) * syScale; };

  p.save();
  p.setClipRect(sp);
  for (const auto &bg : sec.skyline) {
   p.setPen(Qt::NoPen);
   p.setBrush(QColor(218, 215, 208, 90));
   p.drawRect(QRectF(sx(bg.s0), sy(bg.groundElev + bg.height), std::max(1.5, sx(bg.s1) - sx(bg.s0)), std::max(1.0, sy(bg.groundElev) - sy(bg.groundElev + bg.height))));
  }
  for (const auto &b : sec.buildings) {
   const bool isProp = (b.kind == "proposed");
   p.setBrush(isProp ? QColor("#d2643c") : QColor("#eae7e1"));
   p.setPen(QPen(isProp ? QColor("#7c2d12") : QColor("#232220"), isProp ? 1.6 : 1.3));
   const double yTop = sy(b.groundElev + b.baseOffset + b.height);
   const double yBot = sy(b.groundElev + b.baseOffset);
   p.drawRect(QRectF(sx(b.s0), yTop, std::max(2.0, sx(b.s1) - sx(b.s0)), std::max(2.0, yBot - yTop)));
  }
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#474644"));
  p.drawRect(QRectF(sp.left(), baseY, sp.width(), 24));
  for (const auto &w : sec.waters) {
   p.setBrush(QColor("#92c4d6"));
   p.drawRect(QRectF(sx(w.s0), baseY, std::max(2.0, sx(w.s1) - sx(w.s0)), 10));
  }
  p.restore();
  p.setPen(QColor("#4a4844"));
  p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
  p.drawText(QRectF(sp.left(), sp.bottom() + 6, 40, 18), Qt::AlignLeft, "A");
  p.drawText(QRectF(sp.right() - 120, sp.bottom() + 6, 120, 18), Qt::AlignRight, QString("%1 m  A'").arg(qRound(sec.length)));
 }

 // Bottom climate summary card
 const auto station = nearestClimate(climateFile, project.site);
 const QRectF climBox(60, secBox.bottom() + 24, W - 120, H - secBox.bottom() - 88);
 p.setPen(QPen(QColor("#e3e0d9"), 1));
 p.setBrush(Qt::white);
 p.drawRoundedRect(climBox, 10, 10);
 p.setPen(QColor("#1a1918"));
 p.setFont(QFont("Segoe UI", 11, QFont::DemiBold));
 if (!station.isEmpty()) {
  const auto clim = station["climate"].toObject();
  const auto months = clim["monthly"].toArray();
  p.drawText(climBox.adjusted(20, 14, -20, -climBox.height() + 40), Qt::AlignLeft,
             tr("TRẠM KHÍ HẬU TMY GẦN NHẤT: ", "NEAREST TMY CLIMATE STATION: ") + station["station"].toString()
             + QString(" (%1 km · %2)").arg(station["distance_km"].toDouble(), 0, 'f', 1).arg(clim["period"].toString()));
  // Draw compact 12-month temperature + wind summary bars
  const double gx = climBox.left() + 36, gw = climBox.width() - 72, gy = climBox.bottom() - 36, gh = std::max(50.0, climBox.height() - 96);
  const double mSlot = gw / 12.0;
  for (int m = 0; m < 12 && m < months.size(); ++m) {
   const double tVal = months[m].toObject()["temperature"].toDouble(24.0);
   const double bh = std::clamp((tVal / 40.0) * gh, 8.0, gh);
   p.setPen(Qt::NoPen);
   p.setBrush(QColor("#c47a56"));
   p.drawRoundedRect(QRectF(gx + m * mSlot + 10, gy - bh, mSlot - 20, bh), 3, 3);
   p.setPen(QColor("#5f5c57"));
   p.setFont(QFont("Segoe UI", 8.5));
   p.drawText(QRectF(gx + m * mSlot, gy + 5, mSlot, 18), Qt::AlignCenter, QString("T%1").arg(m + 1));
   p.drawText(QRectF(gx + m * mSlot, gy - bh - 18, mSlot, 16), Qt::AlignCenter, QString::number(tVal, 'f', 1) + "°");
  }
 } else {
  p.drawText(climBox.adjusted(20, 18, -20, -18), Qt::AlignVCenter,
             tr("Chưa có trạm khí hậu TMY đi kèm trong phạm vi 100 km quanh khu đất này.", "No bundled TMY climate station within 100 km of this site."));
 }
 drawFooter();

 p.end();
 return out.commit();
}

} // namespace ga
