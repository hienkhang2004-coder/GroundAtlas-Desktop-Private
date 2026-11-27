#include "sectiondialog.h"
#include "analysis.h"
#include "exporter.h"
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPageSize>
#include <QPushButton>
#include <QSaveFile>
#include <QSlider>
#include <QSvgGenerator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

QString trBi(bool en, const char *vi, const char *enText) {
 return QString::fromUtf8(en ? enText : vi);
}

// Crisp architectural vector human silhouette (true metric height = 1.72m)
void drawHumanFigure(QPainter &p, double cx, double baseY, double pxPerM, int variant) {
 const double h = 1.72 * pxPerM;
 if (h < 5.0) return;
 p.save();
 p.setPen(Qt::NoPen);
 p.setBrush(QColor(38, 40, 44, 215));
 const double headR = h * 0.085;
 p.drawEllipse(QPointF(cx, baseY - h + headR), headR, headR);
 QPainterPath body;
 const double shoulderY = baseY - h * 0.80;
 const double hipY = baseY - h * 0.45;
 const double sw = h * 0.14;
 const double hw = h * 0.10;
 const double stride = (variant % 2 == 0) ? h * 0.13 : h * 0.06;
 body.moveTo(cx - sw, shoulderY);
 body.lineTo(cx + sw, shoulderY);
 body.lineTo(cx + hw, hipY);
 body.lineTo(cx + stride, baseY);
 body.lineTo(cx + stride - h * 0.06, baseY);
 body.lineTo(cx, hipY + h * 0.04);
 body.lineTo(cx - stride + h * 0.06, baseY);
 body.lineTo(cx - stride, baseY);
 body.lineTo(cx - hw, hipY);
 body.closeSubpath();
 p.drawPath(body);
 p.restore();
}

// Architectural layered watercolor-style tree silhouette (true scale)
void drawArchitecturalTree(QPainter &p, double cx, double baseY, double pxPerM, double heightM, int seed) {
 const double h = heightM * pxPerM;
 if (h < 5.5) return;
 p.save();
 const double trunkH = h * 0.36;
 const double crownR = h * 0.34;
 const double crownY = baseY - trunkH - crownR * 0.72;
 // Trunk
 p.setPen(QPen(QColor(92, 86, 78, 175), std::max(0.8, h * 0.055), Qt::SolidLine, Qt::RoundCap));
 p.drawLine(QPointF(cx, baseY), QPointF(cx, baseY - trunkH * 1.15));
 // Soft multi-lobe architectural foliage canopy
 p.setPen(QPen(QColor(82, 108, 76, 130), 0.7));
 const QColor c1(138, 166, 132, 135);
 const QColor c2(114, 144, 109, 125);
 const QColor c3(166, 188, 158, 125);
 const double jx = ((seed % 3) - 1) * crownR * 0.18;
 p.setBrush(c1);
 p.drawEllipse(QPointF(cx - crownR * 0.34 + jx, crownY + crownR * 0.12), crownR * 0.72, crownR * 0.64);
 p.setBrush(c2);
 p.drawEllipse(QPointF(cx + crownR * 0.32 - jx * 0.5, crownY + crownR * 0.08), crownR * 0.68, crownR * 0.62);
 p.setBrush(c3);
 p.drawEllipse(QPointF(cx, crownY - crownR * 0.22), crownR * 0.76, crownR * 0.68);
 p.restore();
}

// Architectural street car cross-section silhouette (~1.8m wide x 1.48m tall)
void drawCarSilhouette(QPainter &p, double cx, double baseY, double pxPerM) {
 const double w = 1.85 * pxPerM;
 const double h = 1.48 * pxPerM;
 if (h < 4.5) return;
 p.save();
 p.setPen(Qt::NoPen);
 p.setBrush(QColor(54, 58, 66, 200));
 const double wheelR = h * 0.16;
 p.drawRoundedRect(QRectF(cx - w * 0.5, baseY - h * 0.56, w, h * 0.42), h * 0.12, h * 0.12);
 QPolygonF cabin{
  {cx - w * 0.38, baseY - h * 0.54},
  {cx - w * 0.28, baseY - h},
  {cx + w * 0.28, baseY - h},
  {cx + w * 0.38, baseY - h * 0.54}
 };
 p.drawPolygon(cabin);
 p.drawEllipse(QPointF(cx - w * 0.33, baseY - wheelR), wheelR, wheelR);
 p.drawEllipse(QPointF(cx + w * 0.33, baseY - wheelR), wheelR, wheelR);
 p.restore();
}

class SectionMiniMap : public QWidget {
 Q_OBJECT
public:
 const ga::Project *project = nullptr;
 QPointF ptA{-400, 0};
 QPointF ptB{400, 0};
 bool english = false;
 bool dark = false;

