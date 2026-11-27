#include "appearance.h"
#include "mainwindow.h"
#include "mapcanvas.h"
#include "massing.h"
#include "perfprofile.h"
#include "sectiondialog.h"
#include "viewport.h"
#include "windowchrome.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numeric>

int main(int argc, char **argv) {
 QSurfaceFormat surface;
 surface.setVersion(3, 3);
 surface.setProfile(QSurfaceFormat::CoreProfile);
 surface.setDepthBufferSize(24);
 surface.setSamples(4);
 surface.setSwapInterval(0);
 QSurfaceFormat::setDefaultFormat(surface);

 QApplication app(argc, argv);
 app.setApplicationName("GroundAtlasPerfBench");
 app.setApplicationVersion("2.2.0");
 if (argc != 3) return 2;

 const QString dataDir = QString::fromLocal8Bit(argv[1]);
 const QString outDir = QString::fromLocal8Bit(argv[2]);
 QDir().mkpath(outDir);

 ga::loadPresentationCatalog(dataDir + "/presentations.json");
 app.setStyleSheet(workspaceStyle(true));
 installApplicationChrome(app);

 QTemporaryDir tmp;
 if (!tmp.isValid()) return 3;

 MainWindow win(tmp.filePath("bench-data"), dataDir + "/hoankiem.gatlas");
 installWindowChrome(&win);
 win.setDarkTheme(true);
 win.resize(1440, 880);
 win.show();

 auto waitEvents = [&](int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) {
   app.processEvents(QEventLoop::AllEvents, 10);
  }
 };
 waitEvents(350);

 // Switch MainWindow explicitly to active 3D Viewport so paintGL() runs live on screen
 if (auto *act3d = win.findChild<QAction *>("3d")) {
  act3d->trigger();
 }
 waitEvents(200);

 // 1. Configure Ultra 3D ArchViz scene (4096px 16-tap Poisson PCF + GPU SSAO + 3D Trees + Fresnel Water + Proposed Massing)
 auto *vp = win.viewport();
 ga::PlotDesign plot;
 plot.active = true;
 plot.isPolygon = false;
 plot.centerX = -150;
 plot.centerY = 120;
 plot.width = 60;
 plot.length = 80;
 ga::MassingConfig massing;
 massing.enabled = true;
 massing.setbackM = 5.0;
 massing.podiumFloors = 3;
 massing.towerFloors = 14;
 massing.towerCoverage = 0.45;
 massing.towerRotationDeg = 15.0;
 vp->setPlot(plot);
 vp->setMassing(massing);

 auto cam = vp->camera();
 cam.mode = "3d";
 cam.preset("isometric");
 cam.zoom = 1.05;
 cam.solar.enabled = true;
 cam.solar.showPath = true;
 cam.solar.goldenHour = true;
 cam.solar.skyDome = true;
 cam.solar.trees3d = true;
 cam.solar.shadowStrength = 0.78;
 cam.solar.dayOfYear = 172;
 cam.solar.hour = 16.5;
 vp->setCamera(cam);
 // Disable basemap tile network waits during pure 3D GPU shader timing so we benchmark pure C++/OpenGL 3.3 frame time
 vp->setContextVisible(false);
 waitEvents(200);
 vp->grabFramebuffer(); // Warmup compile/FBO allocation
 auto syncGpu = [&]() {
  if (vp->context()) {
   vp->makeCurrent();
   vp->context()->functions()->glFinish();
   vp->doneCurrent();
  }
 };
 syncGpu();

 // Capture clean 1920x1080 ArchViz GPU showcase frame (3D Trees + Fresnel Water + Sky Dome + SSAO + Proposed Massing)
 vp->exportSun3dPng(outDir + "/native-v200-archviz-showcase.png", 1920, 1080, false);

 // 2. Benchmark 60 Continuous Real-Time Sun Scrubbing Frames (TRUE GPU glFinish()-synchronized timing + assert 0 mesh rebuilds!)
 const bool viewportVisible = vp->isVisible();
 const double buildsBefore = vp->basemapDiagnostics()["performance"].toObject()["mesh_builds"].toDouble();
 QVector<double> sunTimesMs;
 sunTimesMs.reserve(60);
 for (int i = 0; i < 60; ++i) {
  auto sol = vp->camera().solar;
  sol.hour = 6.5 + (11.0 * i / 59.0);
  QElapsedTimer ft;
  ft.start();
  vp->setSolarLive(sol);
  vp->repaint();
  syncGpu();
  sunTimesMs.append(ft.nsecsElapsed() / 1e6);
 }
 const double buildsAfter = vp->basemapDiagnostics()["performance"].toObject()["mesh_builds"].toDouble();

 // 3. Benchmark 60 Continuous 3D Orbit Frames (real-time yaw/elevation orbiting with 4096px shadows + SSAO + glFinish())
 QVector<double> orbitTimesMs;
 orbitTimesMs.reserve(60);
 for (int i = 0; i < 60; ++i) {
  auto c = vp->camera();
  c.yaw = -45.0 + i * 3.0;
  c.elevation = 32.0 + std::sin(i * 0.12) * 8.0;
  QElapsedTimer ft;
  ft.start();
  vp->setCamera(c);
  vp->repaint();
  syncGpu();
  orbitTimesMs.append(ft.nsecsElapsed() / 1e6);
 }

 auto statsOf = [](QVector<double> vals) {
  std::sort(vals.begin(), vals.end());
  const double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / std::max<qsizetype>(1, vals.size());
  const double p50 = vals[vals.size() / 2];
  const double p95 = vals[std::min<qsizetype>(vals.size() - 1, qsizetype(vals.size() * 0.95))];
  return QJsonObject{
   {"mean_ms", mean},
   {"p50_ms", p50},
   {"p95_ms", p95},
   {"effective_fps_mean", mean > 0.001 ? 1000.0 / mean : 999.0},
   {"effective_fps_p50", p50 > 0.001 ? 1000.0 / p50 : 999.0},
  };
 };

 const auto diag = vp->basemapDiagnostics();

 // 4. Visual Capture of MapCanvas 2.0 (Dark Glass HUD + 6 City Presets + Vector/Satellite Dock + Area Badge)
 vp->setContextVisible(true);
 if (auto *mapAct = win.findChild<QAction *>("map")) {
  mapAct->trigger();
  waitEvents(600);
  if (auto *mc = win.findChild<MapCanvas *>()) {
   for (int i = 0; i < 12; ++i) {
    mc->grabFramebuffer();
    if (mc->basemapDiagnostics()["fully_rendered"].toBool()) break;
    waitEvents(200);
   }
   QPixmap shot = win.grab();
   const QImage glFrame = mc->grabFramebuffer();
   const QPoint glPos = mc->mapTo(&win, QPoint(0, 0));
   QPainter p(&shot);
   p.drawImage(QRect(glPos, mc->size()), glFrame);
   p.end();
   shot.save(outDir + "/native-v200-mapcanvas-dark.png", "PNG");
  }
 }

 // 5. Visual Capture of Dark Mode SectionDialog (Proposed Massing + DEM + Dark Drafting Canvas)
 {
  const auto project = ga::Project::load(dataDir + "/hoankiem.gatlas");
  const QPointF sA(-440, 120), sB(440, 120);
  SectionDialog secDlg(project, ga::defaultLayers(), ga::paletteFor("suushka-study"), false, &win, true, &plot, &massing, &sA, &sB);
  secDlg.resize(1240, 760);
  secDlg.show();
  waitEvents(250);
  secDlg.grab().save(outDir + "/native-v200-section-dialog-dark.png", "PNG");
  secDlg.hide();
 }

 const auto sunStat = statsOf(sunTimesMs);
 const auto orbStat = statsOf(orbitTimesMs);
 const bool zeroRebuilds = (buildsAfter == buildsBefore);
 const bool ssaoOk = diag["ssao_enabled"].toBool();
 const bool shadowOk = diag["hardware_shadow_map"].toBool();
 const bool fastEnough = sunStat["p50_ms"].toDouble() < 16.0 && orbStat["p50_ms"].toDouble() < 16.0;
 const bool passed = viewportVisible && zeroRebuilds && ssaoOk && shadowOk && fastEnough && vp->graphicsError().isEmpty();

 QJsonObject review{
  {"passed", passed},
  {"viewport_visible", viewportVisible},
  {"gpu_glfinish_synced", true},
  {"renderer", vp->renderer()},
  {"target_fps_detected", diag["target_fps"].toInt()},
  {"shadow_map_size", diag["shadow_map_size"].toInt()},
  {"ssao_enabled", ssaoOk},
  {"hardware_shadow_map", shadowOk},
  {"zero_rebuild_sun_scrub", zeroRebuilds},
  {"mesh_builds_before", buildsBefore},
  {"mesh_builds_after", buildsAfter},
  {"sun_scrub_60_frames", sunStat},
  {"orbit_3d_60_frames", orbStat},
  {"triangles", vp->triangleCount()},
 };
 QFile rf(outDir + "/review.json");
 if (rf.open(QIODevice::WriteOnly)) {
  rf.write(QJsonDocument(review).toJson(QJsonDocument::Indented));
 }

 win.hide();
 return passed ? 0 : 1;
}
