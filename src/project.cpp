#include "project.h"
#include "sitegeometry.h"
#include "geojsonoverlay.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

namespace ga {
namespace {
constexpr int maxFeatures=Project::maximumGeometryParts, maxPoints=Project::maximumGeometryPoints;
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
bool finite(double value) { return std::isfinite(value); }
double requiredNumber(const QJsonValue &v) {
 if(!v.isDouble() || !finite(v.toDouble())) fail("Invalid or missing numeric coordinate");
 return v.toDouble();
}
QPolygonF readRing(const QJsonArray &a, int &pointCount, bool geographic=false) {
 if(a.isEmpty()) fail("Empty geometry");
 QPolygonF ring;ring.reserve(a.size());
 for(const auto &v:a) {
  const auto pair=v.toArray();if(pair.size()<2) fail("Invalid coordinate pair");
  const double x=requiredNumber(pair[0]),y=requiredNumber(pair[1]);
  if(std::abs(x)>(geographic?180:2000000)||std::abs(y)>(geographic?85:2000000))fail("Coordinate is outside the supported extent");
  if(++pointCount>maxPoints)fail("Geometry exceeds eight million points");
  ring.append(QPointF(x,y));
 }
 return ring;
}
QJsonArray ringJson(const QPolygonF &ring) { QJsonArray out;for(const auto &p:ring)out.append(QJsonArray{p.x(),p.y()});return out; }
bool present(const QJsonObject &t,const QString &key) {return t.contains(key)&&!t[key].isNull()&&t[key].toVariant().toString()!="no"&& !t[key].toVariant().toString().isEmpty();}
double heightNumber(const QJsonValue &value) {
 if(value.isDouble()) return value.toDouble();
 if(value.isString()) {auto m=QRegularExpression("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*(?:m)?\\s*$").match(value.toString());if(m.hasMatch())return m.captured(1).toDouble();}
 return std::numeric_limits<double>::quiet_NaN();
}
void deriveHeight(Feature &f) {
 if(!f.building())return;
 double value=heightNumber(f.tags.value("height_m"));if(!finite(value))value=heightNumber(f.tags.value("height"));
 if(finite(value)&&value>0&&value<=2000){f.height=value;f.heightSource=f.tags.value("height_source").toString("recorded");return;}
 const double levels=heightNumber(f.tags.value("building:levels"));if(finite(levels)&&levels>0&&levels<=200){f.height=levels*3;f.heightSource="floor-estimate-3m";}
}
void validateSite(const Site &site) {
 if(!finite(site.lat)||!finite(site.lon)||!finite(site.radius)||std::abs(site.lat)>85||std::abs(site.lon)>180||site.radius<=0||site.radius>100000)fail("Invalid site extent (maximum radius: 100 km)");
}
}
bool Feature::knownHeight() const {return finite(height)&&height>0&&height<=3600;}
double ringArea(const QPolygonF &ring) {double a=0;for(int i=0;i<ring.size();++i){const auto &p=ring[i],&q=ring[(i+1)%ring.size()];a+=p.x()*q.y()-q.x()*p.y();}return a/2;}
double Feature::area() const {if(geometry!=Geometry::Polygon||rings.isEmpty())return 0;double a=std::abs(ringArea(rings[0]));for(int i=1;i<rings.size();++i)a-=std::abs(ringArea(rings[i]));return std::max(0.,a);}
QRectF Feature::bounds() const {return rings.isEmpty()?QRectF():rings.first().boundingRect();}
QString stableSourceId(QString id) {return id.replace(QRegularExpression("(?:_clip[0-9]+|_seg[0-9]+)$"),"");}
QPointF localPoint(double lon,double lat,const Site &s) {return {(lon-s.lon)*111320*std::cos(s.lat*M_PI/180),(lat-s.lat)*111320};}
QPointF geographicPoint(const QPointF &p,const Site &s) {return {s.lon+p.x()/(111320*std::cos(s.lat*M_PI/180)),s.lat+p.y()/111320};}
QPolygonF circleBoundary(double r) {QPolygonF p;for(int i=0;i<96;++i)p.append({r*std::cos(i*M_PI/48),r*std::sin(i*M_PI/48)});return p;}
QString classify(const QJsonObject &t) {
 if(present(t,"building"))return "buildings";
 if(t["natural"].toString()=="tree")return "trees";
 if(t["natural"].toString()=="water"||present(t,"water")||present(t,"waterway"))return "water";
 if(present(t,"highway"))return QSet<QString>{"footway","pedestrian","path","steps","living_street"}.contains(t["highway"].toString())?"walk":"roads";
 if(present(t,"railway"))return "rail";
 if(present(t,"historic")||present(t,"heritage"))return "heritage";
 if(present(t,"amenity")||present(t,"shop")||present(t,"tourism"))return "amenities";
 if(QSet<QString>{"park","garden","nature_reserve"}.contains(t["leisure"].toString())||QSet<QString>{"wood","grassland","scrub","wetland"}.contains(t["natural"].toString())||QSet<QString>{"forest","grass","recreation_ground","meadow","orchard"}.contains(t["landuse"].toString()))return "green";
 if(present(t,"landuse"))return "landuse";
 return "context";
}
Stats Project::stats() const {
 Stats s;QMap<QString,double> heights;QSet<QString> buildings;
 for(const auto &f:features){++s.features;++s.layerCounts[f.layer];if(f.geometry==Geometry::Polygon)s.holeCount+=std::max(0,int(f.rings.size())-1);
  if(f.building()){const QString id=f.sourceId.isEmpty()?stableSourceId(f.id):f.sourceId;buildings.insert(id);s.footprintArea+=f.area();if(f.knownHeight())heights[id]=f.height;}}
 s.buildings=buildings.size();s.knownHeights=heights.size();for(double h:heights){s.meanHeight+=h;s.maxHeight=std::max(s.maxHeight,h);}if(s.knownHeights)s.meanHeight/=s.knownHeights;return s;
}
QJsonObject Project::toJson() const {
 // Geometry parts keep their own identity and coordinates. Repeated source
 // metadata is interned once, avoiding hundreds of thousands of copies in the
 // file and in the reader's JSON tree. Schemas 1–3 still read inline metadata.
 QJsonArray rows,templates;QHash<QByteArray,int> indices;QJsonObject previous;int previousIndex=-1;
 for(const auto &f:features){
  QJsonObject meta{{"source_id",f.sourceId},{"layer",f.layer},{"tags",f.tags},{"height_source",f.heightSource},{"properties",f.properties},{"provenance",f.provenance}};
  if(!f.importId.isEmpty()){meta["import_id"]=f.importId;meta["import_record_id"]=f.importRecordId;}if(f.knownHeight())meta["height"]=f.height;
  int index=previousIndex;if(previousIndex<0||meta!=previous){const auto key=QJsonDocument(meta).toJson(QJsonDocument::Compact);const auto found=indices.constFind(key);if(found==indices.cend()){index=templates.size();indices.insert(key,index);templates.append(meta);}else index=found.value();previous=meta;previousIndex=index;}
  QJsonArray rings;for(const auto &r:f.rings)rings.append(ringJson(r));rows.append(QJsonObject{{"id",f.id},{"template",index},{"geometry",f.geometry==Geometry::Point?"Point":f.geometry==Geometry::Line?"LineString":"Polygon"},{"rings",rings}});
 }
 return {{"format","groundatlas-native"},{"schema_version",4},{"site",QJsonObject{{"name",site.name},{"lat",site.lat},{"lon",site.lon},{"radius",site.radius},{"shape",site.shape},{"width",site.width},{"height",site.height},{"rotation",site.rotation},{"boundary",ringJson(site.boundary)}}},{"features",rows},{"feature_templates",templates},{"imports",imports},{"source",source},{"view",view},{"layers",layerState},{"style",style},{"notes",notes}};
}
Project Project::fromJson(const QJsonObject &root) {
 if(root["format"].toString()!="groundatlas-native"||root["schema_version"].toInt()<1||root["schema_version"].toInt()>4)fail("Unsupported GroundAtlas project version");
 Project p;const auto s=root["site"].toObject();p.site.name=s["name"].toString("Untitled");p.site.lat=requiredNumber(s["lat"]);p.site.lon=requiredNumber(s["lon"]);p.site.radius=requiredNumber(s["radius"]);validateSite(p.site);p.site.shape=s["shape"].toString("circle");if(!QSet<QString>{"circle","square","rectangle","polygon"}.contains(p.site.shape))fail("Unknown site shape");p.site.width=s["width"].toDouble(p.site.radius*2);p.site.height=s["height"].toDouble(p.site.radius*2);p.site.rotation=s["rotation"].toDouble();
 int points=0;if(s["boundary"].toArray().size()>=3)p.site.boundary=readRing(s["boundary"].toArray(),points);else p.site.boundary=primitiveBoundary(p.site);if(p.site.boundary.size()>1&&p.site.boundary.first()==p.site.boundary.last())p.site.boundary.removeLast();const auto boundaryIssue=boundaryError(p.site.boundary);if(!boundaryIssue.isEmpty())fail(boundaryIssue);if(!s.contains("shape")){bool round=true;for(const auto &v:p.site.boundary)round&=std::abs(std::hypot(v.x(),v.y())-p.site.radius)<std::max(.001,p.site.radius*.001);if(!round){p.site.shape="polygon";if(p.site.boundary.size()==4){const auto b=p.site.boundary.boundingRect();p.site.width=b.width();p.site.height=b.height();}}}
 if(!root["features"].isArray()||root["features"].toArray().size()>maxFeatures)fail("Invalid feature collection or too many features");
 const bool templated=root["schema_version"].toInt()==4;const auto templates=root["feature_templates"].toArray();if(templated&&(!root["feature_templates"].isArray()||templates.size()>maxFeatures))fail("Invalid feature metadata templates");
 const auto readMetadata=[](const QJsonObject &meta){Feature f;f.sourceId=meta["source_id"].toString();f.importId=meta["import_id"].toString();f.importRecordId=meta["import_record_id"].toString();f.tags=meta["tags"].toObject();f.properties=meta["properties"].toObject();f.provenance=meta["provenance"].toArray();f.heightSource=meta["height_source"].toString();f.layer=classify(f.tags);if(meta["height"].isDouble()){f.height=meta["height"].toDouble();if(!f.knownHeight())fail("Invalid building height");f.heightSource=meta["height_source"].toString("recorded");}return f;};
 QVector<Feature> prototypes;QVector<bool> sourceIdPresent;if(templated)for(const auto &value:templates){if(!value.isObject())fail("Invalid feature metadata templates");const auto meta=value.toObject();prototypes.append(readMetadata(meta));sourceIdPresent.append(meta["source_id"].isString());}
 for(const auto &value:root["features"].toArray()){
  const auto o=value.toObject();Feature f;bool sourceId=false;if(templated){const auto index=o["template"];const double n=index.toDouble(-1);if(!index.isDouble()||n!=std::floor(n)||n<0||n>=templates.size())fail("Invalid feature metadata template reference");f=prototypes[int(n)];sourceId=sourceIdPresent[int(n)];}else{f=readMetadata(o);sourceId=o["source_id"].isString();}
  f.id=o["id"].toString(QString::number(p.features.size()));if(!sourceId)f.sourceId=stableSourceId(f.id);
  const QString kind=o["geometry"].toString();if(kind!="Point"&&kind!="LineString"&&kind!="Polygon")fail("Unsupported geometry type");f.geometry=kind=="Point"?Geometry::Point:kind=="LineString"?Geometry::Line:Geometry::Polygon;
  for(const auto &r:o["rings"].toArray())f.rings.append(readRing(r.toArray(),points));
  if(f.rings.isEmpty()||f.rings[0].size()<(f.geometry==Geometry::Polygon?3:f.geometry==Geometry::Line?2:1))fail("Incomplete geometry");
  if(f.geometry==Geometry::Polygon)for(const auto &ring:f.rings)if(ring.size()<3)fail("Incomplete polygon ring");
  if(f.geometry!=Geometry::Polygon&&f.rings.size()!=1)fail("Invalid non-polygon rings");
  p.features.append(f);
 }
 if(root.contains("imports")&&!root["imports"].isArray())fail("Invalid retained GeoJSON import");
 p.imports=root["imports"].toArray();validateStoredOverlays(p);p.source=root["source"].toObject();p.view=root["view"].toObject();p.layerState=root["layers"].toObject();p.style=root["style"].toString("graphite");p.notes=root["notes"].toString();return p;
}
Project Project::load(const QString &path) {
 QFile file(path);if(!file.open(QIODevice::ReadOnly))fail(file.errorString());if(file.size()>maximumFileBytes)fail("Project exceeds the 256 MiB storage limit; split the project");QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())fail("Invalid project JSON");return fromJson(doc.object());
}
void Project::save(const QString &path) const {const QByteArray bytes=QJsonDocument(toJson()).toJson(QJsonDocument::Compact);if(bytes.size()>maximumFileBytes)fail("Project exceeds the 256 MiB storage limit; split the project");QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))fail(file.errorString());if(file.write(bytes)!=bytes.size()||!file.commit())fail(file.errorString());}
Project Project::fromGeoJson(const QJsonObject &root,const QString &name) {
 if(root["type"].toString()!="FeatureCollection"||!root["features"].isArray()||root["features"].toArray().size()>maxFeatures)fail("Expected GeoJSON FeatureCollection (up to 1,000,000 features)");
 Project p;p.site.name=name;int count=0;double west=180,east=-180,south=85,north=-85;
 for(const auto &value:root["features"].toArray()){
  const auto row=value.toObject(),g=row["geometry"].toObject();const auto c=g["coordinates"].toArray();const QString type=g["type"].toString();QVector<QVector<QPolygonF>> pieces;
  if(type=="Point")pieces.append({readRing(QJsonArray{c},count,true)});
  else if(type=="LineString")pieces.append({readRing(c,count,true)});
  else if(type=="Polygon"){QVector<QPolygonF> rings;for(const auto &r:c)rings.append(readRing(r.toArray(),count,true));pieces.append(rings);}
  else if(type=="MultiLineString"){for(const auto &r:c)pieces.append({readRing(r.toArray(),count,true)});}
  else if(type=="MultiPolygon"){for(const auto &polygon:c){QVector<QPolygonF> rings;for(const auto &r:polygon.toArray())rings.append(readRing(r.toArray(),count,true));pieces.append(rings);}}
  else fail("Supported GeoJSON: Point, LineString, MultiLineString, Polygon and MultiPolygon");
  int part=0;for(const auto &rings:pieces){if(rings.isEmpty()||rings[0].isEmpty())fail("Empty geometry");Feature f;f.properties=row["properties"].toObject();f.provenance=f.properties["sources"].toArray();f.tags=f.properties;if(f.tags["tags"].isObject())f.tags=f.tags["tags"].toObject();f.id=row["id"].toVariant().toString();if(f.id.isEmpty())f.id=QString::number(p.features.size());f.sourceId=f.id;f.id+="_part"+QString::number(part++);f.layer=classify(f.tags);f.geometry=type.contains("Polygon")?Geometry::Polygon:type=="Point"?Geometry::Point:Geometry::Line;f.rings=rings;
   if(f.rings[0].size()<(f.geometry==Geometry::Polygon?3:f.geometry==Geometry::Line?2:1))fail("Incomplete geometry");
   if(f.geometry==Geometry::Polygon)for(const auto &r:f.rings)if(r.size()<3)fail("Incomplete polygon ring");
   deriveHeight(f);p.features.append(f);if(p.features.size()>maxFeatures)fail("Too many geometry parts");
   for(const auto &ring:rings)for(const auto &point:ring){west=std::min(west,point.x());east=std::max(east,point.x());south=std::min(south,point.y());north=std::max(north,point.y());}
  }
 }
 if(p.features.isEmpty())fail("GeoJSON contains no features");
 p.site.lon=(west+east)/2;p.site.lat=(south+north)/2;p.site.radius=std::max(100.,std::hypot((east-west)*111320*std::cos(p.site.lat*M_PI/180),(north-south)*111320)/2*1.04);validateSite(p.site);
 for(auto &f:p.features)for(auto &ring:f.rings)for(auto &point:ring)point=localPoint(point.x(),point.y(),p.site);
 const QPointF sw=localPoint(west,south,p.site),ne=localPoint(east,north,p.site);p.site.boundary=QPolygonF{sw,QPointF(ne.x(),sw.y()),ne,QPointF(sw.x(),ne.y())};p.site.shape="rectangle";p.site.width=ne.x()-sw.x();p.site.height=ne.y()-sw.y();const bool padded=p.site.width<.001||p.site.height<.001;if(padded){p.site.width=std::max(200.,p.site.width);p.site.height=std::max(200.,p.site.height);p.site.radius=std::max(p.site.radius,std::hypot(p.site.width,p.site.height)/2);p.site.boundary=primitiveBoundary(p.site);}
 p.source={{"provider","user-geometry"},{"attribution","User-provided GeoJSON · verify usage rights"},{"coverage_note","Imported geometry; source and heights are not independently validated."},{ "workspace_note",padded?"A degenerate input extent was padded for viewing. This workspace boundary is not a source-coverage claim.":"Workspace boundary derived from the supplied geometry extent."}};return p;
}
QJsonObject Project::geoJson() const {
 QJsonArray rows;for(const auto &f:features){QJsonArray coordinates;for(const auto &ring:f.rings){QJsonArray r;for(const auto &point:ring){const auto ll=geographicPoint(point,site);r.append(QJsonArray{ll.x(),ll.y()});}if(f.geometry==Geometry::Polygon&&r.size()>=3&&r.first()!=r.last())r.append(r.first());coordinates.append(r);}
  QJsonObject props=f.properties;for(auto tag=f.tags.begin();tag!=f.tags.end();++tag)props[tag.key()]=tag.value();if(!f.provenance.isEmpty())props["sources"]=f.provenance;if(f.knownHeight()){props["height_m"]=f.height;props["height_source"]=f.heightSource;}QJsonObject g{{"type",f.geometry==Geometry::Point?"Point":f.geometry==Geometry::Line?"LineString":"Polygon"}};
  if(!f.importId.isEmpty()){props["source_record_id"]=f.importRecordId;props["groundatlas_import_id"]=f.importId;}g["coordinates"]=f.geometry==Geometry::Polygon?QJsonValue(coordinates):f.geometry==Geometry::Line?coordinates.first():coordinates.first().toArray().first();rows.append(QJsonObject{{"type","Feature"},{"id",f.importId.isEmpty()?f.sourceId:f.id},{"properties",props},{"geometry",g}});
 }
 return {{"type","FeatureCollection"},{"features",rows},{"groundatlas_source",source},{"groundatlas_imports",geoJsonImportSummaries(*this)},{"coordinate_note","WGS84 reconstructed from the local tangent-plane approximation."}};
}
QString errorText(const std::exception &e){return QString::fromUtf8(e.what());}
QString appDataPath(const QString &relative){
 const QString base=QCoreApplication::applicationDirPath();
 QString root=base+"/data";
#ifdef Q_OS_MACOS
 if(!QDir(root).exists()){
  const QString bundleRes=QDir::cleanPath(base+"/../Resources/data");
  if(QDir(bundleRes).exists())root=bundleRes;
 }
#endif
 if(relative.isEmpty())return root;
 return QDir::cleanPath(root+"/"+relative);
}
}
