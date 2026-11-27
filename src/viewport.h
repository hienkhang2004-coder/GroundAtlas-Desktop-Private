#pragma once
#include "perfprofile.h"
#include "scene.h"
#include "section.h"
#include "planpainter.h"
#include "urbananalytics.h"
#include "massing.h"
#include "model3d.h"
#include "planning.h"
#include "planningtiles.h"
#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
#include <memory>
class NativeBasemap;

class Viewport:public QOpenGLWidget,protected QOpenGLExtraFunctions {
 Q_OBJECT
public:
 explicit Viewport(QWidget *parent=nullptr);
 Viewport(const QString &cacheDirectory,QWidget *parent=nullptr);
 ~Viewport() override;
 void setProject(const ga::Project *project);
 void setLayers(const QVector<ga::Layer> &layers);
 void setPalette(const ga::Palette &palette);
 void setDiagram(const QString &diagram);
 void setPlot(const ga::PlotDesign &plot);
 ga::PlotDesign plot() const {return plot_;}
 void setMassing(const ga::MassingConfig &massing);
 ga::MassingConfig massing() const {return massing_;}
 void setModels3d(const QVector<ga::ImportedModel3D> &models);
 const QVector<ga::ImportedModel3D> &models3d() const {return models3d_;}
 void setSelectedModel3d(int index);
 int selectedModel3d() const {return selectedModel3d_;}
 void setPlacingModel3d(bool active);
 bool isPlacingModel3d() const {return placingModel3d_;}
 void focusModel3d(int index);
 void beginPlotDrawing();
 void finishPlotDrawing();
 void undoPlotVertex();
 void cancelPlotDrawing();
 bool isDrawingPlot() const {return drawingPlot_;}
 void setAnalyticalOverlay(const ga::AnalyticalOverlay &overlay);
 const ga::AnalyticalOverlay &analyticalOverlay() const {return overlay_;}
 void setFuturePlanningOverlay(const ga::FuturePlanningOverlay &overlay);
 const ga::FuturePlanningOverlay &futurePlanningOverlay() const {return planningOverlay_;}
 void setPlanningImageOverlay(const ga::PlanningImageOverlay &img);
 const ga::PlanningImageOverlay &planningImageOverlay() const {return planningImage_;}
 void setHanoiPlanning(bool enabled,const QString &layerId=QStringLiteral("qhpk"),double opacity=0.78);
 bool hanoiPlanningEnabled() const {return hanoiPlanEnabled_;}
 QString hanoiPlanningLayer() const {return hanoiPlanLayer_;}
 double hanoiPlanningOpacity() const {return hanoiPlanOpacity_;}
 void setHanoiTileComposite(const ga::HanoiTileComposite &composite);
 const ga::HanoiTileComposite &hanoiTileComposite() const {return hanoiComposite_;}
 void setLanguage(bool english);
 void setDarkUi(bool dark);
 bool darkUi() const {return darkUi_;}
 void setAutoDarkBasemap(bool enabled);
 void setGraphicsConfig(const ga::ResolvedGraphicsConfig &cfg);
 ga::ResolvedGraphicsConfig graphicsConfig() const {return gfxConfig_;}
 void setSmoothDamping(bool enabled);
 void setTerrain3d(bool enabled,const QString &envDirectory={});
 bool terrain3dEnabled() const {return terrain3d_;}
 void setHudDockVisible(bool visible);
 void setCamera(const ga::Camera &camera);
 void setSolarLive(const ga::SolarState &solar);
 void animateCameraTo(const ga::Camera &target,int durationMs=300);
 void animateResetView(int durationMs=320);
 void animateZoomBy(double factor,int durationMs=190);
 void animateAlignNorth(int durationMs=280);
 bool isCameraAnimating() const {return camAnimTimer_.isActive();}
 void setMeasuring(bool active);
 bool isMeasuring() const {return measuring_;}
 void clearMeasurement();
 void setSectionCutActive(bool active);
 bool isSectionCutActive() const {return sectionCutActive_;}
 void setSectionCutLine(QPointF a,QPointF b);
 QPointF sectionCutA() const {return sectionA_;}
 QPointF sectionCutB() const {return sectionB_;}
 QJsonObject geographicView()const;
 void setGeographicView(double latitude,double longitude,double logicalZoom);
 void setContextStyleEnabled(bool enabled);
 void setContextVisible(bool enabled);
 bool contextVisible()const{return contextVisible_;}
 void setBasemapMode(const QString &mode);
 QString basemapMode()const{return basemapMode_;}
 QJsonObject basemapDiagnostics()const;
 void shutdown();
 void finishInteraction(){interactionIdle_.stop();interacting_=false;planDirty_=true;update();}
 ga::Camera camera() const{return camera_;}
 void resetView();
 void zoomBy(double factor);
 void setSelected(int index);
 int selected() const {return selected_;}
 int triangleCount() const{return (mesh_.triangles.size()+mesh_.transparent.size())/3;}
 QString renderer() const {return renderer_;}
 QString glVersion() const {return glVersion_;}
 int maxTextureSize() const {return maxTextureSize_;}
 int maxMsaaSamples() const {return maxMsaaSamples_;}
 QString graphicsError() const {return graphicsError_;}
 bool exportVector(const QString &file,bool pdf);
 bool exportObj(const QString &file);
 bool exportHighResPng(const QString &file,int targetWidth,int targetHeight,bool transparentBackground);
 bool exportPlanPng(const QString &file,int targetWidth=3840,int targetHeight=2160,bool transparentBackground=false);
 bool exportSun3dPng(const QString &file,int targetWidth=3840,int targetHeight=2160,bool transparentBackground=false);
 bool exportSunTimelapseGif(const QString &file,int width=960,int height=600,int frames=24,int delayCs=8);
 bool exportTurntableGif(const QString &file,int width=960,int height=600,int frames=36,int delayCs=7);
signals:
 void cameraChanged();
 void framingChanged();
 void featureSelected(int index);
 void cursorPosition(QPointF local);
 void graphicsReady(QString renderer);
 void plotDrawingChanged(bool active,int vertices);
 void plotDrawn(QPolygonF localRing);
 void plotEdited(ga::PlotDesign plot);
 void measuringChanged(bool active);
 void sectionCutChanged(QPointF a,QPointF b,bool active);
 void dockActionTriggered(QString actionId);
 void model3dMoved(int index,double x,double y);
 void placingModel3dChanged(bool active);
 void hanoiPlanningToggled(bool enabled);
 void basemapModeChanged(QString mode);
 void compatFallbackTriggered(QString reason,int glError);
protected:
 void initializeGL() override;
 void resizeGL(int,int)override;
 void resizeEvent(QResizeEvent *)override;
 void hideEvent(QHideEvent *)override;
 void paintGL() override;
 void mousePressEvent(QMouseEvent *) override;
 void mouseMoveEvent(QMouseEvent *) override;
 void mouseReleaseEvent(QMouseEvent *) override;
 void leaveEvent(QEvent *) override;
 void wheelEvent(QWheelEvent *) override;
 void mouseDoubleClickEvent(QMouseEvent *) override;
 void keyPressEvent(QKeyEvent *) override;
private:
 const ga::Project *project_=nullptr;
 QVector<ga::Layer> layers_=ga::defaultLayers();
 ga::Palette palette_=ga::paletteFor("graphite");
 ga::Camera camera_;
 ga::Mesh mesh_;
 ga::Mesh sunMesh_;
 ga::Stats stats_;
 ga::PlotDesign plot_;
 ga::MassingConfig massing_;
 QVector<ga::ImportedModel3D> models3d_;
 int selectedModel3d_=-1;
 bool placingModel3d_=false,draggingModel3d_=false;
 QPointF dragModelStartLocal_,dragModelStartXY_;
 bool drawingPlot_=false,freehandPlot_=false,skipPlotRelease_=false;
 int dragPlotVertex_=-1;
 bool dragPlotWhole_=false;
 QPointF dragPlotStartLocal_;
 QPolygonF dragPlotStartRing_;
 int hoveredPlotVertex_=-1;
 bool hoveredPlotBody_=false;
 QPolygonF plotDraft_;
 bool measuring_=false;
 QPolygonF measurePts_;
 bool sectionCutActive_=false;
 QPointF sectionA_{-420,0},sectionB_{420,0};
 int dragSectionPart_=0,hoverSectionPart_=0; // 0=none, 1=A, 2=B, 3=line
 QPointF dragSectionStartLocal_,dragSectionStartA_,dragSectionStartB_;
 QPointF hoverLocal_;
 bool hasHoverLocal_=false;
 ga::AnalyticalOverlay overlay_;
 ga::FuturePlanningOverlay planningOverlay_;
 ga::PlanningImageOverlay planningImage_;
 bool hanoiPlanEnabled_=false;
 QString hanoiPlanLayer_=QStringLiteral("qhpk");
 double hanoiPlanOpacity_=0.78;
 ga::HanoiTileComposite hanoiComposite_;
 bool hanoiPlanTexDirty_=false;
 ga::TerrainGrid terrainDem_;
 QString envDirectory_;
 bool terrain3d_=false;
 ga::ResolvedGraphicsConfig gfxConfig_;
 bool autoDarkBasemap_=true,smoothDamping_=false;
 QTimer dampTimer_;
 QElapsedTimer dampClock_;
 bool dampZoomActive_=false;
 double dampTargetZoom_=1.0;
 QPointF dampAnchorLocal_,dampAnchorScreen_;
 double dampYawVel_=0.0,dampElVel_=0.0;
 PlanCache planCache_;
 PlanGeometry planGeometry_;
 PlanImageCache interactionImage_;
 QTimer interactionIdle_;
 bool interacting_=false;
 QImage planImage_;
 QString diagram_="context",renderer_,glVersion_,graphicsError_;
 int maxTextureSize_=4096,maxMsaaSamples_=4;
 bool dirty_=true,uploadDirty_=true,sunDirty_=true,sunUploadDirty_=true,shadowNeedsResize_=false,planDirty_=true,planCacheDirty_=true,english_=false,dragging_=false;
 bool darkUi_=false,hudDockVisible_=true,compassHovered_=false,basemapThumbHovered_=false;
 QString basemapMode_=QStringLiteral("vector");
 int selected_=-1,hoveredFeature_=-1,triangleVertices_=0,lineVertices_=0,transparentVertices_=0;
 int sunTriVerts_=0,sunLineVerts_=0,sunTransVerts_=0;
 QPointF lastMouse_,pressMouse_,hoverScreen_;
 QString hoveredDockId_;
 QTimer camAnimTimer_;
 QElapsedTimer camAnimClock_;
 int camAnimDurationMs_=300;
 ga::Camera camAnimStart_,camAnimTarget_;
 QOpenGLShaderProgram shader_;
 QOpenGLShaderProgram shadowShader_;
 QOpenGLShaderProgram skyShader_;
 QOpenGLShaderProgram tintShader_;
 QOpenGLShaderProgram ssaoShader_;
 QOpenGLShaderProgram ssaoBlurShader_;
 QOpenGLShaderProgram planTexShader_;
 QOpenGLBuffer buffer_{QOpenGLBuffer::VertexBuffer};
 QOpenGLVertexArrayObject vao_;
 QOpenGLBuffer sunBuffer_{QOpenGLBuffer::VertexBuffer};
 QOpenGLVertexArrayObject sunVao_;
 QOpenGLBuffer planTexBuffer_{QOpenGLBuffer::VertexBuffer};
 QOpenGLVertexArrayObject planTexVao_;
 QOpenGLVertexArrayObject quadVao_;
 GLuint shadowFbo_=0,shadowTex_=0,hanoiPlanTex_=0;
 int planTexVerts_=0;
 GLuint ssaoDepthFbo_=0,ssaoDepthTex_=0,ssaoFbo_=0,ssaoTex_=0,ssaoBlurFbo_=0,ssaoBlurTex_=0;
 int shadowSize_=2048,ssaoW_=0,ssaoH_=0;
 bool ssaoReady_=false;
 std::unique_ptr<NativeBasemap> basemap_;
 bool contextVisible_=true,contextStyled_=true,contextViewDirty_=true;
 bool sortDirty_=true;
 QVector<QVector3D> transparentCenters_;
 quint64 meshBuilds_=0,bufferUploads_=0,uploadedBytes_=0,planPaints_=0;
 double stackHeight_=0;
 QSize navigationSize_;
 ga::Camera sceneCamera()const;
 void cleanupGL();
 void initShadowMap(int size);
 void initSsaoBuffers(int halfW,int halfH);
 void destroySsaoBuffers();
 void rebuildSunDome();
 void uploadSunDome();
 void uploadHanoiPlanTexture();
 void reloadTerrainDem();
 void renderScene3DToTarget(GLuint targetFbo,int pixelW,int pixelH,const QSize &logicalSize,bool drawBasemap,bool transparentBg);
 QMatrix4x4 matrix() const;
 QMatrix4x4 sunLightMatrix(const QVector3D &sunDir,const QPointF &offset) const;
 QPointF groundPoint(const QPointF &screen) const;
 void panGround(QPointF before,QPointF after);
 void constrainZoom();
 void rebuild();
 void upload();
 void sortTransparent();
 void drawOverlay(QPainter &painter);
 void drawPlan(QPainter &painter,const QRectF &rect);
 void beginInteraction();
 void stopCameraAnimation();
 struct DockItem{QString id;QRectF rect;QString label;QString tip;bool active=false;bool accent=false;};
 QRectF compassRect() const;
 QRectF basemapThumbRect() const;
 QRectF dockPillRect() const;
 QVector<DockItem> dockItems() const;
 int hitTestBuilding(const QPointF &local) const;
 void triggerDockAction(const QString &id);
};
