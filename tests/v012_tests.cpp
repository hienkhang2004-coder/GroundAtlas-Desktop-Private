#include "analysis.h"
#include "appearance.h"
#include "dataaccess.h"
#include "dialogs_v2.h"
#include "exporter.h"
#include "mainwindow.h"
#include "massing.h"
#include "model3d.h"
#include "planning.h"
#include "planningtiles.h"
#include "license.h"
#include "licensedialog.h"
#include "dataaccess.h"
#include "mapcanvas.h"
#include "osmworkflow.h"
#include "sitegeometry.h"
#include <QSignalSpy>
#include <QPushButton>
#include <QAction>
#include <QScrollArea>
#include <QScrollBar>
#include "perfprofile.h"
#include "project.h"
#include "report.h"
#include "scene.h"
#include "section.h"
#include "sectiondialog.h"
#include "solar.h"
#include "urbananalytics.h"
#include "viewport.h"
#include "windowchrome.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QWheelEvent>
#include <QFileInfo>
#include <QLibrary>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef far
#undef near
#endif
#include <QListWidget>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QtEndian>
#include <cmath>

class NativeV012Tests : public QObject {
 Q_OBJECT
private slots:
 void initTestCase() {
  ga::loadPresentationCatalog(QString(GA_DATA) + "/presentations.json");
  qApp->setStyleSheet(workspaceStyle(false));
  installApplicationChrome(*qApp);
 }

 void solarGeometryForHanoi() {
  const auto summerNoon = ga::solarPosition(21.03, 172, 12.0);
  QVERIFY(summerNoon.aboveHorizon);
  QVERIFY(summerNoon.altitudeDeg > 84.0 && summerNoon.altitudeDeg <= 90.0);

  const auto winterNoon = ga::solarPosition(21.03, 355, 12.0);
  QVERIFY(winterNoon.aboveHorizon);
  QVERIFY(winterNoon.altitudeDeg > 42.0 && winterNoon.altitudeDeg < 49.0);
  QVERIFY(std::abs(winterNoon.azimuthDeg - 180.0) < 3.0);

  const auto morning = ga::solarPosition(21.03, 80, 9.0);
  const auto afternoon = ga::solarPosition(21.03, 80, 15.0);
  QVERIFY(morning.direction.x() > 0.3f && afternoon.direction.x() < -0.3f);
  QVERIFY(ga::solarArcPoints(21.03, 172, 500.0, 36).size() >= 15);
 }

 void allEightUrbanAnalyticsEngines() {
  const auto project = ga::Project::load(QString(GA_DATA) + "/hoankiem.gatlas");
  const QString envDir = QString(GA_DATA) + "/environment";
  const QString climFile = QString(GA_DATA) + "/climate.json";
  ga::SolarState solar;
  solar.enabled = true;
  solar.dayOfYear = 172;
  solar.hour = 16.25;

  for (const auto &kind : QStringList{"sunlight-hours", "solar-radiation", "svf", "utci", "traffic-noise", "city-block", "floor-area", "business",
                                      "env:gedtm30", "env:terrain", "env:slope", "env:flood", "env:landsat", "env:sentinel", "env:canopy", "env:population"}) {
   const auto ov = ga::computeAnalyticalOverlay(project, kind, solar, 135.0, {0, 0}, envDir, climFile);
   QVERIFY2(ov.valid(), qPrintable(kind));
   QVERIFY2(!ov.heatmap.isNull(), qPrintable(kind));
   QVERIFY2(ov.maxVal >= ov.minVal, qPrintable(kind));
  }
  const auto iso = ga::computeAnalyticalOverlay(project, "viewshed", solar, 135.0, {0, 0}, envDir, climFile);
  QVERIFY(iso.isovistPolygon.size() >= 72);

  const auto walk = ga::computeAnalyticalOverlay(project, "walkshed", solar, 135.0, {0, 0}, envDir, climFile);
  QVERIFY(walk.walkSegments.size() > 50);

  const auto cong = ga::computeAnalyticalOverlay(project, "congestion", solar, 135.0, {0, 0}, envDir, climFile);
  QVERIFY(cong.walkSegments.size() > 50);

  const auto wind = ga::computeAnalyticalOverlay(project, "wind-flow", solar, 135.0, {0, 0}, envDir, climFile);
  QVERIFY(wind.streamlines.size() >= 10);

  const auto rose = ga::computeAnalyticalOverlay(project, "wind-rose", solar, 135.0, {0, 0}, envDir, climFile);
  QCOMPARE(rose.windRose16.size(), 16);

  const auto sp2d = ga::computeAnalyticalOverlay(project, "sunpath-2d", solar, 135.0, {0, 0}, envDir, climFile);
  QVERIFY(sp2d.showSunpath2d);

  const auto sh2d = ga::computeAnalyticalOverlay(project, "shadow-2d", solar, 135.0, {0, 0}, envDir, climFile);
  QVERIFY(sh2d.shadowPolygons.size() > 200);

  // Verify freehand polygon PlotDesign & Ramer-Douglas-Peucker simplification
  ga::PlotDesign polyPlot;
  polyPlot.active = true;
  polyPlot.isPolygon = true;
  polyPlot.customRing = QPolygonF{{-60, -40}, {70, -50}, {95, 25}, {15, 75}, {-75, 45}};
  QVERIFY(polyPlot.areaM2() > 10000.0);
  QVERIFY(polyPlot.perimeterM() > 350.0);
  const auto roundTrip = ga::PlotDesign::fromJson(polyPlot.toJson());
  QVERIFY(roundTrip.isPolygon && roundTrip.customRing.size() == 5);
  QPolygonF dense;
  for (int i = 0; i <= 50; ++i) dense.append({double(i) * 2.0, std::sin(i * 0.2) * 0.1});
  QVERIFY(ga::simplifyPolyline(dense, 0.5).size() < 6);
 }

 void realShadowsInOpenGLMesh() {
  const auto project = ga::Project::load(QString(GA_DATA) + "/hoankiem.gatlas");
  const auto layers = ga::defaultLayers();
  const auto pal = ga::paletteFor("graphite");
  ga::Camera camOff;
  camOff.mode = "3d";
  const auto meshOff = ga::buildMesh(project, layers, pal, camOff);
  ga::Camera camOn = camOff;
  camOn.solar.enabled = true;
  camOn.solar.showPath = true;
  camOn.solar.dayOfYear = 172;
  camOn.solar.hour = 15.5;
  const auto meshOn = ga::buildMesh(project, layers, pal, camOn);
  QVERIFY(meshOn.transparent.size() > meshOff.transparent.size() + 300);
  QVERIFY(meshOn.lines.size() > meshOff.lines.size() + 100);
 }

 void sectionCutFlatAndDem() {
  const auto project = ga::Project::load(QString(GA_DATA) + "/hoankiem.gatlas");
  const auto flat = ga::computeSection(project, {-460, 0}, {460, 0}, ga::SectionMode::FlatUrban, nullptr, 120.0);
  QVERIFY(!flat.buildings.isEmpty() && !flat.waters.isEmpty() && !flat.roads.isEmpty());
  const auto fields = ga::environmentFields(QString(GA_DATA) + "/environment", project.site);
  ga::TerrainGrid dem;
  for (const auto &f : fields) if (f["id"].toString() == "gedtm30") { dem = ga::TerrainGrid::fromEnvironmentField(f); break; }
  QVERIFY(dem.valid);
  const auto terrain = ga::computeSection(project, {-460, 0}, {460, 0}, ga::SectionMode::TerrainDem, &dem, 120.0);
  QVERIFY(terrain.hasDem && terrain.maxGround > 5.0);
 }

 void exportersLaserReportAndV2WorkstationUi() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto project = ga::Project::load(QString(GA_DATA) + "/hoankiem.gatlas");
  const auto layers = ga::defaultLayers();
  const auto pal = ga::paletteFor("graphite");

  // 1. CAD & 3D Exporters
  QVERIFY(ga::exportDxf(project, layers, pal, "context", tmp.filePath("hk.dxf")));
  QVERIFY(ga::exportColladaDae(project, layers, pal, "context", tmp.filePath("hk.dae")));
  const QString glbPath = tmp.filePath("hk.glb");
  QVERIFY(ga::exportGlb(project, layers, pal, "context", glbPath));
  QVERIFY(ga::exportStl(project, layers, pal, "context", tmp.filePath("hk.stl")));

