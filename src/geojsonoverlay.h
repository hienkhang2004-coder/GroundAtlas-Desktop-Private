#pragma once
#include "project.h"
#include <atomic>

namespace ga {
QStringList geoJsonRoles();
QString geoJsonRoleName(const QString &role,bool english);
QString localizedImportError(const QString &message,bool english);
// These operations are transactional: neither failed imports nor cancellation mutate the source.
Project addGeoJsonOverlay(Project source,const QByteArray &bytes,const QString &name,
 const QString &role,const QString &id={},const std::atomic_bool *cancel=nullptr);
Project removeGeoJsonOverlay(Project source,const QString &id,const std::atomic_bool *cancel=nullptr);
void reclipGeoJsonOverlays(Project &project,const std::atomic_bool *cancel=nullptr);
void validateStoredOverlays(const Project &project);
QJsonArray geoJsonImportSummaries(const Project &project);
}
