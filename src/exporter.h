#pragma once
#include "project.h"
#include "scene.h"
#include "section.h"
#include "massing.h"
#include "model3d.h"
#include <QString>

namespace ga {

struct DccAppInfo {
 QString name;
 QString executable;
 bool found() const { return !executable.isEmpty(); }
};

struct DccApps {
 DccAppInfo sketchup;
 DccAppInfo blender;
 DccAppInfo autocad;
 DccAppInfo rhino;
};

DccApps detectDccApps();

// Pure C++ native CAD & 3D exporters (zero WebView/JS dependencies)
bool exportDxf(const Project &project,
               const QVector<Layer> &layers,
               const Palette &palette,
               const QString &diagram,
               const QString &filePath,
               const PlotDesign *plot = nullptr,
               const MassingConfig *massing = nullptr,
               const TerrainGrid *dem = nullptr,
               const QVector<ImportedModel3D> *models3d = nullptr);

bool exportSectionDxf(const SectionData &section,
                      const QString &siteName,
                      const QString &filePath);

bool exportColladaDae(const Project &project,
                      const QVector<Layer> &layers,
                      const Palette &palette,
                      const QString &diagram,
                      const QString &filePath,
                      const PlotDesign *plot = nullptr,
                      const MassingConfig *massing = nullptr,
                      const QVector<ImportedModel3D> *models3d = nullptr);

bool exportGlb(const Project &project,
               const QVector<Layer> &layers,
               const Palette &palette,
               const QString &diagram,
               const QString &filePath,
               const PlotDesign *plot = nullptr,
               const MassingConfig *massing = nullptr,
               const QVector<ImportedModel3D> *models3d = nullptr);

bool exportStl(const Project &project,
               const QVector<Layer> &layers,
               const Palette &palette,
               const QString &diagram,
               const QString &filePath,
               const PlotDesign *plot = nullptr,
               const MassingConfig *massing = nullptr,
               const QVector<ImportedModel3D> *models3d = nullptr);

// Native OS CAD/3D bridges using user's installed DCC tools
bool openInSketchUpWithSkpSave(const QString &daePath,
                               const QString &skpOutputPath,
                               QString *errorOut = nullptr);

bool convertGlbToBlendNative(const QString &glbPath,
                             const QString &blendOutputPath,
                             bool launchBlenderAfter,
                             QString *errorOut = nullptr);

bool openInAutoCADNative(const QString &dxfPath, QString *errorOut = nullptr);

} // namespace ga