  // 2. Laser Cut DXF + SVG
  ga::LaserCutOptions lo;
  QVERIFY(ga::exportLaserCutDxf(project, lo, tmp.filePath("hk_laser.dxf")));
  QVERIFY(ga::exportLaserCutSvg(project, lo, tmp.filePath("hk_laser.svg")));
  QVERIFY(QFileInfo(tmp.filePath("hk_laser.dxf")).size() > 2000);
  QVERIFY(QFileInfo(tmp.filePath("hk_laser.svg")).size() > 2000);

  // 3. 3-Page A3 Architectural Site Report PDF
  const QString reportPdf = tmp.filePath("hk_report.pdf");
  QVERIFY(ga::exportSiteReportPdf(project, layers, pal, QString(GA_DATA) + "/climate.json", QString(GA_DATA) + "/environment", false, reportPdf));
  QVERIFY(QFileInfo(reportPdf).size() > 20000);

  // 4. Native GroundAtlas v2 MainWindow Clone (Light + Dark + Sun 3D + Design Plot) visual capture
  const QString shotDir = QDir(QString(GA_DATA) + "/../validation").absolutePath();
  QDir().mkpath(shotDir);
  MainWindow win(tmp.filePath("mw-data"), QString(GA_DATA) + "/hoankiem.gatlas");
  installWindowChrome(&win);
  win.resize(1500, 920);
  win.show();
  QTest::qWait(450);

  auto captureWindow = [&](const QString &filePath) {
   for (int i = 0; i < 18; ++i) {
    win.viewport()->grabFramebuffer();
    if (win.viewport()->basemapDiagnostics()["fully_rendered"].toBool()) break;
    QTest::qWait(250);
   }
   QTest::qWait(250);
   QPixmap shot = win.grab();
   const QImage glFrame = win.viewport()->grabFramebuffer();
   const QPoint glPos = win.viewport()->mapTo(&win, QPoint(0, 0));
   QPainter p(&shot);
   p.drawImage(QRect(glPos, win.viewport()->size()), glFrame);
   p.end();
   return shot.save(filePath, "PNG");
  };

  // Verify 1.3.0 hardware auto-profiler (Low / Medium / Ultra / up to 500 Hz) & Overpass primary mirror
  {
   ga::HardwareInfo highHw{"NVIDIA GeForce RTX 3060", 16, 32, 180, ga::PerfTier::Ultra};
   const auto highCfg = ga::resolveGraphicsConfig(ga::PerfTier::Auto, highHw);
   QCOMPARE(highCfg.shadowMapSize, 4096);
   QCOMPARE(highCfg.pcfQuality, 2);
   QCOMPARE(highCfg.targetFps, 180);

   ga::HardwareInfo hz500{"NVIDIA GeForce RTX 4090", 24, 64, 500, ga::PerfTier::Ultra};
   QCOMPARE(ga::resolveGraphicsConfig(ga::PerfTier::Auto, hz500).targetFps, 500);

   ga::HardwareInfo lowHw{"Intel HD Graphics 4000", 4, 4, 60, ga::PerfTier::Low};
   const auto lowCfg = ga::resolveGraphicsConfig(ga::PerfTier::Auto, lowHw);
   QCOMPARE(lowCfg.shadowMapSize, 1024);
   QCOMPARE(lowCfg.pcfQuality, 0);
   QCOMPARE(lowCfg.targetFps, 60);

   DataAccess::Options opts;
   QVERIFY(opts.endpoints.size() >= 5);
   QVERIFY(opts.endpoints.first().contains("api.openstreetmap.org/api/0.6/map.json"));
  }

  // Verify 2.3.0 diagnostics, Free -> Pro Per-Seat Ed25519 Activation, Suushka · Study default style & Amenities unchecked
  QCOMPARE(win.diagnostics()["version"].toString(), QString("2.3.1"));
  QVERIFY(win.licenseManager() != nullptr);
  QVERIFY(!win.licenseManager()->isPro());
  {
   // Capture Free-mode License Dialog with Early-Bird slot bar & 1/3/5/10 Seat packages
   LicenseDialog licDlg(win.licenseManager(), false, false, QStringLiteral("Xuất Bộ hồ sơ 8 tệp (CAD / SketchUp / 4K / PDF)"), &win);
   licDlg.show();
   QTest::qWait(220);
   QVERIFY(licDlg.grab().save(shotDir + "/native-v230-license-dialog.png", "PNG"));
   licDlg.close();

   // Verify real Ed25519 Per-Seat License activation switches topbar pill cleanly from Free -> Pro!
   QByteArray testSeed(32, 'G');
   QByteArray testPub;
   const qint64 nowUtc = QDateTime::currentSecsSinceEpoch();
   const QJsonObject payload{
    {"v", 1}, {"key", "GA2X-V230-****-PRO1"}, {"tier", "pro_2x"}, {"major", 2},
    {"seats", 1}, {"used", 1}, {"machine", win.licenseManager()->machineHash()},
    {"email", "kts@groundatlas.vn"}, {"issued", double(nowUtc)}, {"exp", double(nowUtc + 14 * 86400)}
   };
   const QString tok = ga::signLeaseTokenWithSeed(payload, testSeed, &testPub);
   win.licenseManager()->setPublicKey(testPub);
   QString licErr;
   QVERIFY2(win.licenseManager()->installSignedLease(tok, &licErr), qPrintable(licErr));
   QVERIFY(win.licenseManager()->isPro());
   QCOMPARE(win.diagnostics()["license"].toObject()["pro"].toBool(), true);
  }
  auto *stylesBox = win.findChild<QComboBox *>("styles");
  QVERIFY(stylesBox && stylesBox->currentData().toString() == "suushka-study");
  auto *layerTree = win.findChild<QTreeWidget *>("layers");
  QVERIFY(layerTree);
  bool foundAmenities = false;
  for (int i = 0; i < layerTree->topLevelItemCount(); ++i) {
   auto *it = layerTree->topLevelItem(i);
   if (it->data(0, Qt::UserRole).toString() == "amenities") {
    foundAmenities = true;
    QCOMPARE(it->checkState(0), Qt::Unchecked);
   }
  }
  QVERIFY(foundAmenities);

  auto *diagList = win.findChild<QListWidget *>("diagram-list");
  QVERIFY(diagList);
  QVERIFY2(diagList->count() >= 60, qPrintable(QString("Expected >=60 diagrams, got %1").arg(diagList->count())));

  // Verify 60–500 Hz smooth camera animation & Ruler tool APIs
  win.viewport()->animateZoomBy(1.15, 80);
  QTRY_VERIFY_WITH_TIMEOUT(!win.viewport()->isCameraAnimating(), 2000);
  win.viewport()->animateAlignNorth(80);
  QTRY_VERIFY_WITH_TIMEOUT(!win.viewport()->isCameraAnimating(), 2000);
  QVERIFY(std::abs(win.viewport()->camera().yaw) < 0.5);
  win.viewport()->setMeasuring(true);
  QVERIFY(win.viewport()->isMeasuring());
  win.viewport()->setMeasuring(false);

  // Capture Light Mode v1.3 (Suushka · Study default, NO orange amenity clutter + Freehand Plot + Ruler Dock!)
  ga::PlotDesign freePlot;
  freePlot.active = true;
  freePlot.isPolygon = true;
  freePlot.customRing = QPolygonF{{-70, -45}, {65, -60}, {95, 20}, {35, 85}, {-55, 65}};
  win.viewport()->setPlot(freePlot);
  auto cam = win.viewport()->camera();
  cam.mode = "2d";
  cam.zoom = 0.92;
  cam.panX = 0;
  cam.panY = 0;
  win.viewport()->setCamera(cam);
  QVERIFY(captureWindow(shotDir + "/native-v130-light.png"));

  // Verify 4K Transparent PNG Export (2D mode + 3D GPU mode)
  const QString png2d = tmp.filePath("render_2d_alpha.png");
  QVERIFY(win.viewport()->exportHighResPng(png2d, 1920, 1080, true));
  QVERIFY(QFileInfo(png2d).size() > 10000);