 explicit SectionMiniMap(QWidget *parent = nullptr) : QWidget(parent) {
  setFixedSize(230, 152);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
 }

signals:
 void cutChanged(QPointF a, QPointF b);

protected:
 void paintEvent(QPaintEvent *) override {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), dark ? QColor("#181715") : QColor("#f3f4f6"));
  p.setPen(QPen(dark ? QColor("#34312d") : QColor("#dfe3ea"), 1));
  p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
  if (!project) return;

  const auto toScreen = [&](QPointF w) { return mapToWidget(w); };

  // Site boundary circle/polygon
  QPolygonF bPoly;
  for (const auto &v : project->site.boundary) bPoly.append(toScreen(v));
  p.setPen(QPen(dark ? QColor("#44403a") : QColor("#cbd2dc"), 1));
  p.setBrush(dark ? QColor("#23211e") : QColor("#e9ecef"));
  p.drawPolygon(bPoly);

  // Quick water & buildings silhouette
  p.save();
  QPainterPath clipPath;
  clipPath.addPolygon(bPoly);
  p.setClipPath(clipPath);
  p.setPen(Qt::NoPen);
  for (const auto &f : project->features) {
   if (f.rings.isEmpty()) continue;
   if (f.layer == "water" && f.geometry == ga::Geometry::Polygon) {
    p.setBrush(dark ? QColor("#295469") : QColor("#a8d0df"));
    QPolygonF poly;
    for (const auto &v : f.rings.first()) poly.append(toScreen(v));
    p.drawPolygon(poly);
   } else if (f.building() && f.geometry == ga::Geometry::Polygon) {
    p.setBrush(dark ? QColor(186, 178, 166, 95) : QColor(115, 120, 128, 120));
    QPolygonF poly;
    for (const auto &v : f.rings.first()) poly.append(toScreen(v));
    p.drawPolygon(poly);
   }
  }
  p.restore();

  // Section cut line A -> A'
  const QPointF sA = toScreen(ptA);
  const QPointF sB = toScreen(ptB);
  const QPointF dir = sB - sA;
  const double len = std::hypot(dir.x(), dir.y());

  // Subtle viewing-direction shading strip on +normal side
  if (len > 4.0) {
   const QPointF n(-dir.y() / len, dir.x() / len);
   QPolygonF lookZone{sA, sB, sB - n * 16.0, sA - n * 16.0};
   p.setPen(Qt::NoPen);
   p.setBrush(dark ? QColor(224, 145, 115, 48) : QColor(196, 86, 52, 34));
   p.drawPolygon(lookZone);
  }

  const QColor accent = dark ? QColor("#e09173") : QColor("#b94726");
  p.setPen(QPen(accent, 2.0, Qt::DashLine));
  p.drawLine(sA, sB);

  auto drawHandle = [&](QPointF pos, const QString &tag, bool active) {
   p.setPen(QPen(dark ? QColor("#181715") : Qt::white, 2.0));
   p.setBrush(active ? accent : (dark ? QColor("#f2efe9") : QColor("#242930")));
   p.drawEllipse(pos, active ? 7.5 : 6.2, active ? 7.5 : 6.2);
   p.setPen(dark ? QColor("#f2efe9") : QColor("#1f242b"));
   QFont f("Segoe UI", 8, QFont::Bold);
   p.setFont(f);
   p.drawText(QRectF(pos.x() - 14, pos.y() - 21, 28, 14), Qt::AlignCenter, tag);
  };
  drawHandle(sA, "A", dragMode_ == DragA || hoverMode_ == DragA);
  drawHandle(sB, "A'", dragMode_ == DragB || hoverMode_ == DragB);

  p.setPen(dark ? QColor("#a09990") : QColor("#6f7885"));
  p.setFont(QFont("Segoe UI", 7));
  p.drawText(rect().adjusted(8, 0, -8, -4), Qt::AlignBottom | Qt::AlignHCenter,
             trBi(english, "Kéo A / A' hoặc đường cắt", "Drag A / A' or cut line"));
 }

 void mousePressEvent(QMouseEvent *e) override {
  if (e->button() != Qt::LeftButton || !project) return;
  dragMode_ = hitTest(e->position());
  lastWorld_ = widgetToMap(e->position());
  if (dragMode_ != None) setCursor(Qt::ClosedHandCursor);
  update();
 }

 void mouseMoveEvent(QMouseEvent *e) override {
  if (!project) return;
  if (dragMode_ == None) {
   hoverMode_ = hitTest(e->position());
   setCursor(hoverMode_ == None ? Qt::ArrowCursor : Qt::OpenHandCursor);
   update();
   return;
  }
  const QPointF w = widgetToMap(e->position());
  const double lim = std::max(120.0, project->site.radius * 1.12);
  auto clampPt = [&](QPointF p) {
   const double r = std::hypot(p.x(), p.y());
   return r > lim ? p * (lim / r) : p;
  };
  if (dragMode_ == DragA) {
   const QPointF next = clampPt(w);
   if (QLineF(next, ptB).length() >= 30.0) ptA = next;
  } else if (dragMode_ == DragB) {
   const QPointF next = clampPt(w);
   if (QLineF(ptA, next).length() >= 30.0) ptB = next;
  } else if (dragMode_ == DragLine) {
   const QPointF delta = w - lastWorld_;
   ptA = clampPt(ptA + delta);
   ptB = clampPt(ptB + delta);
  }
  lastWorld_ = w;
  emit cutChanged(ptA, ptB);
  update();
 }

 void mouseReleaseEvent(QMouseEvent *) override {
  dragMode_ = None;
  setCursor(Qt::OpenHandCursor);
  update();
 }

private:
 enum DragMode { None, DragA, DragB, DragLine };
 DragMode dragMode_ = None, hoverMode_ = None;
 QPointF lastWorld_;

 double scaleFactor() const {
  if (!project) return 0.1;
  const double r = std::max(80.0, project->site.radius);
  return std::min(width() - 36.0, height() - 36.0) / (2.25 * r);
 }
 QPointF mapToWidget(QPointF w) const {
  const double s = scaleFactor();
  return QPointF(width() * 0.5 + w.x() * s, height() * 0.47 - w.y() * s);
 }
 QPointF widgetToMap(QPointF px) const {
  const double s = scaleFactor();
  return QPointF((px.x() - width() * 0.5) / s, -(px.y() - height() * 0.47) / s);
 }
 DragMode hitTest(QPointF px) const {
  if (QLineF(px, mapToWidget(ptA)).length() <= 13.0) return DragA;
  if (QLineF(px, mapToWidget(ptB)).length() <= 13.0) return DragB;
  // Distance to segment
  const QPointF a = mapToWidget(ptA), b = mapToWidget(ptB);
  const QPointF ab = b - a;
  const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
  if (l2 > 1.0) {
   const double t = std::clamp(((px.x() - a.x()) * ab.x() + (px.y() - a.y()) * ab.y()) / l2, 0.0, 1.0);
   if (QLineF(px, a + ab * t).length() <= 9.0) return DragLine;
  }
  return None;
 }
};

