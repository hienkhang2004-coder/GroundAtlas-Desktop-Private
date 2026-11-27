#pragma once
#include "scene.h"

namespace ga {
// Context is presentation only. These helpers never add tiled features to a
// Project, source window, analysis input or geometry export.
QString mutedColor(const QString &hex,double amount=.28);
QJsonObject contextPalette(const QVector<Layer> &layers,const QString &paper);
QJsonObject restyleContext(const QJsonObject &original,const QVector<Layer> &layers,
                          const QString &paper,bool enabled=true);
QStringList contextPaintProperties(const QJsonObject &layer);
}