  // Capture Dark Mode + Auto-Dark Basemap + 4096px 16-tap Poisson PCF + Zero-Rebuild Continuous Sun Scrubbing!
  win.setDarkTheme(true);
  QVERIFY(win.viewport()->darkUi());
  cam.mode = "3d";
  cam.preset("isometric");
  cam.zoom = 1.02;
  cam.solar.enabled = true;
  cam.solar.showPath = true;
  cam.solar.goldenHour = true;
  cam.solar.shadowStrength = 0.78;
  cam.solar.dayOfYear = 172;
  cam.solar.hour = 16.5;
  win.viewport()->setCamera(cam);
  win.selectStudioTab(7);
  QTest::qWait(300);

  // Verify ZERO city mesh rebuilds while scrubbing Sun Hour smoothly across multiple angles!
  win.viewport()->grabFramebuffer();
  const double buildsBefore = win.viewport()->basemapDiagnostics()["performance"].toObject()["mesh_builds"].toDouble();
  for (double h : {14.25, 15.10, 15.75, 16.50}) {
   auto s = win.viewport()->camera().solar;
   s.hour = h;
   win.viewport()->setSolarLive(s);
   win.viewport()->grabFramebuffer();
  }
  const double buildsAfter = win.viewport()->basemapDiagnostics()["performance"].toObject()["mesh_builds"].toDouble();
  QCOMPARE(buildsAfter, buildsBefore);

  QVERIFY(captureWindow(shotDir + "/native-v130-dark-sun3d.png"));
  QVERIFY(win.viewport()->basemapDiagnostics()["hardware_shadow_map"].toBool());
  QVERIFY(win.viewport()->basemapDiagnostics()["archviz_v2"].toBool());