class SectionCanvas : public QWidget {
public:
 ga::SectionData data;
 QString siteName;
 bool english = false;
 bool dark = false;
 bool compact = false;
 double vScale = 1.0;

 explicit SectionCanvas(QWidget *parent = nullptr) : QWidget(parent) {
  setMinimumSize(320, 120);
 }

 void paintSection(QPainter &p, const QRectF &page, bool forPrint = false) const {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const bool useDark = dark && !forPrint;
  const QColor bgCol = forPrint ? Qt::white : (useDark ? QColor("#161514") : QColor("#fcfbf8"));
  const QColor gridCol = useDark ? QColor("#2a2825") : QColor("#ebe8e1");
  const QColor mutedCol = useDark ? QColor("#9a948c") : QColor("#858c96");
  const QColor inkCol = useDark ? QColor("#e8e4dc") : QColor("#2c2d30");
  const QColor earthFill = useDark ? QColor("#2b2926") : QColor("#4a4a48");
  const QColor earthStroke = useDark ? QColor("#9c958a") : QColor("#272726");
  p.fillRect(page, bgCol);

  if (data.length < 1.0) {
   p.setPen(mutedCol);
   p.drawText(page, Qt::AlignCenter, trBi(english, "Chưa có dữ liệu mặt cắt.", "No section data."));
   p.restore();
   return;
  }

  const double padL = compact ? 48.0 : 62.0;
  const double padR = compact ? 20.0 : 32.0;
  const double padT = forPrint ? 68.0 : (compact ? 14.0 : 28.0);
  const double padB = compact ? 36.0 : 112.0;
  const QRectF plot(page.left() + padL, page.top() + padT,
                    std::max(120.0, page.width() - padL - padR),
                    std::max(60.0, page.height() - padT - padB));

  if (forPrint) {
   p.setPen(QColor("#22262b"));
   p.setFont(QFont("Segoe UI", 14, QFont::DemiBold));
   p.drawText(QRectF(page.left() + padL, page.top() + 18, page.width() - padL - padR, 26),
              Qt::AlignLeft | Qt::AlignVCenter,
              QString("%1 — %2 A–A' (%3 m)").arg(siteName, trBi(english, "MẶT CẮT KIẾN TRÚC", "ARCHITECTURAL SECTION")).arg(qRound(data.length)));
   p.setPen(QColor("#6c7480"));
   p.setFont(QFont("Segoe UI", 8.5));
   p.drawText(QRectF(page.left() + padL, page.top() + 42, page.width() - padL - padR, 18),
              Qt::AlignLeft | Qt::AlignVCenter, data.demNote);
  }

  const double pxPerMX = plot.width() / data.length;
  const double rawSpanY = std::max(24.0, (data.maxTop - data.minGround) + 16.0);
  const double naturalPxPerMY = pxPerMX * vScale;
  const double targetUrbanPxPerMY = (plot.height() * (compact ? 0.68 : 0.56)) / rawSpanY * vScale;
  const double maxFitPxPerMY = (plot.height() * 0.84) / rawSpanY;
  const double pxPerMY = std::min(std::max(naturalPxPerMY, targetUrbanPxPerMY), maxFitPxPerMY);

  const double earthDepthM = std::max(compact ? 4.0 : 8.0, (plot.height() * 0.14) / std::max(0.5, pxPerMY));
  const double baseY = plot.bottom() - (compact ? 8.0 : 18.0);
  const double zeroElev = data.minGround - earthDepthM;

  auto sx = [&](double s) { return plot.left() + s * pxPerMX; };
  auto sy = [&](double elev) { return baseY - (elev - zeroElev) * pxPerMY; };

  // 1. Elevation grid & Y-axis ticks
  const double stepY = (rawSpanY > 120.0) ? 50.0 : (rawSpanY > 55.0 ? 20.0 : (rawSpanY > 25.0 ? 10.0 : 5.0));
  const double firstTick = std::floor(data.minGround / stepY) * stepY;
  p.setFont(QFont("Segoe UI", compact ? 7.2 : 8));
  for (double e = firstTick; e <= data.maxTop + stepY; e += stepY) {
   const double y = sy(e);
   if (y < plot.top() || y > baseY) continue;
   p.setPen(QPen(gridCol, 1));
   p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
   p.setPen(mutedCol);
   p.drawText(QRectF(page.left() + 4, y - 9, padL - 10, 18), Qt::AlignRight | Qt::AlignVCenter,
              QString::number(qRound(e)) + " m");
  }

  // 2. Atmospheric background skyline (buildings behind cut plane)
  p.save();
  p.setClipRect(plot);
  for (const auto &bg : data.skyline) {
   const double x0 = sx(bg.s0), x1 = sx(bg.s1);
   const double yTop = sy(bg.groundElev + bg.height);
   const double yBot = sy(bg.groundElev);
   const double fade = std::clamp(1.0 - bg.depth / 140.0, 0.25, 0.85);
   const int alpha = qRound(34 + fade * 46);
   p.setPen(QPen(useDark ? QColor(120, 116, 110, alpha + 20) : QColor(175, 173, 168, alpha + 20), 0.6));
   p.setBrush(useDark ? QColor(72, 68, 63, alpha) : QColor(218, 216, 210, alpha));
   p.drawRect(QRectF(x0, yTop, std::max(1.2, x1 - x0), std::max(1.0, yBot - yTop)));
  }

  // 3. Scale-adaptive architectural entourage (trees, people, cars on greens & roads)
  const bool showTrees = (!compact && data.length <= 800.0 && pxPerMY >= 1.2);
  const bool showStreetDetail = (!compact && data.length <= 420.0 && pxPerMY >= 2.2);
  if (showTrees) {
   int seed = 1;
   for (const auto &g : data.greens) {
    const double span = g.s1 - g.s0;
    const double spacing = std::max(10.0, data.length / 42.0);
    for (double s = g.s0 + spacing * 0.4; s < g.s1 - 2.0; s += spacing) {
     const double treeH = 5.5 + (seed % 4) * 1.6;
     drawArchitecturalTree(p, sx(s), sy(g.groundElev), pxPerMY, treeH, seed++);
    }
    if (span >= 8.0 && showStreetDetail) {
     drawHumanFigure(p, sx(g.s0 + span * 0.35), sy(g.groundElev), pxPerMY, seed++);
    }
   }
  }
  if (showStreetDetail) {
   int idx = 0;
   for (const auto &r : data.roads) {
    const double mid = (r.s0 + r.s1) * 0.5;
    const double widthM = r.s1 - r.s0;
    p.setPen(Qt::NoPen);
    p.setBrush(useDark ? QColor(120, 124, 130, 180) : QColor(115, 118, 124, 180));
    p.drawRect(QRectF(sx(r.s0), sy(r.groundElev) - 1.5, std::max(2.0, sx(r.s1) - sx(r.s0)), 3.0));
    if (widthM >= 6.0) {
     drawCarSilhouette(p, sx(mid), sy(r.groundElev), pxPerMY);
     drawHumanFigure(p, sx(r.s0 + 1.2), sy(r.groundElev), pxPerMY, idx++);
     drawHumanFigure(p, sx(r.s1 - 1.2), sy(r.groundElev), pxPerMY, idx++);
     const double lampX = sx(r.s0 + 0.5);
     const double lampTop = sy(r.groundElev + 5.8);
     p.setPen(QPen(QColor(135, 138, 144, 175), 1.0));
     p.drawLine(QPointF(lampX, sy(r.groundElev)), QPointF(lampX, lampTop));
     p.drawLine(QPointF(lampX, lampTop), QPointF(lampX + std::min(12.0, pxPerMX * 1.4), lampTop));
    }
   }
  }

  // 4. Cut buildings (foreground crisp section poche + Proposed Concept Massing accent!)
  bool hasProposed = false;
  for (const auto &b : data.buildings) {
   const double x0 = sx(b.s0), x1 = sx(b.s1);
   const double yTop = sy(b.groundElev + b.baseOffset + b.height);
   const double yBot = sy(b.groundElev + b.baseOffset);
   const QRectF rect(x0, yTop, std::max(1.8, x1 - x0), std::max(2.0, yBot - yTop));
   const bool isProp = (b.kind == "proposed");
   if (isProp) {
    hasProposed = true;
    QColor fill = b.color.isValid() ? b.color : QColor("#d2643c");
    fill.setAlpha(useDark ? 215 : 225);
    p.setBrush(fill);
    p.setPen(QPen(useDark ? QColor("#fff7ed") : fill.darker(155), 1.6));
    p.drawRect(rect);
    if (b.height >= 4.0 && rect.width() >= 6.0) {
     p.setPen(QPen(QColor(255, 255, 255, 130), 0.75));
     const double flStep = (b.subkind == "podium") ? 4.2 : 3.4;
     for (double fl = flStep; fl < b.height - 0.8; fl += flStep) {
      const double fy = sy(b.groundElev + b.baseOffset + fl);
      p.drawLine(QPointF(rect.left() + 1.0, fy), QPointF(rect.right() - 1.0, fy));
     }
    }
   } else {
    p.setBrush(useDark ? (b.knownHeight ? QColor("#36332f") : QColor("#2a2825"))
                       : (b.knownHeight ? QColor("#eae7e1") : QColor("#f2f0eb")));
    p.setPen(QPen(inkCol, b.knownHeight ? 1.35 : 0.9));
    p.drawRect(rect);
    if (!compact && data.length <= 450.0 && b.knownHeight && b.height >= 6.0 && rect.width() >= 10.0) {
     p.setPen(QPen(useDark ? QColor(180, 174, 165, 70) : QColor(120, 118, 114, 90), 0.65));
     for (double fl = 3.3; fl < b.height - 1.0; fl += 3.3) {
      const double fy = sy(b.groundElev + fl);
      p.drawLine(QPointF(rect.left() + 1.2, fy), QPointF(rect.right() - 1.2, fy));
     }
    }
   }
  }

  // 5. Cut earth mass polygon
  if (!data.groundProfile.isEmpty()) {
   QPolygonF earth;
   earth.reserve(data.groundProfile.size() + 2);
   for (const auto &gp : data.groundProfile) earth.append(QPointF(sx(gp.x()), sy(gp.y())));
   earth.append(QPointF(plot.right(), baseY));
   earth.append(QPointF(plot.left(), baseY));
   p.setPen(Qt::NoPen);
   p.setBrush(earthFill);
   p.drawPolygon(earth);

   QPainterPath gPath;
   gPath.moveTo(sx(data.groundProfile.first().x()), sy(data.groundProfile.first().y()));
   for (int i = 1; i < data.groundProfile.size(); ++i) {
    gPath.lineTo(sx(data.groundProfile[i].x()), sy(data.groundProfile[i].y()));
   }
   p.setPen(QPen(earthStroke, 1.5));
   p.setBrush(Qt::NoBrush);
   p.drawPath(gPath);
  }

  // 6. Water cut basins
  for (const auto &w : data.waters) {
   const double x0 = sx(w.s0), x1 = sx(w.s1);
   const double ySurf = sy(w.groundElev);
   const double depthPx = std::min(compact ? 10.0 : 18.0, std::max(5.0, 2.2 * pxPerMY));
   p.setPen(Qt::NoPen);
   p.setBrush(useDark ? QColor("#2f7e9c") : QColor("#92c4d6"));
   p.drawRect(QRectF(x0, ySurf, std::max(2.0, x1 - x0), depthPx));
   p.setPen(QPen(useDark ? QColor("#67c3e4") : QColor("#5b9ab3"), 1.6));
   p.drawLine(QPointF(x0, ySurf), QPointF(x1, ySurf));
  }
  p.restore();

  // 7. Callout labels (building & proposed heights above; streets & water below)
  p.setFont(QFont("Segoe UI", compact ? 7.4 : 8));
  int lowerTier = 0;
  for (const auto &c : data.callouts) {
   const double x = sx(c.s);
   if (c.kind == "building" || c.kind == "proposed") {
    const double y = sy(c.elev) - 3.0;
    if (y > plot.top() + 4.0) {
     if (c.kind == "proposed") {
      p.setFont(QFont("Segoe UI", compact ? 7.6 : 8.4, QFont::Bold));
      p.setPen(useDark ? QColor("#f7a98b") : QColor("#b94726"));
      const int pw = std::max(68, p.fontMetrics().horizontalAdvance(c.label) + 10);
      const double px = std::clamp(x - pw * 0.5, plot.left() + 2.0, std::max(plot.left() + 2.0, plot.right() - pw - 2.0));
      p.drawText(QRectF(px, y - 15, pw, 15), Qt::AlignCenter, c.label);
      p.setFont(QFont("Segoe UI", compact ? 7.4 : 8));
     } else if (!compact) {
      p.setPen(mutedCol);
      p.drawText(QRectF(x - 24, y - 14, 48, 14), Qt::AlignCenter, c.label);
     }
    }
   } else if (!compact) {
    const double y0 = sy(c.elev);
    const double y1 = baseY + 14.0 + (lowerTier % 2) * 14.0;
    ++lowerTier;
    const QColor col = (c.kind == "water") ? (useDark ? QColor("#67c3e4") : QColor("#367c99"))
                                           : (useDark ? QColor("#d9a96c") : QColor("#9a7243"));
    p.setPen(QPen(col, 1.0));
    p.drawLine(QPointF(x, y0), QPointF(x, y1));
    p.setBrush(col);
    p.drawEllipse(QPointF(x, y0), 2.3, 2.3);
    const int lblW = std::min(155, p.fontMetrics().horizontalAdvance(c.label) + 6);
    const bool flipLeft = (x + 4.0 + lblW > plot.right() + 4.0);
    p.drawText(flipLeft ? QRectF(x - lblW - 4.0, y1 - 8, lblW, 16) : QRectF(x + 4.0, y1 - 8, lblW, 16),
               (flipLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, c.label);
   }
  }

  if (compact) {
   // Compact A / distance / A' bar for bottom LiveSectionDock
   p.setPen(inkCol);
   p.setFont(QFont("Segoe UI", 8, QFont::Bold));
   p.drawText(QRectF(plot.left(), baseY + 6, 30, 16), Qt::AlignLeft, "A");
   p.drawText(QRectF(plot.right() - 105, baseY + 6, 105, 16), Qt::AlignRight,
              QString("%1 m  A'").arg(QLocale(english ? "en_US" : "vi_VN").toString(qRound(data.length))));
   const double tickStep = data.length > 700 ? 200.0 : (data.length > 300 ? 100.0 : 50.0);
   p.setFont(QFont("Segoe UI", 7.2));
   p.setPen(mutedCol);
   for (double d = tickStep; d < data.length - tickStep * 0.4; d += tickStep) {
    p.drawText(QRectF(sx(d) - 24, baseY + 6, 48, 16), Qt::AlignCenter, QString("%1 m").arg(qRound(d)));
   }
   p.restore();
   return;
  }

  // 8. Bottom land-cover ribbon strip + distance ticks
  const double stripY = baseY + 46.0;
  const double stripH = 11.0;
  p.setPen(Qt::NoPen);
  p.setBrush(useDark ? QColor("#252320") : QColor("#eceae5"));
  p.drawRect(QRectF(plot.left(), stripY, plot.width(), stripH));
  for (const auto &g : data.greens) { p.setBrush(QColor("#7ba176")); p.drawRect(QRectF(sx(g.s0), stripY, std::max(1.5, sx(g.s1) - sx(g.s0)), stripH)); }
  for (const auto &w : data.waters) { p.setBrush(useDark ? QColor("#38bdf8") : QColor("#92c4d6")); p.drawRect(QRectF(sx(w.s0), stripY, std::max(1.5, sx(w.s1) - sx(w.s0)), stripH)); }
  for (const auto &r : data.roads)  { p.setBrush(QColor("#80858c")); p.drawRect(QRectF(sx(r.s0), stripY, std::max(1.5, sx(r.s1) - sx(r.s0)), stripH)); }
  for (const auto &b : data.buildings) { p.setBrush(b.kind == "proposed" ? QColor("#d2643c") : (useDark ? QColor("#6b6760") : QColor("#cfd2d6"))); p.drawRect(QRectF(sx(b.s0), stripY, std::max(1.5, sx(b.s1) - sx(b.s0)), stripH)); }

  p.setPen(inkCol);
  p.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
  p.drawText(QRectF(plot.left(), stripY + 14, 30, 16), Qt::AlignLeft, "A");
  p.drawText(QRectF(plot.right() - 105, stripY + 14, 105, 16), Qt::AlignRight,
             QString("%1 m  A'").arg(QLocale(english ? "en_US" : "vi_VN").toString(qRound(data.length))));

  const double tickStep = data.length > 700 ? 200.0 : (data.length > 300 ? 100.0 : (data.length > 120 ? 50.0 : 20.0));
  p.setFont(QFont("Segoe UI", 7.5));
  p.setPen(mutedCol);
  for (double d = tickStep; d < data.length - tickStep * 0.4; d += tickStep) {
   p.drawText(QRectF(sx(d) - 28, stripY + 14, 56, 16), Qt::AlignCenter, QString("%1 m").arg(qRound(d)));
  }

  // 9. Compact architectural legend
  const double legY = stripY + 38.0;
  QVector<QPair<QColor, QString>> legend = {
   {earthFill, trBi(english, "Địa hình", "Ground")},
   {useDark ? QColor("#36332f") : QColor("#eae7e1"), trBi(english, "Công trình cắt", "Cut building")},
   {useDark ? QColor("#48443f") : QColor("#dad8d2"), trBi(english, "Hậu cảnh", "Skyline")},
   {QColor("#80858c"), trBi(english, "Đường", "Road")},
   {QColor("#7ba176"), trBi(english, "Mảng xanh", "Green")},
   {useDark ? QColor("#2f7e9c") : QColor("#92c4d6"), trBi(english, "Mặt nước", "Water")},
  };
  if (hasProposed) legend.prepend({QColor("#d2643c"), trBi(english, "Khối đề xuất", "Proposed")});
  double lx = plot.left();
  p.setFont(QFont("Segoe UI", 7.8));
  for (const auto &item : legend) {
   p.setPen(QPen(mutedCol, 0.7));
   p.setBrush(item.first);
   p.drawRect(QRectF(lx, legY + 2, 9, 9));
   p.setPen(mutedCol);
   const int tw = p.fontMetrics().horizontalAdvance(item.second);
   p.drawText(QRectF(lx + 13, legY - 1, tw + 6, 15), Qt::AlignLeft | Qt::AlignVCenter, item.second);
   lx += tw + 24;
  }

  p.restore();
 }

protected:
 void paintEvent(QPaintEvent *) override {
  QPainter p(this);
  paintSection(p, rect(), false);
 }
};

} // namespace

