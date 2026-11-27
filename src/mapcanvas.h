#pragma once
#include "scene.h"
#include "planpainter.h"
#include <QOpenGLWidget>
#include <QOpenGLVertexArrayObject>
#include <QCache>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QRectF>
#include <memory>
namespace QMapLibre {class Map;}
class QNetworkAccessManager;
class QTimer;

class MapCanvas:public QOpenGLWidget {
 Q_OBJECT
public:
 MapCanvas(const QString &cacheDirectory,QWidget *parent=nullptr);
 ~MapCanvas() override;
 void shutdown();
 void finishInteraction(){interactionIdle_.stop();interacting_=false;overlayDirty_=true;hudDirty_=true;update();if(deferredNavSync_){deferredNavSync_=false;emit navigationChanged();emit viewChanged();}}
 bool isInteracting()const{return interacting_;}
 void setContextEnabled(bool enabled);
 bool contextEnabled()const{return contextEnabled_;}
 void setDarkUi(bool dark){if(darkUi_==dark)return;darkUi_=dark;hudDirty_=true;update();}
 bool darkUi()const{return darkUi_;}
 void setBasemapMode(const QString &mode);
 QString basemapMode()const{return basemapMode_;}
 void setForceRasterFallback(bool enabled);
 bool usingRasterFallback()const{return useRasterFallback_;}
 void setHanoiPlanning(bool enabled,const QString &layerId=QStringLiteral("qhpk"),double opacity=0.78);
 bool hanoiPlanningEnabled()const{return hanoiPlanEnabled_;}
 QString hanoiPlanningLayer()const{return hanoiPlanLayer_;}
 double hanoiPlanningOpacity()const{return hanoiPlanOpacity_;}
 QJsonObject basemapDiagnostics()const;
 void setProject(const ga::Project *project);
 void setPresentation(QVector<ga::Layer> layers,ga::Palette palette,QString diagram);
 void setLanguage(bool english){english_=english;hudDirty_=true;update();}
 void centerOn(double lat,double lon,double zoom=16);
 QPointF location() const;
 QJsonObject view() const{return{{"lat",lat_},{"lon",lon_},{"zoom",zoom_},{"context_style",contextEnabled_},{"basemap_mode",basemapMode_},{"hanoi_planning",hanoiPlanEnabled_},{"hanoi_planning_layer",hanoiPlanLayer_},{"hanoi_planning_opacity",hanoiPlanOpacity_},{"renderer","maplibre-native"}};}
 void zoomBy(double amount);
 void setCandidate(const ga::Site &site,bool locked=false){candidate_=site;candidateLocked_=locked;hudDirty_=true;update();}
 void beginBoundaryDrawing();
 void finishBoundaryDrawing();
 void undoBoundaryVertex();
 void cancelBoundaryDrawing();
 QJsonObject draftState() const;
 void restoreDraft(const QJsonObject &state);
signals:
 void viewChanged();
 void navigationChanged();
 void sitePicked(double lat,double lon);
 void message(QString text);
 void boundaryDrawn(QPolygonF geographicVertices);
 void boundaryEdited(QPolygonF geographicVertices);
 void drawingChanged(bool active,int vertices);
 void basemapModeChanged(QString mode);
 void hanoiPlanningToggled(bool enabled);
 void cityJumpRequested(double lat,double lon,QString name);
 void loadSiteRequested();
 void compatFallbackTriggered(QString reason,int glError);
protected:
 void initializeGL()override;
 void resizeGL(int,int)override;
 void paintGL()override;
 void mousePressEvent(QMouseEvent *)override;
 void mouseMoveEvent(QMouseEvent *)override;
 void mouseReleaseEvent(QMouseEvent *)override;
 void wheelEvent(QWheelEvent *)override;
 void mouseDoubleClickEvent(QMouseEvent *)override;
 void keyPressEvent(QKeyEvent *)override;
 void leaveEvent(QEvent *)override;
private:
 struct HudBtn {
  QString id,label,tooltip;
  QRectF rect;
  bool active=false,accent=false;
  double lat=0,lon=0;
 };
 const ga::Project *project_=nullptr;
 PlanCache planCache_;
 PlanGeometry planGeometry_;
 PlanImageCache interactionImage_;
 QTimer interactionIdle_;
 bool interacting_=false;
 QVector<ga::Layer> layers_=ga::defaultLayers();
 ga::Palette palette_=ga::paletteFor("graphite");
 QString diagram_="context",cacheDirectory_,renderer_,mapError_,basemapMode_="vector";
 std::unique_ptr<QMapLibre::Map> vector_;
 QOpenGLVertexArrayObject vectorVao_;
 QTimer *loadingTimer_=nullptr;
 unsigned vectorGlError_=0;
 int consecutiveGlErrors_=0;
 bool forceRasterFallback_=false,useRasterFallback_=false,blackProbeDone_=false;
 QString watchdogReason_;
 QNetworkAccessManager *rasterNet_=nullptr;
 QCache<QString,QImage> rasterTiles_{160};
 QSet<QString> rasterPending_;
 QSet<QString> rasterFailed_;
 QJsonObject originalStyle_;
 QImage siteOverlay_,hudBuffer_;
 QSize lastVectorSize_,lastVectorPixelSize_;
 unsigned lastVectorFbo_=0;
 bool contextEnabled_=true,styleReady_=false,fullyRendered_=false,overlayDirty_=true,hudDirty_=true,hasSiteOverlay_=false,darkUi_=false;
 bool deferredNavSync_=false;
 bool hanoiPlanEnabled_=false;
 QString hanoiPlanLayer_=QStringLiteral("qhpk"),appliedHanoiLayer_;
 double hanoiPlanOpacity_=0.78;
 double lat_=21.0288,lon_=105.8525,zoom_=16;
 ga::Site candidate_;
 QPolygonF draft_;
 bool candidateLocked_=false,drawing_=false,freehand_=false,skipRelease_=false,vertexMoved_=false;
 int draggedVertex_=-1;
 QPointF lastMouse_,pressMouse_,hoverScreen_;
 bool dragging_=false,hasHover_=false,english_=false;
 QVector<HudBtn> hudButtons_;
 QString hoverHudId_;
 QPointF world(double lon,double lat,double z) const;
 QPointF unworld(const QPointF &p,double z) const;
 QPointF screen(double lon,double lat) const;
 double minZoom() const;
 double clampCenterLat(double lat,double z) const;
 void cleanupVector();
 void syncVectorView();
 void applyVectorStyle();
 void loadStyleForMode();
 void syncHanoiPlanningLayer();
 void engageRasterFallback(const QString &reason,int glError=0);
 void drawRasterFallback(QPainter &painter);
 void requestRasterTile(const QString &key,const QString &url,const QString &fallbackUrl={});
 void drawGeometry(QPainter &painter);
 void drawSelection(QPainter &painter);
 void drawHud(QPainter &painter);
 QString hitTestHud(const QPointF &pos) const;
 bool handleHudClick(const QString &id);
 QPointF geographicAt(const QPointF &screenPoint) const;
 void beginInteraction();
};