  // Verify Concept Massing Studio (inset polygon + FAR/BCR/GFA + 3D mesh + real shadow casting!)
  {
   ga::PlotDesign rectPlot;
   rectPlot.active = true;
   rectPlot.isPolygon = false;
   rectPlot.centerX = -150;
   rectPlot.centerY = 120;
   rectPlot.width = 60;
   rectPlot.length = 80;
   ga::MassingConfig mc;
   mc.enabled = true;
   mc.setbackM = 5.0;
   mc.podiumFloors = 3;
   mc.podiumFloorHeight = 4.2;
   mc.towerFloors = 14;
   mc.towerFloorHeight = 3.4;
   mc.towerCoverage = 0.45;
   mc.towerRotationDeg = 15.0;
   const auto mm = ga::evaluateMassing(rectPlot, mc);
   QVERIFY(mm.valid);
   QVERIFY(std::abs(mm.siteAreaM2 - 4800.0) < 1.0);
   QVERIFY(std::abs(mm.podiumFootprintM2 - 3500.0) < 20.0); // 50x70m inset
   QVERIFY(mm.bcrPercent > 70.0 && mm.bcrPercent < 76.0);
   QVERIFY(mm.far > 6.0 && mm.far < 7.5);
   QCOMPARE(mm.totalFloors, 17);
   const auto mcRT = ga::MassingConfig::fromJson(mc.toJson());
   QCOMPARE(mcRT.podiumFloors, 3);
   QCOMPARE(mcRT.towerFloors, 14);
   QVERIFY(std::abs(mcRT.setbackM - 5.0) < 1e-6);

   // Render Proposed Massing on real GPU casting 4096px shadow onto Hoàn Kiếm neighborhood!
   win.viewport()->setPlot(rectPlot);
   win.viewport()->setMassing(mc);
   QTest::qWait(250);
   QVERIFY(captureWindow(shotDir + "/native-v200-massing-sun3d.png"));

   // Verify Interactive Live Section A-A' directly on Viewport cutting THROUGH Proposed Massing + Dark Mode!
   win.viewport()->setSectionCutLine({-440, 120}, {440, 120});
   win.viewport()->setSectionCutActive(true);
   QVERIFY(win.viewport()->isSectionCutActive());
   const auto secWithMassing = ga::computeSection(project, {-440, 120}, {440, 120}, ga::SectionMode::FlatUrban, nullptr, 120.0, &rectPlot, &mc);
   bool sawProposed = false;
   for (const auto &b : secWithMassing.buildings) if (b.kind == "proposed") sawProposed = true;
   QVERIFY(sawProposed);
   QTest::qWait(250);
   QVERIFY(captureWindow(shotDir + "/native-v200-live-section-dark.png"));

   // Verify 1-Click 8-File Presentation Kit (4K Plan + 4K Sun 3D + Section SVG/DXF + CAD DXF 3DFACE + SketchUp DAE + Report PDF + Sun GIF)
   const QString kitDir = tmp.filePath("kit-out");
   QVERIFY(win.exportProjectKit(kitDir));
   for (const QString &fname : QStringList{
     "01_Plan_Suushka_4K.png", "02_Sun3D_GoldenHour_4K.png", "03_Section_AA.svg", "04_Section_AA.dxf",
     "05_Site_Model_CAD.dxf", "06_Site_Model_SketchUp.dae", "07_Site_Report_A3.pdf", "08_Sun_Timelapse.gif"
   }) {
    const QFileInfo fi(kitDir + "/" + fname);
    QVERIFY2(fi.exists() && fi.size() > 2000, qPrintable("Missing/empty kit file: " + fname));
   }
   {
    QFile dxfF(kitDir + "/05_Site_Model_CAD.dxf");
    QVERIFY(dxfF.open(QIODevice::ReadOnly));
    const QByteArray dxfBytes = dxfF.readAll();
    QVERIFY(dxfBytes.contains("GA_PLOT"));
    QVERIFY(dxfBytes.contains("GA_PROPOSED"));
    QVERIFY(dxfBytes.contains("3DFACE"));
   }
   {
    QFile gifF(kitDir + "/08_Sun_Timelapse.gif");
    QVERIFY(gifF.open(QIODevice::ReadOnly));
    QVERIFY(gifF.read(6) == "GIF89a");
   }
   // Verify 360° Orbital Turntable GIF export
   const QString turnGif = tmp.filePath("turntable.gif");
   QVERIFY(win.viewport()->exportTurntableGif(turnGif, 480, 300, 8, 8));
   QVERIFY(QFileInfo(turnGif).size() > 10000);

   win.viewport()->setSectionCutActive(false);

   // ── Verify Native 3D Model Import Studio (.obj/.mtl, SketchUp .dae in inches, .glb, .stl, AND real Trimble .skp!) ──
   {
    // 1. Wavefront OBJ + MTL import
    const QString mtlPath = tmp.filePath("pavilion.mtl");
    {
     QFile mf(mtlPath);
     QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
     mf.write("newmtl Terracotta\nKd 0.82 0.36 0.22\nd 1.0\nnewmtl Glass\nKd 0.45 0.72 0.85\nd 0.45\n");
    }
    const QString objPath = tmp.filePath("pavilion.obj");
    {
     QFile of(objPath);
     QVERIFY(of.open(QIODevice::WriteOnly | QIODevice::Text));
     of.write("# Architectural Pavilion\nmtllib pavilion.mtl\nusemtl Terracotta\n"
              "v 0 0 0\nv 40 0 0\nv 40 30 0\nv 0 30 0\n"
              "v 0 0 28\nv 40 0 28\nv 40 30 28\nv 0 30 28\n"
              "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n");
    }
    QString errObj;
    const auto mObj = ga::load3DModelFile(objPath, &errObj);
    QVERIFY2(mObj.valid(), qPrintable(errObj));
    QCOMPARE(mObj.triangleCount(), 12);
    QVERIFY(std::abs(mObj.rawSizeM.x() - 40.0f) < 0.05f);
    QVERIFY(std::abs(mObj.rawSizeM.y() - 30.0f) < 0.05f);
    QVERIFY(std::abs(mObj.rawSizeM.z() - 28.0f) < 0.05f);

    // 2. SketchUp COLLADA (.dae) in inches (meter="0.0254") with <library_nodes> + <instance_node>!
    const QString daePath = tmp.filePath("sketchup_villa.dae");
    {
     QFile df(daePath);
     QVERIFY(df.open(QIODevice::WriteOnly | QIODevice::Text));
     // 1000 x 800 x 1200 inches => 25.4 x 20.32 x 30.48 metres!
     df.write(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
      " <asset><unit name=\"inch\" meter=\"0.0254\"/><up_axis>Z_UP</up_axis></asset>\n"
      " <library_effects><effect id=\"fx1\"><profile_COMMON><technique sid=\"common\"><lambert><diffuse><color>0.85 0.42 0.24 1</color></diffuse></lambert></technique></profile_COMMON></effect></library_effects>\n"
      " <library_materials><material id=\"mat1\"><instance_effect url=\"#fx1\"/></material></library_materials>\n"
      " <library_geometries><geometry id=\"g1\"><mesh>\n"
      "  <source id=\"g1p\"><float_array id=\"g1pa\" count=\"24\">0 0 0 1000 0 0 1000 800 0 0 800 0 0 0 1200 1000 0 1200 1000 800 1200 0 800 1200</float_array></source>\n"
      "  <vertices id=\"g1v\"><input semantic=\"POSITION\" source=\"#g1p\"/></vertices>\n"
      "  <polylist material=\"M1\" count=\"6\"><input semantic=\"VERTEX\" source=\"#g1v\" offset=\"0\"/><vcount>4 4 4 4 4 4</vcount><p>0 3 2 1 4 5 6 7 0 1 5 4 1 2 6 5 2 3 7 6 3 0 4 7</p></polylist>\n"
      " </mesh></geometry></library_geometries>\n"
      " <library_nodes><node id=\"comp_tower\"><instance_geometry url=\"#g1\"><bind_material><technique_common><instance_material symbol=\"M1\" target=\"#mat1\"/></technique_common></bind_material></instance_geometry></node></library_nodes>\n"
      " <library_visual_scenes><visual_scene id=\"S\"><node id=\"root\"><matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix><instance_node url=\"#comp_tower\"/></node></visual_scene></library_visual_scenes>\n"
      "</COLLADA>\n");
    }
    QString errDae;
    const auto mDae = ga::load3DModelFile(daePath, &errDae);
    QVERIFY2(mDae.valid(), qPrintable(errDae));
    QCOMPARE(mDae.triangleCount(), 12);
    QVERIFY(std::abs(mDae.rawSizeM.x() - 25.4f) < 0.05f);
    QVERIFY(std::abs(mDae.rawSizeM.y() - 20.32f) < 0.05f);
    QVERIFY(std::abs(mDae.rawSizeM.z() - 30.48f) < 0.05f);

    // 3. Round-trip GLB + Binary STL import
    const QString rtGlb = tmp.filePath("roundtrip.glb");
    QVERIFY(ga::exportGlb(project, layers, pal, "context", rtGlb));
    QString errGlb;
    const auto mGlb = ga::load3DModelFile(rtGlb, &errGlb);
    QVERIFY2(mGlb.valid() && mGlb.triangleCount() > 100, qPrintable(errGlb));

    const QString rtStl = tmp.filePath("roundtrip.stl");
    QVERIFY(ga::exportStl(project, layers, pal, "context", rtStl));
    QString errStl;
    const auto mStl = ga::load3DModelFile(rtStl, &errStl);
    QVERIFY2(mStl.valid() && mStl.triangleCount() > 100, qPrintable(errStl));

    // 4. Real Trimble SketchUp (.skp) end-to-end test via locally installed SketchUpAPI.dll (if present!)
    QString showcaseModelPath = daePath;
#ifdef Q_OS_WIN
    if (ga::sketchUpDirectImportAvailable()) {
     const QString skpPath = tmp.filePath("sketchup_tower_2023.skp");
     const std::wstring wDll = ga::findSketchUpApiDll().toStdWString();
     HMODULE suMod = LoadLibraryExW(wDll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
     QVERIFY2(suMod != nullptr, "Failed to LoadLibraryExW SketchUpAPI.dll");
     auto suSym = [&](const char *name) -> void * { return reinterpret_cast<void *>(GetProcAddress(suMod, name)); };
     struct SUModelRef { void *ptr = nullptr; };
     struct SUEntitiesRef { void *ptr = nullptr; };
     struct SUFaceRef { void *ptr = nullptr; };
     struct SUMaterialRef { void *ptr = nullptr; };
     struct SUPoint3D { double x, y, z; };
     struct SUColor { uint8_t r, g, b, a; };
     auto fnInit = reinterpret_cast<void (*)()>(suSym("SUInitialize"));
     auto fnCreate = reinterpret_cast<int (*)(SUModelRef *)>(suSym("SUModelCreate"));
     auto fnEnts = reinterpret_cast<int (*)(SUModelRef, SUEntitiesRef *)>(suSym("SUModelGetEntities"));
     auto fnFace = reinterpret_cast<int (*)(SUFaceRef *, const SUPoint3D *, size_t)>(suSym("SUFaceCreateSimple"));
     auto fnAddFaces = reinterpret_cast<int (*)(SUEntitiesRef, size_t, const SUFaceRef *)>(suSym("SUEntitiesAddFaces"));
     auto fnMatCreate = reinterpret_cast<int (*)(SUMaterialRef *)>(suSym("SUMaterialCreate"));
     auto fnMatColor = reinterpret_cast<int (*)(SUMaterialRef, const SUColor *)>(suSym("SUMaterialSetColor"));
     auto fnFaceMat = reinterpret_cast<int (*)(SUFaceRef, SUMaterialRef)>(suSym("SUFaceSetFrontMaterial"));
     auto fnSave = reinterpret_cast<int (*)(SUModelRef, const char *)>(suSym("SUModelSaveToFile"));
     auto fnRel = reinterpret_cast<int (*)(SUModelRef *)>(suSym("SUModelRelease"));
     if (fnInit && fnCreate && fnEnts && fnFace && fnAddFaces && fnSave && fnRel) {
      fnInit();
      SUModelRef mdl{};
      QCOMPARE(fnCreate(&mdl), 0);
      SUEntitiesRef ents{};
      QCOMPARE(fnEnts(mdl, &ents), 0);
      SUMaterialRef matTerra{}, matGlass{};
      if (fnMatCreate && fnMatColor) {
       fnMatCreate(&matTerra);
       SUColor cT{212, 98, 56, 255};
       fnMatColor(matTerra, &cT);
       fnMatCreate(&matGlass);
       SUColor cG{86, 152, 188, 255};
       fnMatColor(matGlass, &cG);
      }
      // Build a real stepped 34x26x52 m SketchUp architectural tower (converted to inches: / 0.0254)
      auto addBoxIn = [&](double x0, double y0, double z0, double x1, double y1, double z1, SUMaterialRef mat) {
       const double k = 1.0 / 0.0254;
       const double ax = x0 * k, ay = y0 * k, az = z0 * k, bx = x1 * k, by = y1 * k, bz = z1 * k;
       const SUPoint3D q[6][4] = {
        {{ax, ay, az}, {ax, by, az}, {bx, by, az}, {bx, ay, az}},
        {{ax, ay, bz}, {bx, ay, bz}, {bx, by, bz}, {ax, by, bz}},
        {{ax, ay, az}, {bx, ay, az}, {bx, ay, bz}, {ax, ay, bz}},
        {{bx, ay, az}, {bx, by, az}, {bx, by, bz}, {bx, ay, bz}},
        {{bx, by, az}, {ax, by, az}, {ax, by, bz}, {bx, by, bz}},
        {{ax, by, az}, {ax, ay, az}, {ax, ay, bz}, {ax, by, bz}},
       };
       SUFaceRef faces[6]{};
       for (int fi = 0; fi < 6; ++fi) {
        QCOMPARE(fnFace(&faces[fi], q[fi], 4), 0);
        if (fnFaceMat && mat.ptr) fnFaceMat(faces[fi], mat);
       }
       QCOMPARE(fnAddFaces(ents, 6, faces), 0);
      };
      addBoxIn(0, 0, 0, 34, 26, 14, matTerra);      // Podium 34x26x14 m
      addBoxIn(6, 4, 14, 28, 22, 48, matGlass);     // Glass tower 22x18x34 m (total 48 m)
      addBoxIn(10, 7, 48, 24, 19, 52, matTerra);    // Crown roof 52 m
      const QByteArray pUtf8 = QDir::toNativeSeparators(skpPath).toUtf8();
      QCOMPARE(fnSave(mdl, pUtf8.constData()), 0);
      fnRel(&mdl);

      QString errSkp;
      const auto mSkp = ga::load3DModelFile(skpPath, &errSkp);
      QVERIFY2(mSkp.valid(), qPrintable(errSkp));
      QVERIFY(mSkp.triangleCount() >= 36);
      QVERIFY(std::abs(mSkp.rawSizeM.x() - 34.0f) < 0.1f);
      QVERIFY(std::abs(mSkp.rawSizeM.y() - 26.0f) < 0.1f);
      QVERIFY(std::abs(mSkp.rawSizeM.z() - 52.0f) < 0.1f);
      showcaseModelPath = skpPath;
     }
    }
#endif

    // Turn off parametric massing & import the real SketchUp model straight onto our Design Plot!
    ga::MassingConfig offMass;
    offMass.enabled = false;
    win.viewport()->setMassing(offMass);
    QVERIFY(win.import3DModel(showcaseModelPath));
    QCOMPARE(win.diagnostics()["models_3d"].toInt(), 1);

    // Verify Live Section A-A' slices through the imported SketchUp 3D model!
    win.viewport()->setSectionCutLine({-440, 120}, {440, 120});
    win.viewport()->setSectionCutActive(true);
    const auto &importedList = win.viewport()->models3d();
    const auto sec3d = ga::computeSection(project, {-440, 120}, {440, 120}, ga::SectionMode::FlatUrban, nullptr, 120.0, nullptr, nullptr, &importedList);
    bool sawImported3d = false;
    for (const auto &b : sec3d.buildings) if (b.subkind == "imported3d") sawImported3d = true;
    QVERIFY(sawImported3d);

    // Verify CAD DXF export includes GA_IMPORTED_3D 3DFACE geometry
    const QString dxf3d = tmp.filePath("with_sketchup.dxf");
    QVERIFY(ga::exportDxf(project, layers, pal, "context", dxf3d, nullptr, nullptr, nullptr, &importedList));
    {
     QFile f(dxf3d);
     QVERIFY(f.open(QIODevice::ReadOnly));
     const QByteArray bytes = f.readAll();
     QVERIFY(bytes.contains("GA_IMPORTED_3D"));
     QVERIFY(bytes.contains("3DFACE"));
    }

    // Real RTX 3060 GPU visual capture of imported SketchUp model casting Golden Hour 4096px shadows + Live Section!
    QTest::qWait(300);
    QVERIFY(captureWindow(shotDir + "/native-v210-sketchup-import-section.png"));
    win.viewport()->setSectionCutActive(false);
    QTest::qWait(200);
    QVERIFY(win.viewport()->exportSun3dPng(shotDir + "/native-v210-sketchup-showcase-1080p.png", 1920, 1080, false));
   }

   // ── Verify Hanoi Future Planning Sync (VN-2000 <-> WGS84, QHC 2045 Metro, TT 04/2022 Zoning, 3D Height Envelopes & DXF Import!) ──
   {
    // 1. Exact VN-2000 Hanoi 3-deg (CM 105E) <-> WGS84 round-trip precision (< 0.00001 deg)
    const QPointF vn = ga::wgs84ToVn2000(105.8525, 21.0288, 105.0, 0.9999);
    QVERIFY(vn.x() > 580000.0 && vn.x() < 598000.0);
    QVERIFY(vn.y() > 2320000.0 && vn.y() < 2335000.0);
    const QPointF ll = ga::vn2000ToWgs84(vn.x(), vn.y(), 105.0, 0.9999);
    QVERIFY(std::abs(ll.x() - 105.8525) < 0.00001);
    QVERIFY(std::abs(ll.y() - 21.0288) < 0.00001);

    // 2. Subdivision auto-detection (Hoan Kiem H1-1B + Red River R1-R5)
    const QString planDir = QStringLiteral(GA_DATA) + "/planning";
    const auto subHK = ga::detectHanoiSubdivision(21.0288, 105.8525, planDir);
    QVERIFY(subHK.matched);
    QCOMPARE(subHK.code, QString("H1-1B"));
    const auto subRR = ga::detectHanoiSubdivision(21.0350, 105.8750, planDir);
    QVERIFY(subRR.matched);
    QCOMPARE(subRR.code, QString("R1-R5"));

    // 3. Import a real VN-2000 Hanoi AutoCAD DXF planning parcel (HH-01 45m mixed-use lot)
    const QString dxfPlanPath = tmp.filePath("hanoi_vn2000_qhpk.dxf");
    {
     const QPointF c0 = ga::wgs84ToVn2000(105.8502, 21.0298, 105.0, 0.9999);
     const QPointF c1 = ga::wgs84ToVn2000(105.8511, 21.0298, 105.0, 0.9999);
     const QPointF c2 = ga::wgs84ToVn2000(105.8511, 21.0306, 105.0, 0.9999);
     const QPointF c3 = ga::wgs84ToVn2000(105.8502, 21.0306, 105.0, 0.9999);
     const QPointF mid = (c0 + c2) * 0.5;
     QFile df(dxfPlanPath);
     QVERIFY(df.open(QIODevice::WriteOnly | QIODevice::Text));
     QTextStream ds(&df);
     ds.setRealNumberPrecision(10);
     ds << "0\nSECTION\n2\nENTITIES\n"
        << "0\nLWPOLYLINE\n8\nQH_HH\n70\n1\n"
        << "10\n" << c0.x() << "\n20\n" << c0.y() << "\n"
        << "10\n" << c1.x() << "\n20\n" << c1.y() << "\n"
        << "10\n" << c2.x() << "\n20\n" << c2.y() << "\n"
        << "10\n" << c3.x() << "\n20\n" << c3.y() << "\n"
        << "0\nTEXT\n8\nQH_TEXT\n10\n" << mid.x() << "\n20\n" << mid.y() << "\n1\nHH-01 45m\n"
        << "0\nENDSEC\n0\nEOF\n";
    }
    QVERIFY(win.importPlanningVector(dxfPlanPath, ga::PlanningCrs::Auto));
    QCOMPARE(win.diagnostics()["planning_custom_zones"].toInt(), 1);

    // 4. Verify QHC 2045 Metro & Corridors (Station C9 Hoan Kiem!) + GPU Visual Capture
    const auto ovQhc = ga::computeFuturePlanningOverlay(project, "qhc-2045", planDir);
    QVERIFY(ovQhc.valid());
    QVERIFY(!ovQhc.corridors.isEmpty());
    bool sawC9 = false;
    for (const auto &st : ovQhc.stations) if (st.code == "C9") sawC9 = true;
    QVERIFY(sawC9);

    win.viewport()->setFuturePlanningOverlay(ovQhc);
    QTest::qWait(250);
    QVERIFY(captureWindow(shotDir + "/native-v220-hanoi-qhc2045-metro.png"));

    // 5. Verify QHCT 1/500 Translucent 3D Statutory Height Control Envelopes + GPU Visual Capture
    const auto customParsed = ga::importPlanningVectorFile(dxfPlanPath, project.site, ga::PlanningCrs::Auto);
    const auto ovQhct = ga::computeFuturePlanningOverlay(project, "qhct-500", planDir, &customParsed, &rectPlot, 0.85);
    QVERIFY(ovQhct.valid() && ovQhct.show3DEnvelopes && !ovQhct.zones.isEmpty());
    win.viewport()->setFuturePlanningOverlay(ovQhct);
    QTest::qWait(250);
    QVERIFY(captureWindow(shotDir + "/native-v220-hanoi-qhct500-3d-envelopes.png"));

    // 6. Verify Existing vs. Future Diff mode
    const auto ovCmp = ga::computeFuturePlanningOverlay(project, "compare", planDir, &customParsed, &rectPlot, 0.82);
    QVERIFY(ovCmp.valid() && !ovCmp.diffBuildings.isEmpty());
    win.viewport()->setFuturePlanningOverlay({});
   }

   // ── Verify v2.2.1: LIVE non-Hoan-Kiem OSM Site Load (Tay Ho Tay / Ngoai Giao Doan) + Live quyhoach.hanoi.vn Tile Overlay! ──
   {
    win.setDarkTheme(false);
    ga::Site tayHoTay;
    tayHoTay.name = QStringLiteral("Tây Hồ Tây — Ngoại Giao Đoàn");
    tayHoTay.lat = 21.0645;
    tayHoTay.lon = 105.7935;
    tayHoTay.radius = 550.0;
    tayHoTay.shape = QStringLiteral("circle");
    tayHoTay.boundary = ga::primitiveBoundary(tayHoTay);

    DataAccess liveData(tmp.filePath("live-osm-cache"));
    QSignalSpy loadedSpy(&liveData, &DataAccess::loaded);
    QSignalSpy failedSpy(&liveData, &DataAccess::failed);
    liveData.load(tayHoTay, QStringLiteral("all"), true);
    QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.size() > 0 || failedSpy.size() > 0, 20000);
    QVERIFY2(loadedSpy.size() == 1, failedSpy.isEmpty() ? "Live OSM load timed out" : qPrintable(failedSpy.last()[0].toString()));
    auto loadedProj = qvariant_cast<ga::Project>(loadedSpy.last()[0]);
    QVERIFY2(loadedProj.stats().buildings >= 10 && loadedProj.stats().features >= 40,
             qPrintable(QString("Expected >=10 buildings & >=40 features in Tay Ho Tay, got %1 bldgs / %2 feats")
              .arg(loadedProj.stats().buildings).arg(loadedProj.stats().features)));
    ga::applySmartHeightEstimates(loadedProj);
    loadedProj.style = QStringLiteral("suushka-study");
    loadedProj.layerState["amenities"] = QJsonObject{{"id", "amenities"}, {"visible", false}};

    const QString tayHoPath = tmp.filePath("tayhotay_live.gatlas");
    loadedProj.save(tayHoPath);
    QVERIFY(win.loadPath(tayHoPath, false));

    // Toggle 1-click quyhoach.hanoi.vn Land-Use Layer ON and wait for live tiles!
    win.setHanoiPlanning(true, QStringLiteral("qhpk"), 0.84);
    QVERIFY(win.hanoiPlanningEnabled());
    QCOMPARE(win.diagnostics()["hanoi_planning_tiles"].toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(win.viewport()->hanoiTileComposite().valid(), 15000);
    QVERIFY(win.viewport()->hanoiTileComposite().tilesAvailable > 0);

    // Capture 1: 3D ArchViz Tay Ho Tay with 3D buildings rising right out of live quyhoach.hanoi.vn hatched zoning!
    auto cam3 = win.viewport()->camera();
    cam3.mode = QStringLiteral("3d");
    cam3.projection = QStringLiteral("perspective");
    cam3.yaw = -24.0;
    cam3.elevation = 46.0;
    cam3.zoom = 1.15;
    cam3.solar.enabled = true;
    cam3.solar.hour = 16.0;
    cam3.solar.goldenHour = true;
    cam3.solar.skyDome = true;
    win.viewport()->setCamera(cam3);
    QTest::qWait(550);
    QVERIFY(captureWindow(shotDir + "/native-v221-tayhotay-3d-qh.png"));

    // Capture 2: 2D Plan Tay Ho Tay with live quyhoach.hanoi.vn hatched zoning + scrolled Left Sidebar (verify ZERO horizontal clipping!)
    if (auto *ls = win.findChild<QScrollArea *>("left-scroll")) {
     if (auto *lb = win.findChild<QWidget *>("left-body")) {
      QVERIFY2(lb->width() <= ls->viewport()->width(),
               qPrintable(QString("Left sidebar overflow: body=%1 viewport=%2").arg(lb->width()).arg(ls->viewport()->width())));
     }
     QCOMPARE(ls->horizontalScrollBar()->maximum(), 0);
     ls->verticalScrollBar()->setValue(460);
    }
    auto cam2 = win.viewport()->camera();
    cam2.mode = QStringLiteral("2d");
    cam2.zoom = 1.15;
    win.viewport()->setCamera(cam2);
    QTest::qWait(550);
    QVERIFY(captureWindow(shotDir + "/native-v221-tayhotay-2d-qh.png"));

    // Capture 3: Geographic MapCanvas over Tay Ho Tay with live MapLibre quyhoach.hanoi.vn overlay!
    if (auto *mapAct = win.findChild<QAction *>("map")) {
     mapAct->trigger();
     if (auto *mc = win.findChild<MapCanvas *>()) {
      mc->centerOn(21.0645, 105.7935, 15.8);
     }
     QTest::qWait(2200);
     QVERIFY(captureWindow(shotDir + "/native-v221-map-hanoi-qh.png"));
    }
    win.setHanoiPlanning(false);
   }
  }

  const QString png3d = tmp.filePath("render_3d_alpha.png");
  QVERIFY(win.viewport()->exportHighResPng(png3d, 1920, 1080, true));
  QVERIFY(QFileInfo(png3d).size() > 10000);
  win.hide();
 }