struct SectionDialog::Impl {
 ga::Project project;
 QVector<ga::Layer> layers;
 ga::Palette palette;
 ga::TerrainGrid dem;
 bool english = false;
 bool darkTheme = false;
 ga::PlotDesign plot;
 ga::MassingConfig massing;
 QVector<ga::ImportedModel3D> models3d;
 ga::SectionMode mode = ga::SectionMode::FlatUrban;
 double contextDepth = 120.0;
 SectionMiniMap *miniMap = nullptr;
 SectionCanvas *canvas = nullptr;
 QLabel *mLength = nullptr;
 QLabel *mRelief = nullptr;
 QLabel *mMaxTop = nullptr;
 QLabel *mCutCount = nullptr;
 QLabel *demBadge = nullptr;
 QComboBox *modeCombo = nullptr;
 QSlider *vScaleSlider = nullptr;
 QSlider *depthSlider = nullptr;

 void recompute() {
  canvas->data = ga::computeSection(project, miniMap->ptA, miniMap->ptB, mode, dem.valid ? &dem : nullptr, contextDepth,
                                    (plot.active && massing.enabled) ? &plot : nullptr,
                                    (plot.active && massing.enabled) ? &massing : nullptr,
                                    models3d.isEmpty() ? nullptr : &models3d);
  canvas->siteName = project.site.name;
  canvas->update();
  const auto &d = canvas->data;
  const QLocale loc(english ? "en_US" : "vi_VN");
  mLength->setText(loc.toString(qRound(d.length)) + " m");
  mRelief->setText(loc.toString(d.maxGround - d.minGround, 'f', 1) + " m");
  mMaxTop->setText(loc.toString(d.maxTop, 'f', 1) + " m");
  mCutCount->setText(loc.toString(d.buildings.size()));
  demBadge->setText(d.demNote);
 }
};

