#pragma once
#include "scene.h"
#include "section.h"
#include <QColor>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QPolygonF>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <functional>

namespace ga {

struct ImportedModel3D {
 QString id;
 QString name;
 QString sourcePath;
 QString format; // "skp", "dae", "obj", "glb", "gltf", "stl"
 bool visible = true;

 // Placement in local site metres
 double x = 0.0;
 double y = 0.0;
 double z = 0.0;
 double yawDeg = 0.0;
 double scale = 1.0;
 bool flipYUp = false;
 bool overrideColor = false;
 QColor tintColor{"#d2643c"};

 // Normalized geometry (centered at XY=(0,0), bottom flush at Z=0, units in metres)
 QVector<Vertex> rawTriangles;
 QVector3D rawSizeM{0.0f, 0.0f, 0.0f};
 QPolygonF rawFootprint2D;

 int triangleCount() const { return rawTriangles.size() / 3; }
 bool valid() const { return !rawTriangles.isEmpty(); }
 QMatrix4x4 worldMatrix(double groundZ = 0.0) const;
 QPolygonF worldFootprint2D() const;
 QVector3D effectiveSizeM() const;

 QJsonObject toJson() const;
 static ImportedModel3D fromJson(const QJsonObject &v, QString *errorOut = nullptr);
};

// Returns path to locally installed Trimble SketchUpAPI.dll if found (Windows), empty otherwise.
QString findSketchUpApiDll();
bool sketchUpDirectImportAvailable();

// Returns true for .skp, .dae, .obj, .glb, .gltf, .stl
bool isSupported3DModelFile(const QString &filePath);

// Universal pure C++20 3D importer (direct .skp via installed SketchUpAPI.dll + zero-dep .dae/.obj/.glb/.gltf/.stl)
ImportedModel3D load3DModelFile(const QString &filePath, QString *errorOut = nullptr);

// Append visible imported 3D models into the GPU scene mesh (casts real 4096px GPU shadows & SSAO!)
void appendImportedModelsToMesh(Mesh &mesh,
                                const QVector<ImportedModel3D> &models,
                                const std::function<double(const QPointF &)> &terrainZ = {});

// Slice imported 3D models against section plane A-A' for LiveSectionDock & SectionDialog
void appendImportedModelsToSection(SectionData &sec,
                                   const QVector<ImportedModel3D> &models,
                                   QPointF a,
                                   QPointF b,
                                   const TerrainGrid *dem,
                                   SectionMode mode,
                                   double &maxTop,
                                   QVector<SectionCallout> &rawCallouts);

// Compute 2D convex hull footprint from local XY vertices
QPolygonF computeModelFootprint2D(const QVector<Vertex> &tris);

} // namespace ga