 void halongCvddPlanningOverlayAndZeroLagZoom() {
  // 1. Verify Ha Long CVDD 2025 layer metadata, bounds, local HTTP tile server, and instant disk fast-path
  const auto info = ga::hanoiPlanningLayerInfo(QStringLiteral("halong_cvdd"));
  QCOMPARE(info.id, QStringLiteral("halong_cvdd"));
  QCOMPARE(info.minZoom, 12);
  QCOMPARE(info.maxZoom, 18);
  QVERIFY(info.hasBounds);
  QVERIFY(info.west > 107.0 && info.east < 107.12);
  QVERIFY(info.south > 20.92 && info.north < 20.99);
  QVERIFY(ga::isLikelyInHaLongArea(info.focusLat, info.focusLon));
  QVERIFY(!ga::isLikelyInHaLongArea(21.0288, 105.8525));
  QVERIFY(info.urlTemplate.contains(QStringLiteral("/halong-cvdd/{z}/{x}/{y}.png")));

  // Verify instant direct-disk tile compositing right over Cảng tàu quốc tế Hạ Long (zero white background!)
  ga::Site hlSite;
  hlSite.name = QStringLiteral("Cảng tàu QT Hạ Long · CV Đại Dương");
  hlSite.lat = info.focusLat;
  hlSite.lon = info.focusLon;
  hlSite.radius = 360.0;
  hlSite.shape = QStringLiteral("circle");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  ga::HanoiPlanningTileFetcher fetcher(tmp.path());
  QSignalSpy readySpy(&fetcher, &ga::HanoiPlanningTileFetcher::compositeReady);
  QElapsedTimer fetchTimer;
  fetchTimer.start();
  fetcher.fetchForSite(hlSite, QStringLiteral("halong_cvdd"));
  QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 2000);
  const qint64 fetchMs = fetchTimer.elapsed();
  const auto comp = qvariant_cast<ga::HanoiTileComposite>(readySpy.last()[0]);
  QVERIFY(comp.valid());
  QCOMPARE(comp.layerId, QStringLiteral("halong_cvdd"));
  QVERIFY(comp.tilesAvailable > 0);
  QVERIFY(comp.zoom >= 17);
  qInfo() << "Ha Long CVDD instant composite:" << comp.tilesAvailable << "/" << comp.tilesTotal
          << "tiles at z" << comp.zoom << "in" << fetchMs << "ms";