SectionDialog::SectionDialog(const ga::Project &project,
                             const QVector<ga::Layer> &layers,
                             const ga::Palette &palette,
                             bool english,
                             QWidget *parent,
                             bool darkTheme,
                             const ga::PlotDesign *plot,
                             const ga::MassingConfig *massing,
                             const QPointF *initialA,
                             const QPointF *initialB,
                             const QVector<ga::ImportedModel3D> *models3d)
 : QDialog(parent), impl_(new Impl{project, layers, palette, {}, english, darkTheme, plot ? *plot : ga::PlotDesign{}, massing ? *massing : ga::MassingConfig{}, models3d ? *models3d : QVector<ga::ImportedModel3D>{}}) {
 setWindowTitle(trBi(english, "Mặt cắt kiến trúc — GroundAtlas Desktop", "Architectural Section Studio — GroundAtlas Desktop"));
 resize(1240, 790);
 setMinimumSize(940, 620);

 // Load bundled gedtm30 elevation DEM grid if available for this site
 const auto envFields = ga::environmentFields(ga::appDataPath("environment"), project.site);
 for (const auto &field : envFields) {
  if (field["id"].toString() == "gedtm30") {
   impl_->dem = ga::TerrainGrid::fromEnvironmentField(field);
   break;
  }
 }

 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(18, 16, 18, 14);
 root->setSpacing(12);

 // Top row: interactive draggable A–A' mini-map + metric cards + mode/orientation controls
 auto *topRow = new QHBoxLayout();
 topRow->setSpacing(14);
 root->addLayout(topRow);

 impl_->miniMap = new SectionMiniMap(this);
 impl_->miniMap->project = &impl_->project;
 impl_->miniMap->english = english;
 impl_->miniMap->dark = darkTheme;
 const double r = std::max(80.0, project.site.radius * 0.92);
 impl_->miniMap->ptA = initialA ? *initialA : QPointF(-r, 0);
 impl_->miniMap->ptB = initialB ? *initialB : QPointF(r, 0);
 topRow->addWidget(impl_->miniMap);

 auto *rightTop = new QVBoxLayout();
 rightTop->setSpacing(10);
 topRow->addLayout(rightTop, 1);

 // Metric strip
 auto *metricsRow = new QHBoxLayout();
 metricsRow->setSpacing(10);
 rightTop->addLayout(metricsRow);
 auto makeCard = [&](const QString &cap, QLabel *&valOut) {
  auto *card = new QFrame(this);
  card->setObjectName("metric-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 9, 12, 9);
  cl->setSpacing(2);
  valOut = new QLabel("—", card);
  valOut->setObjectName("metric-value");
  valOut->setStyleSheet("font-size:20px;font-weight:600;");
  auto *c = new QLabel(cap, card);
  c->setObjectName("metric-caption");
  cl->addWidget(valOut);
  cl->addWidget(c);
  metricsRow->addWidget(card, 1);
 };
 makeCard(trBi(english, "Chiều dài mặt cắt", "Section length"), impl_->mLength);
 makeCard(trBi(english, "Chênh cao địa hình", "Terrain relief"), impl_->mRelief);
 makeCard(trBi(english, "Đỉnh cao nhất", "Peak elevation"), impl_->mMaxTop);
 makeCard(trBi(english, "Công trình cắt qua", "Cut buildings"), impl_->mCutCount);

 // Controls strip
 auto *ctrlRow = new QHBoxLayout();
 ctrlRow->setSpacing(8);
 rightTop->addLayout(ctrlRow);

 impl_->modeCombo = new QComboBox(this);
 impl_->modeCombo->setObjectName("section-mode");
 impl_->modeCombo->addItem(trBi(english, "Chế độ Phẳng (Đô thị)", "Flat Datum (Urban)"), int(ga::SectionMode::FlatUrban));
 impl_->modeCombo->addItem(trBi(english, "Chế độ Địa hình (DEM)", "Terrain Elevation (DEM)"), int(ga::SectionMode::TerrainDem));
 ctrlRow->addWidget(impl_->modeCombo);

 auto addPreset = [&](const QString &text, std::function<void()> fn) {
  auto *b = new QPushButton(text, this);
  connect(b, &QPushButton::clicked, this, [this, fn] { fn(); impl_->miniMap->update(); impl_->recompute(); });
  ctrlRow->addWidget(b);
 };
 addPreset(trBi(english, "Đ–T", "E–W"), [this, r] { impl_->miniMap->ptA = {-r, 0}; impl_->miniMap->ptB = {r, 0}; });
 addPreset(trBi(english, "B–N", "N–S"), [this, r] { impl_->miniMap->ptA = {0, r}; impl_->miniMap->ptB = {0, -r}; });
 addPreset("45°", [this, r] { const double d = r * 0.707; impl_->miniMap->ptA = {-d, -d}; impl_->miniMap->ptB = {d, d}; });
 addPreset(trBi(english, "⇄ Đảo A↔A'", "⇄ Flip A↔A'"), [this] { std::swap(impl_->miniMap->ptA, impl_->miniMap->ptB); });

 ctrlRow->addSpacing(8);
 ctrlRow->addWidget(new QLabel(trBi(english, "Tỉ lệ đứng", "V-Scale"), this));
 impl_->vScaleSlider = new QSlider(Qt::Horizontal, this);
 impl_->vScaleSlider->setRange(10, 25);
 impl_->vScaleSlider->setValue(10);
 impl_->vScaleSlider->setFixedWidth(84);
 ctrlRow->addWidget(impl_->vScaleSlider);

 ctrlRow->addWidget(new QLabel(trBi(english, "Hậu cảnh", "Skyline"), this));
 impl_->depthSlider = new QSlider(Qt::Horizontal, this);
 impl_->depthSlider->setRange(0, 240);
 impl_->depthSlider->setValue(120);
 impl_->depthSlider->setFixedWidth(84);
 ctrlRow->addWidget(impl_->depthSlider);
 ctrlRow->addStretch();

 impl_->demBadge = new QLabel(this);
 impl_->demBadge->setObjectName("muted");
 rightTop->addWidget(impl_->demBadge);

 // Center canvas
 impl_->canvas = new SectionCanvas(this);
 impl_->canvas->english = english;
 impl_->canvas->dark = darkTheme;
 root->addWidget(impl_->canvas, 1);

 // Bottom export bar
 auto *footRow = new QHBoxLayout();
 footRow->setSpacing(8);
 root->addLayout(footRow);

 auto *hint = new QLabel(trBi(english,
  "Kéo 2 đầu A–A' để thu hẹp mặt cắt (≤420 m tự bật cây, người & mặt cắt đường đúng tỉ lệ).",
  "Drag A–A' handles to tighten the cut (≤420 m enables true-scale trees, people & street cross-sections)."), this);
 hint->setObjectName("muted");
 footRow->addWidget(hint, 1);

 auto addExportBtn = [&](const QString &label, const QString &ext, std::function<bool(const QString &)> fn, bool primary = false) {
  auto *btn = new QPushButton(label, this);
  if (primary) btn->setObjectName("primary");
  connect(btn, &QPushButton::clicked, this, [this, ext, fn, english] {
   const QString def = impl_->project.site.name + "_section." + ext;
   const QString path = QFileDialog::getSaveFileName(this, trBi(english, "Xuất mặt cắt", "Export section"), def, ext.toUpper() + " (*." + ext + ")");
   if (path.isEmpty()) return;
   if (!fn(path)) {
    QMessageBox::warning(this, "GroundAtlas", trBi(english, "Không ghi được tệp mặt cắt.", "Could not write section file."));
   }
  });
  footRow->addWidget(btn);
 };
 addExportBtn("SVG", "svg", [this](const QString &p) { return exportSvg(p); });
 addExportBtn("PDF (A3)", "pdf", [this](const QString &p) { return exportPdf(p); });
 addExportBtn("PNG (2×)", "png", [this](const QString &p) { return exportPng(p); });
 addExportBtn(trBi(english, "Xuất DXF (AutoCAD)", "Export DXF (AutoCAD)"), "dxf", [this](const QString &p) { return exportDxf(p); }, true);

 auto *closeBtn = new QPushButton(trBi(english, "Đóng", "Close"), this);
 connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
 footRow->addWidget(closeBtn);

 // Wire live interactive controls
 connect(impl_->miniMap, &SectionMiniMap::cutChanged, this, [this] { impl_->recompute(); });
 connect(impl_->modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
  impl_->mode = static_cast<ga::SectionMode>(impl_->modeCombo->itemData(idx).toInt());
  impl_->recompute();
 });
 connect(impl_->vScaleSlider, &QSlider::valueChanged, this, [this](int v) {
  impl_->canvas->vScale = v / 10.0;
  impl_->canvas->update();
 });
 connect(impl_->depthSlider, &QSlider::valueChanged, this, [this](int v) {
  impl_->contextDepth = double(v);
  impl_->recompute();
 });

 impl_->recompute();
}

