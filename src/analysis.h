#pragma once
#include "project.h"
#include <QImage>
namespace ga {
double distanceKm(double lat1,double lon1,double lat2,double lon2);
QJsonObject nearestClimate(const QString &file,const Site &site);
QVector<QJsonObject> environmentFields(const QString &directory,const Site &site);
QImage gridImage(const QJsonObject &field);
QColor rampColor(double value);
QJsonObject buildingAnalysis(const Project &project);
QJsonObject dataQuality(const Project &project);
}