  // Verify transparent background is genuinely stripped (both alpha==0 AND colored opaque parcels exist!)
  int transparentPx = 0, coloredPx = 0;
  for (int y = 0; y < comp.image.height(); y += 4) {
   for (int x = 0; x < comp.image.width(); x += 4) {
    const QRgb px = comp.image.pixel(x, y);
    if (qAlpha(px) == 0) ++transparentPx;
    else if (qAlpha(px) > 180 && (qRed(px) < 245 || qGreen(px) < 245 || qBlue(px) < 245)) ++coloredPx;
   }
  }
  QVERIFY2(transparentPx > 100, qPrintable(QString("Expected transparent background pixels, got %1").arg(transparentPx)));
  QVERIFY2(coloredPx > 500, qPrintable(QString("Expected opaque planning parcels, got %1").arg(coloredPx)));

  // 2. When running on real hardware GPU with GA_V012_SCREENSHOT_DIR, capture live Map (Satellite + Vector) & 3D + benchmark rapid zoom!
  const QString shotDir = qEnvironmentVariable("GA_V012_SCREENSHOT_DIR");
  if (shotDir.isEmpty()) return;

  QDir().mkpath(shotDir);
  const QString dataDir = QStringLiteral(GA_DATA);
  ga::loadPresentationCatalog(dataDir + "/presentations.json");