ga::SectionData SectionDialog::currentSection() const {
 return impl_->canvas->data;
}

bool SectionDialog::exportSvg(const QString &path) const {
 QSaveFile out(path);
 if (!out.open(QIODevice::WriteOnly)) return false;
 QSvgGenerator svg;
 svg.setOutputDevice(&out);
 svg.setSize({1600, 760});
 svg.setViewBox(QRect(0, 0, 1600, 760));
 svg.setTitle(impl_->project.site.name + " Section A-A'");
 QPainter p(&svg);
 impl_->canvas->paintSection(p, QRectF(0, 0, 1600, 760), true);
 p.end();
 return out.commit();
}

bool SectionDialog::exportPdf(const QString &path) const {
 QSaveFile out(path);
 if (!out.open(QIODevice::WriteOnly)) return false;
 QPdfWriter writer(&out);
 writer.setPageSize(QPageSize(QPageSize::A3));
 writer.setPageOrientation(QPageLayout::Landscape);
 writer.setResolution(120);
 writer.setTitle(impl_->project.site.name + " Section A-A'");
 writer.setCreator("GroundAtlas Desktop 2.2.0 · Pure C++/Qt");
 QPainter p(&writer);
 impl_->canvas->paintSection(p, QRectF(0, 0, writer.width(), writer.height()), true);
 p.end();
 return out.commit();
}

