#include "section.h"
#include "massing.h"
#include "model3d.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ga {
namespace {

QVector<QPair<double, double>> intersectPolygonWithSegment(const QVector<QPolygonF> &rings,
                                                           const QPointF &a,
                                                           const QPointF &u,
                                                           double length) {
 if (rings.isEmpty() || length <= 1e-4) return {};
 const QPointF n(-u.y(), u.x());
 QVector<double> cuts;
 for (const auto &ring : rings) {
  const int m = ring.size();
  if (m < 3) continue;
  for (int i = 0; i < m; ++i) {
   const QPointF p1 = ring[i] - a;
   const QPointF p2 = ring[(i + 1) % m] - a;
   const double y1 = p1.x() * n.x() + p1.y() * n.y();
   const double y2 = p2.x() * n.x() + p2.y() * n.y();
   // Half-open crossing test avoids double-counting vertices
   if ((y1 <= 0.0 && y2 > 0.0) || (y2 <= 0.0 && y1 > 0.0)) {
    const double t = -y1 / (y2 - y1);
    const double x1 = p1.x() * u.x() + p1.y() * u.y();
    const double x2 = p2.x() * u.x() + p2.y() * u.y();
    cuts.append(x1 + t * (x2 - x1));
   }
  }
 }
 if (cuts.size() < 2) return {};
 std::sort(cuts.begin(), cuts.end());
 QVector<QPair<double, double>> spans;
 for (int i = 0; i + 1 < cuts.size(); i += 2) {
  const double s0 = std::clamp(cuts[i], 0.0, length);
  const double s1 = std::clamp(cuts[i + 1], 0.0, length);
  if (s1 - s0 > 0.35) spans.append({s0, s1});
 }
 return spans;
}

double roadHalfWidth(const Feature &f) {
 const QString hw = f.tags["highway"].toString();
 if (hw == "motorway" || hw == "trunk") return 11.0;
 if (hw == "primary") return 9.0;
 if (hw == "secondary") return 7.0;
 if (hw == "tertiary") return 5.5;
 if (hw == "residential" || hw == "unclassified" || hw == "living_street") return 4.0;
 if (hw == "pedestrian" || hw == "footway" || hw == "path" || hw == "cycleway") return 2.2;
 return 4.5;
}

double sampleProfile(const QVector<QPointF> &profile, double s) {
 if (profile.isEmpty()) return 0.0;
 if (s <= profile.first().x()) return profile.first().y();
 if (s >= profile.last().x()) return profile.last().y();
 auto it = std::lower_bound(profile.begin(), profile.end(), s, [](const QPointF &p, double val) { return p.x() < val; });
 if (it == profile.begin()) return it->y();
 const auto &p1 = *it;
 const auto &p0 = *(it - 1);
 const double dx = p1.x() - p0.x();
 if (dx <= 1e-9) return p0.y();
 const double t = (s - p0.x()) / dx;
 return p0.y() * (1.0 - t) + p1.y() * t;
}

} // namespace

TerrainGrid TerrainGrid::fromEnvironmentField(const QJsonObject &field) {
 TerrainGrid g;
 const auto bbox = field["bbox"].toArray();
 const int w = field["width"].toInt();
 const int h = field["height"].toInt();
 const auto rows = field["grid"].toArray();
 if (bbox.size() != 4 || w <= 1 || h <= 1 || rows.size() != h) return g;
 g.minLon = bbox[0].toDouble();
 g.minLat = bbox[1].toDouble();
 g.maxLon = bbox[2].toDouble();
 g.maxLat = bbox[3].toDouble();
 g.width = w;
 g.height = h;
 g.source = field["source"].toString("GEDTM30");
 g.values.resize(w * h);
 const double nanVal = std::numeric_limits<double>::quiet_NaN();
 double lo = 1e9, hi = -1e9;
 for (int y = 0; y < h; ++y) {
  const auto row = rows[y].toArray();
  for (int x = 0; x < w; ++x) {
   const auto v = x < row.size() ? row[x] : QJsonValue();
   if (v.isDouble() && std::isfinite(v.toDouble())) {
    const double d = v.toDouble();
    g.values[y * w + x] = d;
    lo = std::min(lo, d);
    hi = std::max(hi, d);
   } else {
    g.values[y * w + x] = nanVal;
   }
  }
 }
 if (lo <= hi) {
  g.minVal = lo;
  g.maxVal = hi;
  g.valid = true;
 }
 return g;
}

double TerrainGrid::sample(double lon, double lat, double fallback) const {
 if (!valid || maxLon <= minLon || maxLat <= minLat) return fallback;
 const double fx = std::clamp((lon - minLon) / (maxLon - minLon), 0.0, 1.0) * (width - 1);
 const double fy = std::clamp((maxLat - lat) / (maxLat - minLat), 0.0, 1.0) * (height - 1);
 const int x0 = std::min(width - 1, int(std::floor(fx)));
 const int y0 = std::min(height - 1, int(std::floor(fy)));
 const int x1 = std::min(width - 1, x0 + 1);
 const int y1 = std::min(height - 1, y0 + 1);
 const double tx = fx - x0;
 const double ty = fy - y0;
 const double c00 = values[y0 * width + x0];
 const double c10 = values[y0 * width + x1];
 const double c01 = values[y1 * width + x0];
 const double c11 = values[y1 * width + x1];
 double sum = 0, wsum = 0;
 const auto acc = [&](double v, double weight) {
  if (std::isfinite(v)) { sum += v * weight; wsum += weight; }
 };
 acc(c00, (1 - tx) * (1 - ty));
 acc(c10, tx * (1 - ty));
 acc(c01, (1 - tx) * ty);
 acc(c11, tx * ty);
 return wsum > 1e-6 ? sum / wsum : fallback;
}

