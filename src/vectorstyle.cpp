#include "vectorstyle.h"
#include <QRegularExpression>
#include <cmath>

namespace ga {
QString mutedColor(const QString &hex,double amount){
 if(!QRegularExpression("^#[0-9a-fA-F]{6}$").match(hex).hasMatch())return "#eeeeee";
 QString result="#";
 for(int i:{1,3,5}){const int channel=hex.mid(i,2).toInt(nullptr,16);result+=QString::number(qRound(255+(channel-255)*amount),16).rightJustified(2,'0');}
 return result;
}
QJsonObject contextPalette(const QVector<Layer> &layers,const QString &paper){
 const auto find=[&](const QString &id,const QString &fallback){for(const auto &layer:layers)if(layer.id==id)return layer.color.name();return fallback;};
 QString ink="#53616a";for(const auto &layer:layers)if(layer.id=="buildings"){const auto stroke=layer.presentation["stroke"].toString();if(!stroke.isEmpty()&&stroke!="none")ink=stroke;break;}
 return{{"paper",mutedColor(paper,.32)},{"building",mutedColor(find("buildings","#b8b4ae"),.38)},
 {"ink",mutedColor(ink,.23)},{"roads",mutedColor(find("roads","#cebaa2"),.25)},
 {"walk",mutedColor(find("walk","#b5aaa0"),.25)},{"rail",mutedColor(find("rail","#8e8d91"),.25)},
 {"green",mutedColor(find("green","#a7bc9b"),.28)},{"water",mutedColor(find("water","#94b9c8"),.28)}};
}
QStringList contextPaintProperties(const QJsonObject &layer){
 if(layer["id"].toString().startsWith("groundatlas-site-vector"))return{};
 const auto type=layer["type"].toString();
 if(type=="background")return{"background-color"};
 if(type=="fill")return layer["source-layer"]=="building"?QStringList{"fill-color","fill-outline-color"}:QStringList{"fill-color"};
 if(type=="line")return{"line-color"};
 const auto text=layer["layout"].toObject()["text-field"];
 if(type=="symbol"&&(!text.isNull()&&!text.isUndefined())&&(!text.isString()||!text.toString().isEmpty()))return{"text-color","text-halo-color"};
 return{};
}
QJsonObject restyleContext(const QJsonObject &original,const QVector<Layer> &layers,const QString &paper,bool enabled){
 if(!enabled)return original;
 auto result=original;auto definitions=result["layers"].toArray();const auto colors=contextPalette(layers,paper);
 for(int i=0;i<definitions.size();++i){auto layer=definitions[i].toObject();const auto keys=contextPaintProperties(layer);if(keys.isEmpty())continue;auto paint=layer["paint"].toObject();const auto source=layer.value("source-layer").toString(),id=layer.value("id").toString(),type=layer.value("type").toString();
  for(const auto &key:keys){QString role="paper";
   if(type=="fill")role=source=="building"?"building":source=="water"?"water":source=="park"||source=="landcover"?"green":"paper";
   if(type=="line")role=source=="waterway"?"water":source=="boundary"?"ink":id.contains("railway")?"rail":QRegularExpression("path|pier").match(id).hasMatch()?"walk":"roads";
   if(key=="fill-outline-color"||key=="text-color")role="ink";
   if(key=="text-halo-color")role="paper";
   paint[key]=colors[role];
  }
  layer["paint"]=paint;definitions[i]=layer;
 }
 result["layers"]=definitions;return result;
}
}