bool SectionDialog::exportPng(const QString &path) const {
 QImage img(2400, 1140, QImage::Format_ARGB32);
 img.fill(Qt::white);
 QPainter p(&img);
 p.scale(1.5, 1.5);
 impl_->canvas->paintSection(p, QRectF(0, 0, 1600, 760), true);
 p.end();
 return img.save(path, "PNG");
}

bool SectionDialog::exportDxf(const QString &path) const {
 return ga::exportSectionDxf(impl_->canvas->data, impl_->project.site.name, path);
}

struct LiveSectionDock::Impl {
 SectionCanvas *canvas = nullptr;
 QLabel *title = nullptr;
 QPushButton *btnEw = nullptr, *btnNs = nullptr, *btnFlip = nullptr, *btnStudio = nullptr, *btnClose = nullptr;
 bool english = false;
 bool dark = false;
};

LiveSectionDock::LiveSectionDock(QWidget *parent) : QWidget(parent), impl_(new Impl()) {
 setObjectName("live-section-dock");
 setFixedHeight(192);
 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(0, 0, 0, 0);
 root->setSpacing(0);

 auto *bar = new QFrame(this);
 bar->setObjectName("v2-header");
 bar->setFixedHeight(32);
 auto *hl = new QHBoxLayout(bar);
 hl->setContentsMargins(12, 0, 10, 0);
 hl->setSpacing(6);

 impl_->title = new QLabel("MẶT CẮT TRỰC TIẾP A–A'", bar);
 impl_->title->setObjectName("eyebrow");
 hl->addWidget(impl_->title, 1);

 auto mkBtn = [&](const QString &txt, const QString &preset = {}) {
  auto *b = new QPushButton(txt, bar);
  b->setObjectName("quick-chip");
  b->setFixedHeight(22);
  if (!preset.isEmpty()) connect(b, &QPushButton::clicked, this, [this, preset] { emit presetRequested(preset); });
  hl->addWidget(b);
  return b;
 };
 impl_->btnEw = mkBtn("Đ–T", "ew");
 impl_->btnNs = mkBtn("B–N", "ns");
 impl_->btnFlip = mkBtn("⇄ Đảo", "flip");
 impl_->btnStudio = mkBtn("Mở Studio ↗");
 connect(impl_->btnStudio, &QPushButton::clicked, this, &LiveSectionDock::openFullStudioRequested);
 impl_->btnClose = mkBtn("✕");
 impl_->btnClose->setFixedWidth(26);
 connect(impl_->btnClose, &QPushButton::clicked, this, &LiveSectionDock::closeRequested);
 root->addWidget(bar);

 impl_->canvas = new SectionCanvas(this);
 impl_->canvas->compact = true;
 root->addWidget(impl_->canvas, 1);
}