SectionData computeSection(const Project &project,
                           QPointF a,
                           QPointF b,
                           SectionMode mode,
                           const TerrainGrid *dem,
                           double contextDepth,
                           const PlotDesign *plot,
                           const MassingConfig *massing,
                           const QVector<ImportedModel3D> *models3d) {
 SectionData out;
 out.a = a;
 out.b = b;
 out.mode = mode;
 const QPointF d = b - a;
 out.length = std::hypot(d.x(), d.y());
 if (out.length < 1.0) return out;
 const QPointF u(d.x() / out.length, d.y() / out.length);
 const QPointF n(-u.y(), u.x());

 out.hasDem = (dem && dem->valid);
 const bool useDem = (mode == SectionMode::TerrainDem && out.hasDem);
 out.demNote = useDem
  ? QString("DEM %1").arg(dem->source)
  : (mode == SectionMode::FlatUrban ? QString("Flat urban datum 0.0 m") : QString("No DEM coverage · flat datum"));

 // 1. Build ground elevation profile
 const int samples = std::clamp(int(std::ceil(out.length / 4.0)), 48, 280);
 out.groundProfile.reserve(samples + 1);
 double minG = 1e9, maxG = -1e9;
 for (int i = 0; i <= samples; ++i) {
  const double s = out.length * double(i) / samples;
  double elev = 0.0;
  if (useDem) {
   const QPointF pt = a + u * s;
   const QPointF ll = geographicPoint(pt, project.site);
   elev = dem->sample(ll.x(), ll.y(), 0.0);
  }
  out.groundProfile.append({s, elev});
  minG = std::min(minG, elev);
  maxG = std::max(maxG, elev);
 }
 if (!useDem) { minG = 0.0; maxG = 0.0; }
 out.minGround = minG;
 out.maxGround = maxG;
 double maxTop = maxG + 12.0;

 // 2. Intersect every project feature with section segment A -> B
 QVector<SectionCallout> rawCallouts;
 for (const auto &f : project.features) {
  if (f.rings.isEmpty()) continue;
  const QString name = f.tags["name"].toString();

  // Bounding-box quick rejection in section local frame (s, perp)
  const QRectF br = f.bounds();
  const QPointF corners[4] = {br.topLeft(), br.topRight(), br.bottomRight(), br.bottomLeft()};
  double sMin = 1e9, sMax = -1e9, pMin = 1e9, pMax = -1e9;
  for (const auto &c : corners) {
   const QPointF rel = c - a;
   const double s = rel.x() * u.x() + rel.y() * u.y();
   const double p = rel.x() * n.x() + rel.y() * n.y();
   sMin = std::min(sMin, s); sMax = std::max(sMax, s);
   pMin = std::min(pMin, p); pMax = std::max(pMax, p);
  }
  if (sMax < 0.0 || sMin > out.length) continue;

  const bool crossesPlane = (pMin <= 0.5 && pMax >= -0.5);

  if (f.building() && f.geometry == Geometry::Polygon) {
   const double h = f.knownHeight() ? std::max(2.8, f.height) : 4.2;
   if (crossesPlane) {
    const auto spans = intersectPolygonWithSegment(f.rings, a, u, out.length);
    for (const auto &sp : spans) {
     const double mid = (sp.first + sp.second) * 0.5;
     const double ge = sampleProfile(out.groundProfile, mid);
     out.buildings.append({sp.first, sp.second, ge, h, 0.0, f.knownHeight(), "building", f.tags["building"].toString(), name, QColor("#e7e4de")});
     maxTop = std::max(maxTop, ge + h);
     if (f.knownHeight() && h >= 12.0 && (sp.second - sp.first) >= 6.0) {
      rawCallouts.append({mid, ge + h, QString::number(qRound(h)) + " m", "building"});
     }
    }
   } else if (pMin > 0.0 && pMin <= contextDepth) {
    // Background skyline projection (in front of / behind cut looking along +n)
    const double cs0 = std::clamp(sMin, 0.0, out.length);
    const double cs1 = std::clamp(sMax, 0.0, out.length);
    if (cs1 - cs0 > 1.5) {
     const double ge = sampleProfile(out.groundProfile, (cs0 + cs1) * 0.5);
     out.skyline.append({cs0, cs1, ge, h, pMin});
     maxTop = std::max(maxTop, ge + h);
    }
   }
  } else if (f.layer == "water" && f.geometry == Geometry::Polygon && crossesPlane) {
   const auto spans = intersectPolygonWithSegment(f.rings, a, u, out.length);
   for (const auto &sp : spans) {
    const double mid = (sp.first + sp.second) * 0.5;
    const double ge = sampleProfile(out.groundProfile, mid);
    out.waters.append({sp.first, sp.second, ge, 0.0, 0.0, false, "water", f.tags["natural"].toString(), name, QColor("#98c6d8")});
    if (!name.isEmpty() && sp.second - sp.first >= 8.0) {
     rawCallouts.append({mid, ge, name, "water"});
    }
   }
  } else if (f.layer == "green" && f.geometry == Geometry::Polygon && crossesPlane) {
   const auto spans = intersectPolygonWithSegment(f.rings, a, u, out.length);
   for (const auto &sp : spans) {
    const double mid = (sp.first + sp.second) * 0.5;
    const double ge = sampleProfile(out.groundProfile, mid);
    out.greens.append({sp.first, sp.second, ge, 0.0, 0.0, false, "green", f.tags["leisure"].toString(), name, QColor("#b7cbb3")});
   }
  } else if (f.layer == "roads" && f.geometry == Geometry::Line && crossesPlane) {
   const double hw = roadHalfWidth(f);
   const auto &poly = f.rings.first();
   for (int i = 0; i + 1 < poly.size(); ++i) {
    const QPointF p1 = poly[i] - a;
    const QPointF p2 = poly[i + 1] - a;
    const double y1 = p1.x() * n.x() + p1.y() * n.y();
    const double y2 = p2.x() * n.x() + p2.y() * n.y();
    if ((y1 <= 0.0 && y2 > 0.0) || (y2 <= 0.0 && y1 > 0.0)) {
     const double t = -y1 / (y2 - y1);
     const double s = (p1.x() * u.x() + p1.y() * u.y()) + t * ((p2.x() - p1.x()) * u.x() + (p2.y() - p1.y()) * u.y());
     if (s >= 0.0 && s <= out.length) {
      const double ge = sampleProfile(out.groundProfile, s);
      out.roads.append({std::max(0.0, s - hw), std::min(out.length, s + hw), ge, 0.0, 0.0, false, "road", f.tags["highway"].toString(), name, QColor("#7c8085")});
      if (!name.isEmpty()) rawCallouts.append({s, ge, name, "road"});
     }
    }
   }
  }
 }

 // 2B. Intersect Proposed Concept Massing (Podium + Tower) if active!
 if (plot && massing && plot->active && massing->enabled) {
  const auto mm = evaluateMassing(*plot, *massing);
  if (mm.valid) {
   const QColor mc = massing->accentColor.isValid() ? massing->accentColor : QColor("#d2643c");
   auto addMassingCut = [&](const QPolygonF &ring, double baseOff, double h, const QString &sub) {
    if (ring.size() < 3 || h <= 0.2) return;
    const auto spans = intersectPolygonWithSegment({ring}, a, u, out.length);
    for (const auto &sp : spans) {
     const double mid = (sp.first + sp.second) * 0.5;
     const double ge = sampleProfile(out.groundProfile, mid);
     out.buildings.append({sp.first, sp.second, ge, h, baseOff, true, "proposed", sub,
                           QString("Proposed %1 m").arg(qRound(baseOff + h)), mc});
     maxTop = std::max(maxTop, ge + baseOff + h);
     if (sub == "tower" || massing->towerFloors == 0) {
      rawCallouts.append({mid, ge + baseOff + h, QString("★ %1 m").arg(qRound(baseOff + h)), "proposed"});
     }
    }
   };
   if (massing->podiumFloors > 0) addMassingCut(mm.podiumRing, 0.0, mm.podiumHeightM, "podium");
   if (massing->towerFloors > 0) addMassingCut(mm.towerRing, mm.podiumHeightM, mm.towerHeightM, "tower");
  }
 }

 // Slice any visible imported 3D models (SketchUp / DAE / OBJ / GLB / STL) against section plane A-A'
 if (models3d && !models3d->isEmpty()) {
  appendImportedModelsToSection(out, *models3d, a, b, dem, mode, maxTop, rawCallouts);
 }

 // Sort skyline far-to-near so nearer buildings paint over distant skyline cleanly
 std::sort(out.skyline.begin(), out.skyline.end(), [](const SectionBackground &x, const SectionBackground &y) {
  return x.depth > y.depth;
 });

 // Deduplicate nearby street/water callouts so labels stay crisp and legible
 std::sort(rawCallouts.begin(), rawCallouts.end(), [](const SectionCallout &x, const SectionCallout &y) {
  return x.s < y.s;
 });
 const double minLabelGap = std::max(18.0, out.length * 0.055);
 for (const auto &c : rawCallouts) {
  bool dup = false;
  for (const auto &prev : out.callouts) {
   if (prev.kind == c.kind && (prev.label == c.label || std::abs(prev.s - c.s) < minLabelGap)) {
    dup = true;
    break;
   }
  }
  if (!dup) out.callouts.append(c);
 }

 out.maxTop = std::max(maxTop, out.maxGround + 15.0);
 return out;
}

} // namespace ga
