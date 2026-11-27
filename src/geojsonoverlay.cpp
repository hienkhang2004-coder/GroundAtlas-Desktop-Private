#include "geojsonoverlay.h"
#include "geoimport.h"
#include "sitegeometry.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLineF>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ga {
namespace {
constexpr qint64 maximumImportBytes=20000000;
[[noreturn]] void fail(const QString &message){throw std::runtime_error(message.toUtf8().constData());}
void checkCancel(const std::atomic_bool *cancel){if(cancel&&cancel->load())fail("Canceled");}
bool truthy(const QJsonValue &v){return !v.isNull()&&!v.isUndefined()&&(v.isBool()?v.toBool():v.isString()?!v.toString().isEmpty():v.isDouble()?v.toDouble()!=0:true);}
QString text(const QJsonValue &v){if(v.isString())return v.toString();if(v.isBool())return v.toBool()?"true":"false";if(v.isDouble()){const auto bytes=QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);return QString::fromUtf8(bytes.mid(1,bytes.size()-2));}return {};}
QByteArray hash(const QJsonObject &root){return QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();}
double userMetres(const QJsonValue &v){
 const QString value=text(v);const auto match=QRegularExpression("^\\s*([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)").match(value);
 const double n=match.hasMatch()?match.captured(1).toDouble():0.;
 return n>0&&n<=1200?(QRegularExpression("ft|feet|'").match(value).hasMatch()?n*.3048:n):0.;
}
QPolygonF coordinates(const QJsonValue &value,int &count,const std::atomic_bool *cancel){
 if(!value.isArray()||value.toArray().isEmpty())fail("Empty GeoJSON coordinates");
 QPolygonF ring;for(const auto &v:value.toArray()){
  checkCancel(cancel);const auto pair=v.toArray();if(pair.size()<2||!pair[0].isDouble()||!pair[1].isDouble())fail("GeoJSON requires WGS84 longitude, latitude coordinates");
  const double lon=pair[0].toDouble(),lat=pair[1].toDouble();if(!std::isfinite(lon)||!std::isfinite(lat)||std::abs(lon)>180||std::abs(lat)>85)fail("GeoJSON requires WGS84 longitude, latitude coordinates");
  if(++count>300000)fail("GeoJSON exceeds 300000 points; split the file");
  ring.append({lon,lat});
 }return ring;
}
QVector<QVector<QPolygonF>> pieces(const QJsonObject &geometry,int &count,const std::atomic_bool *cancel){
 const auto type=geometry["type"].toString();const auto value=geometry["coordinates"];QVector<QVector<QPolygonF>> result;
 const auto polygon=[&](const QJsonValue &v){if(!v.isArray()||v.toArray().isEmpty())fail("Empty GeoJSON coordinates");QVector<QPolygonF> rings;for(const auto &r:v.toArray()){auto ring=coordinates(r,count,cancel);if(ring.size()<3)fail("Incomplete GeoJSON geometry");rings.append(ring);}return rings;};
 if(type=="Point")result.append({coordinates(QJsonArray{value},count,cancel)});
 else if(type=="LineString")result.append({coordinates(value,count,cancel)});
 else if(type=="Polygon")result.append(polygon(value));
 else if(type=="MultiLineString"||type=="MultiPolygon"){
  if(!value.isArray()||value.toArray().isEmpty())fail("Empty GeoJSON coordinates");
  for(const auto &part:value.toArray())if(type=="MultiPolygon")result.append(polygon(part));else result.append({coordinates(part,count,cancel)});
 }else fail("Unsupported GeoJSON geometry type");
 if(type=="LineString"||type=="MultiLineString")for(const auto &part:result)if(part[0].size()<2)fail("Incomplete GeoJSON geometry");
 return result;
}
void validate(const QJsonObject &root,const std::atomic_bool *cancel){
 if(root["type"]!="FeatureCollection"||!root["features"].isArray()||root["features"].toArray().size()>50000)fail("Expected GeoJSON FeatureCollection with at most 50000 features");
 int count=0;for(const auto &v:root["features"].toArray()){checkCancel(cancel);if(!v.isObject()||!v.toObject()["geometry"].isObject())fail("Unsupported GeoJSON geometry type");pieces(v.toObject()["geometry"].toObject(),count,cancel);}
}
QString category(const QJsonValue &v){const auto s=text(v);for(const auto &entry:QVector<QPair<QString,QString>>{{"pharmacy","pharmacy"},{"hospital","hospital"},{"clinic|doctor|medical|dentist","clinic"},{"university|college","university"},{"school|education|kindergarten","school"},{"cafe|coffee","cafe"},{"restaurant|food","restaurant"}})if(QRegularExpression(entry.first).match(s).hasMatch())return entry.second;return s;}
QJsonObject normalizedTags(const QJsonObject &p){
 QJsonObject tags;for(const auto &key:QStringList{"name","name:vi","brand","access","foot","oneway:foot","layer","indoor","area","building:use","historic","heritage","railway","bus","bicycle","public_transport","healthcare","cycleway","cycleway:left","cycleway:right","cycleway:both","start_date","waterway","importLayer","shop","office","tourism","power","telecom","tower:type","man_made","bridge","tunnel","route","aerialway","aeroway","place","boundary","households","building:households"})if(p[key].isString()||p[key].isDouble())tags[key]=text(p[key]);
 if(truthy(p["building"]))tags["building"]=p["building"];
 if(truthy(p["height"]))tags["height"]=text(p["height"]);
 if(truthy(p["floors"]))tags["building:levels"]=text(p["floors"]);
 for(const auto &key:QStringList{"highway","landuse","leisure","natural","water"})if(truthy(p[key]))tags[key]=p[key];
 if(truthy(p["amenity"]))tags["amenity"]=category(p["amenity"]);
 if(p["land_cover"]=="forest")tags["landuse"]="forest";
 tags["source"]="User GeoJSON";if(truthy(p["height_source"]))tags["height:source"]=p["height_source"];if(p["height_source"]=="google_ai_2023")tags["height:year"]="2023";return tags;
}
void budget(const Project &project){
 qint64 points=project.site.boundary.size();if(project.features.size()>Project::maximumGeometryParts)fail("Combined overlays exceed one million geometry parts; split the project");
 for(const auto &f:project.features)for(const auto &r:f.rings)points+=r.size();
 if(points>Project::maximumGeometryPoints)fail("Combined geometry exceeds eight million points");
}
QVector<Feature> clipped(const QJsonObject &entry,const Site &site,const std::atomic_bool *cancel){
 const SiteClipper clipper(site);QVector<Feature> result;const QString id=entry["id"].toString();int count=0,index=0;qint64 retained=0;
 for(const auto &value:entry["geojson"].toObject()["features"].toArray()){
  checkCancel(cancel);const auto row=value.toObject(),p=row["properties"].toObject(),geometry=row["geometry"].toObject();const auto type=geometry["type"].toString();const auto parts=pieces(geometry,count,cancel);int partIndex=0;
  Feature prototype;prototype.importId=id;prototype.importRecordId=text(row["id"]);prototype.sourceId="import-"+id+"-record-"+QString::number(index);prototype.properties=p;prototype.provenance=p["sources"].toArray();prototype.tags=normalizedTags(p);prototype.layer=classify(prototype.tags);
  bool numeric=false;const double height=p["height"].isDouble()?p["height"].toDouble():text(p["height"]).trimmed().toDouble(&numeric);if((p["height"].isDouble()||numeric)&&std::isfinite(height)&&height>0&&height<=3600){prototype.height=height;prototype.heightSource=p["height_source"].toString();}
  const auto append=[&](const Feature &fragment){for(const auto &r:fragment.rings)retained+=r.size();if(retained>Project::maximumGeometryPoints)fail("Combined geometry exceeds eight million points");result.append(fragment);if(result.size()>Project::maximumGeometryParts)fail("Combined overlays exceed one million geometry parts; split the project");};
  for(auto rings:parts){
   Feature f=prototype;f.id=f.sourceId+"-part-"+QString::number(partIndex++);
   for(auto &ring:rings)for(auto &point:ring)point=localPoint(point.x(),point.y(),site);
   f.geometry=type.contains("Polygon")?Geometry::Polygon:type=="Point"?Geometry::Point:Geometry::Line;
   // The web also treats a closed LineString as an area during clipping.
   if(f.geometry==Geometry::Line&&rings[0].size()>3&&QLineF(rings[0].first(),rings[0].last()).length()<.01)f.geometry=Geometry::Polygon;
   f.rings=rings;
   if(f.geometry==Geometry::Line){
    // SiteGeometry.clipLine emits each retained input edge separately. Keep that
    // partition as well as its geometry so imported-file counts match the web.
    // Read through const access: a mutable operator[] would detach the entire
    // shared line after copying f, once per edge (quadratic memory traffic).
    const auto &line=std::as_const(f.rings).first();
    for(int edge=1;edge<line.size();++edge){checkCancel(cancel);auto segment=f;segment.id+="-edge-"+QString::number(edge);segment.rings={QPolygonF{line[edge-1],line[edge]}};for(const auto &fragment:clipper.clip(segment))append(fragment);}
   }else for(const auto &fragment:clipper.clip(f))append(fragment);
  }++index;
 }return result;
}
}
QStringList geoJsonRoles(){return {"building","parcels","planning","trees","heritage","transit","landscape"};}
QString geoJsonRoleName(const QString &role,bool english){const QMap<QString,QPair<QString,QString>> names={{"building",{"Công trình","Buildings"}},{"parcels",{"Ranh thửa","Parcels"}},{"planning",{"Quy hoạch","Planning"}},{"trees",{"Cây xanh","Trees"}},{"heritage",{"Di sản","Heritage"}},{"transit",{"Ga / trạm","Transit stops"}},{"landscape",{"Cảnh quan","Landscape"}}};const auto v=names.value(role);return english?v.second:v.first;}
QString localizedImportError(const QString &message,bool english){if(english)return message;const QMap<QString,QString> messages={
 {"Canceled","Đã hủy nhập dữ liệu."},{"GeoJSON file could not be read","Không đọc được file GeoJSON."},{"GeoJSON file exceeds 20 MB; split the file","File GeoJSON vượt 20 MB. Hãy chia nhỏ file."},{"Invalid GeoJSON JSON","Nội dung JSON không hợp lệ."},{"Unknown GeoJSON import role","Loại lớp nhập không được hỗ trợ."},{"Invalid or duplicate GeoJSON import identity","Mã lớp nhập không hợp lệ hoặc bị trùng."},{"Expected GeoJSON FeatureCollection with at most 50000 features","Cần GeoJSON FeatureCollection, tối đa 50.000 đối tượng."},{"Unsupported GeoJSON geometry type","File có loại hình học chưa được hỗ trợ."},{"Empty GeoJSON coordinates","Có tọa độ rỗng trong file."},{"Incomplete GeoJSON geometry","Hình học trong file chưa đủ đỉnh."},{"GeoJSON requires WGS84 longitude, latitude coordinates","Cần tọa độ WGS84 [kinh độ, vĩ độ]."},{"GeoJSON exceeds 300000 points; split the file","File vượt 300.000 điểm. Hãy chia nhỏ file."},{"No imported geometry intersects the current site boundary","File không có đối tượng giao với ranh khu đất hiện tại."},{"Combined overlays exceed one million geometry parts; split the project","Dự án vượt một triệu phần hình học. Hãy chia nhỏ dự án."},{"Combined geometry exceeds eight million points","Dự án vượt tám triệu điểm hình học."},{"Project exceeds the 256 MiB storage limit; split the project","Dự án vượt giới hạn lưu trữ 256 MiB. Hãy chia nhỏ dự án."},{"GeoJSON import no longer exists","Lớp nhập này không còn trong dự án."},{"Invalid retained GeoJSON import","Dữ liệu lớp nhập đã lưu không hợp lệ."}};return messages.value(message,message);}
