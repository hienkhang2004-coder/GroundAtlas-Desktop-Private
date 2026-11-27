#include "vectorstyle.h"
#include <QTest>
#include <QFile>
#include <QJsonDocument>

class VectorContextTests:public QObject{
 Q_OBJECT
 QJsonObject fixture_;
private slots:
 void initTestCase(){QFile file(QString::fromUtf8(GA_VECTOR_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));fixture_=QJsonDocument::fromJson(file.readAll()).object();ga::loadPresentationCatalog(QString::fromUtf8(GA_DATA)+"/presentations.json");}
 void mutedColorMatchesWeb(){for(const auto &v:fixture_["colorCases"].toArray()){const auto o=v.toObject();QCOMPARE(ga::mutedColor(o["hex"].toString(),o["amount"].toDouble()),o["expected"].toString());}}
 void contextPaletteAndStyleMatchWeb(){int count=0;for(const auto &v:fixture_["cases"].toArray()){const auto o=v.toObject();const auto layers=ga::presentationLayers(ga::defaultLayers(),o["id"].toString(),o["id"].toString().startsWith("ga-diagram-"));QCOMPARE(ga::contextPalette(layers,o["paper"].toString()),o["palette"].toObject());const auto original=o["original"].toObject();const auto styled=ga::restyleContext(original,layers,o["paper"].toString());if(styled!=o["styled"].toObject()){QFile difference(QString::fromUtf8(GA_DATA)+"/../validation/vector-context-difference.json");if(difference.open(QIODevice::WriteOnly))difference.write(QJsonDocument(QJsonObject{{"id",o["id"]},{"actual",styled},{"expected",o["styled"]}}).toJson());}QVERIFY2(styled==o["styled"].toObject(),qPrintable(o["id"].toString()));QCOMPARE(ga::restyleContext(original,layers,o["paper"].toString(),false),original);QCOMPARE(original,o["original"].toObject());++count;}QCOMPARE(count,118);}
 void sourceAndAnalysisInputStayIndependent(){ga::Project p;p.source={{"provider","test-source-window"}};p.site.boundary=ga::circleBoundary(500);ga::Feature feature;feature.id="building";feature.layer="buildings";feature.tags={{"building","yes"}};feature.rings={QPolygonF{{0,0},{10,0},{10,10},{0,10}}};p.features={feature};const auto before=p.toJson();const auto original=fixture_["cases"].toArray()[0].toObject()["original"].toObject();const auto result=ga::restyleContext(original,ga::defaultLayers(),"#123456");QVERIFY(result!=original);QCOMPARE(result["sources"],original["sources"]);QCOMPARE(p.toJson(),before);QCOMPARE(p.stats().buildings,1);QCOMPARE(p.geoJson()["features"].toArray().size(),1);}
 void siteLayersAndUnrelatedPaintStayUntouched(){const auto o=fixture_["cases"].toArray()[1].toObject();const auto original=o["original"].toObject();const auto styled=ga::restyleContext(original,ga::defaultLayers(),"#102030");const auto before=original["layers"].toArray(),after=styled["layers"].toArray();for(int i=0;i<before.size();++i){const auto layer=before[i].toObject();if(layer["id"].toString().startsWith("groundatlas-site-vector")||layer["id"]=="icon-only")QCOMPARE(after[i],before[i]);if(layer["type"]=="line")QCOMPARE(after[i].toObject()["paint"].toObject()["line-width"],layer["paint"].toObject()["line-width"]);}}
};
QTEST_GUILESS_MAIN(VectorContextTests)
#include "vector_context_tests.moc"
