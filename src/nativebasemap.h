#pragma once
#include "contextcamera.h"
#include <QObject>
#include <QOpenGLVertexArrayObject>
#include <QTimer>
#include <memory>
namespace QMapLibre {class Map;}

// Lives on the widget's GUI/GL thread. initialize/render/release require that
// widget's context current. Camera, culling, picking and drawing use one matrix.
class NativeBasemap:public QObject {
 Q_OBJECT
public:
 explicit NativeBasemap(QString cacheDirectory,QObject *parent=nullptr);
 ~NativeBasemap() override;
 void initialize(QSize size,double ratio);
 void release();
 void render(QSize size,double ratio,unsigned framebuffer);
 void setCamera(const ga::ContextCamera &camera);
 void setPresentation(QVector<ga::Layer> layers,ga::Palette palette,bool styleEnabled);
 void setHanoiPlanning(bool enabled,const QString &layerId=QStringLiteral("qhpk"),double opacity=0.78);
 QJsonObject diagnostics()const;
 QPointF pixelForCoordinate(double lat,double lon)const;
signals:
 void needsRendering();
private:
 QString cacheDirectory_,renderer_,error_;
 QJsonObject originalStyle_;
 std::unique_ptr<QMapLibre::Map> map_;
 QOpenGLVertexArrayObject vao_;
 QTimer loading_;
 QVector<ga::Layer> layers_;
 ga::Palette palette_;
 ga::ContextCamera camera_;
 bool styled_=true,styleReady_=false,ready_=false;
 bool hanoiPlanEnabled_=false;
 QString hanoiPlanLayer_=QStringLiteral("qhpk"),appliedHanoiLayer_;
 double hanoiPlanOpacity_=0.78;
 unsigned glError_=0;
 void applyStyle();
 void applyCamera();
 void syncHanoiPlanningLayer();
};