  MainWindow win(tmp.filePath("hl-session"), dataDir + "/hoankiem.gatlas");
  win.resize(1480, 940);
  win.show();
  QVERIFY(QTest::qWaitForWindowExposed(&win, 5000));

  auto captureWindow = [&](const QString &filePath) -> bool {
   QTest::qWait(220);
   QImage img = win.grab().toImage();
   if (auto *mc = win.findChild<MapCanvas *>()) {
    if (mc->isVisible()) {
     const QImage gl = mc->grabFramebuffer();
     if (!gl.isNull()) {
      QPainter p(&img);
      const QPoint pos = mc->mapTo(&win, QPoint(0, 0));
      p.drawImage(QRect(pos, mc->size()), gl);
      p.end();
     }
    }
   }
   if (win.viewport() && win.viewport()->isVisible()) {
    const QImage gl = win.viewport()->grabFramebuffer();
    if (!gl.isNull()) {
     QPainter p(&img);
     const QPoint pos = win.viewport()->mapTo(&win, QPoint(0, 0));
     p.drawImage(QRect(pos, win.viewport()->size()), gl);
     p.end();
    }
   }
   return img.save(filePath);
  };

  auto *mapAct = win.findChild<QAction *>("map");
  auto *mc = win.findChild<MapCanvas *>();
  QVERIFY(mapAct && mc);
  mapAct->trigger();
  QTest::qWait(300);

  // Switch to Satellite + enable Ha Long CVDD 2025 planning layer (auto-jumps to Cảng tàu QT Hạ Long!)
  mc->setBasemapMode(QStringLiteral("satellite"));
  win.setHanoiPlanning(true, QStringLiteral("halong_cvdd"), 0.80);
  QCOMPARE(mc->hanoiPlanningLayer(), QStringLiteral("halong_cvdd"));
  mc->centerOn(info.focusLat, info.focusLon, 16.0);
  QTRY_VERIFY_WITH_TIMEOUT(mc->basemapDiagnostics()["fully_rendered"].toBool(), 20000);
  QTest::qWait(350);
  QVERIFY2(mc->basemapDiagnostics()["error"].toString().isEmpty(),
           qPrintable("Map error: " + mc->basemapDiagnostics()["error"].toString()));
  QCOMPARE(mc->basemapDiagnostics()["gl_error"].toInt(), 0);
  QVERIFY(captureWindow(shotDir + "/native-v232-halong-map-satellite-qh.png"));

  // Benchmark rapid wheel zoom sweep (z13.5 -> z18.0 -> z14.0, 40 wheel ticks with live GPU repaints)
  mc->centerOn(info.focusLat, info.focusLon, 14.0);
  QTest::qWait(400);
  QVector<double> frameMs;
  frameMs.reserve(40);
  const QPointF centerPt(mc->width() * 0.5, mc->height() * 0.5);
  for (int i = 0; i < 40; ++i) {
   const int dy = (i < 20) ? 60 : -60;
   QElapsedTimer t;
   t.start();
   QWheelEvent wheel(centerPt, centerPt, QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
   QApplication::sendEvent(mc, &wheel);
   mc->repaint();
   QCoreApplication::processEvents();
   frameMs.append(double(t.nsecsElapsed()) / 1e6);
  }
  std::sort(frameMs.begin(), frameMs.end());
  const double avgMs = std::accumulate(frameMs.begin(), frameMs.end(), 0.0) / double(frameMs.size());
  const double p95Ms = frameMs[int(frameMs.size() * 0.95)];
  qInfo() << "Ha Long Map Zoom Sweep (40 frames): avg =" << avgMs << "ms, p95 =" << p95Ms << "ms, max =" << frameMs.last() << "ms";
  QCOMPARE(mc->basemapDiagnostics()["gl_error"].toInt(), 0);
  QVERIFY2(avgMs < 25.0, qPrintable(QString("Expected smooth zoom avg < 25ms, got %1ms").arg(avgMs)));

  // Capture Vector Basemap + transparent Ha Long CVDD 2025 overlay
  mc->setBasemapMode(QStringLiteral("vector"));
  mc->centerOn(info.focusLat, info.focusLon, 16.0);
  QTRY_VERIFY_WITH_TIMEOUT(mc->basemapDiagnostics()["fully_rendered"].toBool(), 20000);
  QTest::qWait(350);
  QVERIFY(captureWindow(shotDir + "/native-v232-halong-map-vector-qh.png"));

  // Also test 2D/3D viewport draping over Ha Long Cruise Port (using synthetic minimal site project so zero Overpass timeout risk)
  ga::Project hlProj;
  hlProj.site = hlSite;
  hlProj.style = QStringLiteral("suushka-study");
  const QString hlPath = tmp.filePath("halong_port.gatlas");
  hlProj.save(hlPath);
  QVERIFY(win.loadPath(hlPath, false));
  win.setHanoiPlanning(true, QStringLiteral("halong_cvdd"), 0.84);
  QTRY_VERIFY_WITH_TIMEOUT(win.viewport()->hanoiTileComposite().valid(), 5000);
  auto cam3 = win.viewport()->camera();
  cam3.mode = QStringLiteral("3d");
  cam3.projection = QStringLiteral("perspective");
  cam3.yaw = -28.0;
  cam3.elevation = 48.0;
  cam3.zoom = 1.25;
  win.viewport()->setCamera(cam3);
  if (auto *v3 = win.findChild<QAction *>("view-3d")) v3->trigger();
  QTest::qWait(1400);
  QVERIFY(captureWindow(shotDir + "/native-v232-halong-3d-qh.png"));

  // Switch 3D Viewport to Satellite mode (like clicking the Google-Maps bottom-left corner card!) and verify sync + capture!
  win.viewport()->setBasemapMode(QStringLiteral("satellite"));
  QCOMPARE(mc->basemapMode(), QStringLiteral("satellite"));
  QCOMPARE(win.viewport()->basemapMode(), QStringLiteral("satellite"));
  QTest::qWait(2200);
  QVERIFY(captureWindow(shotDir + "/native-v232-halong-3d-satellite-qh.png"));
  win.viewport()->setBasemapMode(QStringLiteral("vector"));

  win.setHanoiPlanning(false);
  win.hide();
 }

