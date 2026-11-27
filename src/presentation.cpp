#include "scene.h"
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace ga {
namespace {
QVector<Layer> catalogLayers;
QVector<Palette> catalogPalettes;
QVector<DiagramPreset> catalogDiagrams;
QHash<QString,QJsonArray> styleLayers;
bool oneOf(const QString &v,std::initializer_list<const char*> values){for(const auto value:values)if(v==QLatin1String(value))return true;
 return false;}
double number(const Feature &f,const QString &key){
 if(f.properties.contains(key)){const auto value=f.properties[key];return value.isDouble()?value.toDouble():NAN;}
 const auto value=f.tags[key=="floors"?"building:levels":"start_date"];
 if(value.isDouble())return key=="year"?std::trunc(value.toDouble()):value.toDouble();
 const auto match=QRegularExpression(key=="year"?"^\\s*([+-]?\\d+)":"^\\s*(\\d+(?:\\.\\d+)?)\\s*(?:m)?\\s*$").match(value.toString());
 return match.hasMatch()?match.captured(1).toDouble():NAN;
}
bool positive(double n){return std::isfinite(n)&&n>0;}
bool households(const QJsonValue &v){if(v.isDouble())return v.toDouble()>=0&&v.toDouble()<=9007199254740991.&&std::floor(v.toDouble())==v.toDouble();
 if(!v.isString()||!QRegularExpression("^\\d+$").match(v.toString().trimmed()).hasMatch())return false;
 bool ok=false;
 const auto n=v.toString().trimmed().toDouble(&ok);return ok&&n<=9007199254740991.;}
QColor validColor(const QJsonValue &v){return v.isString()&&QRegularExpression("^#[0-9a-fA-F]{6}$").match(v.toString()).hasMatch()?QColor(v.toString()):QColor();}
double metres(const QJsonValue &v){double value=NAN;
 if(v.isDouble())value=v.toDouble();else if(v.isString()){const auto match=QRegularExpression("^(\\d+(?:\\.\\d+)?)\\s*(m|metres?|meters?|ft|feet|')?$",QRegularExpression::CaseInsensitiveOption).match(v.toString().trimmed());
 if(match.hasMatch()){value=match.captured(1).toDouble();
 if(oneOf(match.captured(2).toLower(),{"ft","feet","'"}))value*=.3048;}}return positive(value)&&value<=200?value:NAN;}
QJsonValue tag(const Feature &f,const QString &key){return f.tags.contains(key)?f.tags[key]:f.properties[key];}
QString canonicalStyle(const QString &id){if(styleLayers.contains(id))return id;
 if(styleLayers.contains("ga-style-"+id))return "ga-style-"+id;
 return id;}
QStringList theme(const QString &id){
 if(oneOf(id,{"figure","footprint"}))return{"buildings"};
 if(id=="height")return{"info-height"};
 if(id=="floors")return{"info-floors"};
 if(id=="use")return{"info-use"};
 if(id=="age")return{"info-year"};
 if(id=="green")return{"water","green","green-woods","trees"};
 if(id=="water")return{"water","waterway"};
 if(id=="park")return{"green-park","trees"};
 if(oneOf(id,{"roads","transport"}))return{"roads","walk","cycle","rail","piers"};
 if(id=="walk")return{"walk"};
 if(id=="cycle")return{"cycle"};
 if(id=="transit")return{"transit"};
 if(id=="amenity")return{"amenities"};
 if(id=="school")return{"facility-education"};
 if(id=="health")return{"facility-health"};
 if(id=="heritage")return{"heritage"};
 if(id=="trees")return{"trees"};
 if(id=="landuse")return{"landuse"};
 if(id=="planning")return{"planning"};
 if(id=="parcels")return{"parcels"};
 return{};
}
}
bool presentTag(const QJsonValue &value){return value.isString()&&!oneOf(value.toString().trimmed().toLower(),{"","no","none","0","false","unknown"});}
bool matchesLayer(const Feature &f,const QString &id){
 const auto &t=f.tags;const auto a=t["amenity"].toString(),h=t["highway"].toString(),n=t["natural"].toString(),l=t["landuse"].toString(),le=t["leisure"].toString(),w=t["water"].toString(),ww=t["waterway"].toString(),shop=t["shop"].toString(),office=t["office"].toString(),rail=t["railway"].toString(),bld=t["building"].toString();
 const bool b=presentTag(t["building"]),line=!f.rings.isEmpty()&&f.rings[0].size()>1,activeRail=oneOf(rail,{"rail","light_rail","subway","tram","narrow_gauge","funicular","monorail"}),foot=oneOf(h,{"footway","pedestrian","path","steps"});
 if(id=="context")return f.layer=="context";
 if(id=="piers")return t["man_made"]=="pier";
 if(id=="water")return n=="water"||presentTag(t["water"]);
 if(id=="waterway")return presentTag(t["waterway"]);
 if(id=="green")return oneOf(l,{"forest","grass","meadow","recreation_ground","village_green"})||oneOf(le,{"park","garden","nature_reserve"})||oneOf(n,{"wood","grassland","scrub"});
 if(id=="trees")return n=="tree";
 if(id=="landuse")return presentTag(t["landuse"]);
 if(id=="planning"||id=="parcels")return t["importLayer"]==id;
 if(id=="roads")return line&&oneOf(h,{"motorway","motorway_link","trunk","trunk_link","primary","primary_link","secondary","secondary_link","tertiary","tertiary_link","residential","service","unclassified","track","road","busway"});
 if(id=="walk")return line&&(foot||h=="living_street");
 if(id=="cycle"){if(!line)return false;
 if(h=="cycleway")return true;
 for(const auto k:{"cycleway","cycleway:left","cycleway:right","cycleway:both"})if(presentTag(t[k])&&t[k]!="separate")return true;
 return false;}
 if(id=="rail")return line&&activeRail;
 if(id=="transit")return h=="bus_stop"||oneOf(rail,{"station","halt","tram_stop"})||oneOf(t["public_transport"].toString(),{"platform","stop_position","station"});
 if(id=="buildings")return b;
 if(id=="amenities")return presentTag(t["amenity"]);
 if(id=="heritage")return presentTag(t["historic"])||presentTag(t["heritage"]);
 if(id=="info-use")return b&&((presentTag(t["building:use"])&&t["building:use"]!="yes")||(presentTag(t["building"])&&!oneOf(bld,{"yes","construction"})));
 if(id=="info-height")return b&&positive(f.height);
 if(id=="info-floors")return b&&positive(number(f,"floors"));
 if(id=="info-year"){const auto year=number(f,"year");return b&&positive(year)&&year==std::floor(year)&&year<=9999;}
 if(id=="info-households")return b&&households(t.contains("building:households")&&!t["building:households"].isNull()?t["building:households"]:t["households"]);
 const bool personal=oneOf(shop,{"hairdresser","beauty","laundry","dry_cleaning","tailor","massage","tattoo"}),automotive=oneOf(shop,{"car","car_repair","car_parts","motorcycle","motorcycle_repair","tyres"});
 if(id=="business-food")return oneOf(a,{"restaurant","fast_food","food_court"});
 if(id=="business-cafe")return oneOf(a,{"cafe","bar","pub","biergarten"});
 if(id=="business-retail")return presentTag(t["shop"])&&!personal&&!automotive&&shop!="vacant";
 if(id=="business-service")return personal;
 if(id=="business-finance")return oneOf(a,{"bank","atm","bureau_de_change"})||oneOf(office,{"financial","financial_advisor","insurance","estate_agent"});
 if(id=="business-office")return presentTag(t["office"])&&office!="vacant";
 if(id=="business-hotel")return oneOf(t["tourism"].toString(),{"hotel","motel","hostel","guest_house","apartment"});
 if(id=="business-auto")return automotive;
 if(id=="facility-education")return oneOf(a,{"school","university","kindergarten","college","library"})||oneOf(bld,{"school","university","kindergarten","college"});
 if(id=="facility-health")return oneOf(a,{"hospital","clinic","doctors","dentist","pharmacy"})||presentTag(t["healthcare"])||oneOf(bld,{"hospital","clinic"});
 if(id=="facility-culture")return oneOf(a,{"theatre","cinema","arts_centre","music_venue"})||t["tourism"]=="museum";
 if(id=="facility-worship")return a=="place_of_worship";
 if(id=="facility-civic")return oneOf(a,{"townhall","community_centre","police","fire_station","post_office","courthouse"});
 if(id=="facility-leisure")return oneOf(le,{"sports_centre","stadium","fitness_centre","playground","pitch"});
 if(id=="facility-parking")return oneOf(a,{"parking","parking_entrance","bicycle_parking"});
 if(id.startsWith("power-")){const auto suffix=id.mid(6);return suffix=="line"?line&&oneOf(t["power"].toString(),{"line","minor_line","cable"}):t["power"]==suffix;}
 if(id=="communication")return presentTag(t["telecom"])||t["tower:type"]=="communication"||t["man_made"]=="communications_tower";
 if(id=="traffic-motorway")return line&&oneOf(h,{"motorway","motorway_link","trunk","trunk_link"});
 if(id=="traffic-primary")return line&&oneOf(h,{"primary","primary_link","secondary","secondary_link","tertiary","tertiary_link"});
 if(id=="traffic-local")return line&&oneOf(h,{"residential","service","unclassified","living_street"});
 if(id=="traffic-tunnel")return line&&presentTag(t["tunnel"])&&(presentTag(t["highway"])||activeRail);
 if(id=="traffic-bridge")return line&&((presentTag(t["bridge"])&&(presentTag(t["highway"])||activeRail)&&!foot)||t["man_made"]=="bridge");
 if(id=="traffic-ferry")return line&&t["route"]=="ferry";
 if(id=="traffic-tram")return line&&rail=="tram";
 if(id=="traffic-subway")return line&&rail=="subway";
 if(id=="traffic-footbridge")return line&&presentTag(t["bridge"])&&foot;
 if(id=="traffic-steps")return line&&h=="steps";
 if(id=="traffic-cable")return line&&oneOf(t["aerialway"].toString(),{"cable_car","gondola","chair_lift","mixed_lift","drag_lift","t-bar","j-bar","platter","rope_tow","zip_line","goods"});
 if(id=="traffic-runway")return line&&oneOf(t["aeroway"].toString(),{"runway","taxiway"});
 if(id=="traffic-pier")return line&&t["man_made"]=="pier";
 if(id=="water-canal")return ww=="canal"||w=="canal";
 if(id=="water-stream")return ww=="stream";
 if(id=="water-river")return ww=="river"||w=="river";
 if(id=="water-drain")return oneOf(ww,{"drain","ditch"});
 if(id=="water-pool")return le=="swimming_pool";
 if(id=="water-sea")return w=="sea"||t["place"]=="sea";
 if(id=="water-lake")return oneOf(w,{"lake","reservoir"});
 if(id=="water-pond")return oneOf(w,{"pond","basin"});
 if(id=="green-woods")return n=="wood"||l=="forest";
 if(id=="green-grass")return oneOf(l,{"grass","meadow","village_green"})||n=="grassland";
 if(id=="green-farmland")return oneOf(l,{"farmland","farmyard","orchard","vineyard"});
 if(id=="green-wetland")return n=="wetland";
 if(id=="green-sand")return oneOf(n,{"sand","beach"});
 if(id=="green-park")return oneOf(le,{"park","garden"});
 if(id=="green-protected")return le=="nature_reserve"||oneOf(t["boundary"].toString(),{"protected_area","national_park"});
 if(id=="green-recreation")return l=="recreation_ground"||le=="recreation_ground";
 if(id=="green-cemetery")return l=="cemetery"||a=="grave_yard";
 return false;
}
QColor detailedColor(const Feature &f,const QString &id,const QColor &fallback){
 if(id=="info-height"||id=="info-floors"){const auto n=id=="info-height"?f.height:number(f,"floors");
 if(!positive(n))return fallback;
 const QVector<double> limits=id=="info-height"?QVector<double>{10,20,40,80}:QVector<double>{3,6,10,20};
 int i=0;while(i<4&&n>=limits[i])++i;return QColor(QStringList{"#dce6ed","#b2c9d8","#88abc2","#5a84a4","#315b7a"}[i]);}
 if(id=="info-year"){const auto year=number(f,"year");
 if(!positive(year)||year!=std::floor(year)||year>9999)return fallback;
 int i=0;for(const int n:{1945,1980,2000,2015})if(year>=n)++i;return QColor(QStringList{"#80544e","#a87964","#c39b7d","#d8bd9d","#e7d8c2"}[i]);}
 if(id=="info-use"){const auto use=f.tags["building:use"].toString().isEmpty()?f.tags["building"].toString():f.tags["building:use"].toString();
 if(oneOf(use,{"house","residential","apartments","detached","terrace"}))return QColor("#c9b295");
 if(oneOf(use,{"retail","commercial"}))return QColor("#c78f9d");
 if(use=="office")return QColor("#8caac1");
 if(oneOf(use,{"industrial","warehouse"}))return QColor("#b0a1c1");}return fallback;
}
QVector<Layer> defaultLayers(){if(!catalogLayers.isEmpty())return catalogLayers;return{{"water","Mặt nước","Water bodies",QColor("#94b9c8")},{"green","Mảng xanh","Green space",QColor("#a7bc9b")},{"roads","Đường xe cơ giới","Motor roads",QColor("#cebaa2")},{"walk","Đường đi bộ","Pedestrian routes",QColor("#b5aaa0")},{"rail","Đường sắt","Railways",QColor("#8e8d91")},{"buildings","Công trình","Buildings",QColor("#b8b4ae")},{"trees","Cây đơn lẻ","Individual trees",QColor("#6d9273")}};}
QVector<Palette> palettes(){if(!catalogPalettes.isEmpty())return catalogPalettes;return{{"graphite","Nét chì","Graphite",QColor("#faf9f6"),QColor("#b8b4ae"),QColor("#d6d1c9"),QColor("#b0b8a2"),QColor("#a7bfc2"),QColor("#34332f")}};}
Palette paletteFor(const QString &id){for(const auto &p:palettes())if(p.id==id||p.id=="ga-style-"+id)return p;for(const auto &d:catalogDiagrams)if(d.id==id)return d.palette;for(const auto &p:palettes())if(p.id=="ga-style-graphite")return p;return palettes().first();}
Layer restoreLayer(const Layer &base,const QJsonObject &o){Layer l=base;const auto color=validColor(o["color"]);
 if(color.isValid())l.color=color;
 if(o["visible"].isBool())l.visible=o["visible"].toBool();
 if(o["opacity"].isDouble()&&std::isfinite(o["opacity"].toDouble()))l.opacity=std::clamp(o["opacity"].toDouble(),0.,1.);
 if(o["presentation"].isObject())l.presentation={};
 auto opts=l.presentation;const auto stroke=validColor(o["stroke"]);
 if(stroke.isValid()||o["stroke"]=="none")opts["stroke"]=o["stroke"];
 for(const auto key:{"strokeWidth","lineWidth","lineMinWidth"})if(o[key].isDouble()&&std::isfinite(o[key].toDouble())&&(QLatin1String(key)!="lineWidth"||o[key].toDouble()>0))opts[key]=std::clamp(o[key].toDouble(),QLatin1String(key)=="lineWidth"?.1:0.,QLatin1String(key)=="lineWidth"?200.:8.);
 if(o["roadHierarchy"].isBool())opts["roadHierarchy"]=o["roadHierarchy"];
 const auto shadow=o["shadow"].toObject();
 if(validColor(shadow["color"]).isValid()&&shadow["opacity"].isDouble()&&shadow["dx"].isDouble()&&shadow["dy"].isDouble())opts["shadow"]=QJsonObject{{"color",shadow["color"]},{"opacity",std::clamp(shadow["opacity"].toDouble(),0.,1.)},{"dx",std::clamp(shadow["dx"].toDouble(),-50.,50.)},{"dy",std::clamp(shadow["dy"].toDouble(),-50.,50.)}};
 const auto tree=o["tree"].toObject();
 if(tree["radius"].isDouble()&&tree["radius"].toDouble()>0){QJsonArray colors;for(const auto &v:tree["colors"].toArray())if(colors.size()<8&&validColor(v).isValid())colors.append(v);opts["tree"]=QJsonObject{{"radius",std::clamp(tree["radius"].toDouble(),.1,30.)},{"colors",colors}};}l.presentation=opts;return l;
}
QJsonObject layerJson(const Layer &l){auto o=l.presentation;o["id"]=l.id;o["visible"]=l.visible;o["color"]=l.color.name();o["opacity"]=l.opacity;o["presentation"]=l.presentation;return o;}
QVector<Layer> restoreLayerState(const QVector<Layer> &base,const QJsonObject &state,const QJsonArray &order){QVector<Layer> result;QSet<QString> seen;const auto append=[&](const QString &id){if(seen.contains(id))return;for(const auto &l:base)if(l.id==id){result.append(restoreLayer(l,state[id].toObject()));seen.insert(id);break;}};for(const auto &v:order)append(v.toString());for(const auto &l:base)append(l.id);return result;}
void loadPresentationCatalog(const QString &file){QFile f(file);
 if(!f.open(QIODevice::ReadOnly))return;
 const auto root=QJsonDocument::fromJson(f.readAll()).object();QVector<Palette> ps;QVector<DiagramPreset> ds;QVector<Layer> ls;QHash<QString,QJsonArray> states;
 for(const auto &v:root["layers"].toArray()){const auto o=v.toObject();const auto color=validColor(o["color"]);
 if(!color.isValid()||o["id"].toString().isEmpty())continue;
 Layer l;l.id=o["id"].toString();l.vi=o["vi"].toString();l.en=o["en"].toString();l.groupVi=o["group"].toString();l.groupEn=o["groupEn"].toString();l.color=color;l.visible=o.contains("defaultVisible")?o["defaultVisible"].toBool():l.id!="landuse";ls.append(l);}
 const auto parse=[&](const QJsonObject &o){const auto c=o["colors"].toObject();return Palette{o["id"].toString(),o["vi"].toString(),o["en"].toString(),QColor(c["paper"].toString()),QColor(c["building"].toString()),QColor(c["road"].toString()),QColor(c["green"].toString()),QColor(c["water"].toString()),QColor(c["ink"].toString())};};
 for(const auto &v:root["styles"].toArray()){const auto o=v.toObject();const auto p=parse(o);
 if(!p.id.isEmpty()&&p.paper.isValid()&&p.ink.isValid()){ps.append(p);states[p.id]=o["layerState"].toArray();}}
 for(const auto &v:root["diagrams"].toArray()){const auto o=v.toObject();DiagramPreset d;d.palette=parse(o);d.id=d.palette.id;d.vi=d.palette.vi;d.en=d.palette.en;d.opacity=o["layerOpacity"].toObject();for(const auto &l:o["visibleLayers"].toArray())d.layers.append(l.toString());for(const auto &l:o["requiredLayers"].toArray())d.requiredLayers.append(l.toString());
 if(!d.id.isEmpty()&&!d.layers.isEmpty()){ds.append(d);states[d.id]=o["layerState"].toArray();}}
 if(!ps.isEmpty()&&!ds.isEmpty()){catalogPalettes=ps;catalogDiagrams=ds;styleLayers=states;
 if(!ls.isEmpty()){Layer other;other.id="context";other.vi="Bối cảnh chưa phân loại";other.en="Unclassified context";other.groupVi="Khác";other.groupEn="Other";other.color=QColor("#dadbd3");other.visible=false;ls.append(other);catalogLayers=ls;}}
}
QVector<DiagramPreset> diagramPresets(){return catalogDiagrams;}
QVector<Layer> presentationLayers(const QVector<Layer> &current,const QString &style,bool kit){const auto key=canonicalStyle(style);const auto rows=styleLayers.value(key);QHash<QString,QJsonObject> byId;QJsonArray order;for(const auto &v:rows){const auto o=v.toObject();byId[o["id"].toString()]=o;order.append(o["id"]);}auto result=current;
 if(key=="suushka-study"){QJsonObject state;for(auto i=byId.cbegin();i!=byId.cend();++i)state[i.key()]=i.value();result=restoreLayerState(current,state,order);}
 for(auto &l:result){const auto visible=l.visible;const auto opacity=l.opacity;const auto o=byId.value(l.id);l.presentation={};l=restoreLayer(l,o);
 if(!kit&&key!="suushka-study"){l.visible=visible;l.opacity=opacity;}}return result;
}
QVector<Layer> thematicLayers(QVector<Layer> ls,const QString &diagram){const auto ids=theme(diagram);
 if(ids.isEmpty())return ls;
 for(auto &l:ls){l.visible=ids.contains(l.id);
 if(diagram=="figure"&&l.id=="buildings")l.color=QColor("#273744");}return ls;}
bool diagramMatches(const Feature &f,const QString &id){for(const auto &d:catalogDiagrams)if(d.id==id){for(const auto &layer:d.layers)if(matchesLayer(f,layer))return true;
 return false;}const auto ids=theme(id);
 if(ids.isEmpty())return true;
 for(const auto &layer:ids)if(matchesLayer(f,layer))return true;
 return false;}
bool diagramAvailable(const Project &p,const QString &id){for(const auto &d:catalogDiagrams)if(d.id==id){const auto ids=d.requiredLayers.isEmpty()?d.layers:d.requiredLayers;for(const auto &f:p.features)for(const auto &layer:ids)if(matchesLayer(f,layer))return true;
 return false;}for(const auto &f:p.features)if(diagramMatches(f,id))return true;
 return false;}
bool sameClassification(const Feature &a,const Feature &b){const auto line=[](const Feature &f){return !f.rings.isEmpty()&&f.rings[0].size()>1;};return a.layer==b.layer&&a.geometry==b.geometry&&line(a)==line(b)&&(a.height==b.height||(std::isnan(a.height)&&std::isnan(b.height)))&&a.tags==b.tags&&a.properties==b.properties;}
const QVector<Paint> &FeaturePaintCache::paints(const Feature &f){if(!valid_||!sameClassification(previous_,f)||(f.tags["natural"]=="tree"&&previous_.id!=f.id)){previous_=f;paints_=featurePaints(f,layers_,diagram_);valid_=true;}return paints_;}
QMap<QString,int> layerCounts(const QVector<Feature> &features){QMap<QString,int> counts;const auto definitions=defaultLayers();for(const auto &l:definitions)counts[l.id]=0;Feature previous;QStringList matched;bool valid=false;for(const auto &f:features)if(!f.rings.isEmpty()&&!f.rings[0].isEmpty()){if(!valid||!sameClassification(previous,f)){matched.clear();for(const auto &l:definitions)if(matchesLayer(f,l.id))matched.append(l.id);previous=f;valid=true;}for(const auto &id:matched)++counts[id];}return counts;}
Geometry layerFeatureKind(const Feature &f,const Layer &l){if(f.geometry==Geometry::Point)return Geometry::Point;const auto &t=f.tags;const bool center=l.presentation.contains("lineWidth")&&t["area"]!="yes"&&(presentTag(t["highway"])||presentTag(t["railway"])||(presentTag(t["waterway"])&&t["waterway"]!="riverbank"&&t["natural"]!="water"&&!presentTag(t["water"])));return center?Geometry::Line:f.geometry;}
double lineWidthMetres(const Feature &f,const Layer &l,double radius){const double scale=166/std::max(.001,radius);const auto &o=l.presentation;
 if(!o.contains("lineWidth"))return (l.id=="roads"?1.5:.8)/scale;
 double width=metres(tag(f,"width"));
 if(!positive(width)){static const QHash<QString,double> widths={{"motorway",18},{"motorway_link",9},{"trunk",16},{"trunk_link",8},{"primary",14},{"primary_link",7},{"secondary",11},{"secondary_link",6},{"tertiary",9},{"tertiary_link",5},{"residential",6},{"unclassified",6},{"service",4},{"track",3},{"road",6},{"busway",7}};width=o["roadHierarchy"].toBool()&&widths.contains(f.tags["highway"].toString())?widths[f.tags["highway"].toString()]:o["lineWidth"].toDouble();}return std::max(width,o["lineMinWidth"].toDouble()/scale);}
double pointRadiusMetres(const Feature &f,const Layer &l,double radius){if(l.id=="trees"&&l.presentation.contains("tree")){const auto recorded=metres(tag(f,"diameter_crown"));return positive(recorded)?std::min(30.,recorded/2):l.presentation["tree"].toObject()["radius"].toDouble(2.4);}return(l.id=="trees"?1.3:2.4)*radius/166.;}
QColor pointColor(const Feature &f,const Layer &l){const auto colors=l.presentation["tree"].toObject()["colors"].toArray();
 if(l.id!="trees"||colors.isEmpty()||!colors.contains(l.color.name()))return l.color;
 quint32 hash=2166136261;for(int i=0;i<f.id.size();++i){const auto c=f.id[i].unicode();hash^=c;hash*=16777619;if(QChar::isHighSurrogate(c)&&i+1<f.id.size()&&QChar::isLowSurrogate(f.id[i+1].unicode()))++i;}return QColor(colors[int(hash%colors.size())].toString());}
QVector<Paint> featurePaints(const Feature &f,const QVector<Layer> &layers,const QString &diagram){QVector<Paint> result;
 if(!diagramMatches(f,diagram))return result;
 const auto themed=theme(diagram);for(int i=0;i<layers.size();++i){const auto &l=layers[i];
 if(l.visible&&l.opacity>0&&(themed.isEmpty()||themed.contains(l.id))&&matchesLayer(f,l.id))result.append({l,l.id=="trees"?pointColor(f,l):detailedColor(f,l.id,l.color),l.opacity,i,layerFeatureKind(f,l)});}return result;}
QColor compositePaints(const QVector<Paint> &paints){double alpha=0,r=0,g=0,b=0;for(const auto &p:paints){const double a=std::clamp(p.opacity,0.,1.),next=a+alpha*(1-a);
 if(next>0){r=(p.color.redF()*a+r*alpha*(1-a))/next;g=(p.color.greenF()*a+g*alpha*(1-a))/next;b=(p.color.blueF()*a+b*alpha*(1-a))/next;}alpha=next;}return QColor::fromRgbF(r,g,b,alpha);}
QColor featureColor(const Feature &f,const Palette &,const QString &,const QColor &fallback){return f.building()?detailedColor(f,"buildings",fallback):fallback;}
}
