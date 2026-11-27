#pragma once
#include "perfprofile.h"
#include "project.h"
#include "scene.h"
#include "section.h"
#include "urbananalytics.h"
#include "massing.h"
#include "model3d.h"
#include "planning.h"
#include "planningtiles.h"
#include "license.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QKeySequence>
#include <QTimer>
#include <functional>

class Viewport;
class MapCanvas;
class DataAccess;
class LiveSectionDock;
class QStackedWidget;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QToolButton;
class QFrame;
class QLabel;
class QComboBox;
class QTreeWidget;
class QListWidget;
class QSlider;
class QPlainTextEdit;
class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QTabBar;
class SiteSelection;
class QCheckBox;
class QProgressBar;
class SessionStore;
namespace ux {
class SlidingPillTrack;
class AnimatedStackedWidget;
class AnimatedDrawer;
class FloatingToast;
}

class MainWindow : public QMainWindow {
 Q_OBJECT
public:
 explicit MainWindow(QString dataDirectory, QString projectPath = {}, QWidget *parent = nullptr);
 bool loadPath(const QString &path, bool confirm = true);
 bool import3DModel(const QString &path);
 bool importPlanningVector(const QString &path, ga::PlanningCrs crs = ga::PlanningCrs::Auto);
 void setHanoiPlanning(bool enabled, const QString &layerId = {}, double opacity = -1.0);
 bool hanoiPlanningEnabled() const { return hanoiPlanEnabled_; }
 ga::LicenseManager *licenseManager() const { return license_; }
 void showLicenseDialog(const QString &blockedFeature = {});
 void showUpdateDialog(bool userInitiated = true);
 void showOpenSourceLicenses();
 bool exportProjectKit(const QString &outputDir);
 bool importGeoJson(const QString &path, const QString &role);
 bool removeImportedGeoJson(const QString &id);
 QJsonObject diagnostics() const;
 Viewport *viewport() const { return viewport_; }
 void setDarkTheme(bool dark);
 bool darkTheme() const { return darkTheme_; }
 void setStudioOpen(bool open);
 void selectStudioTab(int index);
 void showToast(const QString &message, const QColor &accent = QColor("#c45632"), int durationMs = 2400);

protected:
 void closeEvent(QCloseEvent *) override;
 void resizeEvent(QResizeEvent *) override;
 void dragEnterEvent(QDragEnterEvent *) override;
 void dropEvent(QDropEvent *) override;

private:
 ga::Project project_;
 QVector<ga::Layer> layers_ = ga::defaultLayers();
 ga::PlotDesign plot_;
 ga::MassingConfig massing_;
 QVector<ga::ImportedModel3D> models3d_;
 QString activePlanningLevel_;
 double planningOpacity_ = 0.80;
 ga::PlanningCustomData planningCustom_;
 ga::PlanningImageOverlay planningImage_;
 ga::HanoiPlanningTileFetcher *hanoiTiles_ = nullptr;
 ga::LicenseManager *license_ = nullptr;
 QPushButton *licensePillBtn_ = nullptr;
 QPushButton *updatePillBtn_ = nullptr;
 QFrame *updateBanner_ = nullptr;
 QLabel *updateBannerLbl_ = nullptr;
 bool updateBannerDismissed_ = false;
 QTimer licenseOfferTick_;
 QLineEdit *customXyzEdit_ = nullptr;
 bool hanoiPlanEnabled_ = false;
 QString hanoiPlanLayer_ = QStringLiteral("qhpk");
 double hanoiPlanOpacity_ = 0.78;
 QString hanoiPlanStatus_;
 Viewport *viewport_ = nullptr;
 MapCanvas *map_ = nullptr;
 DataAccess *data_ = nullptr;
 LiveSectionDock *liveSection_ = nullptr;
 ga::TerrainGrid liveDem_;
 QStackedWidget *views_ = nullptr;
 ux::AnimatedStackedWidget *studioStack_ = nullptr;
 ux::AnimatedDrawer *leftDrawer_ = nullptr;
 ux::AnimatedDrawer *studioDrawer_ = nullptr;
 ux::FloatingToast *toast_ = nullptr;
 QVector<ux::SlidingPillTrack *> segTracks_;
 QWidget *leftPanel_ = nullptr;
 QWidget *studioPanel_ = nullptr;
 QLabel *logoIcon_ = nullptr;
 QToolButton *settingsIconBtn_ = nullptr;
 QToolButton *leftToggle_ = nullptr;
 QToolButton *rightToggle_ = nullptr;
 QPushButton *studioToggleBtn_ = nullptr;
 QToolButton *themeLightBtn_ = nullptr;
 QToolButton *themeDarkBtn_ = nullptr;
 QToolButton *langViBtn_ = nullptr;
 QToolButton *langEnBtn_ = nullptr;
 QVector<QToolButton *> studioTiles_;