 void v232CompatibilityAndZoomOutSafety() {
  const QString outDir = qEnvironmentVariable("GA_VERIFY_232_DIR");
  const QString dataDir = QStringLiteral(GA_DATA);
  ga::loadPresentationCatalog(dataDir + "/presentations.json");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  MainWindow win(tmp.filePath("v232-verify"), dataDir + "/hoankiem.gatlas");
  win.resize(1480, 920);
  win.show();
  QVERIFY(QTest::qWaitForWindowExposed(&win, 5000));
  QTest::qWait(1500);

  // 1. Viewport NativeBasemap must render with ZERO GL error via offscreen non-MSAA FBO
  const auto vpDiag0 = win.viewport()->basemapDiagnostics();
  QVERIFY2(vpDiag0["initialized"].toBool(), "Viewport NativeBasemap did not initialize");
  QCOMPARE(vpDiag0["gl_error"].toInt(), 0);

  auto *mc = win.findChild<MapCanvas *>();
  auto *mapAct = win.findChild<QAction *>("map");
  auto *v2Act = win.findChild<QAction *>("view-2d");
  auto *v3Act = win.findChild<QAction *>("view-3d");
  QVERIFY(mc && mapAct && v2Act && v3Act);

  // 2. Switch to Map tab -> TP.HCM -> verify crisp vector basemap & gl_error == 0
  mapAct->trigger();
  mc->centerOn(10.7769, 106.7009, 15.8);
  QTRY_VERIFY_WITH_TIMEOUT(mc->basemapDiagnostics()["style_loaded"].toBool(), 15000);
  QTest::qWait(1600);
  const auto mapDiag = mc->basemapDiagnostics();
  QCOMPARE(mapDiag["gl_error"].toInt(), 0);
  if (!outDir.isEmpty()) {
   QDir().mkpath(outDir);
   mc->grabFramebuffer().save(outDir + "/verify_1_map_hcm.png");
  }

  // 3. Aggressive wheel zoom-out on MapCanvas (35 ticks off-center) -> verify zoom >= minZoom & lat clamped (no pole/Mongolia detach!)
  for (int i = 0; i < 35; ++i) {
   QWheelEvent we(QPointF(220, 180), mc->mapToGlobal(QPointF(220, 180)), QPoint(), QPoint(0, -120),
                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
   QApplication::sendEvent(mc, &we);
   QTest::qWait(18);
  }
  QTest::qWait(1200);
  const auto mvOut = mc->view();
  QVERIFY2(mvOut["zoom"].toDouble() >= 2.15, qPrintable(QString("MapCanvas zoom dropped too low: %1").arg(mvOut["zoom"].toDouble())));
  QVERIFY2(std::abs(mvOut["lat"].toDouble()) < 72.0, qPrintable(QString("MapCanvas lat drifted to pole: %1").arg(mvOut["lat"].toDouble())));
  QCOMPARE(mc->basemapDiagnostics()["gl_error"].toInt(), 0);
  if (!outDir.isEmpty()) mc->grabFramebuffer().save(outDir + "/verify_2_map_zoomout.png");

  // 4. Switch to 2D & 3D on HCM -> aggressive wheel zoom-out -> verify zero GL error, bounded zoom, no ocean teleport
  QVERIFY(win.loadPath(dataDir + "/sites/benthanh-osm.gatlas", false));
  QTest::qWait(1800);
  QCOMPARE(win.viewport()->basemapDiagnostics()["gl_error"].toInt(), 0);
  if (!outDir.isEmpty()) win.viewport()->grabFramebuffer().save(outDir + "/verify_3_hcm_3d.png");

  v2Act->trigger();
  QTest::qWait(1200);
  for (int i = 0; i < 45; ++i) {
   QWheelEvent we(QPointF(260, 200), win.viewport()->mapToGlobal(QPointF(260, 200)), QPoint(), QPoint(0, -120),
                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
   QApplication::sendEvent(win.viewport(), &we);
   QTest::qWait(18);
  }
  QTest::qWait(1200);
  const auto geo2d = win.viewport()->geographicView();
  QVERIFY2(geo2d["zoom"].toDouble() >= 2.15, qPrintable(QString("2D zoom too low: %1").arg(geo2d["zoom"].toDouble())));
  QCOMPARE(win.viewport()->basemapDiagnostics()["gl_error"].toInt(), 0);
  if (!outDir.isEmpty()) win.viewport()->grabFramebuffer().save(outDir + "/verify_4_2d_zoomout.png");

  win.hide();
 }

 void v233UniversalCompatibilityAndFallbacks() {
  // 1. Hardware capability matrix: integrated GPUs, VMs, low maxTex, and Safe modes resolve safely
  const auto intelHw = ga::probeHardware("Intel(R) Iris(R) Xe Graphics", nullptr, "4.6.0", 4096, 4);
  QVERIFY(intelHw.recommendedTier != ga::PerfTier::Ultra);
  const auto vmHw = ga::probeHardware("Microsoft Basic Render Driver", nullptr, "1.1.0", 1024, 0);
  QCOMPARE(vmHw.recommendedTier, ga::PerfTier::Low);
  const auto vmCfg = ga::resolveGraphicsConfig(ga::PerfTier::Auto, vmHw, ga::GraphicsCompatMode::Auto);
  QVERIFY(!vmCfg.ssaoEnabled);
  QVERIFY(vmCfg.shadowMapSize <= 1024);

  const auto rtxHw = ga::probeHardware("NVIDIA GeForce RTX 3060/PCIe/SSE2", nullptr, "4.6.0", 16384, 8);
  QCOMPARE(rtxHw.recommendedTier, ga::PerfTier::Ultra);
  const auto compatCfg = ga::resolveGraphicsConfig(ga::PerfTier::Auto, rtxHw, ga::GraphicsCompatMode::Compatible);
  QVERIFY(!compatCfg.ssaoEnabled);
  QCOMPARE(compatCfg.shadowMapSize, 1024);
  const auto swCfg = ga::resolveGraphicsConfig(ga::PerfTier::Ultra, rtxHw, ga::GraphicsCompatMode::SoftwareSafe);
  QVERIFY(!swCfg.ssaoEnabled);
  QVERIFY(!swCfg.architecturalCrease);

  // 2. CLI safety flags & BootSafetyState behavior
  const auto prevBoot = ga::globalBootSafety();
  const auto stCompat = ga::beginGraphicsBoot({}, ga::GraphicsCompatMode::Auto, false, true, false);
  QCOMPARE(stCompat.effectiveBootMode, ga::GraphicsCompatMode::Compatible);
  QCOMPARE(stCompat.desiredMsaaSamples(), 0);
  const auto stRaster = ga::beginGraphicsBoot({}, ga::GraphicsCompatMode::Auto, false, false, true);
  QCOMPARE(stRaster.effectiveBootMode, ga::GraphicsCompatMode::RasterSafe);
  QVERIFY(stRaster.forceRasterBasemap());
  ga::setGlobalBootSafety(prevBoot);

  // 3. Textured Quad blit mode + Universal QPainter Raster Fallback + System Diagnostic Report
  if (QGuiApplication::platformName().contains("offscreen", Qt::CaseInsensitive)) return;
  const QString dataDir = QString::fromUtf8(GA_DATA);
  const QString outDir = qEnvironmentVariable("GA_V232_OUT");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  MainWindow win(tmp.filePath("v233-compat"), dataDir + "/hoankiem.gatlas");
  win.resize(1320, 860);
  win.show();
  QVERIFY(QTest::qWaitForWindowExposed(&win, 5000));
  QTest::qWait(1400);

  const auto vpDiag = win.viewport()->basemapDiagnostics();
  QCOMPARE(vpDiag["blit_mode"].toString(), QString("textured-quad"));
  QCOMPARE(vpDiag["gl_error"].toInt(), 0);

  auto *mc = win.findChild<MapCanvas *>();
  auto *mapAct = win.findChild<QAction *>("map");
  QVERIFY(mc && mapAct);
  mapAct->trigger();
  mc->centerOn(21.0288, 105.8525, 15.0);
  mc->setForceRasterFallback(true);
  QVERIFY(mc->usingRasterFallback());
  QTest::qWait(1800);
  const QImage rasterShot = mc->grabFramebuffer();
  QVERIFY(!rasterShot.isNull());
  // Verify pixels outside center are NOT solid black (universal fallback drew light basemap/grid!)
  const QColor cornerPx = rasterShot.pixelColor(rasterShot.width() / 6, rasterShot.height() / 6);
  QVERIFY2(cornerPx.red() > 20 || cornerPx.green() > 20 || cornerPx.blue() > 20,
           qPrintable(QString("Raster fallback corner unexpectedly black: %1").arg(cornerPx.name())));
  QVERIFY(mc->basemapDiagnostics()["raster_fallback"].toBool());
  if (!outDir.isEmpty()) {
   QDir().mkpath(outDir);
   rasterShot.save(outDir + "/verify_5_raster_fallback.png");
  }
  mc->setForceRasterFallback(false);

  const QString diagReport = rtxHw.diagnosticReport(win.diagnostics(), ga::GraphicsCompatMode::Auto, false);
  QVERIFY(diagReport.contains("GroundAtlas Desktop Diagnostics"));
  QVERIFY(diagReport.contains("textured-quad"));
  win.hide();
 }
};

int main(int argc, char **argv) {
 QSurfaceFormat surface;
 surface.setVersion(3, 3);
#ifdef Q_OS_MACOS
 surface.setProfile(QSurfaceFormat::CoreProfile);
#else
 surface.setProfile(QSurfaceFormat::CompatibilityProfile);
#endif
 surface.setDepthBufferSize(24);
 surface.setStencilBufferSize(8);
 surface.setSamples(4);
 QSurfaceFormat::setDefaultFormat(surface);
 QApplication app(argc, argv);
 NativeV012Tests tc;
 return QTest::qExec(&tc, argc, argv);
}
#include "v012_tests.moc"
