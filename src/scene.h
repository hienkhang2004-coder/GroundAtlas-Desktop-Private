#pragma once
#include "project.h"
#include "solar.h"
#include <QMatrix4x4>
#include <QVector3D>
#include <array>

namespace ga {
struct Layer {
 QString id,vi,en;
 QColor color;
 bool visible=true;
 double opacity=1;
 QJsonObject presentation;
 QString groupVi,groupEn;
 Layer()=default;
 Layer(QString key,QString vietnamese,QString english,QColor paint,bool shown=true):id(std::move(key)),vi(std::move(vietnamese)),en(std::move(english)),color(paint),visible(shown){}
 bool operator==(const Layer &) const = default;
};
struct Palette {
 QString id,vi,en;
 QColor paper,building,road,green,water,ink;
};
struct DiagramPreset {QString id,vi,en,styleId;QStringList layers;QJsonObject opacity;Palette palette;QStringList requiredLayers;};
void loadPresentationCatalog(const QString &file);
QVector<DiagramPreset> diagramPresets();
bool diagramMatches(const Feature &feature,const QString &diagram);
struct Vertex {float x,y,z,nx,ny,nz,r,g,b,a=1,mat=0;};
struct Mesh {QVector<Vertex> triangles,lines,transparent;int roofs=0,unknownFlat=0;};
struct Camera {
 QString mode="3d",projection="isometric";
 double yaw=-45,elevation=35.26438968,zoom=1,panX=0,panY=0;
 bool exploded=false,grid=false;
 double separation=60;
 SolarState solar;
 // Runtime framing derived from rendered geometry; never analytical/project data.
 double stackHeight=0;
 QJsonObject toJson() const;
 static Camera fromJson(const QJsonObject &value);
 QMatrix4x4 matrix(double radius,double aspect) const;
 void preset(const QString &id);
};
QVector<Layer> defaultLayers();
bool presentTag(const QJsonValue &value);
bool matchesLayer(const Feature &feature,const QString &id);
QColor detailedColor(const Feature &feature,const QString &id,const QColor &fallback);
QMap<QString,int> layerCounts(const QVector<Feature> &features);
QJsonObject layerJson(const Layer &layer);
Layer restoreLayer(const Layer &base,const QJsonObject &value);
QVector<Layer> restoreLayerState(const QVector<Layer> &base,const QJsonObject &state,const QJsonArray &order={});
QVector<Layer> presentationLayers(const QVector<Layer> &layers,const QString &style,bool diagram=false);
QVector<Layer> thematicLayers(QVector<Layer> layers,const QString &diagram);
bool diagramAvailable(const Project &project,const QString &id);
Geometry layerFeatureKind(const Feature &feature,const Layer &layer);
double lineWidthMetres(const Feature &feature,const Layer &layer,double radius);
double pointRadiusMetres(const Feature &feature,const Layer &layer,double radius);
QColor pointColor(const Feature &feature,const Layer &layer);
struct Paint {Layer layer;QColor color;double opacity=1;int order=0;Geometry kind=Geometry::Polygon;};
QVector<Paint> featurePaints(const Feature &feature,const QVector<Layer> &layers,const QString &diagram);
bool sameClassification(const Feature &a,const Feature &b);
// Reuse classification for consecutive parts of a record without dropping any
// geometry. Tree colors still follow each feature's actual identity.
class FeaturePaintCache {
public:
 FeaturePaintCache(QVector<Layer> layers,QString diagram):layers_(std::move(layers)),diagram_(std::move(diagram)){}
 const QVector<Paint> &paints(const Feature &feature);
private:
 QVector<Layer> layers_;QString diagram_;Feature previous_;QVector<Paint> paints_;bool valid_=false;
};
QColor compositePaints(const QVector<Paint> &paints);
QVector<Palette> palettes();
Palette paletteFor(const QString &id);
QColor featureColor(const Feature &f,const Palette &p,const QString &diagram,const QColor &fallback);
struct TerrainGrid;
struct PlotDesign;
struct MassingConfig;
struct ImportedModel3D;
struct FuturePlanningOverlay;
Mesh buildMesh(const Project &project,const QVector<Layer> &layers,const Palette &palette,const Camera &camera,const QString &diagram="context",int selected=-1,bool includeSunDome=true,const TerrainGrid *dem=nullptr,const PlotDesign *plot=nullptr,const MassingConfig *massing=nullptr,const QVector<ImportedModel3D> *models3d=nullptr,const FuturePlanningOverlay *planning=nullptr);
Mesh buildSunDomeMesh(const Project &project,const SolarState &solar);
double meshTopArea(const Feature &feature);
}