 QLineEdit *search_ = nullptr;
 QListWidget *searchResults_ = nullptr;
 QLabel *candidateText_ = nullptr;
 SiteSelection *siteSelection_ = nullptr;
 QComboBox *sourceChoice_ = nullptr;
 QLabel *sourceHint_ = nullptr;
 QCheckBox *smartHeights_ = nullptr;
 QProgressBar *loadProgress_ = nullptr;
 QPushButton *loadButton_ = nullptr, *cancelButton_ = nullptr, *enrichButton_ = nullptr;

 // Design plot (Khu đất đồ án · Chữ nhật & Vẽ ranh tự do)
 QDoubleSpinBox *plotWidth_ = nullptr, *plotLength_ = nullptr, *plotRotation_ = nullptr;
 QCheckBox *plotDims_ = nullptr;
 QLabel *plotStatus_ = nullptr, *plotDraftHint_ = nullptr;
 QToolButton *plotModeRect_ = nullptr, *plotModePoly_ = nullptr;
 QWidget *plotRectPanel_ = nullptr, *plotPolyPanel_ = nullptr;
 QPushButton *plotDrawBtn_ = nullptr, *plotFinishBtn_ = nullptr, *plotUndoBtn_ = nullptr, *plotCancelBtn_ = nullptr;

 // Proposed Concept Massing Studio (Podium + Tower + FAR / BCR / GFA)
 QCheckBox *massingEnabled_ = nullptr;
 QWidget *massingPanel_ = nullptr;
 QDoubleSpinBox *massSetback_ = nullptr, *massPodH_ = nullptr, *massTowH_ = nullptr, *massTowRot_ = nullptr;
 QSpinBox *massPodFloors_ = nullptr, *massTowFloors_ = nullptr, *massTowCov_ = nullptr;
 QLabel *massingKpi_ = nullptr;

 // Imported 3D Models Studio (SketchUp .skp / .dae / .obj / .glb / .stl)
 QListWidget *model3dList_ = nullptr;
 QWidget *model3dEditor_ = nullptr;
 QLabel *model3dMeta_ = nullptr;
 QDoubleSpinBox *m3dX_ = nullptr, *m3dY_ = nullptr, *m3dZ_ = nullptr, *m3dYaw_ = nullptr, *m3dScale_ = nullptr;
 QComboBox *m3dUnitPreset_ = nullptr;
 QCheckBox *m3dFlipYUp_ = nullptr, *m3dOverrideColor_ = nullptr;
 QPushButton *m3dPlaceBtn_ = nullptr, *m3dSnapPlotBtn_ = nullptr, *m3dFocusBtn_ = nullptr, *m3dRemoveBtn_ = nullptr;

 // Hanoi Land-Use Planning Layer (quyhoach.hanoi.vn 1-click toggle + optional advanced tools)
 QPushButton *hanoiPlanToggleBtn_ = nullptr;
 QComboBox *hanoiPlanLayerCombo_ = nullptr;
 QSlider *hanoiPlanOpacitySlider_ = nullptr;
 QLabel *hanoiPlanOpacityLbl_ = nullptr;
 QLabel *hanoiPlanStatusLbl_ = nullptr;
 QWidget *planAdvPanel_ = nullptr;
 QLabel *planSubdivLbl_ = nullptr, *planCustomLbl_ = nullptr;
 QVector<QPushButton *> planLevelBtns_;
 QSlider *planOpacitySlider_ = nullptr;
 QComboBox *planCrsCombo_ = nullptr;
 QPushButton *planClearCustomBtn_ = nullptr;
 QWidget *planImgPanel_ = nullptr;
 QSlider *planImgOpacity_ = nullptr;
 QDoubleSpinBox *planImgWidth_ = nullptr, *planImgRot_ = nullptr, *planImgX_ = nullptr, *planImgY_ = nullptr;

 // Laser cut controls
 QComboBox *laserScale_ = nullptr;
 QCheckBox *laserSeparate_ = nullptr, *laserRoads_ = nullptr, *laserWater_ = nullptr;

 ga::Site candidate_;
 bool geographic_ = false, loading_ = false;
 quint64 revision_ = 0, loadRevision_ = 0;
 QString dataDirectory_, currentPath_, diagram_ = "context", activeAnalytic_;
 bool english_ = false, darkTheme_ = false, imperialUnits_ = false, modified_ = false, updating_ = false;
 ga::PerfTier perfTier_ = ga::PerfTier::Auto;
 bool smoothDamping_ = true, autoDarkBasemap_ = true, terrain3d_ = false;
 ga::HardwareInfo hwInfo_;
 ga::ResolvedGraphicsConfig gfxConfig_;
 QTimer autosave_;
 SessionStore *sessionStore_ = nullptr;
 bool closing_ = false;

