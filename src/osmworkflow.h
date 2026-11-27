#pragma once
#include "project.h"
#include <atomic>
namespace ga {
QString osmSelectors(const QString &profile,const QString &bbox);
QString osmQuery(const Site &site,const QString &profile="all");
Project mergeOsmContext(Project base,const Project &osm,const QString &operationId,const std::atomic_bool *cancel=nullptr);
int applySmartHeightEstimates(Project &project);
int clearSmartHeightEstimates(Project &project);
}
