#pragma once
#include "project.h"
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVector>

class QNetworkReply;

namespace ga {

struct HanoiTileLayerInfo {
 QString id;
 QString vi;
 QString en;
 QString urlTemplate;
 int minZoom = 10;
 int maxZoom = 17;
};

struct HanoiTileComposite {
 QString layerId;
 int zoom = 16;
 int tilesTotal = 0;
 int tilesLoaded = 0;
 int tilesAvailable = 0;
 double siteLat = 0.0;
 double siteLon = 0.0;
 QPointF topLeft;
 QPointF topRight;
 QPointF bottomRight;
 QPointF bottomLeft;
 QImage image;

 bool valid() const { return !image.isNull() && !layerId.isEmpty() && tilesAvailable > 0; }
};

QVector<HanoiTileLayerInfo> hanoiPlanningLayers();
QString hanoiPlanningTileTemplate(const QString &layerId);
void setCustomPlanningTileTemplate(const QString &urlTemplate);
QString customPlanningTileTemplate();
bool isLikelyInHanoiArea(double lat, double lon);

class HanoiPlanningTileFetcher : public QObject {
 Q_OBJECT
public:
 explicit HanoiPlanningTileFetcher(QString cacheDirectory, QObject *parent = nullptr);
 ~HanoiPlanningTileFetcher() override;

 void fetchForSite(const Site &site, const QString &layerId = QStringLiteral("qhpk"));
 void cancel();

signals:
 void fetchProgress(int done, int total);
 void compositeReady(ga::HanoiTileComposite composite);

private:
 struct Job;
 QString cacheDirectory_;
 QNetworkAccessManager network_;
 quint64 generation_ = 0;
 QVector<QPointer<QNetworkReply>> activeReplies_;

 void finalizeJob(const std::shared_ptr<Job> &job);
};

} // namespace ga