Project addGeoJsonOverlay(Project source,const QByteArray &bytes,const QString &name,const QString &role,const QString &requestedId,const std::atomic_bool *cancel){
 checkCancel(cancel);if(bytes.size()>maximumImportBytes)fail("GeoJSON file exceeds 20 MB; split the file");if(!geoJsonRoles().contains(role))fail("Unknown GeoJSON import role");QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())fail("Invalid GeoJSON JSON");auto root=doc.object();validate(root,cancel);
 const auto siteIssue=selectionError(source.site,100000);if(!siteIssue.isEmpty())fail(siteIssue);if(source.site.boundary.isEmpty())source.site.boundary=primitiveBoundary(source.site);
 const QString id=requestedId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):requestedId;if(!QRegularExpression("^[a-zA-Z0-9-]{1,80}$").match(id).hasMatch())fail("Invalid or duplicate GeoJSON import identity");for(const auto &v:source.imports)if(v.toObject()["id"]==id)fail("Invalid or duplicate GeoJSON import identity");
 QJsonArray rows;int index=0;for(const auto &v:root["features"].toArray()){
  checkCancel(cancel);auto f=v.toObject(),p=f["properties"].toObject();if(f["id"].isNull()||f["id"].isUndefined())f["id"]="feature-"+QString::number(index);p["importLayer"]=role;
  if(role=="building"){if(!truthy(p["building"]))p["building"]="yes";const double h=userMetres(p["height"]);p["height"]=h>0?QJsonValue(h):QJsonValue(QJsonValue::Null);p["height_source"]=h>0?QJsonValue("user_file"):QJsonValue(QJsonValue::Null);}
  if(role=="trees")p["natural"]="tree";
  if(role=="heritage"&&!truthy(p["historic"]))p["historic"]="yes";
  if(role=="transit"&&!truthy(p["public_transport"]))p["public_transport"]="platform";
  if(role=="landscape"&&!truthy(p["landuse"]))p["landuse"]="grass";
  f["properties"]=p;rows.append(f);++index;
 }root["features"]=rows;
 QJsonObject entry{{"id",id},{"name",QFileInfo(name).fileName()},{"role",role},{"input_bytes",bytes.size()},{"input_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"normalized_sha256",QString::fromLatin1(hash(root))},{"imported_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"source_note","User-supplied GeoJSON; verify usage rights. Imported parcels/planning are not statutory or cadastral evidence."},{"geojson",root}};
 const auto features=clipped(entry,source.site,cancel);if(features.isEmpty())fail("No imported geometry intersects the current site boundary");entry["count"]=features.size();entry["records"]=rows.size();source.features.append(features);source.imports.append(entry);budget(source);checkCancel(cancel);if(QJsonDocument(source.toJson()).toJson(QJsonDocument::Compact).size()>Project::maximumFileBytes)fail("Project exceeds the 256 MiB storage limit; split the project");checkCancel(cancel);return source;
}
Project removeGeoJsonOverlay(Project source,const QString &id,const std::atomic_bool *cancel){checkCancel(cancel);bool found=false;QJsonArray imports;for(const auto &v:source.imports){if(v.toObject()["id"]==id)found=true;else imports.append(v);}if(!found)fail("GeoJSON import no longer exists");QVector<Feature> retained;for(const auto &f:source.features){checkCancel(cancel);if(f.importId!=id)retained.append(f);}source.imports=imports;source.features=retained;return source;}
void reclipGeoJsonOverlays(Project &project,const std::atomic_bool *cancel){QVector<Feature> retained;for(const auto &f:project.features)if(f.importId.isEmpty())retained.append(f);project.features=retained;QJsonArray entries;for(const auto &v:project.imports){checkCancel(cancel);auto entry=v.toObject();const auto parts=clipped(entry,project.site,cancel);entry["count"]=parts.size();entries.append(entry);project.features.append(parts);budget(project);}project.imports=entries;budget(project);}
void validateStoredOverlays(const Project &project){QSet<QString> ids;QMap<QString,int> counts;for(const auto &v:project.imports){if(!v.isObject())fail("Invalid retained GeoJSON import");const auto entry=v.toObject();const auto id=entry["id"].toString();if(!QRegularExpression("^[a-zA-Z0-9-]{1,80}$").match(id).hasMatch()||ids.contains(id)||!geoJsonRoles().contains(entry["role"].toString())||entry["input_bytes"].toDouble(-1)<0||entry["input_bytes"].toDouble()>maximumImportBytes||!entry["geojson"].isObject())fail("Invalid retained GeoJSON import");validate(entry["geojson"].toObject(),nullptr);if(entry["normalized_sha256"].toString().toLatin1()!=hash(entry["geojson"].toObject()))fail("Invalid retained GeoJSON import");ids.insert(id);counts[id]=entry["count"].toInt(-1);}
 QMap<QString,int> actual;for(const auto &f:project.features)if(!f.importId.isEmpty()){if(!ids.contains(f.importId))fail("Invalid retained GeoJSON import");++actual[f.importId];}for(const auto &id:ids)if(counts[id]!=actual[id])fail("Invalid retained GeoJSON import");}
QJsonArray geoJsonImportSummaries(const Project &project){QJsonArray result;for(const auto &v:project.imports){auto entry=v.toObject();entry.remove("geojson");result.append(entry);}return result;}
}
