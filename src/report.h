#pragma once
#include "project.h"
#include "scene.h"
#include "section.h"
#include "massing.h"
#include <QString>

namespace ga {

struct LaserCutOptions {
 double scaleDenom = 1000.0; // e.g. 1:1000
 double sheetWidthMm = 600.0;
 double sheetHeightMm = 400.0;
 bool separateBaseplate = true;
 bool engraveRoads = true;
 bool includeWater = true;
};

// Pure C++ Laser Cut DXF/SVG generator in true mm scale!
bool exportLaserCutDxf(const Project &project, const LaserCutOptions &opts, const QString &filePath);
bool exportLaserCutSvg(const Project &project, const LaserCutOptions &opts, const QString &filePath);

// Pure C++ Multi-Page A3 Architectural Site Intelligence PDF Board
bool exportSiteReportPdf(const Project &project,
                         const QVector<Layer> &layers,
                         const Palette &palette,
                         const QString &climateFile,
                         const QString &envDir,
                         bool english,
                         const QString &filePath,
                         const PlotDesign *plot = nullptr,
                         const MassingConfig *massing = nullptr);

} // namespace ga