 QTreeWidget *layerTree_ = nullptr;
 QLineEdit *layerSearch_ = nullptr;
 QComboBox *layerGroup_ = nullptr;
 QMap<QString, int> detailedCounts_;
 QCheckBox *contextStyle_ = nullptr, *refreshSource_ = nullptr;
 bool enriching_ = false;

 QComboBox *importRole_ = nullptr;
 QListWidget *importList_ = nullptr;
 QPushButton *importButton_ = nullptr, *removeImportButton_ = nullptr;
 bool importing_ = false;
 QString importDocumentId_;
 quint64 importDataRevision_ = 0;

 QComboBox *styles_ = nullptr, *diagrams_ = nullptr, *projections_ = nullptr, *language_ = nullptr, *sunSeason_ = nullptr;
 QLineEdit *diagramSearch_ = nullptr;
 QString diagramCategory_ = "all";
 QVector<QToolButton *> diagramCatBtns_;
 QListWidget *diagramList_ = nullptr;
 QSlider *separation_ = nullptr, *sunHour_ = nullptr, *sunShadow_ = nullptr;
 QCheckBox *sunEnabled_ = nullptr, *sunPath_ = nullptr, *sunGolden_ = nullptr, *sunSkyDome_ = nullptr, *trees3dCheck_ = nullptr, *terrain3dCheck_ = nullptr;
 QComboBox *sunSpeed_ = nullptr;
 QPushButton *sunPlayBtn_ = nullptr;
 QTimer sunAnimTimer_;
 QElapsedTimer sunAnimClock_;
 double sunAnimHour_ = 16.0;
 QLabel *sunReadout_ = nullptr, *analyticSummary_ = nullptr;
 QPlainTextEdit *notes_ = nullptr;
 QLabel *projectName_ = nullptr, *siteDetails_ = nullptr, *statsText_ = nullptr, *selectionText_ = nullptr, *statusText_ = nullptr, *cursorText_ = nullptr, *offlineText_ = nullptr;

 QMap<QString, QAction *> actions_;
 QVector<std::function<void()>> translations_;
 QVector<QLabel *> metricValues_;

 struct Document {
  QString id, path;
  ga::Project project;
  bool modified = false;
  quint64 dataRevision = 0, contentRevision = 0;
 };
 QVector<Document> documents_;
 QTabBar *tabs_ = nullptr;
 int activeDocument_ = -1;
 bool switching_ = false;
 bool navigating_ = false;

 QString trText(const char *vi, const char *en) const;
 void buildUi();
 void rebuildIcons();
 void setLanguage(bool english);
 void syncProject();
 void syncLayers();
 void filterLayers();
 void editLayer();
 void moveLayer(int delta);
 void applyDiagram(const QString &id);
 void applyAnalytic(const QString &kind);
 void populateDiagramList();
 void syncInspector(int index = -1);
 void applyStyle(const QString &id, bool mark = true);
 void markModified();
 void syncTitle();
 bool maybeSave();
 bool save(bool saveAs = false);
 void open();
 void newProject();
 void saveAutosave();
 void restoreAutosave();
 void exportFile(const QString &kind);
 void showSource();
 void showAnalysis();
 void showSection();
 void updateLiveSection();
 void showStyleGallery();
 void showExportStudio();
 struct UndoState {
  QString style, diagram;
  QVector<ga::Layer> layers;
  ga::PlotDesign plot;
  ga::MassingConfig massing;
 };
 QVector<UndoState> undoStack_, redoStack_;

 void pushUndoState();
 void undoAction();
 void redoAction();
 void applyHardwareProfile();
 void persistPreferences();
 void showCommandPalette();
 void applySolarLiveFromUi();
 void showSettings();
 void syncSolarUi();
 void applySolarFromUi();
 void syncPlotUi();
 void applyPlotFromUi(bool active);
 void syncMassingUi();
 void applyMassingFromUi();
 void choose3DModelFile();
 void syncModels3dUi();
 void applySelectedModel3dFromUi();
 void removeSelectedModel3d();
 void applyFuturePlanning(const QString &levelId);
 void refreshHanoiPlanningTiles();
 void syncPlanningUi();
 void syncLicenseUi();
 bool requireProFeature(const QString &viName, const QString &enName);
 void choosePlanningVectorFile();
 void choosePlanningImageFile();
 void showHanoiPlanningSources();
 void chooseSite(double lat, double lon, const QString &name = {});
 void syncMap();
 void showGeographicView();
 void showArchitecturalView(const ga::Camera &camera);
 void syncNavigationFromActive();
 void syncSelection();
 ga::Project captureProject() const;
 void snapshotDocument();
 void addDocument(ga::Project project, const QString &path = {});
 void activateDocument(int index);
 bool closeDocument(int index);
 void syncTabs();
 void chooseGeoJsonOverlay();
 void syncImports();
 void refreshProjectGeometry();
 QAction *action(const QString &id, const char *vi, const char *en, const QKeySequence &shortcut = {});
 QLabel *label(const char *vi, const char *en, const QString &name = {});
};