void LiveSectionDock::setDarkTheme(bool dark) {
 impl_->dark = dark;
 impl_->canvas->dark = dark;
 impl_->canvas->update();
}

void LiveSectionDock::setLanguage(bool english) {
 impl_->english = english;
 impl_->canvas->english = english;
 impl_->btnEw->setText(trBi(english, "Đ–T", "E–W"));
 impl_->btnNs->setText(trBi(english, "B–N", "N–S"));
 impl_->btnFlip->setText(trBi(english, "⇄ Đảo", "⇄ Flip"));
 impl_->btnStudio->setText(trBi(english, "Mở Studio ↗", "Full Studio ↗"));
 impl_->canvas->update();
}

void LiveSectionDock::setSectionData(const ga::SectionData &data, const QString &siteName) {
 impl_->canvas->data = data;
 impl_->canvas->siteName = siteName;
 impl_->canvas->update();
 const QLocale loc(impl_->english ? "en_US" : "vi_VN");
 impl_->title->setText(QString("%1  ·  %2 m  ·  %3 %4")
  .arg(trBi(impl_->english, "MẶT CẮT TRỰC TIẾP A–A'", "LIVE SECTION A–A'"))
  .arg(loc.toString(qRound(data.length)))
  .arg(data.buildings.size())
  .arg(trBi(impl_->english, "công trình cắt qua (kéo A / A' trên khung nhìn)", "cut buildings (drag A / A' on viewport)")));
}

#include "sectiondialog.moc"
