#include "osmworkflow.h"
#include "sitegeometry.h"
#include <QSet>
#include <QDateTime>
#include <cmath>
#include <stdexcept>
namespace ga {
QString osmSelectors(const QString &profile,const QString &bbox){
 const auto selector=[&](const QString &value){return value+"("+bbox+");";};
 QStringList list;
 if(profile=="context")list={"nwr[natural=tree]","nwr[leisure]","nwr[amenity]","nwr[historic]","nwr[heritage]","nwr[healthcare]"};
 else if(profile=="mobility")list={"way[highway~\"^(footway|pedestrian|path|steps|cycleway|living_street)$\"]","way[cycleway]","way[\"cycleway:left\"]","way[\"cycleway:right\"]","nwr[public_transport]","node[highway=bus_stop]","nwr[railway~\"^(station|halt|tram_stop)$\"]"};
 else if(profile=="buildings")list={"nwr[building][start_date]","nwr[building][height]","nwr[building][\"building:levels\"]","nwr[building][\"building:use\"]"};
 else if(profile=="all")list={"nwr[building]","way[highway]","nwr[landuse]","nwr[leisure]","nwr[natural]","way[waterway]","nwr[amenity]","nwr[historic]","nwr[heritage]","nwr[public_transport]","node[highway=bus_stop]","nwr[railway=station]"};
 else throw std::runtime_error("Unknown OSM detail profile");
 QString result;for(const auto &value:list)result+=selector(value);return result;
}
QString osmQuery(const Site &site,const QString &profile){
 const auto issue=selectionError(site);if(!issue.isEmpty())throw std::runtime_error(issue.toUtf8().constData());
 double west=180,east=-180,south=85,north=-85;for(const auto &point:site.boundary.isEmpty()?primitiveBoundary(site):site.boundary){const auto ll=geographicPoint(point,site);west=std::min(west,ll.x());east=std::max(east,ll.x());south=std::min(south,ll.y());north=std::max(north,ll.y());}
 if(west< -180||east>180||south< -85||north>85)throw std::runtime_error("This source window crosses the supported geographic boundary");
 const auto number=[](double value){return QString::number(value,'f',7);};const QString bbox=number(south)+","+number(west)+","+number(north)+","+number(east);
 return "[out:json][timeout:40][maxsize:33554432];("+osmSelectors(profile,bbox)+");out geom;";
}
namespace {
QSet<QString> osmRecords(const Feature &feature){QSet<QString> result;const auto id=feature.sourceId.isEmpty()?stableSourceId(feature.id):feature.sourceId;
 if(id.startsWith("node/")||id.startsWith("way/")||id.startsWith("relation/"))result.insert(id);
 for(const auto &value:feature.provenance){const auto row=value.toObject();if(row["dataset"].toString().compare("OpenStreetMap",Qt::CaseInsensitive)==0){const auto record=row["record_id"].toVariant().toString();if(!record.isEmpty())result.insert(record);}}return result;}
}
Project mergeOsmContext(Project base,const Project &osm,const QString &operationId,const std::atomic_bool *cancel){
 if(std::abs(base.site.lat-osm.site.lat)>1e-9||std::abs(base.site.lon-osm.site.lon)>1e-9||base.site.boundary!=osm.site.boundary)throw std::runtime_error("OSM enrichment has a different site boundary");
 qint64 points=base.site.boundary.size();for(const auto &feature:base.features)for(const auto &ring:feature.rings)points+=ring.size();
 QSet<QString> existing,added,duplicates;for(const auto &feature:base.features)existing.unite(osmRecords(feature));
 const QSet<QString> excludedRoads={"motorway","trunk","primary","secondary","tertiary","residential","service","footway","path","pedestrian"};int count=0,excluded=0;
 for(auto feature:osm.features){
  if(cancel&&cancel->load())throw std::runtime_error("Canceled");
  // Match the web's supplemental OSM workflow: preserve primary buildings and
  // its main road network; enrich context rather than replacing source heights.
  const auto building=feature.tags["building"];const bool taggedBuilding=building.isString()?!building.toString().isEmpty():building.toBool();
  if(taggedBuilding||excludedRoads.contains(feature.tags["highway"].toString())){++excluded;continue;}
  const auto records=osmRecords(feature);bool duplicate=false;for(const auto &record:records)duplicate|=existing.contains(record);if(duplicate){duplicates.unite(records);continue;}
  for(const auto &ring:feature.rings)points+=ring.size();
  if(points>Project::maximumGeometryPoints)throw std::runtime_error("Combined geometry exceeds eight million points");
  feature.id="enrichment-"+operationId+"-"+feature.id;feature.properties["_groundatlas_enrichment"]=operationId;base.features.append(feature);added.unite(records);++count;
  if(base.features.size()>Project::maximumGeometryParts)throw std::runtime_error("Too many geometry parts after enrichment");
 }
 const auto original=base.source["attribution"].toString();if(!base.source.contains("primary_attribution"))base.source["primary_attribution"]=original;
 if(count&&!original.contains("OpenStreetMap",Qt::CaseInsensitive))base.source["attribution"]=original+"; © OpenStreetMap contributors · ODbL-1.0";
 auto history=base.source["enrichments"].toArray();history.append(QJsonObject{{"id",operationId},{"kind","osm-context"},{"added_geometry_parts",count},{"added_source_records",added.size()},{"existing_source_records_skipped",duplicates.size()},{"excluded_primary_geometry_parts",excluded},{"source",osm.source}});base.source["enrichments"]=history;
 base.source["enrichment_note"]="Supplemental OSM context only. Primary building geometry, source heights, missing heights and source-window coverage remain unchanged.";
 return base;
}

int applySmartHeightEstimates(Project &project){
 int estimated=0;
 for(auto &f:project.features){
  if(!f.building()||f.knownHeight())continue;
  const double area=f.area();
  const QString bType=f.tags["building"].toString().toLower();
  // Deterministic subtle urban height variation (0.0 .. 1.0) from feature ID
  uint hash=2166136261u;
  for(const auto ch:f.sourceId.toUtf8())hash=(hash^uint(static_cast<unsigned char>(ch)))*16777619u;
  const double jitter=(hash%1000)/1000.0;

  double h=10.5;
  if(bType=="shed"||bType=="garage"||bType=="garages"||bType=="hut"||bType=="kiosk"||bType=="roof"||bType=="carport"||area<28.0){
   h=3.2+jitter*1.4;
  }else if(bType=="tower"||bType=="skyscraper"){
   h=32.0+std::min(48.0,std::sqrt(std::max(100.0,area))*0.9)+jitter*12.0;
  }else if(bType=="apartments"||bType=="office"||bType=="hotel"||bType=="commercial"||bType=="hospital"){
   if(area>700.0)h=22.0+std::min(26.0,std::sqrt(area-600.0)*0.45)+jitter*8.0;
   else if(area>250.0)h=15.0+jitter*7.5;
   else h=11.5+jitter*5.0;
  }else if(bType=="school"||bType=="university"||bType=="college"||bType=="public"||bType=="civic"||bType=="government"){
   h=10.5+jitter*5.5;
  }else if(bType=="industrial"||bType=="warehouse"||bType=="factory"||bType=="hangar"){
   h=7.2+jitter*4.0;
  }else if(bType=="temple"||bType=="church"||bType=="cathedral"||bType=="pagoda"||bType=="mosque"){
   h=8.5+jitter*5.5;
  }else if(bType=="detached"||bType=="bungalow"||bType=="villa"){
   h=7.0+jitter*3.5;
  }else{
   // Typical dense Asian/European urban fabric scaled gently by footprint area
   if(area<75.0)h=9.6+jitter*5.4;
   else if(area<220.0)h=11.5+jitter*6.5;
   else if(area<650.0)h=14.0+jitter*8.5;
   else h=16.5+std::min(18.0,std::sqrt(area-600.0)*0.32)+jitter*6.0;
  }
  f.height=std::round(h*2.0)/2.0;
  f.heightSource="smart_estimate";
  f.properties["_ga_estimated_height"]=true;
  ++estimated;
 }
 if(estimated>0){
  project.source["smart_height_estimates"]=estimated;
 }
 return estimated;
}

int clearSmartHeightEstimates(Project &project){
 int cleared=0;
 for(auto &f:project.features){
  if(f.heightSource=="smart_estimate"){
   f.height=0;
   f.heightSource.clear();
   f.properties.remove("_ga_estimated_height");
   ++cleared;
  }
 }
 project.source.remove("smart_height_estimates");
 return cleared;
}
}
