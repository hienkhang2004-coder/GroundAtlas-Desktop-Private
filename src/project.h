#pragma once
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QPolygonF>
#include <QString>
#include <QVector>
#include <limits>

namespace ga {
enum class Geometry { Point, Line, Polygon };
struct Feature {
 QString id, sourceId, layer;
 QString importId, importRecordId;
 Geometry geometry = Geometry::Polygon;
 QVector<QPolygonF> rings;
 QJsonObject tags;
 QJsonObject properties;
 QJsonArray provenance;
 double height = std::numeric_limits<double>::quiet_NaN();
 QString heightSource;
 bool building() const { return layer == "buildings"; }
 bool knownHeight() const;
 double area() const;
 QRectF bounds() const;
};
struct Site {
 QString name = "Khu đất mới";
 double lat = 21.0288, lon = 105.8525, radius = 500;
 QString shape="circle";
 double width=1000,height=1000,rotation=0;
 QPolygonF boundary;
};
struct Stats {
 int features = 0, buildings = 0, knownHeights = 0, holeCount = 0;
 double footprintArea = 0, meanHeight = 0, maxHeight = 0;
 QMap<QString,int> layerCounts;
};
struct Project {
 Site site;
 QVector<Feature> features;
 QJsonObject source, view, layerState;
 // Raw WGS84 user layers are retained independently of their currently clipped parts.
 QJsonArray imports;
 QString style = "graphite", notes;
 Stats stats() const;
 QJsonObject toJson() const;
 static Project fromJson(const QJsonObject &root);
 static Project fromGeoJson(const QJsonObject &root, const QString &name);
 static Project load(const QString &path);
 void save(const QString &path) const;
 QJsonObject geoJson() const;
 static constexpr qint64 maximumFileBytes=256LL*1024*1024;
 static constexpr int maximumGeometryParts=1000000;
 static constexpr int maximumGeometryPoints=8000000;
};
double ringArea(const QPolygonF &ring);
QString classify(const QJsonObject &tags);
QPointF localPoint(double lon, double lat, const Site &site);
QPointF geographicPoint(const QPointF &point, const Site &site);
QPolygonF circleBoundary(double radius);
QString stableSourceId(QString id);
QString errorText(const std::exception &error);
QString appDataPath(const QString &relative = {});
}

Q_DECLARE_METATYPE(ga::Project)
