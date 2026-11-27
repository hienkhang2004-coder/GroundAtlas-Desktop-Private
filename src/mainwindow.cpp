#include "mainwindow.h"
#include "analysis.h"
#include "analysisdialog.h"
#include "appearance.h"
#include "dataaccess.h"
#include "dialogs_v2.h"
#include "licensedialog.h"
#include "exporter.h"
#include "geojsonoverlay.h"
#include "layereditor.h"
#include "mapcanvas.h"
#include "osmworkflow.h"
#include "report.h"
#include "sectiondialog.h"
#include "sessionstore.h"
#include "sitegeometry.h"
#include "siteselection.h"
#include "solar.h"
#include "uxmotion.h"
#include "viewport.h"
#include "windowchrome.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

namespace {
QIcon colorDotIcon(const QColor &c) {
 QPixmap pm(28, 28);
 pm.fill(Qt::transparent);
 QPainter p(&pm);
 p.setRenderHint(QPainter::Antialiasing, true);
 p.setPen(QPen(QColor(255, 255, 255, 175), 2.2));
 p.setBrush(c);
 p.drawEllipse(QRectF(5.0, 5.0, 18.0, 18.0));
 p.end();
 pm.setDevicePixelRatio(2.0);
 return QIcon(pm);
}
} // namespace

QString MainWindow::trText(const char *vi, const char *en) const {
 return QString::fromUtf8(english_ ? en : vi);
}

MainWindow::MainWindow(QString directory, QString path, QWidget *parent)
 : QMainWindow(parent), dataDirectory_(std::move(directory)) {
 resize(1520, 940);
 setMinimumSize(1120, 700);
 setWindowIcon(brandBadgeIcon());
 setAcceptDrops(true);
 buildUi();

 // Load persisted preferences (isolated from test temp directories)
 const auto prefs = ga::loadAppPreferences(dataDirectory_);
 perfTier_ = prefs.perfTier;
 smoothDamping_ = prefs.smoothDamping;
 autoDarkBasemap_ = prefs.autoDarkBasemap;
 terrain3d_ = prefs.terrain3d;
 imperialUnits_ = prefs.imperialUnits;
 if (prefs.darkTheme) setDarkTheme(true);
 if (prefs.english) language_->setCurrentIndex(1);
 if (!prefs.windowGeometry.isEmpty()) restoreGeometry(prefs.windowGeometry);
 if (leftDrawer_ && !prefs.leftDrawerOpen) {
  leftDrawer_->setOpen(false, false);
  if (leftToggle_) leftToggle_->setText(QString::fromUtf8("›"));
 }
 if (studioDrawer_ && !prefs.studioDrawerOpen) {
  studioDrawer_->setOpen(false, false);
  if (studioToggleBtn_) { const QSignalBlocker b(studioToggleBtn_); studioToggleBtn_->setChecked(false); }
  if (rightToggle_) rightToggle_->setText(QString::fromUtf8("‹"));
 }
 applyHardwareProfile();

 QDir().mkpath(dataDirectory_);
 sessionStore_ = new SessionStore(dataDirectory_, this);
 connect(sessionStore_, &SessionStore::saved, this, [this](bool changed) {
  if (changed && !closing_) statusBar()->showMessage(trText("Đã cập nhật bản khôi phục trên máy", "Local recovery updated"), 2000);
 });
 connect(sessionStore_, &SessionStore::failed, this, [this](const QString &error) {
  statusBar()->showMessage(trText("Tự lưu chưa thành công: ", "Autosave failed: ") + error, 10000);
 });
 connect(&autosave_, &QTimer::timeout, this, &MainWindow::saveAutosave);
 autosave_.setInterval(15000);
 autosave_.start();

 if (path.isEmpty()) path = ga::appDataPath("hoankiem.gatlas");
 if (QFileInfo::exists(path)) loadPath(path, false);
 else newProject();
}

QAction *MainWindow::action(const QString &id, const char *vi, const char *en, const QKeySequence &shortcut) {
 auto *a = new QAction(this);
 a->setObjectName(id);
 if (!shortcut.isEmpty()) a->setShortcut(shortcut);
 translations_.append([=, this] { a->setText(trText(vi, en)); });
 a->setText(trText(vi, en));
 actions_[id] = a;
 return a;
}

QLabel *MainWindow::label(const char *vi, const char *en, const QString &name) {
 auto *l = new QLabel(this);
 l->setWordWrap(true);
 if (!name.isEmpty()) l->setObjectName(name);
 translations_.append([=, this] { l->setText(trText(vi, en)); });
 l->setText(trText(vi, en));
 return l;
}

void MainWindow::setDarkTheme(bool dark) {
 darkTheme_ = dark;
 setStyleSheet(workspaceStyle(darkTheme_));
 if (qApp) qApp->setStyleSheet(workspaceStyle(darkTheme_));
 if (themeLightBtn_ && themeDarkBtn_) {
  const QSignalBlocker b1(themeLightBtn_), b2(themeDarkBtn_);
  themeLightBtn_->setChecked(!dark);
  themeDarkBtn_->setChecked(dark);
 }
 for (auto *track : segTracks_) {
  if (track) {
   track->setDarkTheme(dark);
   if (isVisible()) track->syncAnimated();
   else track->syncImmediate();
  }
 }
 if (viewport_) viewport_->setDarkUi(dark);
 if (map_) map_->setDarkUi(dark);
 if (liveSection_) liveSection_->setDarkTheme(dark);
 if (toast_) toast_->setDarkTheme(dark);
 if (siteSelection_) siteSelection_->setDarkTheme(dark);
 syncLicenseUi();
 rebuildIcons();
}

void MainWindow::showToast(const QString &message, const QColor &accent, int durationMs) {
 if (toast_) toast_->showMessage(message, accent, durationMs);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
 QMainWindow::resizeEvent(event);
 if (toast_) toast_->reposition();
}

void MainWindow::rebuildIcons() {
 for (const auto &k : QStringList{"new", "open", "save", "view-2d", "view-3d", "explode", "grid", "sun", "reset", "map", "section", "analysis", "export-png"}) {
  if (actions_.contains(k)) actions_[k]->setIcon(workspaceIcon(k, darkTheme_));
 }
 const QStringList tileIcons = {"diagram", "explode", "stats", "analysis", "laser", "section", "report", "sun"};
 for (int i = 0; i < studioTiles_.size() && i < tileIcons.size(); ++i) {
  studioTiles_[i]->setIcon(workspaceIcon(tileIcons[i], darkTheme_));
 }
}

void MainWindow::setStudioOpen(bool open) {
 if (!studioDrawer_) return;
 studioDrawer_->setOpen(open, true);
 if (studioToggleBtn_) {
  const QSignalBlocker b(studioToggleBtn_);
  studioToggleBtn_->setChecked(open);
 }
 if (rightToggle_) rightToggle_->setText(open ? QString::fromUtf8("›") : QString::fromUtf8("‹"));
}

void MainWindow::selectStudioTab(int index) {
 if (!studioStack_ || index < 0 || index >= studioStack_->count()) return;
 studioStack_->slideToIndex(index);
 for (int i = 0; i < studioTiles_.size(); ++i) {
  const QSignalBlocker b(studioTiles_[i]);
  studioTiles_[i]->setChecked(i == index);
 }
 setStudioOpen(true);
}

void MainWindow::buildUi() {
 setStyleSheet(workspaceStyle(false));

 // Keep standard 3-menu native menuBar (used by macOS-style windowchrome & tests)
 auto *fileMenu = menuBar()->addMenu(trText("Dự án", "Project"));
 auto *viewMenu = menuBar()->addMenu(trText("Góc nhìn", "View"));
 auto *helpMenu = menuBar()->addMenu(trText("Thông tin", "Info"));
 translations_.append([=, this] {
  fileMenu->setTitle(trText("Dự án", "Project"));
  viewMenu->setTitle(trText("Góc nhìn", "View"));
  helpMenu->setTitle(trText("Thông tin", "Info"));
 });

 const auto newAction = action("new", "Mới", "New", QKeySequence::New);
 const auto openAction = action("open", "Mở dự án", "Open project", QKeySequence::Open);
 const auto importAction = action("import", "Thêm lớp GeoJSON…", "Add GeoJSON overlay…");
 const auto import3dAction = action("import-3d", "Nhập mô hình 3D (SketchUp / DAE / OBJ / GLB)…", "Import 3D Model (SketchUp / DAE / OBJ / GLB)…", QKeySequence("Ctrl+Shift+I"));
 const auto importPlanAction = action("import-plan", "Nhập bản vẽ Quy hoạch (DXF VN-2000 / GeoJSON)…", "Import Planning Vector (DXF VN-2000 / GeoJSON)…");
 const auto importPlanImgAction = action("import-plan-img", "Phủ ảnh bản đồ Quy hoạch (PNG / JPG)…", "Overlay Planning Map Image (PNG / JPG)…");
 const auto saveAction = action("save", "Lưu", "Save", QKeySequence::Save);
 const auto saveAsAction = action("save-as", "Lưu thành…", "Save as…", QKeySequence::SaveAs);
 const auto restore = action("restore", "Khôi phục tự lưu", "Restore autosave");
 fileMenu->addActions({newAction, openAction, importAction, import3dAction, importPlanAction, importPlanImgAction, saveAction, saveAsAction, restore});
 fileMenu->addSeparator();
 connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
 connect(openAction, &QAction::triggered, this, &MainWindow::open);
 connect(importAction, &QAction::triggered, this, &MainWindow::chooseGeoJsonOverlay);
 connect(import3dAction, &QAction::triggered, this, &MainWindow::choose3DModelFile);
 connect(importPlanAction, &QAction::triggered, this, &MainWindow::choosePlanningVectorFile);
 connect(importPlanImgAction, &QAction::triggered, this, &MainWindow::choosePlanningImageFile);
 connect(saveAction, &QAction::triggered, this, [this] { save(); });
 connect(saveAsAction, &QAction::triggered, this, [this] { save(true); });
 connect(restore, &QAction::triggered, this, &MainWindow::restoreAutosave);

 auto *exportMenu = fileMenu->addMenu(trText("Xuất", "Export"));
 translations_.append([=, this] { exportMenu->setTitle(trText("Xuất", "Export")); });
 for (const auto &row : QVector<QStringList>{
       {"project-kit", "Bộ hồ sơ Đồ án 1-Click (8 tệp)…", "1-Click Presentation Kit (8 files)…"},
       {"sun-gif", "Hoạt ảnh Nắng 1 ngày (.GIF)…", "Sun Timelapse Animation (.GIF)…"},
       {"turntable-gif", "Hoạt ảnh Xoay 360° (.GIF)…", "360° Turntable Animation (.GIF)…"},
       {"png", "PNG (Ảnh khung nhìn)", "PNG (Viewport)"},
       {"png-4k", "PNG 4K siêu nét (3840×2160)", "4K Render PNG (3840×2160)"},
       {"png-8k-alpha", "PNG 8K nền trong suốt (7680×4320)", "8K Transparent PNG (7680×4320)"},
       {"svg", "SVG (Vector 2D)", "SVG (2D Vector)"},
       {"pdf", "PDF (Bản vẽ A3)", "PDF (A3 Sheet)"},
       {"dxf", "DXF (AutoCAD 2D/3D)", "DXF (AutoCAD 2D/3D)"},
       {"dae", "DAE (SketchUp COLLADA)", "DAE (SketchUp COLLADA)"},
       {"glb", "GLB (Blender / glTF 2.0)", "GLB (Blender / glTF 2.0)"},
       {"stl", "STL (In 3D / Mesh)", "STL (3D Print / Mesh)"},
       {"obj", "OBJ (3D Mesh)", "OBJ (3D Mesh)"},
       {"geojson", "GeoJSON (GIS WGS84)", "GeoJSON (GIS WGS84)"},
       {"report", "Báo cáo Hồ sơ A3 (.PDF)", "3-Page Site Report (.PDF)"},
       {"laser-dxf", "Cắt Laser 1:1000 (.DXF)", "Laser Cut 1:1000 (.DXF)"},
      }) {
  auto *a = new QAction(this);
  translations_.append([=, this] { a->setText(trText(row[1].toUtf8().constData(), row[2].toUtf8().constData())); });
  a->setText(trText(row[1].toUtf8().constData(), row[2].toUtf8().constData()));
  exportMenu->addAction(a);
  connect(a, &QAction::triggered, this, [this, id = row[0]] { exportFile(id); });
 }
 exportMenu->addSeparator();
 for (const auto &row : QVector<QStringList>{
       {"skp", "Xuất & Mở SketchUp (.skp thật)…", "Export & Open in SketchUp (.skp)…"},
       {"blend", "Xuất file Blender (.blend thật)…", "Export Native Blender (.blend)…"},
       {"autocad", "Xuất & Mở AutoCAD (.dxf)…", "Export & Open in AutoCAD (.dxf)…"},
      }) {
  auto *a = new QAction(this);
  translations_.append([=, this] { a->setText(trText(row[1].toUtf8().constData(), row[2].toUtf8().constData())); });
  a->setText(trText(row[1].toUtf8().constData(), row[2].toUtf8().constData()));
  exportMenu->addAction(a);
  connect(a, &QAction::triggered, this, [this, id = row[0]] { exportFile(id); });
 }

 auto *licenseAction = action("license", "Bản quyền & Kích hoạt (Per-Seat)…", "License & Activation (Per-Seat)…");
 auto *updateAction = action("check-updates", "Kiểm tra cập nhật…", "Check for Updates…");
 auto *ossAction = action("oss-licenses", "Giấy phép nguồn mở & Dữ liệu…", "Open-Source & Data Licenses…");
 auto *sourceAction = action("source", "Nguồn & giới hạn dữ liệu", "Data sources & limitations");
 auto *about = action("about", "Về GroundAtlas Desktop", "About GroundAtlas Desktop");
 helpMenu->addActions({licenseAction, updateAction, ossAction, sourceAction, about});
 connect(licenseAction, &QAction::triggered, this, [this] { showLicenseDialog(); });
 connect(updateAction, &QAction::triggered, this, [this] { showUpdateDialog(true); });
 connect(ossAction, &QAction::triggered, this, &MainWindow::showOpenSourceLicenses);
 connect(sourceAction, &QAction::triggered, this, &MainWindow::showSource);
 connect(about, &QAction::triggered, this, [this] {
  QMessageBox::about(this, "GroundAtlas Desktop",
   trText("GroundAtlas Native 2.3.1 · 100% C++20 / Qt 6.8 / OpenGL 3.3 / MapLibre Native\n\n"
          "Kiến trúc thuần mã máy (0% WebView/Electron) tự tối ưu theo phần cứng tới 500 Hz.\n"
          "• Bản quyền Per-Seat (1 Seat = 1 Máy · Lifetime 2.x · Ký số Ed25519 + Windows DPAPI + Offline 14 ngày).\n"
          "• Tự động kiểm tra & thông báo phiên bản cập nhật mới (miễn phí trọn đời dòng 2.x).\n"
          "• Nạp khu đất OSM toàn cầu siêu tốc (OSM API v0.6 + tự chia lưới 2×2 + Overpass dự phòng).\n"
          "• Lớp bật/tắt 1-Click Quy hoạch sử dụng đất (hỗ trợ nguồn XYZ tùy chỉnh) đè trên Bản đồ, 2D & 3D.\n"
          "• Nhập mô hình 3D trực tiếp từ SketchUp (.skp, .dae, .obj, .glb, .stl), đổ bóng 4096px, mặt cắt A–A' sống, Concept Massing FAR/BCR, và xuất Bộ hồ sơ 8 tệp 1-Click.\n\n"
          "Xem chi tiết bản quyền thư viện nguồn mở (Qt 6.8 LGPLv3, MapLibre BSD, Monocypher, OSM ODbL) tại mục Thông tin → Giấy phép nguồn mở.",
          "GroundAtlas Native 2.3.1 · 100% C++20 / Qt 6.8 / OpenGL 3.3 / MapLibre Native\n\n"
          "Pure compiled C++ workstation (0% WebView/Electron) with hardware auto-profiling up to 500 Hz.\n"
          "• Commercial Per-Seat Licensing (1 Seat = 1 Machine · Lifetime 2.x · Ed25519 + Windows DPAPI + 14-day offline lease).\n"
          "• Fast global OSM site loading (OSM API v0.6 + auto 2×2 tiling + Overpass failover).\n"
          "• 1-Click Planning Tile Overlay (with custom XYZ Tile URL support) on Map, 2D & 3D.\n"
          "• Direct SketchUp .skp / .dae / .obj / .glb / .stl import, 4096px PCF shadows, live Section A–A', Concept Massing FAR/BCR & 1-Click 8-File Presentation Kit."));
 });

 // Core view & tool actions
 const auto mapAction = action("map", "Bản đồ", "Map");
 const auto flat = action("view-2d", "2D", "2D");
 const auto three = action("view-3d", "3D", "3D");
 const auto explode = action("explode", "Phân lớp", "Explode");
 const auto grid = action("grid", "Lưới", "Grid");
 const auto sunAction = action("sun", "Nắng 3D", "Sun 3D", QKeySequence("Ctrl+U"));
 const auto reset = action("reset", "Về tâm", "Recenter", QKeySequence("F"));
 const auto sectionAction = action("section", "Mặt cắt", "Section", QKeySequence("Ctrl+K"));
 const auto analysisAction = action("analysis", "Phân tích", "Analysis", QKeySequence("Ctrl+I"));
 const auto exportPngAction = action("export-png", "Xuất", "Export", QKeySequence("Ctrl+E"));
 for (auto *a : {mapAction, flat, three, explode, grid, sunAction}) a->setCheckable(true);
 three->setChecked(true);
 viewMenu->addActions({mapAction, flat, three, explode, grid, sunAction, reset, sectionAction, analysisAction});

 // Hidden QComboBox("language") kept in sync with VI|EN pill for existing tests & accessibility
 language_ = new QComboBox(this);
 language_->setObjectName("language");
 language_->addItem("VI", "vi");
 language_->addItem("EN", "en");
 language_->hide();
 connect(language_, &QComboBox::currentIndexChanged, this, [this](int index) { setLanguage(index == 1); });

 // ═══════════════════════════════════════════════════════════════════════════
 // CENTRAL SHELL: Exact GroundAtlas v2 Web Layout (44px Header + Tabs + 3-Pane)
 // ═══════════════════════════════════════════════════════════════════════════
 auto *shell = new QWidget(this);
 auto *shellLayout = new QVBoxLayout(shell);
 shellLayout->setContentsMargins(0, 0, 0, 0);
 shellLayout->setSpacing(0);
 setCentralWidget(shell);

 // ── 1. 42px Compact Adobe Spectrum 2 Header Bar ──
 auto *v2Header = new QWidget(shell);
 v2Header->setObjectName("v2-header");
 v2Header->setFixedHeight(42);
 auto *hdr = new QHBoxLayout(v2Header);
 hdr->setContentsMargins(12, 0, 12, 0);
 hdr->setSpacing(6);

 auto *logoIcon = new QLabel(v2Header);
 logoIcon->setPixmap(brandBadgeIcon().pixmap(22, 22));
 hdr->addWidget(logoIcon);
 auto *brand = new QLabel("GroundAtlas", v2Header);
 brand->setObjectName("brand");
 hdr->addWidget(brand);
 auto *verBadge = new QLabel("2.3", v2Header);
 verBadge->setObjectName("v2-badge");
 hdr->addWidget(verBadge);

 auto addHeaderCmd = [&](const char *vi, const char *en, std::function<void()> fn) {
  auto *b = new QPushButton(trText(vi, en), v2Header);
  b->setObjectName("header-cmd");
  translations_.append([=, this] { b->setText(trText(vi, en)); });
  connect(b, &QPushButton::clicked, this, std::move(fn));
  hdr->addWidget(b);
  return b;
 };
 addHeaderCmd("Dự án", "Projects", [this, fileMenu] { fileMenu->exec(QCursor::pos()); });
 addHeaderCmd("Phong cách", "Styles", [this] { showStyleGallery(); });
 addHeaderCmd("Nguồn", "Source", [this] { showSource(); });
 addHeaderCmd("Mặt cắt", "Section", [this] { showSection(); });
 addHeaderCmd("Cài đặt", "Settings", [this] { showSettings(); });
 addHeaderCmd("⌘ Lệnh", "⌘ Commands", [this] { showCommandPalette(); });
 addHeaderCmd("Thông tin", "About", [this, about] { about->trigger(); });

 hdr->addStretch(1);

 // Light / Dark sliding segmented pill
 auto *themePill = new ux::SlidingPillTrack(v2Header);
 segTracks_.append(themePill);
 themeLightBtn_ = new QToolButton(themePill);
 themeLightBtn_->setText(QString::fromUtf8("☼"));
 themeLightBtn_->setCheckable(true);
 themeLightBtn_->setChecked(true);
 themeDarkBtn_ = new QToolButton(themePill);
 themeDarkBtn_->setText(QString::fromUtf8("☾"));
 themeDarkBtn_->setCheckable(true);
 themePill->addSegment(themeLightBtn_);
 themePill->addSegment(themeDarkBtn_);
 connect(themeLightBtn_, &QToolButton::clicked, this, [this] {
  setDarkTheme(false);
  showToast(trText("Giao diện Sáng · Spectrum Light", "Light Theme · Spectrum Light"), QColor("#3b63fb"));
 });
 connect(themeDarkBtn_, &QToolButton::clicked, this, [this] {
  setDarkTheme(true);
  showToast(trText("Giao diện Tối · Spectrum Dark", "Dark Theme · Spectrum Dark"), QColor("#4b75ff"));
 });
 hdr->addWidget(themePill);

 // Map / 2D / 3D sliding segmented pill
 auto *viewPill = new ux::SlidingPillTrack(v2Header);
 segTracks_.append(viewPill);
 for (auto *a : {mapAction, flat, three}) {
  auto *b = new QToolButton(viewPill);
  b->setDefaultAction(a);
  b->setToolButtonStyle(Qt::ToolButtonTextOnly);
  viewPill->addSegment(b);
 }
 hdr->addWidget(viewPill);

 // Mô hình 3D (Export Studio modal)
 auto *model3dBtn = new QPushButton(trText("Xuất 3D", "Export 3D"), v2Header);
 model3dBtn->setObjectName("header-pill");
 translations_.append([=, this] { model3dBtn->setText(trText("Xuất 3D", "Export 3D")); });
 connect(model3dBtn, &QPushButton::clicked, this, &MainWindow::showExportStudio);
 hdr->addWidget(model3dBtn);

 // Studio inspector toggle pill
 studioToggleBtn_ = new QPushButton("Studio", v2Header);
 studioToggleBtn_->setObjectName("header-pill-dark");
 studioToggleBtn_->setCheckable(true);
 studioToggleBtn_->setChecked(true);
 connect(studioToggleBtn_, &QPushButton::toggled, this, &MainWindow::setStudioOpen);
 hdr->addWidget(studioToggleBtn_);

 // VI | EN sliding segmented pill
 auto *langPill = new ux::SlidingPillTrack(v2Header);
 segTracks_.append(langPill);
 langViBtn_ = new QToolButton(langPill);
 langViBtn_->setText("VI");
 langViBtn_->setCheckable(true);
 langViBtn_->setChecked(true);
 langEnBtn_ = new QToolButton(langPill);
 langEnBtn_->setText("EN");
 langEnBtn_->setCheckable(true);
 langPill->addSegment(langViBtn_);
 langPill->addSegment(langEnBtn_);
 connect(langViBtn_, &QToolButton::clicked, this, [this] { language_->setCurrentIndex(0); });
 connect(langEnBtn_, &QToolButton::clicked, this, [this] { language_->setCurrentIndex(1); });
 hdr->addWidget(langPill);

 // Update available pill (hidden unless Cloudflare reports latest_version > current)
 updatePillBtn_ = new QPushButton(QStringLiteral("↑ Update"), v2Header);
 updatePillBtn_->setObjectName("update-pill");
 updatePillBtn_->setStyleSheet("QPushButton{background:#3b63fb;color:#ffffff;border:none;border-radius:14px;padding:4px 10px;font-weight:600;font-size:11px;} QPushButton:hover{background:#274dea;}");
 updatePillBtn_->hide();
 connect(updatePillBtn_, &QPushButton::clicked, this, [this] { showUpdateDialog(true); });
 hdr->addWidget(updatePillBtn_);

 // License / Pro status pill
 licensePillBtn_ = new QPushButton(trText("Bản quyền", "License"), v2Header);
 licensePillBtn_->setObjectName("license-pill");
 connect(licensePillBtn_, &QPushButton::clicked, this, [this] { showLicenseDialog(); });
 hdr->addWidget(licensePillBtn_);

 // Primary Save / Export Spectrum Cobalt CTA pill
 auto *savePill = new QPushButton(trText("Lưu ▾", "Save ▾"), v2Header);
 savePill->setObjectName("header-primary");
 translations_.append([=, this] { savePill->setText(trText("Lưu ▾", "Save ▾")); });
 auto *saveDropdown = new QMenu(this);
 saveDropdown->addAction(saveAction);
 saveDropdown->addAction(saveAsAction);
 saveDropdown->addSeparator();
 saveDropdown->addMenu(exportMenu);
 savePill->setMenu(saveDropdown);
 hdr->addWidget(savePill);

 shellLayout->addWidget(v2Header);

 // ── 2. Compact Multi-Document Project Tab Strip ──
 auto *strip = new QWidget(shell);
 strip->setObjectName("document-strip");
 auto *stripLayout = new QHBoxLayout(strip);
 stripLayout->setContentsMargins(0, 0, 6, 0);
 stripLayout->setSpacing(1);
 tabs_ = new QTabBar(strip);
 tabs_->setObjectName("document-tabs");
 tabs_->setExpanding(false);
 tabs_->setTabsClosable(true);
 tabs_->setMovable(true);
 tabs_->setElideMode(Qt::ElideRight);
 tabs_->setUsesScrollButtons(true);
 stripLayout->addWidget(tabs_, 1);
 auto *addTab = new QToolButton(strip);
 addTab->setObjectName("new-document");
 addTab->setText("+");
 addTab->setAccessibleName(trText("Dự án mới", "New project"));
 stripLayout->addWidget(addTab);
 connect(addTab, &QToolButton::clicked, this, &MainWindow::newProject);
 connect(tabs_, &QTabBar::currentChanged, this, [this](int index) { if (!switching_) activateDocument(index); });
 connect(tabs_, &QTabBar::tabCloseRequested, this, &MainWindow::closeDocument);
 connect(tabs_, &QTabBar::tabMoved, this, [this](int from, int to) {
  if (switching_ || from == to) return;
  snapshotDocument();
  const auto activeId = documents_[activeDocument_].id;
  documents_.move(from, to);
  for (int i = 0; i < documents_.size(); ++i) if (documents_[i].id == activeId) activeDocument_ = i;
  syncTabs();
 });
 shellLayout->addWidget(strip);

 // ── 2b. Non-blocking In-App Update Notification Banner (Spectrum 2 Cobalt) ──
 updateBanner_ = new QFrame(shell);
 updateBanner_->setObjectName("update-banner");
 updateBanner_->setStyleSheet("QFrame#update-banner{background:#1d3ed4;border-bottom:1px solid #3b63fb;} QFrame#update-banner QLabel{color:#ffffff;border:none;background:transparent;font-size:12px;font-weight:500;}");
 auto *ubLay = new QHBoxLayout(updateBanner_);
 ubLay->setContentsMargins(14, 5, 10, 5);
 ubLay->setSpacing(10);
 updateBannerLbl_ = new QLabel(updateBanner_);
 updateBannerLbl_->setWordWrap(false);
 auto *ubDlBtn = new QPushButton(trText("Xem & Tải bản mới ↗", "View & Download ↗"), updateBanner_);
 ubDlBtn->setStyleSheet("QPushButton{background:#ffffff;color:#1d3ed4;border:none;border-radius:13px;padding:3px 11px;font-weight:600;font-size:11.5px;} QPushButton:hover{background:#eef2ff;}");
 connect(ubDlBtn, &QPushButton::clicked, this, [this] { showUpdateDialog(true); });
 auto *ubClose = new QToolButton(updateBanner_);
 ubClose->setText(QString::fromUtf8("×"));
 ubClose->setStyleSheet("QToolButton{color:#bfdbfe;border:none;font-size:15px;font-weight:700;padding:0 4px;} QToolButton:hover{color:#ffffff;}");
 connect(ubClose, &QToolButton::clicked, this, [this] { updateBannerDismissed_ = true; updateBanner_->hide(); });
 ubLay->addWidget(updateBannerLbl_, 1);
 ubLay->addWidget(ubDlBtn);
 ubLay->addWidget(ubClose);
 updateBanner_->hide();
 shellLayout->addWidget(updateBanner_);

 // ── 3. Main 3-Pane Workstation Row (Left 296px | Center Map/2D/3D | Right Studio 276px) ──
 auto *bodyRow = new QWidget(shell);
 auto *bodyLay = new QHBoxLayout(bodyRow);
 bodyLay->setContentsMargins(0, 0, 0, 0);
 bodyLay->setSpacing(0);
 shellLayout->addWidget(bodyRow, 1);

 // ── LEFT PANEL: Compact Spectrum 2 Site Inspector (304px) ──
 leftDrawer_ = new ux::AnimatedDrawer(304, bodyRow);
 leftPanel_ = leftDrawer_->contentHost();
 leftPanel_->setObjectName("left-panel");
 auto *leftWrap = new QVBoxLayout(leftPanel_);
 leftWrap->setContentsMargins(0, 0, 0, 0);
 leftWrap->setSpacing(0);

 auto *leftScroll = new QScrollArea(leftPanel_);
 leftScroll->setObjectName("left-scroll");
 leftScroll->setWidgetResizable(true);
 leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
 leftScroll->setFrameShape(QFrame::NoFrame);
 leftWrap->addWidget(leftScroll, 1);

 auto *leftBody = new QWidget(leftScroll);
 leftBody->setObjectName("left-body");
 leftBody->setMinimumWidth(0);
 auto *layout = new QVBoxLayout(leftBody);
 layout->setContentsMargins(8, 8, 8, 12);
 layout->setSpacing(8);
 leftScroll->setWidget(leftBody);
 // Lock leftBody max width strictly to the scroll viewport so no child control can ever push cards horizontally off-screen
 class SidebarWidthLock : public QObject {
 public:
  QScrollArea *scroll;
  QWidget *body;
  SidebarWidthLock(QScrollArea *s, QWidget *b) : QObject(s), scroll(s), body(b) {}
  bool eventFilter(QObject *o, QEvent *e) override {
   if (o == scroll->viewport() && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) {
    const int vw = scroll->viewport()->width();
    if (vw > 40) body->setMaximumWidth(vw);
    if (auto *hb = scroll->horizontalScrollBar()) hb->setValue(0);
   }
   return false;
  }
 };
 leftScroll->viewport()->installEventFilter(new SidebarWidthLock(leftScroll, leftBody));

 // Search bar at top of Left Panel
 auto *searchRow = new QHBoxLayout();
 searchRow->setSpacing(6);
 search_ = new QLineEdit(leftBody);
 search_->setObjectName("place-search");
 search_->setMinimumWidth(0);
 search_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 search_->setClearButtonEnabled(true);
 searchRow->addWidget(search_, 1);
 auto *searchButton = new QPushButton(leftBody);
 searchButton->setFixedWidth(48);
 translations_.append([=, this] {
  searchButton->setText(trText("Tìm", "Find"));
  search_->setPlaceholderText(trText("Địa điểm hoặc tọa độ…", "Place or lat, lon…"));
 });
 searchRow->addWidget(searchButton);
 layout->addLayout(searchRow);
 searchResults_ = new QListWidget(leftBody);
 searchResults_->setObjectName("place-results");
 searchResults_->setMaximumHeight(110);
 searchResults_->hide();
 layout->addWidget(searchResults_);

 // CARD 1: AREA
 auto *areaCard = new QFrame(leftBody);
 areaCard->setObjectName("v2-card");
 auto *areaLay = new QVBoxLayout(areaCard);
 areaLay->setContentsMargins(11, 10, 11, 11);
 areaLay->setSpacing(7);
 auto *areaHead = new QHBoxLayout();
 areaHead->addWidget(label("AREA", "AREA", "eyebrow"));
 areaHead->addStretch();
 auto *proTag = new QLabel("R ≤ 3 km", areaCard);
 proTag->setObjectName("pro-tag");
 areaHead->addWidget(proTag);
 areaLay->addLayout(areaHead);

 projectName_ = new QLabel(areaCard);
 projectName_->setObjectName("muted");
 projectName_->setWordWrap(true);
 areaLay->addWidget(projectName_);
 siteDetails_ = new QLabel(areaCard);
 siteDetails_->setObjectName("muted");
 siteDetails_->setWordWrap(true);
 areaLay->addWidget(siteDetails_);

 candidateText_ = new QLabel(areaCard);
 candidateText_->setObjectName("muted");
 candidateText_->setWordWrap(true);
 areaLay->addWidget(candidateText_);

 siteSelection_ = new SiteSelection(areaCard);
 areaLay->addWidget(siteSelection_);

 areaLay->addWidget(label("NGUỒN DỮ LIỆU", "DATA SOURCE", "section"));
 sourceChoice_ = new QComboBox(areaCard);
 sourceChoice_->setObjectName("source-choice");
 sourceChoice_->setMinimumWidth(0);
 sourceChoice_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 sourceChoice_->addItem("OpenStreetMap", "osm");
 sourceChoice_->addItem(trText("Dữ liệu đang mở", "Current project data"), "current");
 QFile sourceIndex(ga::appDataPath("sites/index.json"));
 if (sourceIndex.open(QIODevice::ReadOnly)) {
  const auto rows = QJsonDocument::fromJson(sourceIndex.readAll()).object()["entries"].toArray();
  for (const auto &v : rows) {
   const auto entry = v.toObject();
   const auto file = entry["file"].toString();
   if (QFileInfo(file).fileName() != file || !file.endsWith(".gatlas")) continue;
   sourceChoice_->addItem(entry["vi"].toString(), entry["id"].toString());
   sourceChoice_->setItemData(sourceChoice_->count() - 1, entry, Qt::UserRole + 1);
  }
 }
 areaLay->addWidget(sourceChoice_);
 sourceHint_ = new QLabel(areaCard);
 sourceHint_->setObjectName("muted");
 sourceHint_->setWordWrap(true);
 areaLay->addWidget(sourceHint_);

 refreshSource_ = new QCheckBox(areaCard);
 refreshSource_->setObjectName("refresh-osm");
 translations_.append([=, this] {
  refreshSource_->setText(trText("Đọc OSM mới, bỏ qua cache", "Refresh OSM, bypass cache"));
 });
 areaLay->addWidget(refreshSource_);

 smartHeights_ = new QCheckBox(areaCard);
 smartHeights_->setObjectName("smart-heights");
 smartHeights_->setChecked(true);
 translations_.append([=, this] {
  smartHeights_->setText(trText("Ước tính chiều cao khi OSM thiếu", "Smart height estimate (missing OSM)"));
 });
 areaLay->addWidget(smartHeights_);
 connect(smartHeights_, &QCheckBox::toggled, this, [this](bool checked) {
  if (updating_ || project_.features.isEmpty()) return;
  const int changed = checked ? ga::applySmartHeightEstimates(project_) : ga::clearSmartHeightEstimates(project_);
  if (changed > 0) {
   refreshProjectGeometry();
   markModified();
   showToast(checked
    ? trText("Đã ước tính chiều cao thông minh cho %1 công trình", "Estimated heights for %1 unrecorded buildings").arg(changed)
    : trText("Đã trả %1 công trình ước tính về dạng phẳng", "Reverted %1 estimated buildings to flat").arg(changed),
    QColor("#3b63fb"), 2400);
  }
 });

 loadProgress_ = new QProgressBar(areaCard);
 loadProgress_->setObjectName("load-progress");
 loadProgress_->setFixedHeight(6);
 loadProgress_->setTextVisible(false);
 loadProgress_->setRange(0, 0);
 loadProgress_->hide();
 areaLay->addWidget(loadProgress_);

 auto *loadRow = new QHBoxLayout();
 loadRow->setSpacing(6);
 loadButton_ = new QPushButton(areaCard);
 loadButton_->setObjectName("primary");
 loadButton_->setMinimumWidth(0);
 translations_.append([=, this] { loadButton_->setText(trText("Nạp khu đất", "Load site")); });
 loadRow->addWidget(loadButton_, 1);
 auto *resetBtn = new QPushButton(areaCard);
 resetBtn->setObjectName("secondary-pill");
 resetBtn->setMinimumWidth(0);
 translations_.append([=, this] { resetBtn->setText(trText("Đặt lại", "Reset")); });
 connect(resetBtn, &QPushButton::clicked, this, [this, reset] { reset->trigger(); });
 loadRow->addWidget(resetBtn);
 cancelButton_ = new QPushButton(areaCard);
 cancelButton_->setObjectName("source-cancel");
 cancelButton_->hide();
 translations_.append([=, this] { cancelButton_->setText(trText("Hủy", "Cancel")); });
 loadRow->addWidget(cancelButton_);
 areaLay->addLayout(loadRow);

 auto *openSample = new QPushButton(areaCard);
 openSample->setObjectName("secondary-pill");
 openSample->setMinimumWidth(0);
 translations_.append([=, this] { openSample->setText(trText("Xem bản mẫu Hoàn Kiếm ↗", "Open Hoan Kiem sample ↗")); });
 connect(openSample, &QPushButton::clicked, this, [this] { loadPath(ga::appDataPath("hoankiem.gatlas")); });
 areaLay->addWidget(openSample);

 enrichButton_ = new QPushButton(areaCard);
 enrichButton_->setObjectName("enrich-osm");
 enrichButton_->setMinimumWidth(0);
 translations_.append([=, this] { enrichButton_->setText(trText("+ Bổ sung bối cảnh OSM", "+ Enrich with OSM context")); });
 areaLay->addWidget(enrichButton_);
 layout->addWidget(areaCard);

 // CARD 2: KHU ĐẤT ĐỒ ÁN · KÍCH THƯỚC & VẼ RANH TỰ DO (Design Plot)
 auto *plotCard = new QFrame(leftBody);
 plotCard->setObjectName("v2-card");
 auto *plotLay = new QVBoxLayout(plotCard);
 plotLay->setContentsMargins(11, 10, 11, 11);
 plotLay->setSpacing(7);
 auto *plotHead = new QHBoxLayout();
 plotHead->addWidget(label("KHU ĐẤT ĐỒ ÁN", "DESIGN PLOT", "eyebrow"));
 plotHead->addStretch();
 plotStatus_ = new QLabel(trText("Chưa đặt", "Unset"), plotCard);
 plotStatus_->setObjectName("pro-tag");
 plotHead->addWidget(plotStatus_);
 plotLay->addLayout(plotHead);

 // Mode switch: Rectangle vs Freehand Polygon (Animated sliding capsule)
 auto *plotModePill = new ux::SlidingPillTrack(plotCard);
 segTracks_.append(plotModePill);
 plotModeRect_ = new QToolButton(plotModePill);
 plotModeRect_->setCheckable(true);
 plotModeRect_->setChecked(true);
 plotModeRect_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
 plotModePoly_ = new QToolButton(plotModePill);
 plotModePoly_->setCheckable(true);
 plotModePoly_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
 translations_.append([=, this] {
  plotModeRect_->setText(trText("Hình chữ nhật", "Rectangle"));
  plotModePoly_->setText(trText("Vẽ ranh tự do", "Freehand plot"));
 });
 plotModePill->addSegment(plotModeRect_, 1);
 plotModePill->addSegment(plotModePoly_, 1);
 plotLay->addWidget(plotModePill);

 // Subpanel A: Rectangle inputs
 plotRectPanel_ = new QWidget(plotCard);
 auto *prLay = new QVBoxLayout(plotRectPanel_);
 prLay->setContentsMargins(0, 0, 0, 0);
 prLay->setSpacing(6);
 auto *plotInputs = new QGridLayout();
 plotInputs->setSpacing(5);
 plotWidth_ = new QDoubleSpinBox(plotRectPanel_);
 plotWidth_->setRange(4, 1500);
 plotWidth_->setValue(50);
 plotWidth_->setDecimals(0);
 plotWidth_->setButtonSymbols(QAbstractSpinBox::NoButtons);
 plotWidth_->setMinimumWidth(0);
 plotLength_ = new QDoubleSpinBox(plotRectPanel_);
 plotLength_->setRange(4, 1500);
 plotLength_->setValue(80);
 plotLength_->setDecimals(0);
 plotLength_->setButtonSymbols(QAbstractSpinBox::NoButtons);
 plotLength_->setMinimumWidth(0);
 plotRotation_ = new QDoubleSpinBox(plotRectPanel_);
 plotRotation_->setRange(-180, 180);
 plotRotation_->setValue(0);
 plotRotation_->setDecimals(0);
 plotRotation_->setButtonSymbols(QAbstractSpinBox::NoButtons);
 plotRotation_->setMinimumWidth(0);
 plotInputs->addWidget(label("Rộng (m)", "Width (m)", "muted"), 0, 0);
 plotInputs->addWidget(label("Dài (m)", "Length (m)", "muted"), 0, 1);
 plotInputs->addWidget(label("Xoay (°)", "Rot (°)", "muted"), 0, 2);
 plotInputs->addWidget(plotWidth_, 1, 0);
 plotInputs->addWidget(plotLength_, 1, 1);
 plotInputs->addWidget(plotRotation_, 1, 2);
 prLay->addLayout(plotInputs);

 auto *plotBtns = new QHBoxLayout();
 plotBtns->setSpacing(6);
 auto *setPlotBtn = new QPushButton(plotRectPanel_);
 setPlotBtn->setProperty("accent", true);
 translations_.append([=, this] { setPlotBtn->setText(trText("Đặt khu đất", "Set plot")); });
 auto *clearPlotBtn = new QPushButton(plotRectPanel_);
 translations_.append([=, this] { clearPlotBtn->setText(trText("Xóa", "Clear")); });
 plotBtns->addWidget(setPlotBtn, 1);
 plotBtns->addWidget(clearPlotBtn);
 prLay->addLayout(plotBtns);
 plotLay->addWidget(plotRectPanel_);

 // Subpanel B: Freehand / Polygon drawing tools directly on 2D/3D viewport
 plotPolyPanel_ = new QWidget(plotCard);
 plotPolyPanel_->hide();
 auto *ppLay = new QVBoxLayout(plotPolyPanel_);
 ppLay->setContentsMargins(0, 0, 0, 0);
 ppLay->setSpacing(6);
 auto *drawRow = new QHBoxLayout();
 drawRow->setSpacing(4);
 plotDrawBtn_ = new QPushButton(plotPolyPanel_);
 plotDrawBtn_->setProperty("accent", true);
 plotFinishBtn_ = new QPushButton(plotPolyPanel_);
 plotFinishBtn_->setProperty("accent", true);
 plotFinishBtn_->hide();
 plotUndoBtn_ = new QPushButton(QString::fromUtf8("↶"), plotPolyPanel_);
 plotUndoBtn_->setFixedWidth(32);
 plotUndoBtn_->hide();
 plotCancelBtn_ = new QPushButton(QString::fromUtf8("×"), plotPolyPanel_);
 plotCancelBtn_->setFixedWidth(32);
 plotCancelBtn_->hide();
 auto *clearPolyBtn = new QPushButton(plotPolyPanel_);
 translations_.append([=, this] {
  plotDrawBtn_->setText(trText("Vẽ ranh tự do", "Draw freehand"));
  plotFinishBtn_->setText(trText("Hoàn tất", "Finish"));
  clearPolyBtn->setText(trText("Xóa", "Clear"));
 });
 drawRow->addWidget(plotDrawBtn_, 1);
 drawRow->addWidget(plotFinishBtn_, 1);
 drawRow->addWidget(plotUndoBtn_);
 drawRow->addWidget(plotCancelBtn_);
 drawRow->addWidget(clearPolyBtn);
 ppLay->addLayout(drawRow);
 plotDraftHint_ = new QLabel(plotPolyPanel_);
 plotDraftHint_->setObjectName("muted");
 plotDraftHint_->setWordWrap(true);
 translations_.append([=, this] {
  plotDraftHint_->setText(trText("Bấm chấm đỉnh trên khung 2D/3D hoặc giữ Shift + kéo chuột để vẽ nét liền. Nhấp đúp/Enter để khép.",
                                 "Click vertices on 2D/3D viewport or hold Shift + drag freehand. Double-click/Enter to close."));
 });
 ppLay->addWidget(plotDraftHint_);
 plotLay->addWidget(plotPolyPanel_);

 plotDims_ = new QCheckBox(plotCard);
 plotDims_->setChecked(true);
 translations_.append([=, this] { plotDims_->setText(trText("Hiển thị kích thước cạnh && diện tích", "Show edge dimensions && area")); });
 plotLay->addWidget(plotDims_);

 // Concept Massing Studio right inside Design Plot card!
 auto *massSep = new QFrame(plotCard);
 massSep->setFrameShape(QFrame::HLine);
 massSep->setObjectName("hairline");
 plotLay->addWidget(massSep);

 massingEnabled_ = new QCheckBox(plotCard);
 massingEnabled_->setObjectName("massing-enabled");
 translations_.append([=, this] { massingEnabled_->setText(trText("Dựng Khối công trình đề xuất (3D)", "Proposed 3D Building Massing")); });
 plotLay->addWidget(massingEnabled_);

 massingPanel_ = new QWidget(plotCard);
 massingPanel_->hide();
 auto *mpLay = new QVBoxLayout(massingPanel_);
 mpLay->setContentsMargins(0, 2, 0, 0);
 mpLay->setSpacing(6);

 auto *mGrid = new QGridLayout();
 mGrid->setSpacing(5);
 auto mkD = [&](double lo, double hi, double val, int dec, const QString &suf) {
  auto *s = new QDoubleSpinBox(massingPanel_);
  s->setRange(lo, hi); s->setValue(val); s->setDecimals(dec); s->setSuffix(suf);
  s->setButtonSymbols(QAbstractSpinBox::NoButtons); s->setMinimumWidth(0);
  return s;
 };
 auto mkI = [&](int lo, int hi, int val, const QString &suf) {
  auto *s = new QSpinBox(massingPanel_);
  s->setRange(lo, hi); s->setValue(val); s->setSuffix(suf);
  s->setButtonSymbols(QAbstractSpinBox::NoButtons); s->setMinimumWidth(0);
  return s;
 };
 massSetback_ = mkD(0, 40, 4.0, 1, " m");
 massPodFloors_ = mkI(0, 15, 3, " T");
 massTowFloors_ = mkI(0, 80, 12, " T");
 massTowCov_ = mkI(15, 100, 45, "%");
 massPodH_ = mkD(2.8, 7.0, 4.2, 1, " m");
 massTowH_ = mkD(2.6, 6.0, 3.4, 1, " m");
 massTowRot_ = mkD(-90, 90, 0.0, 0, "°");

 mGrid->addWidget(label("Lùi ranh", "Setback", "muted"), 0, 0);
 mGrid->addWidget(label("Tầng đế", "Podium", "muted"), 0, 1);
 mGrid->addWidget(label("Tầng tháp", "Tower", "muted"), 0, 2);
 mGrid->addWidget(massSetback_, 1, 0);
 mGrid->addWidget(massPodFloors_, 1, 1);
 mGrid->addWidget(massTowFloors_, 1, 2);

 mGrid->addWidget(label("Tỷ lệ tháp", "Tower %", "muted"), 2, 0);
 mGrid->addWidget(label("Cao đế", "Pod H", "muted"), 2, 1);
 mGrid->addWidget(label("Cao tháp", "Tow H", "muted"), 2, 2);
 mGrid->addWidget(massTowCov_, 3, 0);
 mGrid->addWidget(massPodH_, 3, 1);
 mGrid->addWidget(massTowH_, 3, 2);
 mpLay->addLayout(mGrid);

 // Accent color presets + Tower rotation row
 auto *mToneRow = new QHBoxLayout();
 mToneRow->setSpacing(5);
 mToneRow->addWidget(label("Xoay tháp", "Rot", "muted"));
 massTowRot_->setFixedWidth(54);
 mToneRow->addWidget(massTowRot_);
 mToneRow->addStretch(1);
 for (const char *hex : {"#d2643c", "#dfd6c7", "#3b82f6", "#b8864b"}) {
  auto *sw = new QPushButton(massingPanel_);
  sw->setFixedSize(20, 20);
  sw->setToolTip(QString::fromLatin1(hex));
  sw->setStyleSheet(QString("background:%1;border-radius:10px;border:1.5px solid rgba(128,128,128,0.45);padding:0;min-height:0;").arg(hex));
  connect(sw, &QPushButton::clicked, this, [this, c = QColor(hex)] {
   pushUndoState();
   massing_.accentColor = c;
   applyMassingFromUi();
  });
  mToneRow->addWidget(sw);
 }
 mpLay->addLayout(mToneRow);

 massingKpi_ = new QLabel(massingPanel_);
 massingKpi_->setObjectName("muted");
 massingKpi_->setWordWrap(true);
 mpLay->addWidget(massingKpi_);
 plotLay->addWidget(massingPanel_);

 connect(massingEnabled_, &QCheckBox::toggled, this, [this](bool on) {
  if (updating_) return;
  if (on && !requireProFeature("Dựng khối công trình đề xuất 3D (Massing FAR / BCR)", "Proposed 3D Building Massing (FAR / BCR)")) {
   const QSignalBlocker sb(massingEnabled_);
   massingEnabled_->setChecked(false);
   return;
  }
  pushUndoState();
  massing_.enabled = on;
  if (on && !plot_.active) {
   plot_.active = true;
   plot_.isPolygon = false;
   plot_.width = plotWidth_->value();
   plot_.length = plotLength_->value();
   plot_.rotationDeg = plotRotation_->value();
   viewport_->setPlot(plot_);
   syncPlotUi();
  }
  if (on && (geographic_ || viewport_->camera().mode == "2d")) {
   auto c = viewport_->camera();
   c.mode = "3d";
   c.exploded = false;
   showArchitecturalView(c);
  }
  applyMassingFromUi();
  if (on) showToast(trText("Đã dựng khối công trình đề xuất · tự tính BCR / FAR / bóng đổ 3D", "Proposed 3D massing active · live BCR / FAR / shadows"), QColor("#d2643c"), 2600);
 });
 for (auto *ds : {massSetback_, massPodH_, massTowH_, massTowRot_}) {
  connect(ds, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) applyMassingFromUi(); });
 }
 for (auto *is : {massPodFloors_, massTowFloors_, massTowCov_}) {
  connect(is, &QSpinBox::valueChanged, this, [this] { if (!updating_) applyMassingFromUi(); });
 }

 connect(plotModeRect_, &QToolButton::clicked, this, [this] {
  plotModeRect_->setChecked(true);
  plotModePoly_->setChecked(false);
  plotRectPanel_->setVisible(true);
  plotPolyPanel_->setVisible(false);
  if (viewport_ && viewport_->isDrawingPlot()) viewport_->cancelPlotDrawing();
 });
 connect(plotModePoly_, &QToolButton::clicked, this, [this] {
  plotModeRect_->setChecked(false);
  plotModePoly_->setChecked(true);
  plotRectPanel_->setVisible(false);
  plotPolyPanel_->setVisible(true);
 });
 connect(setPlotBtn, &QPushButton::clicked, this, [this] { plot_.isPolygon = false; applyPlotFromUi(true); });
 connect(clearPlotBtn, &QPushButton::clicked, this, [this] { applyPlotFromUi(false); });
 connect(clearPolyBtn, &QPushButton::clicked, this, [this] { if (viewport_) viewport_->cancelPlotDrawing(); applyPlotFromUi(false); });
 connect(plotDrawBtn_, &QPushButton::clicked, this, [this] {
  if (geographic_) showArchitecturalView(viewport_->camera());
  viewport_->beginPlotDrawing();
 });
 connect(plotFinishBtn_, &QPushButton::clicked, this, [this] { if (viewport_) viewport_->finishPlotDrawing(); });
 connect(plotUndoBtn_, &QPushButton::clicked, this, [this] { if (viewport_) viewport_->undoPlotVertex(); });
 connect(plotCancelBtn_, &QPushButton::clicked, this, [this] { if (viewport_) viewport_->cancelPlotDrawing(); });
 connect(plotDims_, &QCheckBox::toggled, this, [this] { if (!updating_ && plot_.active) applyPlotFromUi(true); });
 layout->addWidget(plotCard);

 // CARD 2A: MÔ HÌNH 3D NHẬP (SKETCHUP .SKP / .DAE / .OBJ / .GLB / .STL)
 auto *m3dCard = new QFrame(leftBody);
 m3dCard->setObjectName("v2-card");
 auto *m3dLay = new QVBoxLayout(m3dCard);
 m3dLay->setContentsMargins(11, 10, 11, 11);
 m3dLay->setSpacing(6);
 auto *m3dHead = new QHBoxLayout();
 m3dHead->addWidget(label("MÔ HÌNH 3D NHẬP (SKETCHUP)", "IMPORT 3D MODEL (SKETCHUP)", "eyebrow"));
 m3dHead->addStretch();
 auto *m3dTag = new QLabel(ga::sketchUpDirectImportAvailable() ? "SKP · 3D" : "DAE · OBJ · GLB", m3dCard);
 m3dTag->setObjectName("pro-tag");
 m3dHead->addWidget(m3dTag);
 m3dLay->addLayout(m3dHead);

 auto *import3dBtn = new QPushButton(m3dCard);
 import3dBtn->setObjectName("primary-btn");
 import3dBtn->setMinimumWidth(0);
 import3dBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 translations_.append([=, this] {
  import3dBtn->setText(trText("+ Nhập mô hình SketchUp / 3D…", "+ Import SketchUp / 3D Model…"));
 });
 connect(import3dBtn, &QPushButton::clicked, this, &MainWindow::choose3DModelFile);
 m3dLay->addWidget(import3dBtn);

 auto *m3dTip = new QLabel(m3dCard);
 m3dTip->setObjectName("muted");
 m3dTip->setWordWrap(true);
 translations_.append([=, this] {
  m3dTip->setText(ga::sketchUpDirectImportAvailable()
   ? trText("Hỗ trợ trực tiếp .skp (SketchUp), .dae, .obj, .glb, .stl — kéo thả thẳng file vào cửa sổ.",
            "Direct .skp (SketchUp), .dae, .obj, .glb, .stl — or drag & drop files onto window.")
   : trText("Hỗ trợ .dae (SketchUp COLLADA), .obj, .glb, .stl — kéo thả thẳng file vào cửa sổ.",
            "Supports .dae (SketchUp COLLADA), .obj, .glb, .stl — or drag & drop files onto window."));
 });
 m3dLay->addWidget(m3dTip);

 model3dList_ = new QListWidget(m3dCard);
 model3dList_->setObjectName("import-list");
 model3dList_->setMaximumHeight(96);
 model3dList_->hide();
 m3dLay->addWidget(model3dList_);

 model3dEditor_ = new QWidget(m3dCard);
 model3dEditor_->hide();
 auto *meLay = new QVBoxLayout(model3dEditor_);
 meLay->setContentsMargins(0, 2, 0, 0);
 meLay->setSpacing(5);

 model3dMeta_ = new QLabel(model3dEditor_);
 model3dMeta_->setObjectName("muted");
 model3dMeta_->setWordWrap(true);
 meLay->addWidget(model3dMeta_);

 auto mkM3dSpin = [&](double lo, double hi, double step, int dec, const QString &suf) {
  auto *s = new QDoubleSpinBox(model3dEditor_);
  s->setRange(lo, hi);
  s->setSingleStep(step);
  s->setDecimals(dec);
  if (!suf.isEmpty()) s->setSuffix(suf);
  s->setFixedHeight(26);
  return s;
 };
 auto *xyzGrid = new QGridLayout();
 xyzGrid->setSpacing(4);
 m3dX_ = mkM3dSpin(-2500.0, 2500.0, 2.0, 1, " m");
 m3dY_ = mkM3dSpin(-2500.0, 2500.0, 2.0, 1, " m");
 m3dZ_ = mkM3dSpin(-50.0, 250.0, 0.5, 1, " m");
 m3dYaw_ = mkM3dSpin(-180.0, 180.0, 5.0, 1, "°");
 m3dScale_ = mkM3dSpin(0.0001, 1000.0, 0.1, 4, "×");
 m3dUnitPreset_ = new QComboBox(model3dEditor_);
 m3dUnitPreset_->setFixedHeight(26);
 m3dUnitPreset_->setMinimumWidth(0);
 m3dUnitPreset_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 m3dUnitPreset_->addItem("1× (m)", 1.0);
 m3dUnitPreset_->addItem("0,001× (mm)", 0.001);
 m3dUnitPreset_->addItem("0,01× (cm)", 0.01);
 m3dUnitPreset_->addItem("0,0254× (in)", 0.0254);
 xyzGrid->addWidget(label("X (Đông)", "X (East)", "muted"), 0, 0);
 xyzGrid->addWidget(label("Y (Bắc)", "Y (North)", "muted"), 0, 1);
 xyzGrid->addWidget(m3dX_, 1, 0);
 xyzGrid->addWidget(m3dY_, 1, 1);
 xyzGrid->addWidget(label("Cao Z", "Elev Z", "muted"), 2, 0);
 xyzGrid->addWidget(label("Xoay (°)", "Rotate (°)", "muted"), 2, 1);
 xyzGrid->addWidget(m3dZ_, 3, 0);
 xyzGrid->addWidget(m3dYaw_, 3, 1);
 xyzGrid->addWidget(label("Tỉ lệ", "Scale", "muted"), 4, 0);
 xyzGrid->addWidget(label("Đổi đơn vị", "Unit preset", "muted"), 4, 1);
 xyzGrid->addWidget(m3dScale_, 5, 0);
 xyzGrid->addWidget(m3dUnitPreset_, 5, 1);
 meLay->addLayout(xyzGrid);

 m3dFlipYUp_ = new QCheckBox(model3dEditor_);
 translations_.append([=, this] { m3dFlipYUp_->setText(trText("Đảo trục đứng (Y-up ↔ Z-up)", "Flip vertical axis (Y-up ↔ Z-up)")); });
 meLay->addWidget(m3dFlipYUp_);

 m3dOverrideColor_ = new QCheckBox(model3dEditor_);
 translations_.append([=, this] { m3dOverrideColor_->setText(trText("Tô màu mô hình kiến trúc (Maquette)", "Override with Maquette color")); });
 meLay->addWidget(m3dOverrideColor_);

 auto *m3dToneRow = new QHBoxLayout();
 m3dToneRow->setSpacing(5);
 for (const char *hex : {"#f0ece4", "#d2643c", "#c9a679", "#3f4246"}) {
  auto *sw = new QPushButton(model3dEditor_);
  sw->setFixedSize(20, 20);
  sw->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid rgba(0,0,0,0.22); border-radius: 5px; }").arg(hex));
  connect(sw, &QPushButton::clicked, this, [this, c = QColor(hex)] {
   const int idx = model3dList_ ? model3dList_->currentRow() : -1;
   if (idx < 0 || idx >= models3d_.size()) return;
   models3d_[idx].tintColor = c;
   models3d_[idx].overrideColor = true;
   if (m3dOverrideColor_) { const QSignalBlocker b(m3dOverrideColor_); m3dOverrideColor_->setChecked(true); }
   applySelectedModel3dFromUi();
  });
  m3dToneRow->addWidget(sw);
 }
 m3dToneRow->addStretch();
 meLay->addLayout(m3dToneRow);

 auto *m3dActRow1 = new QHBoxLayout();
 m3dActRow1->setSpacing(4);
 m3dPlaceBtn_ = new QPushButton(model3dEditor_);
 m3dPlaceBtn_->setObjectName("quick-chip");
 m3dPlaceBtn_->setCheckable(true);
 m3dSnapPlotBtn_ = new QPushButton(model3dEditor_);
 m3dSnapPlotBtn_->setObjectName("quick-chip");
 translations_.append([=, this] {
  m3dPlaceBtn_->setText(trText("Kéo đặt trên bản đồ", "Drag on viewport"));
  m3dSnapPlotBtn_->setText(trText("Đặt tâm khu đất", "Snap to plot"));
 });
 m3dActRow1->addWidget(m3dPlaceBtn_, 1);
 m3dActRow1->addWidget(m3dSnapPlotBtn_, 1);
 meLay->addLayout(m3dActRow1);

 auto *m3dActRow2 = new QHBoxLayout();
 m3dActRow2->setSpacing(4);
 m3dFocusBtn_ = new QPushButton(model3dEditor_);
 m3dFocusBtn_->setObjectName("quick-chip");
 m3dRemoveBtn_ = new QPushButton(model3dEditor_);
 m3dRemoveBtn_->setObjectName("quick-chip");
 translations_.append([=, this] {
  m3dFocusBtn_->setText(trText("Phóng tới mô hình", "Focus model"));
  m3dRemoveBtn_->setText(trText("Xóa mô hình", "Remove"));
 });
 m3dActRow2->addWidget(m3dFocusBtn_, 1);
 m3dActRow2->addWidget(m3dRemoveBtn_, 1);
 meLay->addLayout(m3dActRow2);
 m3dLay->addWidget(model3dEditor_);

 connect(model3dList_, &QListWidget::currentRowChanged, this, [this](int row) {
  if (updating_) return;
  viewport_->setSelectedModel3d(row);
  syncModels3dUi();
 });
 connect(model3dList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
  if (updating_ || !item) return;
  const int row = model3dList_->row(item);
  if (row >= 0 && row < models3d_.size()) {
   models3d_[row].visible = (item->checkState() == Qt::Checked);
   viewport_->setModels3d(models3d_);
   if (viewport_->isSectionCutActive()) updateLiveSection();
   markModified();
  }
 });
 for (auto *ds : {m3dX_, m3dY_, m3dZ_, m3dYaw_, m3dScale_}) {
  connect(ds, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) applySelectedModel3dFromUi(); });
 }
 connect(m3dUnitPreset_, &QComboBox::activated, this, [this](int idx) {
  if (idx < 0 || !m3dScale_) return;
  m3dScale_->setValue(m3dUnitPreset_->itemData(idx).toDouble());
 });
 connect(m3dFlipYUp_, &QCheckBox::toggled, this, [this] { if (!updating_) applySelectedModel3dFromUi(); });
 connect(m3dOverrideColor_, &QCheckBox::toggled, this, [this] { if (!updating_) applySelectedModel3dFromUi(); });
 connect(m3dPlaceBtn_, &QPushButton::toggled, this, [this](bool on) {
  if (updating_) return;
  if (on && geographic_) showArchitecturalView(viewport_->camera());
  viewport_->setPlacingModel3d(on);
 });
 connect(m3dSnapPlotBtn_, &QPushButton::clicked, this, [this] {
  const int row = model3dList_ ? model3dList_->currentRow() : -1;
  if (row < 0 || row >= models3d_.size()) return;
  if (plot_.active) {
   const QPointF cen = plot_.polygon().boundingRect().center();
   models3d_[row].x = cen.x();
   models3d_[row].y = cen.y();
   if (!plot_.isPolygon) models3d_[row].yawDeg = plot_.rotationDeg;
  } else {
   models3d_[row].x = 0.0;
   models3d_[row].y = 0.0;
  }
  viewport_->setModels3d(models3d_);
  syncModels3dUi();
  if (viewport_->isSectionCutActive()) updateLiveSection();
  markModified();
  showToast(trText("Đã đặt mô hình vào tâm khu đất", "Snapped 3D model to site plot center"), QColor("#d97706"), 2000);
 });
 connect(m3dFocusBtn_, &QPushButton::clicked, this, [this] {
  const int row = model3dList_ ? model3dList_->currentRow() : -1;
  if (row < 0 || row >= models3d_.size()) return;
  if (geographic_) showArchitecturalView(viewport_->camera());
  viewport_->focusModel3d(row);
 });
 connect(m3dRemoveBtn_, &QPushButton::clicked, this, &MainWindow::removeSelectedModel3d);
 layout->addWidget(m3dCard);

 // CARD 2A-bis: LỚP QUY HOẠCH SỬ DỤNG ĐẤT HÀ NỘI (quyhoach.hanoi.vn — 1-CLICK BẬT / TẮT ĐÈ LÊN BẢN ĐỒ, 2D & 3D)
 auto *planCard = new QFrame(leftBody);
 planCard->setObjectName("v2-card");
 auto *plCardLay = new QVBoxLayout(planCard);
 plCardLay->setContentsMargins(11, 10, 11, 11);
 plCardLay->setSpacing(6);
 auto *plHead = new QHBoxLayout();
 plHead->addWidget(label("QUY HOẠCH HÀ NỘI", "HANOI LAND-USE PLAN", "eyebrow"));
 plHead->addStretch();
 auto *plTag = new QLabel("quyhoach.hanoi.vn", planCard);
 plTag->setObjectName("pro-tag");
 plHead->addWidget(plTag);
 plCardLay->addLayout(plHead);

 hanoiPlanToggleBtn_ = new QPushButton(planCard);
 hanoiPlanToggleBtn_->setObjectName("hanoi-plan-toggle");
 hanoiPlanToggleBtn_->setCheckable(true);
 hanoiPlanToggleBtn_->setMinimumHeight(32);
 hanoiPlanToggleBtn_->setMinimumWidth(0);
 hanoiPlanToggleBtn_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 translations_.append([=, this] {
  hanoiPlanToggleBtn_->setText(hanoiPlanEnabled_
   ? trText("✓ Đang đè lớp Quy hoạch Hà Nội", "✓ Hanoi Land-Use Layer Active")
   : trText("Bật lớp Quy hoạch Hà Nội", "Overlay Hanoi Land-Use Layer"));
 });
 connect(hanoiPlanToggleBtn_, &QPushButton::clicked, this, [this](bool on) {
  setHanoiPlanning(on);
 });
 plCardLay->addWidget(hanoiPlanToggleBtn_);

 auto *hqOpRow = new QHBoxLayout();
 hqOpRow->setSpacing(6);
 hqOpRow->addWidget(label("Độ mờ", "Opacity", "muted"));
 hanoiPlanOpacitySlider_ = new QSlider(Qt::Horizontal, planCard);
 hanoiPlanOpacitySlider_->setObjectName("hanoi-plan-opacity");
 hanoiPlanOpacitySlider_->setRange(15, 100);
 hanoiPlanOpacitySlider_->setValue(78);
 hqOpRow->addWidget(hanoiPlanOpacitySlider_, 1);
 hanoiPlanOpacityLbl_ = new QLabel("78%", planCard);
 hanoiPlanOpacityLbl_->setObjectName("muted");
 hanoiPlanOpacityLbl_->setMinimumWidth(32);
 hanoiPlanOpacityLbl_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
 hqOpRow->addWidget(hanoiPlanOpacityLbl_);
 plCardLay->addLayout(hqOpRow);
 connect(hanoiPlanOpacitySlider_, &QSlider::valueChanged, this, [this](int val) {
  if (hanoiPlanOpacityLbl_) hanoiPlanOpacityLbl_->setText(QString::number(val) + "%");
  if (!updating_) setHanoiPlanning(hanoiPlanEnabled_, hanoiPlanLayer_, val / 100.0);
 });

 hanoiPlanLayerCombo_ = new QComboBox(planCard);
 hanoiPlanLayerCombo_->setObjectName("hanoi-plan-layer");
 hanoiPlanLayerCombo_->setFixedHeight(26);
 hanoiPlanLayerCombo_->setMinimumWidth(0);
 hanoiPlanLayerCombo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 for (const auto &info : ga::hanoiPlanningLayers()) {
  hanoiPlanLayerCombo_->addItem(info.vi, info.id);
 }
 connect(hanoiPlanLayerCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
  if (updating_ || idx < 0) return;
  const QString lid = hanoiPlanLayerCombo_->itemData(idx).toString();
  setHanoiPlanning(true, lid, hanoiPlanOpacity_);
 });
 plCardLay->addWidget(hanoiPlanLayerCombo_);

 hanoiPlanStatusLbl_ = new QLabel(planCard);
 hanoiPlanStatusLbl_->setObjectName("muted");
 hanoiPlanStatusLbl_->setWordWrap(true);
 plCardLay->addWidget(hanoiPlanStatusLbl_);

 // Optional Advanced Tools (DXF VN-2000 / Image Overlay / QHC 2045) collapsed cleanly by default
 auto *advToggle = new QPushButton(planCard);
 advToggle->setObjectName("quick-chip");
 advToggle->setCheckable(true);
 advToggle->setChecked(false);
 advToggle->setMinimumWidth(0);
 advToggle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 translations_.append([=, this] {
  advToggle->setText(advToggle->isChecked()
   ? trText("Ẩn công cụ nâng cao (DXF / Ảnh) ▴", "Hide advanced (DXF / Image) ▴")
   : trText("Công cụ nâng cao (DXF / Ảnh phủ) ▾", "Advanced (DXF / Image overlay) ▾"));
 });
 plCardLay->addWidget(advToggle);

 planAdvPanel_ = new QWidget(planCard);
 planAdvPanel_->hide();
 connect(advToggle, &QPushButton::toggled, this, [=, this](bool open) {
  planAdvPanel_->setVisible(open);
  advToggle->setText(open
   ? trText("Ẩn công cụ nâng cao (DXF / Ảnh) ▴", "Hide advanced (DXF / Image) ▴")
   : trText("Công cụ nâng cao (DXF / Ảnh phủ) ▾", "Advanced (DXF / Image overlay) ▾"));
 });
 auto *advLay = new QVBoxLayout(planAdvPanel_);
 advLay->setContentsMargins(0, 4, 0, 0);
 advLay->setSpacing(5);

 // Open Commercial Custom XYZ Planning Tile URL configuration
 advLay->addWidget(label("NGUỒN TILE QUY HOẠCH TÙY CHỈNH (XYZ URL)", "CUSTOM PLANNING XYZ TILE URL", "muted"));
 auto *xyzRow = new QHBoxLayout();
 xyzRow->setSpacing(4);
 customXyzEdit_ = new QLineEdit(planAdvPanel_);
 customXyzEdit_->setObjectName("custom-xyz-url");
 customXyzEdit_->setMinimumWidth(0);
 customXyzEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 customXyzEdit_->setPlaceholderText(QStringLiteral("https://.../{z}/{x}/{y}.png"));
 auto *xyzApply = new QPushButton(trText("Đè URL", "Apply"), planAdvPanel_);
 xyzApply->setObjectName("quick-chip");
 connect(xyzApply, &QPushButton::clicked, this, [this] {
  const QString u = customXyzEdit_->text().trimmed();
  ga::setCustomPlanningTileTemplate(u);
  if (ga::customPlanningTileTemplate().isEmpty()) {
   showToast(trText("URL cần dạng https://.../{z}/{x}/{y}.png", "URL must be https://.../{z}/{x}/{y}.png"), QColor("#dc2626"), 2800);
   return;
  }
  setHanoiPlanning(true, QStringLiteral("custom"), hanoiPlanOpacity_);
  showToast(trText("Đã áp dụng nguồn lớp quy hoạch XYZ tùy chỉnh", "Applied custom XYZ planning tile layer"), QColor("#16a34a"), 2400);
 });
 xyzRow->addWidget(customXyzEdit_, 1);
 xyzRow->addWidget(xyzApply);
 advLay->addLayout(xyzRow);

 planSubdivLbl_ = new QLabel(planAdvPanel_);
 planSubdivLbl_->setObjectName("muted");
 planSubdivLbl_->setWordWrap(true);
 advLay->addWidget(planSubdivLbl_);

 auto *plGrid = new QGridLayout();
 plGrid->setSpacing(4);
 struct PlanBtnDef { const char *id; const char *vi; const char *en; };
 const QVector<PlanBtnDef> planDefs = {
  {"qhc-2045", "QHC 2045 (1/25k)", "QHC 2045 (1:25k)"},
  {"qhpk-2000", "QHPK TT04 (1/2k)", "Zoning (1:2k)"},
  {"qhct-500", "QHCT 3D (1/500)", "3D Envelope (1:500)"},
  {"compare", "Đối chiếu HT↔QH", "Compare Diff"},
 };
 planLevelBtns_.clear();
 for (int i = 0; i < planDefs.size(); ++i) {
  auto *b = new QPushButton(planAdvPanel_);
  b->setObjectName("quick-chip");
  b->setCheckable(true);
  b->setProperty("planId", QString::fromLatin1(planDefs[i].id));
  translations_.append([=, this] { b->setText(trText(planDefs[i].vi, planDefs[i].en)); });
  connect(b, &QPushButton::clicked, this, [this, pid = QString::fromLatin1(planDefs[i].id)] {
   applyFuturePlanning(activePlanningLevel_ == pid ? QString() : pid);
  });
  planLevelBtns_.append(b);
  plGrid->addWidget(b, i / 2, i % 2);
 }
 advLay->addLayout(plGrid);

 auto *plOpRow = new QHBoxLayout();
 plOpRow->setSpacing(6);
 plOpRow->addWidget(label("Độ đậm", "Opacity", "muted"));
 planOpacitySlider_ = new QSlider(Qt::Horizontal, planAdvPanel_);
 planOpacitySlider_->setRange(15, 100);
 planOpacitySlider_->setValue(80);
 connect(planOpacitySlider_, &QSlider::valueChanged, this, [this](int val) {
  planningOpacity_ = val / 100.0;
  if (!activePlanningLevel_.isEmpty()) applyFuturePlanning(activePlanningLevel_);
 });
 plOpRow->addWidget(planOpacitySlider_, 1);
 auto *plOffBtn = new QPushButton(planAdvPanel_);
 plOffBtn->setObjectName("quick-chip");
 translations_.append([=, this] { plOffBtn->setText(trText("Tắt", "Off")); });
 connect(plOffBtn, &QPushButton::clicked, this, [this] { applyFuturePlanning(QString()); });
 plOpRow->addWidget(plOffBtn);
 advLay->addLayout(plOpRow);

 planCrsCombo_ = new QComboBox(planAdvPanel_);
 planCrsCombo_->setFixedHeight(26);
 planCrsCombo_->setMinimumWidth(0);
 planCrsCombo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 planCrsCombo_->addItem("Tự động nhận hệ tọa độ", int(ga::PlanningCrs::Auto));
 planCrsCombo_->addItem("VN-2000 Hà Nội 3° (105°E)", int(ga::PlanningCrs::Vn2000Hanoi3));
 planCrsCombo_->addItem("VN-2000 Toàn quốc 6° (105°E)", int(ga::PlanningCrs::Vn2000National6));
 planCrsCombo_->addItem("WGS84 (Kinh / Vĩ độ)", int(ga::PlanningCrs::Wgs84));
 planCrsCombo_->addItem("Tọa độ cục bộ khu đất (m)", int(ga::PlanningCrs::SiteLocal));
 advLay->addWidget(planCrsCombo_);

 auto *plImpRow = new QHBoxLayout();
 plImpRow->setSpacing(4);
 auto *plImpBtn = new QPushButton(planAdvPanel_);
 plImpBtn->setObjectName("quick-chip");
 translations_.append([=, this] { plImpBtn->setText(trText("+ Nhập DXF/GeoJSON…", "+ Import DXF/GeoJSON…")); });
 connect(plImpBtn, &QPushButton::clicked, this, &MainWindow::choosePlanningVectorFile);
 plImpRow->addWidget(plImpBtn, 1);
 planClearCustomBtn_ = new QPushButton(planAdvPanel_);
 planClearCustomBtn_->setObjectName("quick-chip");
 planClearCustomBtn_->hide();
 translations_.append([=, this] { planClearCustomBtn_->setText(trText("Xóa", "Clear")); });
 connect(planClearCustomBtn_, &QPushButton::clicked, this, [this] {
  planningCustom_ = {};
  syncPlanningUi();
  if (!activePlanningLevel_.isEmpty()) applyFuturePlanning(activePlanningLevel_);
 });
 plImpRow->addWidget(planClearCustomBtn_);
 advLay->addLayout(plImpRow);

 planCustomLbl_ = new QLabel(planAdvPanel_);
 planCustomLbl_->setObjectName("muted");
 planCustomLbl_->setWordWrap(true);
 planCustomLbl_->hide();
 advLay->addWidget(planCustomLbl_);

 auto *plImgRow = new QHBoxLayout();
 plImgRow->setSpacing(4);
 auto *plImgBtn = new QPushButton(planAdvPanel_);
 plImgBtn->setObjectName("quick-chip");
 translations_.append([=, this] { plImgBtn->setText(trText("+ Phủ ảnh QH…", "+ Overlay Image…")); });
 connect(plImgBtn, &QPushButton::clicked, this, &MainWindow::choosePlanningImageFile);
 plImgRow->addWidget(plImgBtn, 1);
 auto *plRefBtn = new QPushButton(planAdvPanel_);
 plRefBtn->setObjectName("quick-chip");
 translations_.append([=, this] { plRefBtn->setText(trText("Nguồn QH ↗", "Sources ↗")); });
 connect(plRefBtn, &QPushButton::clicked, this, &MainWindow::showHanoiPlanningSources);
 plImgRow->addWidget(plRefBtn);
 advLay->addLayout(plImgRow);

 planImgPanel_ = new QWidget(planAdvPanel_);
 planImgPanel_->hide();
 auto *pipLay = new QVBoxLayout(planImgPanel_);
 pipLay->setContentsMargins(0, 2, 0, 0);
 pipLay->setSpacing(4);
 auto *piOpRow = new QHBoxLayout();
 piOpRow->addWidget(label("Mờ ảnh phủ", "Img opacity", "muted"));
 planImgOpacity_ = new QSlider(Qt::Horizontal, planImgPanel_);
 planImgOpacity_->setRange(10, 100);
 planImgOpacity_->setValue(56);
 piOpRow->addWidget(planImgOpacity_, 1);
 pipLay->addLayout(piOpRow);

 auto mkPiSpin = [&](double lo, double hi, double step, const QString &suf) {
  auto *s = new QDoubleSpinBox(planImgPanel_);
  s->setRange(lo, hi); s->setSingleStep(step); s->setDecimals(1); s->setSuffix(suf); s->setFixedHeight(25);
  return s;
 };
 auto *piGrid = new QGridLayout();
 piGrid->setSpacing(4);
 planImgWidth_ = mkPiSpin(50.0, 12000.0, 25.0, " m");
 planImgRot_ = mkPiSpin(-180.0, 180.0, 1.0, "°");
 planImgX_ = mkPiSpin(-3000.0, 3000.0, 10.0, " m");
 planImgY_ = mkPiSpin(-3000.0, 3000.0, 10.0, " m");
 piGrid->addWidget(label("Rộng ảnh", "Width", "muted"), 0, 0);
 piGrid->addWidget(label("Xoay (°)", "Rotate", "muted"), 0, 1);
 piGrid->addWidget(planImgWidth_, 1, 0);
 piGrid->addWidget(planImgRot_, 1, 1);
 piGrid->addWidget(label("Dịch X", "Shift X", "muted"), 2, 0);
 piGrid->addWidget(label("Dịch Y", "Shift Y", "muted"), 2, 1);
 piGrid->addWidget(planImgX_, 3, 0);
 piGrid->addWidget(planImgY_, 3, 1);
 pipLay->addLayout(piGrid);

 auto *piHideBtn = new QPushButton(planImgPanel_);
 piHideBtn->setObjectName("quick-chip");
 translations_.append([=, this] { piHideBtn->setText(trText("Gỡ ảnh bản đồ quy hoạch", "Remove planning map image")); });
 connect(piHideBtn, &QPushButton::clicked, this, [this] {
  planningImage_.enabled = false;
  planningImage_.image = {};
  planningImage_.imagePath.clear();
  viewport_->setPlanningImageOverlay(planningImage_);
  syncPlanningUi();
  markModified();
 });
 pipLay->addWidget(piHideBtn);
 advLay->addWidget(planImgPanel_);

 auto updatePlanImg = [this] {
  if (updating_ || !planningImage_.enabled) return;
  planningImage_.opacity = planImgOpacity_->value() / 100.0;
  planningImage_.widthM = planImgWidth_->value();
  planningImage_.rotationDeg = planImgRot_->value();
  planningImage_.centerX = planImgX_->value();
  planningImage_.centerY = planImgY_->value();
  viewport_->setPlanningImageOverlay(planningImage_);
  markModified();
 };
 connect(planImgOpacity_, &QSlider::valueChanged, this, updatePlanImg);
 for (auto *sp : {planImgWidth_, planImgRot_, planImgX_, planImgY_}) {
  connect(sp, &QDoubleSpinBox::valueChanged, this, updatePlanImg);
 }
 plCardLay->addWidget(planAdvPanel_);
 layout->addWidget(planCard);

 // CARD 2B: BỘ SƠ ĐỒ NHANH (Quick Diagrams right on the left panel too!)
 auto *quickDiagCard = new QFrame(leftBody);
 quickDiagCard->setObjectName("v2-card");
 auto *qdLay = new QVBoxLayout(quickDiagCard);
 qdLay->setContentsMargins(11, 10, 11, 11);
 qdLay->setSpacing(6);
 auto *qdHead = new QHBoxLayout();
 qdHead->addWidget(label("SƠ ĐỒ NHANH", "QUICK DIAGRAMS", "eyebrow"));
 qdHead->addStretch();
 auto *qdCount = new QLabel("69", quickDiagCard);
 qdCount->setObjectName("pro-tag");
 qdHead->addWidget(qdCount);
 qdLay->addLayout(qdHead);
 auto *qdGrid = new QGridLayout();
 qdGrid->setSpacing(4);
 const QVector<QStringList> quickRows = {
  {"context", "Cơ bản CAD", "Base CAD"}, {"figure", "Đặc – Rỗng", "Figure-Grd"},
  {"height", "Chiều cao", "Heights"}, {"roads", "Giao thông", "Mobility"},
  {"analytic:sunpath-2d", "Đường nắng", "Sun-Path"}, {"analytic:sunlight-hours", "Giờ nắng", "Sunlight"},
  {"analytic:wind-flow", "Luồng gió", "Wind 2D"}, {"analytic:walkshed", "Đi bộ 15p", "Walkshed"},
  {"env:gedtm30", "Cao độ DEM", "DEM Elev"}, {"env:flood", "Ngập RP100", "Flood RP100"},
  {"env:sentinel", "NDVI Cây", "NDVI Veg"}, {"env:landsat", "Nhiệt LST", "Heat LST"},
 };
 for (int qi = 0; qi < quickRows.size(); ++qi) {
  auto *qb = new QPushButton(quickDiagCard);
  qb->setObjectName("quick-chip");
  qb->setMinimumWidth(0);
  translations_.append([=, this] { qb->setText(trText(quickRows[qi][1].toUtf8().constData(), quickRows[qi][2].toUtf8().constData())); });
  connect(qb, &QPushButton::clicked, this, [this, id = quickRows[qi][0]] {
   if (id.startsWith("analytic:")) applyAnalytic(id.mid(9));
   else if (id.startsWith("env:")) applyAnalytic(id);
   else { activeAnalytic_.clear(); viewport_->setAnalyticalOverlay({}); applyDiagram(id); }
  });
  qdGrid->addWidget(qb, qi / 3, qi % 3);
 }
 qdLay->addLayout(qdGrid);
 layout->addWidget(quickDiagCard);

 // CARD 3: LAYER + GEOJSON OVERLAYS
 auto *layerCard = new QFrame(leftBody);
 layerCard->setObjectName("v2-card");
 auto *layerLay = new QVBoxLayout(layerCard);
 layerLay->setContentsMargins(12, 11, 12, 12);
 layerLay->setSpacing(7);
 layerLay->addWidget(label("LAYER · 73 LỚP DỮ LIỆU", "LAYER · 73 DATA LAYERS", "eyebrow"));

 contextStyle_ = new QCheckBox(layerCard);
 contextStyle_->setObjectName("context-style");
 contextStyle_->setChecked(true);
 translations_.append([=, this] { contextStyle_->setText(trText("Đồng bộ bối cảnh ngoài ranh", "Style outside map context")); });
 layerLay->addWidget(contextStyle_);
 connect(contextStyle_, &QCheckBox::toggled, this, [this](bool value) {
  map_->setContextEnabled(value);
  viewport_->setContextStyleEnabled(value);
  markModified();
 });

 layerSearch_ = new QLineEdit(layerCard);
 layerSearch_->setObjectName("layer-search");
 layerSearch_->setClearButtonEnabled(true);
 translations_.append([=, this] { layerSearch_->setPlaceholderText(trText("Lọc lớp dữ liệu…", "Filter data layers…")); });
 layerLay->addWidget(layerSearch_);
 connect(layerSearch_, &QLineEdit::textChanged, this, &MainWindow::filterLayers);

 layerGroup_ = new QComboBox(layerCard);
 layerGroup_->setObjectName("layer-group");
 layerGroup_->setMinimumWidth(0);
 layerGroup_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 layerLay->addWidget(layerGroup_);
 connect(layerGroup_, &QComboBox::currentIndexChanged, this, &MainWindow::filterLayers);

 layerTree_ = new QTreeWidget(layerCard);
 layerTree_->setObjectName("layers");
 layerTree_->setMinimumHeight(220);
 layerTree_->setColumnCount(3);
 layerTree_->setHeaderHidden(true);
 layerTree_->setRootIsDecorated(false);
 layerTree_->setIndentation(0);
 layerTree_->header()->setStretchLastSection(false);
 layerTree_->header()->setMinimumSectionSize(18);
 layerTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
 layerTree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
 layerTree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
 layerTree_->setColumnWidth(1, 18);
 layerTree_->setColumnWidth(2, 46);
 layerTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
 layerLay->addWidget(layerTree_);
 connect(layerTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
  if (updating_ || column != 0) return;
  pushUndoState();
  for (auto &l : layers_) if (l.id == item->data(0, Qt::UserRole).toString()) l.visible = item->checkState(0) == Qt::Checked;
  viewport_->setLayers(layers_);
  syncMap();
  markModified();
 });
 connect(layerTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { editLayer(); });

 auto *layerButtons = new QHBoxLayout();
 auto *up = new QPushButton(QString::fromUtf8("↑"), layerCard);
 up->setObjectName("layer-up");
 up->setMaximumWidth(34);
 auto *down = new QPushButton(QString::fromUtf8("↓"), layerCard);
 down->setObjectName("layer-down");
 down->setMaximumWidth(34);
 auto *edit = new QPushButton(layerCard);
 edit->setObjectName("layer-edit");
 translations_.append([=, this] { edit->setText(trText("Chỉnh lớp", "Edit layer")); });
 layerButtons->addWidget(up);
 layerButtons->addWidget(down);
 layerButtons->addWidget(edit, 1);
 layerLay->addLayout(layerButtons);
 connect(up, &QPushButton::clicked, this, [this] { moveLayer(-1); });
 connect(down, &QPushButton::clicked, this, [this] { moveLayer(1); });
 connect(edit, &QPushButton::clicked, this, &MainWindow::editLayer);

 // Imported GeoJSON overlays section
 layerLay->addWidget(label("LỚP GEOJSON NHẬP NGOÀI", "IMPORTED GEOJSON OVERLAYS", "section"));
 importRole_ = new QComboBox(layerCard);
 importRole_->setObjectName("import-role");
 importRole_->setMinimumWidth(0);
 importRole_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
 for (const auto &role : ga::geoJsonRoles()) importRole_->addItem(ga::geoJsonRoleName(role, english_), role);
 layerLay->addWidget(importRole_);
 importButton_ = new QPushButton(layerCard);
 importButton_->setObjectName("import-geojson");
 translations_.append([=, this] { importButton_->setText(trText("+ Thêm lớp GeoJSON", "+ Add GeoJSON overlay")); });
 layerLay->addWidget(importButton_);
 connect(importButton_, &QPushButton::clicked, this, &MainWindow::chooseGeoJsonOverlay);
 importList_ = new QListWidget(layerCard);
 importList_->setObjectName("imported-files");
 importList_->setMinimumHeight(56);
 importList_->setMaximumHeight(115);
 layerLay->addWidget(importList_);
 removeImportButton_ = new QPushButton(layerCard);
 removeImportButton_->setObjectName("remove-import");
 translations_.append([=, this] { removeImportButton_->setText(trText("Gỡ file đã chọn", "Remove selected")); });
 layerLay->addWidget(removeImportButton_);
 connect(importList_, &QListWidget::currentRowChanged, this, [this] { syncSelection(); });
 connect(removeImportButton_, &QPushButton::clicked, this, [this] {
  if (auto *item = importList_->currentItem()) removeImportedGeoJson(item->data(Qt::UserRole).toString());
 });
 layout->addWidget(layerCard);
 layout->addStretch();

 bodyLay->addWidget(leftDrawer_);

 // Collapsible Left Edge Handle
 leftToggle_ = new QToolButton(bodyRow);
 leftToggle_->setObjectName("edge-toggle");
 leftToggle_->setText(QString::fromUtf8("‹"));
 leftToggle_->setFixedSize(14, 44);
 connect(leftToggle_, &QToolButton::clicked, this, [this] {
  const bool next = !leftDrawer_->isOpen();
  leftDrawer_->setOpen(next, true);
  leftToggle_->setText(next ? QString::fromUtf8("‹") : QString::fromUtf8("›"));
 });
 bodyLay->addWidget(leftToggle_, 0, Qt::AlignVCenter);

 // ── CENTER: Native MapLibre GL + Native OpenGL 3.3 / 2D Viewport + Collapsible Live Section Dock ──
 auto *centerCol = new QWidget(bodyRow);
 auto *centerLay = new QVBoxLayout(centerCol);
 centerLay->setContentsMargins(0, 0, 0, 0);
 centerLay->setSpacing(0);
 viewport_ = new Viewport(dataDirectory_ + "/map-cache", centerCol);
 map_ = new MapCanvas(dataDirectory_ + "/map-cache", centerCol);
 views_ = new QStackedWidget(centerCol);
 views_->addWidget(map_);
 views_->addWidget(viewport_);
 views_->setCurrentWidget(viewport_);
 if (liveSection_ && viewport_->isSectionCutActive()) { liveSection_->show(); updateLiveSection(); }
 toast_ = new ux::FloatingToast(views_);
 centerLay->addWidget(views_, 1);

 liveSection_ = new LiveSectionDock(centerCol);
 liveSection_->hide();
 centerLay->addWidget(liveSection_);
 bodyLay->addWidget(centerCol, 1);

 connect(liveSection_, &LiveSectionDock::presetRequested, this, [this](const QString &kind) {
  const double r = std::max(80.0, project_.site.radius * 0.86);
  if (kind == "ew") viewport_->setSectionCutLine({-r, 0}, {r, 0});
  else if (kind == "ns") viewport_->setSectionCutLine({0, r}, {0, -r});
  else if (kind == "flip") viewport_->setSectionCutLine(viewport_->sectionCutB(), viewport_->sectionCutA());
 });
 connect(liveSection_, &LiveSectionDock::openFullStudioRequested, this, &MainWindow::showSection);
 connect(liveSection_, &LiveSectionDock::closeRequested, this, [this] { viewport_->setSectionCutActive(false); });

 // Collapsible Right Edge Handle
 rightToggle_ = new QToolButton(bodyRow);
 rightToggle_->setObjectName("edge-toggle");
 rightToggle_->setText(QString::fromUtf8("›"));
 rightToggle_->setFixedSize(14, 44);
 connect(rightToggle_, &QToolButton::clicked, this, [this] { setStudioOpen(!studioDrawer_->isOpen()); });
 bodyLay->addWidget(rightToggle_, 0, Qt::AlignVCenter);

 // ── RIGHT STUDIO DRAWER: Compact Spectrum 2 2×4 Tool Rail (272px) ──
 studioDrawer_ = new ux::AnimatedDrawer(272, bodyRow);
 studioPanel_ = studioDrawer_->contentHost();
 studioPanel_->setObjectName("studio-panel");
 auto *stRoot = new QVBoxLayout(studioPanel_);
 stRoot->setContentsMargins(8, 8, 8, 8);
 stRoot->setSpacing(8);

 // 2×4 Studio Icon Tiles Grid
 auto *tileGrid = new QGridLayout();
 tileGrid->setSpacing(5);
 const QVector<QPair<const char *, const char *>> tileDefs = {
  {"Sơ đồ", "Diagrams"}, {"Phân lớp", "Explode"}, {"Thống kê", "Stats"}, {"Phân tích", "Analyze"},
  {"Cắt Laser", "Laser"}, {"Mặt cắt", "Section"}, {"Báo cáo", "Report"}, {"Nắng 3D", "Sun 3D"},
 };
 for (int i = 0; i < tileDefs.size(); ++i) {
  auto *t = new QToolButton(studioPanel_);
  t->setObjectName("studio-tile");
  t->setCheckable(true);
  t->setChecked(i == 0);
  t->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  t->setIconSize(QSize(17, 17));
  t->setMinimumHeight(46);
  t->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  translations_.append([=, this] { t->setText(trText(tileDefs[i].first, tileDefs[i].second)); });
  t->setText(trText(tileDefs[i].first, tileDefs[i].second));
  connect(t, &QToolButton::clicked, this, [this, i] { selectStudioTab(i); });
  tileGrid->addWidget(t, i / 4, i % 4);
  studioTiles_.append(t);
 }
 stRoot->addLayout(tileGrid);

 // Studio Animated Stacked Modules (Cross-fade + subtle directional glide)
 studioStack_ = new ux::AnimatedStackedWidget(studioPanel_);
 stRoot->addWidget(studioStack_, 1);

 // ── Studio Tab 0: SƠ ĐỒ (DIAGRAMS — Exact Web Clone!) ──
 {
  auto *page = new QWidget(studioStack_);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(0, 0, 0, 0);
  pl->setSpacing(8);

  auto *actRow = new QHBoxLayout();
  actRow->setSpacing(6);
  auto *expBtn = new QPushButton(trText("Export", "Export"), page);
  expBtn->setObjectName("secondary-pill");
  auto *expAllBtn = new QPushButton(trText("Export all", "Export all"), page);
  expAllBtn->setObjectName("secondary-pill");
  connect(expBtn, &QPushButton::clicked, this, &MainWindow::showExportStudio);
  connect(expAllBtn, &QPushButton::clicked, this, [this] { exportFile("project-kit"); });
  actRow->addWidget(expBtn, 1);
  actRow->addWidget(expAllBtn, 1);
  pl->addLayout(actRow);

  // Style quick selector + gallery button
  auto *stRow = new QHBoxLayout();
  styles_ = new QComboBox(page);
  styles_->setObjectName("styles");
  styles_->setMaxVisibleItems(14);
  connect(styles_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!updating_ && i >= 0) applyStyle(styles_->itemData(i).toString()); });
  auto *galBtn = new QPushButton("⋯", page);
  galBtn->setFixedWidth(30);
  galBtn->setToolTip(trText("Mở thư viện 39 phong cách", "Open 39-style gallery"));
  connect(galBtn, &QPushButton::clicked, this, &MainWindow::showStyleGallery);
  stRow->addWidget(styles_, 1);
  stRow->addWidget(galBtn);
  pl->addLayout(stRow);

  // Hidden diagrams combobox kept in sync for backward compatibility
  diagrams_ = new QComboBox(page);
  diagrams_->setObjectName("diagrams");
  diagrams_->hide();
  connect(diagrams_, &QComboBox::currentIndexChanged, this, [this](int i) { if (!updating_ && i >= 0) applyDiagram(diagrams_->itemData(i).toString()); });

  auto *diagHead = new QHBoxLayout();
  diagHead->addWidget(label("DIAGRAM (69 SƠ ĐỒ)", "DIAGRAM (69 VIEWS)", "eyebrow"));
  diagHead->addStretch();
  pl->addLayout(diagHead);

  diagramSearch_ = new QLineEdit(page);
  diagramSearch_->setObjectName("diagram-search");
  diagramSearch_->setClearButtonEnabled(true);
  diagramSearch_->setMinimumWidth(0);
  translations_.append([=, this] { diagramSearch_->setPlaceholderText(trText("Lọc 69 sơ đồ…", "Filter 69 diagrams…")); });
  connect(diagramSearch_, &QLineEdit::textChanged, this, [this] { populateDiagramList(); });
  pl->addWidget(diagramSearch_);

  auto *catRow = new QHBoxLayout();
  catRow->setSpacing(2);
  const QVector<QStringList> catDefs = {
   {"all", "Tất cả", "All"}, {"arch", "Kiến trúc", "Arch"}, {"env", "Môi trường", "Env"}, {"plan", "Quy hoạch", "Plan"}, {"kit", "Bộ", "Kits"}
  };
  for (const auto &cd : catDefs) {
   auto *cb = new QToolButton(page);
   cb->setObjectName("cat-pill");
   cb->setCheckable(true);
   cb->setChecked(cd[0] == "all");
   cb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
   translations_.append([=, this] { cb->setText(trText(cd[1].toUtf8().constData(), cd[2].toUtf8().constData())); });
   connect(cb, &QToolButton::clicked, this, [this, key = cd[0]] {
    diagramCategory_ = key;
    for (int k = 0; k < diagramCatBtns_.size(); ++k) {
     const QSignalBlocker sb(diagramCatBtns_[k]);
     diagramCatBtns_[k]->setChecked(diagramCatBtns_[k]->property("catKey").toString() == key);
    }
    populateDiagramList();
   });
   cb->setProperty("catKey", cd[0]);
   diagramCatBtns_.append(cb);
   catRow->addWidget(cb);
  }
  pl->addLayout(catRow);

  diagramList_ = new QListWidget(page);
  diagramList_->setObjectName("diagram-list");
  connect(diagramList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
   if (!item) return;
   const QString id = item->data(Qt::UserRole).toString();
   if (id == "hanoi-qh:toggle") {
    setHanoiPlanning(!hanoiPlanEnabled_);
    populateDiagramList();
   } else if (id.startsWith("planning:")) applyFuturePlanning(id.mid(9));
   else if (id.startsWith("analytic:")) {
    if (!activePlanningLevel_.isEmpty()) applyFuturePlanning(QString());
    applyAnalytic(id.mid(9));
   } else if (id.startsWith("env:")) {
    if (!activePlanningLevel_.isEmpty()) applyFuturePlanning(QString());
    applyAnalytic(id);
   } else {
    activeAnalytic_.clear();
    viewport_->setAnalyticalOverlay({});
    if (!activePlanningLevel_.isEmpty()) applyFuturePlanning(QString());
    applyDiagram(id);
   }
  });
  pl->addWidget(diagramList_, 1);
  studioStack_->addWidget(page);
 }

 // ── Studio Tab 1: PHÂN LỚP (EXPLODED STACK & CAMERA) ──
 {
  auto *page = new QWidget(studioStack_);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(10);
  auto *card = new QFrame(page);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 12, 12, 12);
  cl->setSpacing(9);
  cl->addWidget(label("PHÂN LỚP 3D (EXPLODED)", "3D EXPLODED STACK", "eyebrow"));
  auto *expToggle = new QCheckBox(card);
  translations_.append([=, this] { expToggle->setText(trText("Bật tách tầng 3D theo lớp", "Enable 3D exploded layers")); });
  connect(expToggle, &QCheckBox::toggled, explode, &QAction::setChecked);
  connect(explode, &QAction::toggled, expToggle, &QCheckBox::setChecked);
  cl->addWidget(expToggle);

  cl->addWidget(label("KHOẢNG TÁCH LỚP (m)", "LAYER SEPARATION (m)", "section"));
  separation_ = new QSlider(Qt::Horizontal, card);
  separation_->setObjectName("separation");
  separation_->setRange(10, 180);
  separation_->setValue(60);
  connect(separation_, &QSlider::valueChanged, this, [this](int value) {
   if (!updating_) { auto c = viewport_->camera(); c.separation = value; viewport_->setCamera(c); }
  });
  cl->addWidget(separation_);

  cl->addWidget(label("CAMERA KIẾN TRÚC", "ARCHITECTURAL CAMERA", "section"));
  projections_ = new QComboBox(card);
  projections_->setObjectName("camera-presets");
  connect(projections_, &QComboBox::currentIndexChanged, this, [this](int i) {
   if (!updating_ && i >= 0) { auto c = viewport_->camera(); c.preset(projections_->itemData(i).toString()); showArchitecturalView(c); }
  });
  cl->addWidget(projections_);

  auto *gridCheck = new QCheckBox(card);
  translations_.append([=, this] { gridCheck->setText(trText("Hiển thị lưới tọa độ 100 m", "Show 100 m coordinate grid")); });
  connect(gridCheck, &QCheckBox::toggled, grid, &QAction::setChecked);
  connect(grid, &QAction::toggled, gridCheck, &QCheckBox::setChecked);
  cl->addWidget(gridCheck);

  auto *recenterBtn = new QPushButton(card);
  translations_.append([=, this] { recenterBtn->setText(trText("Về tâm khu đất (F)", "Recenter site (F)")); });
  connect(recenterBtn, &QPushButton::clicked, reset, &QAction::trigger);
  cl->addWidget(recenterBtn);

  pl->addWidget(card);
  pl->addStretch();
  studioStack_->addWidget(page);
 }

 // ── Studio Tab 2: THỐNG KÊ (METRICS & BUILDING INSPECTOR) ──
 {
  auto *scroll = new QScrollArea(studioStack_);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *page = new QWidget(scroll);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(10);
  scroll->setWidget(page);

  pl->addWidget(label("SỐ LIỆU KHU ĐẤT", "SITE METRICS", "eyebrow"));
  auto *metrics = new QGridLayout();
  metrics->setSpacing(8);
  const QVector<QStringList> captions = {
   {"Công trình", "Buildings"}, {"Có chiều cao", "With heights"},
   {"Chiều cao TB", "Mean height"}, {"Lỗ đa giác", "Courtyards"}
  };
  for (int i = 0; i < captions.size(); ++i) {
   auto *card = new QFrame(page);
   card->setObjectName("metric-card");
   auto *cl = new QVBoxLayout(card);
   cl->setContentsMargins(10, 9, 10, 9);
   cl->setSpacing(2);
   auto *v = new QLabel("—", card);
   v->setObjectName("metric-value");
   metricValues_.append(v);
   auto *c = new QLabel(card);
   c->setObjectName("metric-caption");
   translations_.append([=, this] { c->setText(captions[i][english_ ? 1 : 0]); });
   cl->addWidget(v);
   cl->addWidget(c);
   metrics->addWidget(card, i / 2, i % 2);
  }
  pl->addLayout(metrics);

  auto *detailCard = new QFrame(page);
  detailCard->setObjectName("v2-card");
  auto *dl = new QVBoxLayout(detailCard);
  dl->setContentsMargins(12, 10, 12, 10);
  dl->setSpacing(6);
  statsText_ = new QLabel(detailCard);
  statsText_->setObjectName("muted");
  statsText_->setWordWrap(true);
  dl->addWidget(statsText_);
  dl->addWidget(label("ĐỐI TƯỢNG ĐANG CHỌN", "SELECTED BUILDING", "section"));
  selectionText_ = new QLabel(detailCard);
  selectionText_->setObjectName("muted");
  selectionText_->setWordWrap(true);
  dl->addWidget(selectionText_);
  pl->addWidget(detailCard);
  pl->addStretch();
  studioStack_->addWidget(scroll);
 }

 // ── Studio Tab 3: PHÂN TÍCH (8 PURE C++ URBAN ANALYTICS + TMY CLIMATE) ──
 {
  auto *scroll = new QScrollArea(studioStack_);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *page = new QWidget(scroll);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(8);
  scroll->setWidget(page);

  auto *card = new QFrame(page);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 11, 12, 12);
  cl->setSpacing(6);
  cl->addWidget(label("MÔ PHỎNG MÔI TRƯỜNG (NATIVE C++)", "URBAN ANALYTICS (PURE C++)", "eyebrow"));

  const QVector<QStringList> analytics = {
   {"sunpath-2d", "Đường đi mặt trời 2D (Sun-Path)", "2D Polar Sun-Path"},
   {"shadow-2d", "Bóng đổ theo giờ (2D)", "Hourly Shadow Projection"},
   {"sunlight-hours", "Giờ nắng trực tiếp", "Direct Sunlight Hours"},
   {"solar-radiation", "Bức xạ mặt trời (kWh/m²)", "Solar Radiation (kWh/m²)"},
   {"svf", "Hệ số nhìn thấy bầu trời SVF", "Sky View Factor (SVF)"},
   {"viewshed", "Trường nhìn 360° (Isovist)", "360° Isovist Viewshed"},
   {"wind-flow", "Luồng gió 2D qua công trình", "2D Wind Flow Streamlines"},
   {"wind-rose", "Hoa gió khí hậu TMY (16 hướng)", "TMY 16-Sector Wind Rose"},
   {"utci", "Tiện nghi nhiệt UTCI (°C)", "Thermal Comfort UTCI (°C)"},
   {"walkshed", "Tiếp cận đi bộ 5–10–15 phút", "5–10–15 min Walkshed"},
   {"traffic-noise", "Tiếng ồn giao thông dB(A)", "Traffic Noise dB(A)"},
   {"congestion", "Áp lực & ùn tắc giao thông", "Traffic Congestion"},
   {"env:gedtm30", "Cao độ địa hình DEM (GEDTM30)", "Ground Elevation DEM"},
   {"env:terrain", "Đồng mức & Cao độ (Contours)", "Terrain Contours & DEM"},
   {"env:slope", "Độ dốc địa hình (°)", "Terrain Slope (°)"},
   {"env:flood", "Nguy cơ ngập · Kịch bản RP100", "Flood Hazard (RP100)"},
   {"env:landsat", "Nhiệt độ bề mặt (LST / Landsat)", "Surface Temperature (LST)"},
   {"env:sentinel", "Thảm thực vật · NDVI (Sentinel-2)", "Vegetation Index (NDVI)"},
   {"env:canopy", "Chiều cao tán cây (Canopy)", "Tree Canopy Height"},
   {"env:population", "Mật độ dân số (WorldPop)", "Population Density"},
  };
  for (const auto &a : analytics) {
   auto *b = new QPushButton(card);
   translations_.append([=, this] { b->setText(trText(a[1].toUtf8().constData(), a[2].toUtf8().constData())); });
   connect(b, &QPushButton::clicked, this, [this, k = a[0]] { applyAnalytic(k); });
   cl->addWidget(b);
  }
  auto *clearAn = new QPushButton(card);
  translations_.append([=, this] { clearAn->setText(trText("Tắt lớp phủ phân tích", "Clear overlay")); });
  connect(clearAn, &QPushButton::clicked, this, [this] {
   activeAnalytic_.clear();
   viewport_->setAnalyticalOverlay({});
   if (analyticSummary_) analyticSummary_->clear();
  });
  cl->addWidget(clearAn);

  analyticSummary_ = new QLabel(card);
  analyticSummary_->setObjectName("muted");
  analyticSummary_->setWordWrap(true);
  cl->addWidget(analyticSummary_);
  pl->addWidget(card);

  auto *climBtn = new QPushButton(page);
  climBtn->setProperty("accent", true);
  translations_.append([=, this] { climBtn->setText(trText("Biểu đồ Khí hậu & Hoa gió TMY ↗", "TMY Climate & Wind Rose Charts ↗")); });
  connect(climBtn, &QPushButton::clicked, this, &MainWindow::showAnalysis);
  pl->addWidget(climBtn);
  pl->addStretch();
  studioStack_->addWidget(scroll);
 }

 // ── Studio Tab 4: CẮT LASER (LASER CUT & 3D PRINT) ──
 {
  auto *page = new QWidget(studioStack_);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(10);
  auto *card = new QFrame(page);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 12, 12, 12);
  cl->setSpacing(8);
  cl->addWidget(label("CẮT LASER & IN SA BÀN 3D", "LASER CUT & 3D PRINT", "eyebrow"));
  cl->addWidget(label("TỈ LỆ MÔ HÌNH", "MODEL SCALE", "section"));
  laserScale_ = new QComboBox(card);
  for (int sc : {500, 1000, 2000, 5000}) laserScale_->addItem(QString("1 : %1").arg(sc), sc);
  laserScale_->setCurrentIndex(1);
  cl->addWidget(laserScale_);
  laserSeparate_ = new QCheckBox(card);
  laserSeparate_->setChecked(true);
  translations_.append([=, this] { laserSeparate_->setText(trText("Tách tấm đế & tấm khối nhà", "Separate baseplate & buildings")); });
  cl->addWidget(laserSeparate_);
  laserRoads_ = new QCheckBox(card);
  laserRoads_->setChecked(true);
  translations_.append([=, this] { laserRoads_->setText(trText("Khắc nét đường giao thông", "Engrave road network")); });
  cl->addWidget(laserRoads_);
  laserWater_ = new QCheckBox(card);
  laserWater_->setChecked(true);
  translations_.append([=, this] { laserWater_->setText(trText("Khắc mặt nước", "Engrave water bodies")); });
  cl->addWidget(laserWater_);

  auto *lDxf = new QPushButton(card);
  lDxf->setProperty("accent", true);
  translations_.append([=, this] { lDxf->setText(trText("Xuất Laser DXF (mm)", "Export Laser DXF (mm)")); });
  connect(lDxf, &QPushButton::clicked, this, [this] { exportFile("laser-dxf"); });
  cl->addWidget(lDxf);

  auto *lSvg = new QPushButton(card);
  translations_.append([=, this] { lSvg->setText(trText("Xuất Laser SVG (đỏ/xanh)", "Export Laser SVG (cut/score)")); });
  connect(lSvg, &QPushButton::clicked, this, [this] { exportFile("laser-svg"); });
  cl->addWidget(lSvg);

  auto *lStl = new QPushButton(card);
  translations_.append([=, this] { lStl->setText(trText("Xuất mô hình In 3D (.STL)", "Export 3D Print Mesh (.STL)")); });
  connect(lStl, &QPushButton::clicked, this, [this] { exportFile("stl"); });
  cl->addWidget(lStl);

  pl->addWidget(card);
  pl->addStretch();
  studioStack_->addWidget(page);
 }

 // ── Studio Tab 5: MẶT CẮT (ARCHITECTURAL SECTION STUDIO) ──
 {
  auto *page = new QWidget(studioStack_);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(10);
  auto *card = new QFrame(page);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 12, 12, 12);
  cl->setSpacing(9);
  cl->addWidget(label("PHÒNG MẶT CẮT KIẾN TRÚC", "ARCHITECTURAL SECTION STUDIO", "eyebrow"));
  cl->addWidget(label(
   "Cắt qua khu đất bằng C++ thời gian thực:\n• Chế độ Phẳng (Đô thị) & Địa hình (DEM)\n• Kéo trực tiếp 2 đầu A–A' trên mini-map\n• Tự bật cây, bóng người 1,72 m & mặt cắt đường khi cắt gần\n• Xuất SVG, PDF A3, PNG 2× & AutoCAD DXF 1:1",
   "Real-time C++ architectural sectioning:\n• Flat Urban Datum & DEM Elevation modes\n• Interactive draggable A–A' mini-map\n• Scale-adaptive trees, people & street cross-sections\n• Export SVG, A3 PDF, 2× PNG & 1:1 AutoCAD DXF", "muted"));
  auto *liveSecBtn = new QPushButton(card);
  liveSecBtn->setProperty("accent", true);
  translations_.append([=, this] { liveSecBtn->setText(trText("Bật Mặt cắt trực tiếp A–A' trên bản vẽ", "Toggle Live Section A–A' on Viewport")); });
  connect(liveSecBtn, &QPushButton::clicked, this, [this] {
   if (geographic_) showArchitecturalView(viewport_->camera());
   viewport_->setSectionCutActive(!viewport_->isSectionCutActive());
  });
  cl->addWidget(liveSecBtn);

  auto *openSec = new QPushButton(card);
  translations_.append([=, this] { openSec->setText(trText("Mở Mặt cắt Studio đầy đủ (Ctrl+K) ↗", "Open Full Section Studio (Ctrl+K) ↗")); });
  connect(openSec, &QPushButton::clicked, this, &MainWindow::showSection);
  cl->addWidget(openSec);
  pl->addWidget(card);
  pl->addStretch();
  studioStack_->addWidget(page);
 }

 // ── Studio Tab 6: BÁO CÁO (SITE REPORT PDF & NOTES) ──
 {
  auto *page = new QWidget(studioStack_);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(10);
  auto *card = new QFrame(page);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 12, 12, 12);
  cl->setSpacing(8);
  cl->addWidget(label("BÁO CÁO HỒ SƠ ĐỒ ÁN", "SITE INTELLIGENCE REPORT", "eyebrow"));
  cl->addWidget(label("GHI CHÚ ĐƯA VÀO BÁO CÁO", "PROJECT NOTES FOR REPORT", "section"));
  notes_ = new QPlainTextEdit(card);
  notes_->setObjectName("notes");
  notes_->setMinimumHeight(110);
  notes_->setMaximumHeight(180);
  connect(notes_, &QPlainTextEdit::textChanged, this, [this] { if (!updating_) { project_.notes = notes_->toPlainText(); markModified(); } });
  cl->addWidget(notes_);

  auto *pdfBtn = new QPushButton(card);
  pdfBtn->setProperty("accent", true);
  translations_.append([=, this] { pdfBtn->setText(trText("Xuất Báo cáo 3 trang A3 (.PDF)", "Export 3-Page A3 Report (.PDF)")); });
  connect(pdfBtn, &QPushButton::clicked, this, [this] { exportFile("report"); });
  cl->addWidget(pdfBtn);

  auto *srcBtn = new QPushButton(card);
  srcBtn->setObjectName("source-info");
  translations_.append([=, this] { srcBtn->setText(trText("Nguồn & giới hạn dữ liệu ↗", "Sources & limitations ↗")); });
  connect(srcBtn, &QPushButton::clicked, this, &MainWindow::showSource);
  cl->addWidget(srcBtn);

  pl->addWidget(card);
  pl->addStretch();
  studioStack_->addWidget(page);
 }

 // ── Studio Tab 7: NẮNG 3D (PC-GRADE OPENGL 3.3 2048px PCF SHADOWS + SUN-SKY) ──
 {
  auto *scroll = new QScrollArea(studioStack_);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *page = new QWidget(scroll);
  auto *pl = new QVBoxLayout(page);
  pl->setContentsMargins(2, 4, 2, 4);
  pl->setSpacing(10);
  scroll->setWidget(page);

  auto *card = new QFrame(page);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 12, 12, 12);
  cl->setSpacing(8);
  cl->addWidget(label("NẮNG & BÓNG ĐỔ 3D (GPU POISSON PCF)", "SUN & 3D SHADOWS (GPU POISSON PCF)", "eyebrow"));
  sunEnabled_ = new QCheckBox(card);
  sunEnabled_->setObjectName("sun-enabled");
  translations_.append([=, this] { sunEnabled_->setText(trText("Bật bóng đổ 3D phần cứng (PCF mượt)", "Enable GPU soft 3D shadows (PCF)")); });
  cl->addWidget(sunEnabled_);
  sunPath_ = new QCheckBox(card);
  sunPath_->setObjectName("sun-path");
  sunPath_->setChecked(true);
  translations_.append([=, this] { sunPath_->setText(trText("Hiển thị vòm quỹ đạo mặt trời 3D", "Show 3D sun-path dome & labels")); });
  cl->addWidget(sunPath_);
  sunGolden_ = new QCheckBox(card);
  sunGolden_->setObjectName("sun-golden");
  sunGolden_->setChecked(true);
  translations_.append([=, this] { sunGolden_->setText(trText("Ánh sáng giờ vàng && bầu trời vật lý", "Golden-hour && sky lighting")); });
  cl->addWidget(sunGolden_);
  sunSkyDome_ = new QCheckBox(card);
  sunSkyDome_->setObjectName("sun-skydome");
  sunSkyDome_->setChecked(true);
  translations_.append([=, this] { sunSkyDome_->setText(trText("Vòm trời && mặt nước phản chiếu", "Atmospheric sky && reflective water")); });
  cl->addWidget(sunSkyDome_);
  trees3dCheck_ = new QCheckBox(card);
  trees3dCheck_->setObjectName("trees-3d");
  trees3dCheck_->setChecked(true);
  translations_.append([=, this] { trees3dCheck_->setText(trText("Cây xanh 3D kiến trúc đổ bóng thật", "3D architectural trees with real shadows")); });
  cl->addWidget(trees3dCheck_);
  terrain3dCheck_ = new QCheckBox(card);
  terrain3dCheck_->setObjectName("terrain-3d");
  translations_.append([=, this] { terrain3dCheck_->setText(trText("Dựng địa hình 3D thật (DEM GEDTM30)", "Enable 3D terrain elevation (DEM)")); });
  connect(terrain3dCheck_, &QCheckBox::toggled, this, [this](bool on) {
   if (updating_) return;
   terrain3d_ = on;
   viewport_->setTerrain3d(on, ga::appDataPath("environment"));
   persistPreferences();
   showToast(on ? trText("Đã bật địa hình 3D theo DEM", "3D DEM terrain enabled") : trText("Đã về mặt nền phẳng", "Flat architectural datum"), QColor("#3b63fb"), 1900);
  });
  cl->addWidget(terrain3dCheck_);

  cl->addWidget(label("MÙA TRONG NĂM", "SOLAR SEASON", "section"));
  sunSeason_ = new QComboBox(card);
  sunSeason_->setObjectName("sun-season");
  cl->addWidget(sunSeason_);

  cl->addWidget(label("GIỜ MẶT TRỜI (06:00 – 18:00 · MƯỢT TỪNG PHÚT)", "SOLAR HOUR (06:00 – 18:00 · CONTINUOUS)", "section"));
  sunHour_ = new QSlider(Qt::Horizontal, card);
  sunHour_->setObjectName("sun-hour");
  sunHour_->setRange(360, 1080); // 720 smooth 1-minute steps (06:00..18:00)
  sunHour_->setValue(960);       // 16:00
  sunHour_->setTracking(true);
  cl->addWidget(sunHour_);

  // Quick time-of-day chips
  auto *presetRow = new QHBoxLayout();
  presetRow->setSpacing(4);
  for (const auto &pr : QVector<QPair<int, const char *>>{{420, "07:00"}, {720, "12:00"}, {960, "16:00"}, {1035, "17:15"}}) {
   auto *pb = new QPushButton(QString::fromLatin1(pr.second), card);
   pb->setObjectName("quick-chip");
   connect(pb, &QPushButton::clicked, this, [this, val = pr.first] {
    sunAnimHour_ = val / 60.0;
    sunHour_->setValue(val);
    if (!sunEnabled_->isChecked()) sunEnabled_->setChecked(true);
   });
   presetRow->addWidget(pb, 1);
  }
  cl->addLayout(presetRow);

  cl->addWidget(label("ĐỘ ĐẬM BÓNG ĐỔ", "SHADOW CONTRAST", "section"));
  sunShadow_ = new QSlider(Qt::Horizontal, card);
  sunShadow_->setObjectName("sun-shadow");
  sunShadow_->setRange(20, 100);
  sunShadow_->setValue(76);
  sunShadow_->setTracking(true);
  cl->addWidget(sunShadow_);

  sunReadout_ = new QLabel(card);
  sunReadout_->setObjectName("muted");
  sunReadout_->setWordWrap(true);
  cl->addWidget(sunReadout_);

  auto *playRow = new QHBoxLayout();
  playRow->setSpacing(6);
  sunPlayBtn_ = new QPushButton(card);
  sunPlayBtn_->setProperty("accent", true);
  sunPlayBtn_->setCheckable(true);
  translations_.append([=, this] {
   sunPlayBtn_->setText(sunPlayBtn_->isChecked()
    ? trText("⏸ Dừng nắng", "⏸ Pause sun")
    : trText("▶ Phát nắng mượt", "▶ Smooth sun cycle"));
  });
  sunSpeed_ = new QComboBox(card);
  sunSpeed_->setFixedWidth(64);
  sunSpeed_->addItem("0.5×", 0.45);
  sunSpeed_->addItem("1×", 0.90);
  sunSpeed_->addItem("2×", 1.85);
  sunSpeed_->setCurrentIndex(1);
  playRow->addWidget(sunPlayBtn_, 1);
  playRow->addWidget(sunSpeed_);
  cl->addLayout(playRow);

  // Continuous delta-time sun animation synced to display Hz (up to 500 FPS, zero mesh rebuild!)
  sunAnimTimer_.setTimerType(Qt::PreciseTimer);
  sunAnimTimer_.setInterval(5);
  connect(&sunAnimTimer_, &QTimer::timeout, this, [this] {
   const double dt = std::clamp(double(sunAnimClock_.restart()) / 1000.0, 0.001, 0.1);
   const double speed = sunSpeed_ ? sunSpeed_->currentData().toDouble() : 0.90;
   sunAnimHour_ += dt * speed;
   if (sunAnimHour_ > 17.85) sunAnimHour_ = 6.15;
   auto sol = viewport_->camera().solar;
   sol.enabled = true;
   sol.hour = sunAnimHour_;
   viewport_->setSolarLive(sol);
   if (sunHour_) {
    const QSignalBlocker bh(sunHour_);
    sunHour_->setValue(std::clamp(int(qRound(sunAnimHour_ * 60.0)), 360, 1080));
   }
   if (sunReadout_) {
    const int hh = int(std::floor(sunAnimHour_)), mm = int(std::floor((sunAnimHour_ - hh) * 60.0));
    const auto sp = ga::solarPosition(project_.site.lat, sol.dayOfYear, sunAnimHour_);
    sunReadout_->setText(QString("%1:%2 · %3 %4° · %5 %6° · PCF %7px (%8 Hz)")
     .arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0'))
     .arg(trText("Độ cao", "Alt")).arg(QString::number(sp.altitudeDeg, 'f', 1))
     .arg(trText("Phương vị", "Az")).arg(QString::number(sp.azimuthDeg, 'f', 1))
     .arg(gfxConfig_.shadowMapSize).arg(gfxConfig_.targetFps));
   }
  });
  connect(sunPlayBtn_, &QPushButton::toggled, this, [this](bool playing) {
   if (playing) {
    sunAnimHour_ = viewport_->camera().solar.hour;
    if (sunAnimHour_ < 6.1 || sunAnimHour_ > 17.8) sunAnimHour_ = 6.2;
    if (sunEnabled_ && !sunEnabled_->isChecked()) sunEnabled_->setChecked(true);
    sunAnimClock_.restart();
    sunAnimTimer_.start();
   } else {
    sunAnimTimer_.stop();
    markModified();
   }
   sunPlayBtn_->setText(playing
    ? trText("⏸ Dừng nắng", "⏸ Pause sun")
    : trText("▶ Phát nắng mượt", "▶ Smooth sun cycle"));
  });

  connect(sunEnabled_, &QCheckBox::toggled, this, [this] { if (!updating_) applySolarFromUi(); });
  connect(sunPath_, &QCheckBox::toggled, this, [this] { if (!updating_) applySolarFromUi(); });
  connect(sunGolden_, &QCheckBox::toggled, this, [this] { if (!updating_) applySolarFromUi(); });
  connect(sunSkyDome_, &QCheckBox::toggled, this, [this] { if (!updating_) applySolarLiveFromUi(); });
  connect(trees3dCheck_, &QCheckBox::toggled, this, [this] { if (!updating_) applySolarFromUi(); });
  connect(sunSeason_, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) applySolarFromUi(); });
  // Instant zero-rebuild GPU shadow update while scrubbing sliders!
  connect(sunHour_, &QSlider::valueChanged, this, [this] { if (!updating_) applySolarLiveFromUi(); });
  connect(sunShadow_, &QSlider::valueChanged, this, [this] { if (!updating_) applySolarLiveFromUi(); });

  pl->addWidget(card);
  pl->addStretch();
  studioStack_->addWidget(scroll);
 }

 bodyLay->addWidget(studioDrawer_);

 // ── Shortcuts, Undo/Redo, Command Palette & Tabs ──
 auto *undoAct = action("undo", "Hoàn tác", "Undo", QKeySequence::Undo);
 auto *redoAct = action("redo", "Làm lại", "Redo", QKeySequence::Redo);
 auto *paletteAct = action("palette", "Bảng lệnh nhanh…", "Command Palette…", QKeySequence("Ctrl+P"));
 auto *rulerAct = action("measure", "Thước đo khoảng cách & diện tích (M)", "Distance & Area Ruler (M)");
 fileMenu->addSeparator();
 fileMenu->addActions({undoAct, redoAct, paletteAct});
 viewMenu->addAction(rulerAct);
 connect(undoAct, &QAction::triggered, this, &MainWindow::undoAction);
 connect(redoAct, &QAction::triggered, this, &MainWindow::redoAction);
 connect(paletteAct, &QAction::triggered, this, &MainWindow::showCommandPalette);
 connect(rulerAct, &QAction::triggered, this, [this] {
  if (geographic_) showArchitecturalView(viewport_->camera());
  viewport_->setMeasuring(!viewport_->isMeasuring());
 });

 auto *closeTab = action("close-document", "Đóng tab", "Close tab", QKeySequence::Close);
 fileMenu->addAction(closeTab);
 connect(closeTab, &QAction::triggered, this, [this] { closeDocument(activeDocument_); });
 auto *nextTab = new QShortcut(QKeySequence("Ctrl+Tab"), this);
 connect(nextTab, &QShortcut::activated, this, [this] { if (tabs_->count() > 1) tabs_->setCurrentIndex((activeDocument_ + 1) % tabs_->count()); });
 auto *previousTab = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
 connect(previousTab, &QShortcut::activated, this, [this] { if (tabs_->count() > 1) tabs_->setCurrentIndex((activeDocument_ + tabs_->count() - 1) % tabs_->count()); });

 // ── DataAccess, LicenseManager, Hanoi Planning Tiles & Signal Wiring ──
 data_ = new DataAccess(dataDirectory_ + "/source-cache", this);
 license_ = new ga::LicenseManager(dataDirectory_, this);
 license_->loadLocal();
 connect(license_, &ga::LicenseManager::statusChanged, this, &MainWindow::syncLicenseUi);
 connect(license_, &ga::LicenseManager::offerUpdated, this, &MainWindow::syncLicenseUi);
 connect(license_, &ga::LicenseManager::operationSucceeded, this, [this](const QString &msg) {
  showToast(msg, QColor("#16a34a"), 3000);
 });
 licenseOfferTick_.setInterval(1000);
 connect(&licenseOfferTick_, &QTimer::timeout, this, &MainWindow::syncLicenseUi);
 syncLicenseUi();
 QTimer::singleShot(1400, license_, &ga::LicenseManager::bootstrap);
 hanoiTiles_ = new ga::HanoiPlanningTileFetcher(dataDirectory_ + "/map-cache", this);
 connect(hanoiTiles_, &ga::HanoiPlanningTileFetcher::fetchProgress, this, [this](int done, int total) {
  if (!hanoiPlanEnabled_) return;
  hanoiPlanStatus_ = trText("Đang tải lớp QH quyhoach.hanoi.vn (%1/%2)…", "Loading quyhoach.hanoi.vn tiles (%1/%2)…").arg(done).arg(total);
  if (hanoiPlanStatusLbl_) hanoiPlanStatusLbl_->setText(hanoiPlanStatus_);
 });
 connect(hanoiTiles_, &ga::HanoiPlanningTileFetcher::compositeReady, this, [this](ga::HanoiTileComposite comp) {
  if (!hanoiPlanEnabled_) return;
  viewport_->setHanoiTileComposite(comp);
  if (comp.valid()) {
   hanoiPlanStatus_ = trText("Đang đè %1/%2 ô quyhoach.hanoi.vn (z%3) trên Bản đồ, 2D & 3D.",
                             "Overlaying %1/%2 quyhoach.hanoi.vn tiles (z%3) on Map, 2D & 3D.")
    .arg(comp.tilesAvailable).arg(comp.tilesTotal).arg(comp.zoom);
  } else {
   hanoiPlanStatus_ = trText("Khu đất đang mở ngoài vùng phủ quyhoach.hanoi.vn (Hà Nội).",
                             "Current site is outside quyhoach.hanoi.vn Hanoi tile coverage.");
  }
  if (hanoiPlanStatusLbl_) hanoiPlanStatusLbl_->setText(hanoiPlanStatus_);
 });
 connect(map_, &MapCanvas::hanoiPlanningToggled, this, [this](bool on) { setHanoiPlanning(on); });
 connect(viewport_, &Viewport::hanoiPlanningToggled, this, [this](bool on) { setHanoiPlanning(on); });
 {
  QFile config(dataDirectory_ + "/network.json");
  if (config.open(QIODevice::ReadOnly)) {
   const auto endpoint = QJsonDocument::fromJson(config.readAll()).object()["overpass_url"].toString();
   if (!endpoint.isEmpty()) data_->setOverpassEndpoint(endpoint);
  }
 }

 connect(enrichButton_, &QPushButton::clicked, this, [this] {
  if (loading_ || project_.features.isEmpty()) return;
  enriching_ = true;
  data_->enrich(captureProject(), refreshSource_->isChecked());
 });
 connect(data_, &DataAccess::overlayReady, this, [this](ga::Project next) {
  const auto target = importDocumentId_;
  importDocumentId_.clear();
  importing_ = false;
  int index = -1;
  for (int i = 0; i < documents_.size(); ++i) if (documents_[i].id == target) { index = i; break; }
  if (index < 0) return;
  auto &document = documents_[index];
  if (document.dataRevision != importDataRevision_) {
   statusBar()->showMessage(trText("Dữ liệu dự án đã đổi. Hãy nhập lại file.", "Project data changed. Import the file again."), 12000);
   return;
  }
  ++document.dataRevision;
  const auto entry = next.imports.last().toObject();
  if (index == activeDocument_) {
   project_.features = std::move(next.features);
   project_.imports = next.imports;
   refreshProjectGeometry();
   markModified();
   snapshotDocument();
  } else {
   document.project.features = std::move(next.features);
   document.project.imports = next.imports;
   document.modified = true;
   ++document.contentRevision;
   syncTabs();
  }
  statusBar()->showMessage(trText("Đã thêm %1 phần hình học từ %2 vào %3.", "Added %1 geometry parts from %2 to %3.")
   .arg(entry["count"].toInt()).arg(entry["name"].toString(), document.project.site.name), 12000);
 });
 connect(data_, &DataAccess::phase, this, [this](const QString &name, int attempt) {
  Q_UNUSED(attempt);
  QString message;
  if (name == "import") message = trText("Đang kiểm tra GeoJSON và cắt theo ranh · xử lý trên máy", "Validating GeoJSON and clipping to site · local");
  else if (name == "cache") message = trText("Đọc OSM đã lưu · cache tối đa 6 giờ", "Reading saved OSM · 6-hour cache");
  else if (name == "retry") message = trText("Nguồn OSM chưa phản hồi · thử máy chủ kế tiếp", "OSM provider unavailable · trying next");
  else if (name == "convert") message = trText("Đang đọc hình học và cắt theo ranh", "Reading geometry and clipping");
  else message = trText("Đang kết nối nguồn OSM", "Connecting to OSM provider");
  statusBar()->showMessage(message);
 });

 connect(siteSelection_, &SiteSelection::selectionChanged, this, [this] { syncSelection(); markModified(); });
 connect(siteSelection_, &SiteSelection::message, this, [this](const QString &t) { statusBar()->showMessage(t, 12000); });
 connect(siteSelection_, &SiteSelection::drawRequested, this, [this] { actions_["map"]->trigger(); map_->beginBoundaryDrawing(); });
 connect(siteSelection_, &SiteSelection::finishRequested, map_, &MapCanvas::finishBoundaryDrawing);
 connect(siteSelection_, &SiteSelection::undoRequested, map_, &MapCanvas::undoBoundaryVertex);
 connect(siteSelection_, &SiteSelection::cancelRequested, map_, &MapCanvas::cancelBoundaryDrawing);
 connect(map_, &MapCanvas::drawingChanged, siteSelection_, &SiteSelection::drawingState);
 connect(map_, &MapCanvas::boundaryDrawn, this, [this](const QPolygonF &ring) {
  siteSelection_->setGeographicBoundary(ring);
  const auto c = siteSelection_->site();
  const auto pt = ga::localPoint(c.lon, c.lat, project_.site);
  if (QLineF(pt, QPointF(0, 0)).length() > std::max(50.0, project_.site.radius * 0.85)) {
   const int osmIdx = sourceChoice_->findData("osm");
   if (osmIdx >= 0 && sourceChoice_->currentData().toString() == "current") {
    const QSignalBlocker b(sourceChoice_);
    sourceChoice_->setCurrentIndex(osmIdx);
    syncSelection();
   }
  }
 });
 connect(map_, &MapCanvas::boundaryEdited, siteSelection_, &SiteSelection::setGeographicBoundary);

 connect(sourceChoice_, &QComboBox::currentIndexChanged, this, [this](int index) {
  if (updating_ || index < 0) return;
  const auto entry = sourceChoice_->itemData(index, Qt::UserRole + 1).toJsonObject();
  const auto center = entry["center"].toArray();
  if (center.size() == 2) {
   ga::Site sel;
   sel.lat = center[0].toDouble();
   sel.lon = center[1].toDouble();
   sel.name = entry[english_ ? "en" : "vi"].toString();
   sel.boundary = ga::primitiveBoundary(sel);
   siteSelection_->setSite(sel, false);
   actions_["map"]->trigger();
   map_->centerOn(sel.lat, sel.lon);
  }
  syncSelection();
  markModified();
 });

 const auto doSearch = [this] {
  const auto text = search_->text().trimmed();
  if (text.isEmpty()) return;
  const auto match = QRegularExpression("^\\s*([+-]?[0-9]+(?:\\.[0-9]+)?)\\s*[,;]\\s*([+-]?[0-9]+(?:\\.[0-9]+)?)\\s*$").match(text);
  if (match.hasMatch()) {
   const double lat = match.captured(1).toDouble(), lon = match.captured(2).toDouble();
   if (std::abs(lat) <= 85 && std::abs(lon) <= 180) {
    chooseSite(lat, lon);
    actions_["map"]->trigger();
    map_->centerOn(lat, lon);
   } else statusBar()->showMessage(trText("Tọa độ ngoài phạm vi hỗ trợ.", "Coordinate outside supported extent."), 6000);
   return;
  }
  searchResults_->show();
  searchResults_->clear();
  searchResults_->addItem(trText("Đang tìm…", "Searching…"));
  data_->search(text, candidate_.lat, candidate_.lon, english_);
 };
 connect(searchButton, &QPushButton::clicked, this, doSearch);
 connect(search_, &QLineEdit::returnPressed, this, doSearch);
 connect(data_, &DataAccess::results, this, [this](const QJsonArray &records) {
  searchResults_->clear();
  for (const auto &v : records) {
   const auto row = v.toObject(), props = row["properties"].toObject();
   const auto point = row["geometry"].toObject()["coordinates"].toArray();
   if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble()) continue;
   QStringList parts;
   for (const auto &key : QStringList{"name", "street", "city", "country"}) {
    const auto t = props[key].toString();
    if (!t.isEmpty() && !parts.contains(t)) parts.append(t);
   }
   auto *item = new QListWidgetItem(parts.join(", "), searchResults_);
   item->setData(Qt::UserRole, point.toVariantList());
  }
  if (searchResults_->count() == 0) searchResults_->addItem(trText("Không có kết quả · Photon / OSM", "No results · Photon / OSM"));
 });
 connect(searchResults_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
  const auto point = item->data(Qt::UserRole).toList();
  if (point.size() != 2) return;
  chooseSite(point[1].toDouble(), point[0].toDouble(), item->text());
  actions_["map"]->trigger();
  map_->centerOn(candidate_.lat, candidate_.lon);
 });

 connect(loadButton_, &QPushButton::clicked, this, [this] {
  candidate_ = siteSelection_->site();
  const auto issue = ga::selectionError(candidate_);
  if (!issue.isEmpty()) {
   statusBar()->showMessage(trText("Chỉnh lại ranh khu đất: ", "Adjust site boundary: ") + ga::localizedSiteError(issue, english_), 12000);
   return;
  }
  map_->cancelBoundaryDrawing();
  loadRevision_ = revision_;
  enriching_ = false;
  QString id = sourceChoice_->currentData().toString();
  // Smart safeguard: if candidate extends outside currently loaded site while source is still "current", fetch live OSM automatically!
  if (id == "current" && !project_.features.isEmpty()) {
   const QPointF rel = ga::localPoint(candidate_.lon, candidate_.lat, project_.site);
   const double dist = std::hypot(rel.x(), rel.y());
   if (dist + ga::envelopeRadius(candidate_) > ga::envelopeRadius(project_.site) + 25.0) {
    id = "osm";
    const int osmIdx = sourceChoice_->findData("osm");
    if (osmIdx >= 0) { const QSignalBlocker b(sourceChoice_); sourceChoice_->setCurrentIndex(osmIdx); }
    showToast(trText("Đang nạp dữ liệu OpenStreetMap cho vị trí mới…", "Fetching live OpenStreetMap data for new location…"), QColor("#0284c7"), 2600);
   }
  }
  if (id == "osm") data_->load(candidate_, "all", refreshSource_->isChecked());
  else if (id == "current") data_->loadCurrent(captureProject(), candidate_);
  else {
   const auto entry = sourceChoice_->currentData(Qt::UserRole + 1).toJsonObject();
   data_->loadSaved(ga::appDataPath("sites/" + QFileInfo(entry["file"].toString()).fileName()), candidate_);
  }
 });
 connect(cancelButton_, &QPushButton::clicked, this, [this] {
  data_->cancel();
  importing_ = false;
  importDocumentId_.clear();
  statusBar()->showMessage(trText("Đã hủy. Dữ liệu dự án vẫn được giữ.", "Canceled. Project data retained."), 6000);
 });
 connect(data_, &DataAccess::downloadProgress, this, [this](qint64 received, qint64 total, const QString &host) {
  if (loadProgress_) {
   if (total > 0) { loadProgress_->setRange(0, int(std::min<qint64>(total, 100000000))); loadProgress_->setValue(int(std::min<qint64>(received, total))); }
   else loadProgress_->setRange(0, 0);
  }
  const double kb = received / 1024.0;
  const QString sizeTxt = kb >= 1024.0 ? QString::number(kb / 1024.0, 'f', 2) + " MB" : QString::number(qRound(kb)) + " KB";
  statusBar()->showMessage(trText("Đang tải OSM (%1) · %2…", "Downloading OSM (%1) · %2…").arg(host, sizeTxt));
 });
 connect(data_, &DataAccess::busy, this, [this](bool value) {
  loading_ = value;
  syncSelection();
  cancelButton_->setVisible(value);
  if (loadProgress_) {
   loadProgress_->setRange(0, 0);
   loadProgress_->setVisible(value);
  }
  if (value) statusBar()->showMessage(trText("Đang nạp và cắt theo ranh…", "Loading and clipping site boundary…"));
  else statusBar()->clearMessage();
 });
 connect(data_, &DataAccess::failed, this, [this](const QString &error) {
  searchResults_->clear();
  const QString msg = importing_
   ? trText("Không nhập được: ", "Import failed: ") + ga::localizedImportError(error, english_)
   : trText("Chưa nạp được khu đất: ", "Could not load site: ") + ga::localizedSiteError(error, english_);
  statusBar()->showMessage(msg, 15000);
  showToast(msg, QColor("#dc2626"), 5000);
  importing_ = false;
  importDocumentId_.clear();
 });
 connect(data_, &DataAccess::loaded, this, [this](ga::Project next) {
  const auto history = next.source.value("enrichments").toArray();
  const int added = history.isEmpty() ? 0 : history.last().toObject()["added_geometry_parts"].toInt();
  const bool enriched = enriching_;
  // Default loaded sites to Suushka · Study & uncheck Amenities (Tiện ích)
  int estCount = 0;
  if (!enriched) {
   next.style = "suushka-study";
   next.layerState["amenities"] = QJsonObject{{"id", "amenities"}, {"visible", false}};
   if (smartHeights_ && smartHeights_->isChecked() && next.view["selection_source"].toString() == "osm") {
    estCount = ga::applySmartHeightEstimates(next);
   }
  }
  addDocument(std::move(next));
  markModified();
  const QString msg = enriched
   ? trText("Đã bổ sung %1 phần hình học OSM vào tab mới.", "Added %1 OSM geometry parts in a new tab.").arg(added)
   : (estCount > 0
      ? trText("Đã nạp khu đất mới (Suushka · ước tính chiều cao %1 nhà thiếu dữ liệu).", "Loaded new site (Suushka · smart-estimated %1 unrecorded buildings).").arg(estCount)
      : trText("Đã nạp khu đất mới (Suushka · Study, ẩn Tiện ích).", "Loaded new site (Suushka · Study, amenities hidden)."));
  statusBar()->showMessage(msg, 10000);
  showToast(msg, QColor("#16a34a"), 2800);
 });

 connect(map_, &MapCanvas::viewChanged, this, &MainWindow::markModified);
 connect(map_, &MapCanvas::navigationChanged, this, [this] {
  if (updating_ || switching_ || navigating_) return;
  navigating_ = true;
  const auto v = map_->view();
  viewport_->setGeographicView(v["lat"].toDouble(), v["lon"].toDouble(), v["zoom"].toDouble());
  navigating_ = false;
 });
 connect(map_, &MapCanvas::message, this, [this](const QString &t) { statusBar()->showMessage(t, 6000); });
 connect(map_, &MapCanvas::cityJumpRequested, this, [this](double lat, double lon, const QString &name) {
  chooseSite(lat, lon, name);
  const int osmIdx = sourceChoice_->findData("osm");
  if (osmIdx >= 0) { const QSignalBlocker b(sourceChoice_); sourceChoice_->setCurrentIndex(osmIdx); syncSelection(); }
  showToast(trText("Đã di chuyển tới ", "Jumped to ") + name, QColor("#3b63fb"), 2000);
 });
 connect(map_, &MapCanvas::loadSiteRequested, this, [this] {
  if (!loadButton_ || !loadButton_->isEnabled()) return;
  // If the user panned the map away from the old candidate circle and clicked "Nạp OSM", snap to map center automatically!
  if (!siteSelection_->locked()) {
   const auto mv = map_->view();
   const double mLat = mv["lat"].toDouble(), mLon = mv["lon"].toDouble();
   const QPointF rel = ga::localPoint(mLon, mLat, candidate_);
   if (std::hypot(rel.x(), rel.y()) > std::max(120.0, ga::envelopeRadius(candidate_) * 0.9)) {
    chooseSite(mLat, mLon);
   }
  }
  const int osmIdx = sourceChoice_->findData("osm");
  if (osmIdx >= 0) {
   const QSignalBlocker b(sourceChoice_);
   sourceChoice_->setCurrentIndex(osmIdx);
   syncSelection();
  }
  loadButton_->click();
 });
 connect(map_, &MapCanvas::basemapModeChanged, this, [this](const QString &mode) {
  showToast(mode == "satellite"
   ? trText("Đã chuyển nền Ảnh vệ tinh toàn cầu (Esri)", "Switched to Global Satellite Imagery (Esri)")
   : trText("Đã chuyển nền Bản đồ Vector (OpenFreeMap)", "Switched to Global Vector Basemap (OpenFreeMap)"),
   QColor("#0284c7"), 2000);
 });
 connect(map_, &MapCanvas::sitePicked, this, [this](double lat, double lon) {
  if (!siteSelection_->locked()) chooseSite(lat, lon);
  const auto point = ga::localPoint(lon, lat, project_.site);
  for (int i = project_.features.size() - 1; i >= 0; --i) {
   const auto &f = project_.features[i];
   if (!f.building() || !f.bounds().contains(point) || ga::featurePaints(f, layers_, diagram_).isEmpty()) continue;
   bool hit = f.rings[0].containsPoint(point, Qt::OddEvenFill);
   for (int j = 1; j < f.rings.size(); ++j) hit &= !f.rings[j].containsPoint(point, Qt::OddEvenFill);
   if (hit) { viewport_->setSelected(i); return; }
  }
  viewport_->setSelected(-1);
 });

 connect(mapAction, &QAction::triggered, this, [this] { showGeographicView(); });
 connect(flat, &QAction::triggered, this, [this] { auto c = viewport_->camera(); c.mode = "2d"; showArchitecturalView(c); });
 connect(three, &QAction::triggered, this, [this] { auto c = viewport_->camera(); c.mode = "3d"; showArchitecturalView(c); });
 connect(explode, &QAction::toggled, this, [this](bool checked) {
  auto c = viewport_->camera();
  c.exploded = checked;
  if (checked) { c.mode = "3d"; c.preset("isometric"); if (c.separation < 15) c.separation = 60; }
  if (geographic_) showArchitecturalView(c);
  else viewport_->animateCameraTo(c, 320);
 });
 connect(grid, &QAction::toggled, this, [this](bool checked) { auto c = viewport_->camera(); c.grid = checked; viewport_->setCamera(c); });
 connect(reset, &QAction::triggered, this, [this] { if (geographic_) map_->setProject(&project_); else viewport_->animateResetView(320); });
 connect(exportPngAction, &QAction::triggered, this, &MainWindow::showExportStudio);
 connect(analysisAction, &QAction::triggered, this, &MainWindow::showAnalysis);
 connect(sectionAction, &QAction::triggered, this, &MainWindow::showSection);
 connect(sunAction, &QAction::toggled, this, [this](bool checked) {
  if (updating_) return;
  auto c = viewport_->camera();
  c.solar.enabled = checked;
  if (checked) { c.mode = "3d"; c.exploded = false; }
  showArchitecturalView(c);
  syncSolarUi();
 });

 statusText_ = new QLabel(this);
 statusBar()->addWidget(statusText_, 1);
 cursorText_ = new QLabel(this);
 statusBar()->addPermanentWidget(cursorText_);
 offlineText_ = label("C++20 · OpenGL 3.3", "C++20 · OpenGL 3.3", "muted");
 offlineText_->setWordWrap(false);
 offlineText_->setContentsMargins(6, 0, 8, 0);
 statusBar()->addPermanentWidget(offlineText_);

 connect(viewport_, &Viewport::framingChanged, this, [this] { if (!geographic_) syncNavigationFromActive(); });
 connect(viewport_, &Viewport::cameraChanged, this, [this] {
  if (!geographic_) syncNavigationFromActive();
  const auto c = viewport_->camera();
  const QSignalBlocker b1(actions_["view-2d"]), b2(actions_["view-3d"]), b3(actions_["explode"]), b4(actions_["grid"]), b6(actions_["sun"]);
  actions_["view-2d"]->setChecked(!geographic_ && c.mode == "2d");
  actions_["view-3d"]->setChecked(!geographic_ && c.mode == "3d");
  actions_["explode"]->setChecked(c.exploded);
  actions_["grid"]->setChecked(c.grid);
  actions_["sun"]->setChecked(c.solar.enabled);
  const QSignalBlocker b5(projections_);
  projections_->setCurrentIndex(projections_->findData(c.projection));
  syncSolarUi();
  for (auto *track : segTracks_) if (track) track->syncAnimated();
  markModified();
 });
 connect(viewport_, &Viewport::featureSelected, this, &MainWindow::syncInspector);
 connect(viewport_, &Viewport::sectionCutChanged, this, [this](QPointF, QPointF, bool active) {
  if (liveSection_) liveSection_->setVisible(active && !geographic_);
  if (active) updateLiveSection();
 });
 connect(viewport_, &Viewport::plotDrawingChanged, this, [this](bool active, int vertices) {
  if (plotDrawBtn_) plotDrawBtn_->setVisible(!active);
  if (plotFinishBtn_) plotFinishBtn_->setVisible(active);
  if (plotUndoBtn_) plotUndoBtn_->setVisible(active);
  if (plotCancelBtn_) plotCancelBtn_->setVisible(active);
  if (plotDraftHint_) {
   plotDraftHint_->setText(active
    ? trText("Đang vẽ %1 đỉnh · Giữ Shift+kéo để vẽ nét tự do · Nhấp đúp/Enter khép ranh.",
             "Drawing %1 vertices · Hold Shift+drag freehand · Double-click/Enter to close.").arg(vertices)
    : trText("Bấm chấm đỉnh trên khung 2D/3D hoặc giữ Shift + kéo chuột để vẽ nét liền. Nhấp đúp/Enter để khép.",
             "Click vertices on 2D/3D viewport or hold Shift + drag freehand. Double-click/Enter to close."));
  }
 });
 connect(viewport_, &Viewport::plotDrawn, this, [this](const QPolygonF &ring) {
  pushUndoState();
  plot_.active = true;
  plot_.isPolygon = true;
  plot_.customRing = ring;
  plot_.showDimensions = !plotDims_ || plotDims_->isChecked();
  viewport_->setPlot(plot_);
  syncPlotUi();
  syncMassingUi();
  markModified();
  const QString msg = trText("Đã tạo khu đất đồ án tự do (%1 đỉnh · %2 m²) — kéo đỉnh để chỉnh sửa!", "Created freehand plot (%1 vertices · %2 m²) — drag vertices to edit!")
   .arg(ring.size()).arg(qRound(plot_.areaM2()));
  statusBar()->showMessage(msg, 6000);
  showToast(msg, QColor("#e11d48"), 3000);
 });
 connect(viewport_, &Viewport::plotEdited, this, [this](const ga::PlotDesign &edited) {
  pushUndoState();
  plot_ = edited;
  syncPlotUi();
  syncMassingUi();
  markModified();
  showToast(trText("Đã cập nhật ranh khu đất (%1 m²)", "Updated plot boundary (%1 m²)").arg(qRound(plot_.areaM2())), QColor("#e11d48"), 1900);
 });
 connect(viewport_, &Viewport::model3dMoved, this, [this](int index, double x, double y) {
  if (index < 0 || index >= models3d_.size()) return;
  models3d_[index].x = x;
  models3d_[index].y = y;
  if (model3dList_ && model3dList_->currentRow() != index) {
   const QSignalBlocker b(model3dList_);
   model3dList_->setCurrentRow(index);
  }
  syncModels3dUi();
  if (viewport_->isSectionCutActive()) updateLiveSection();
  markModified();
 });
 connect(viewport_, &Viewport::placingModel3dChanged, this, [this](bool active) {
  if (m3dPlaceBtn_) {
   const QSignalBlocker b(m3dPlaceBtn_);
   m3dPlaceBtn_->setChecked(active);
  }
 });
 connect(viewport_, &Viewport::dockActionTriggered, this, [this](const QString &id) {
  const auto c = viewport_->camera();
  if (id == "north") showToast(trText("Đã xoay mượt về hướng Bắc (0°)", "Aligned smoothly to North (0°)"), QColor("#3b63fb"), 1800);
  else if (id == "reset") showToast(trText("Đã đưa khu đất về tâm khung nhìn", "Recentered site view"), QColor("#3b63fb"), 1800);
  else if (id == "2d") showToast(trText("Chế độ Mặt bằng 2D", "2D Plan View"), QColor("#16a34a"), 1800);
  else if (id == "iso") showToast(trText("Chế độ Trục đo Đẳng cự 3D", "3D Isometric View"), QColor("#3b63fb"), 1800);
  else if (id == "3d") showToast(trText("Chế độ Phối cảnh 3D", "3D Perspective View"), QColor("#3b63fb"), 1800);
  else if (id == "explode") showToast(c.exploded ? trText("Đã bật Tách tầng 3D", "3D Exploded Stack enabled") : trText("Đã thu gọn các tầng 3D", "3D Exploded Stack collapsed"), QColor("#3b63fb"), 2000);
  else if (id == "grid") showToast(c.grid ? trText("Đã bật lưới tọa độ 100 m", "100 m grid enabled") : trText("Đã ẩn lưới tọa độ", "Coordinate grid hidden"), QColor("#64748b"), 1800);
  else if (id == "measure") showToast(viewport_->isMeasuring() ? trText("Thước đo (M): bấm nhiều điểm đo khoảng cách & diện tích", "Ruler (M): click points to measure distance & area") : trText("Đã tắt thước đo", "Ruler closed"), QColor("#0ea5e9"), 2200);
  else if (id == "section") showToast(viewport_->isSectionCutActive() ? trText("Mặt cắt trực tiếp A–A': kéo 2 đầu A / A' ngay trên bản vẽ!", "Live Section A–A': drag A / A' handles directly on viewport!") : trText("Đã đóng mặt cắt trực tiếp", "Live Section closed"), QColor("#3b63fb"), 2400);
  else if (id == "sun") {
   syncSolarUi();
   showToast(c.solar.enabled ? trText("Nắng 3D · GPU PCF %1px đã bật", "Sun 3D · %1px GPU PCF enabled").arg(gfxConfig_.shadowMapSize) : trText("Đã tắt bóng đổ 3D", "3D shadows disabled"), QColor("#f59e0b"), 2200);
  }
 });
 connect(viewport_, &Viewport::cursorPosition, this, [this](const QPointF &p) {
  const auto ll = ga::geographicPoint(p, project_.site);
  cursorText_->setText(QString::number(ll.y(), 'f', 5) + ", " + QString::number(ll.x(), 'f', 5));
 });
 connect(viewport_, &Viewport::graphicsReady, this, [this](const QString &r) {
  statusText_->setToolTip(r);
  applyHardwareProfile();
 });

 rebuildIcons();
 setLanguage(false);
 ux::applyHandCursors(this);
}

void MainWindow::populateDiagramList() {
 if (!diagramList_) return;
 const QSignalBlocker block(diagramList_);
 diagramList_->clear();
 struct Row { const char *id; const char *vi; const char *en; const char *hex; const char *cat; };
 const QVector<Row> rows = {
  // 25 Kiến trúc, Đô thị & Hạ tầng (exact GroundAtlas v2 Web)
  {"context", "cơ bản (CAD)", "base (CAD)", "#1a1918", "arch"},
  {"footprint", "khối công trình", "building footprints", "#c45632", "arch"},
  {"figure", "đặc – rỗng (figure-ground)", "figure–ground", "#2d2c2a", "arch"},
  {"analytic:city-block", "ô phố đô thị (mật độ)", "city blocks (density)", "#dc4c3a", "arch"},
  {"use", "công năng công trình", "building use", "#d1843b", "arch"},
  {"height", "chiều cao công trình", "building height", "#c85d46", "arch"},
  {"floors", "số tầng công trình", "floor count", "#b86b52", "arch"},
  {"analytic:floor-area", "diện tích đế công trình", "building floor area", "#a84438", "arch"},
  {"age", "năm xây dựng", "construction year", "#8a7060", "arch"},
  {"landuse", "sử dụng đất OSM", "OSM land use", "#a38752", "arch"},
  {"parcels", "ranh thửa (địa chính)", "cadastral parcels", "#9966aa", "arch"},
  {"planning", "quy hoạch đã nhập", "imported planning", "#8a5a9e", "arch"},
  {"green", "mảng xanh", "green space", "#4c8f5c", "arch"},
  {"park", "công viên & vườn", "parks & gardens", "#66a36e", "arch"},
  {"trees", "cây đơn lẻ", "individual trees", "#3e7d4a", "arch"},
  {"water", "mặt nước", "water bodies", "#3b8fb8", "arch"},
  {"roads", "mạng giao thông", "transport network", "#d97742", "arch"},
  {"walk", "đường đi bộ", "pedestrian routes", "#c78b36", "arch"},
  {"cycle", "hạ tầng xe đạp", "cycle infrastructure", "#589c78", "arch"},
  {"transit", "giao thông công cộng", "public transit", "#4a75b8", "arch"},
  {"analytic:business", "kinh doanh & thương mại", "business & commerce", "#ea580c", "arch"},
  {"amenity", "tiện ích công cộng", "public amenities", "#c9587a", "arch"},
  {"school", "giáo dục", "education", "#4a89b8", "arch"},
  {"health", "y tế", "healthcare", "#c94a4a", "arch"},
  {"heritage", "di sản được gắn thẻ", "tagged heritage", "#9e6b3b", "arch"},
  // 20 Môi trường, Mặt trời, Gió, Giao thông & Địa hình vệ tinh
  {"analytic:sunpath-2d", "đường đi mặt trời (2D)", "2D polar sun-path", "#d97706", "env"},
  {"analytic:shadow-2d", "bóng đổ theo giờ (2D)", "hourly shadows (2D)", "#475569", "env"},
  {"analytic:sunlight-hours", "giờ nắng trực tiếp", "direct sunlight hours", "#f59e0b", "env"},
  {"analytic:solar-radiation", "bức xạ mặt trời (kWh/m²)", "solar radiation (kWh/m²)", "#ea580c", "env"},
  {"analytic:svf", "hệ số nhìn trời (SVF)", "sky view factor (SVF)", "#1e3a8a", "env"},
  {"analytic:viewshed", "trường nhìn 360° (Isovist)", "360° isovist viewshed", "#10b981", "env"},
  {"analytic:wind-flow", "trường luồng gió 2D", "2D wind streamlines", "#0d9488", "env"},
  {"analytic:wind-rose", "hoa gió khí hậu (TMY)", "climate wind rose (TMY)", "#0284c7", "env"},
  {"analytic:utci", "tiện nghi nhiệt UTCI (°C)", "thermal comfort UTCI (°C)", "#dc2626", "env"},
  {"analytic:walkshed", "tiếp cận đi bộ 5–10–15p", "5–10–15 min walkshed", "#16a34a", "env"},
  {"analytic:traffic-noise", "tiếng ồn giao thông dB(A)", "traffic noise dB(A)", "#be123c", "env"},
  {"analytic:congestion", "áp lực & ùn tắc giao thông", "traffic congestion", "#dc2626", "env"},
  {"env:gedtm30", "cao độ địa hình (DEM)", "ground elevation (DEM)", "#854d0e", "env"},
  {"env:terrain", "đồng mức & cao độ", "terrain contours", "#a16207", "env"},
  {"env:slope", "độ dốc địa hình (°)", "terrain slope (°)", "#b45309", "env"},
  {"env:flood", "nguy cơ ngập (RP100)", "flood risk (RP100)", "#2563eb", "env"},
  {"env:landsat", "nhiệt độ bề mặt (LST)", "ground surface heat (LST)", "#dc2626", "env"},
  {"env:sentinel", "thảm thực vật (NDVI)", "vegetation NDVI", "#15803d", "env"},
  {"env:canopy", "chiều cao tán cây", "tree canopy height", "#166534", "env"},
  {"env:population", "mật độ dân số (WorldPop)", "population density", "#be185d", "env"},
  // Quy hoạch sử dụng đất Hà Nội (quyhoach.hanoi.vn — 1-click toggle) + quy hoạch nâng cao
  {"hanoi-qh:toggle", "Quy hoạch sử dụng đất Hà Nội (quyhoach.hanoi.vn)", "Hanoi Land-Use Plan Overlay (quyhoach.hanoi.vn)", "#dc2626", "plan"},
  {"planning:qhc-2045", "QH Chung 2045 · Metro & Hạ tầng (1/25k)", "Master Plan 2045 · Metro & Corridors (1:25k)", "#7c3aed", "plan"},
  {"planning:qhpk-2000", "QH Phân khu · Sử dụng đất TT 04 (1/2k)", "Zoning Plan · TT 04/2022 Land Use (1:2k)", "#f28d42", "plan"},
  {"planning:qhct-500", "QH Chi tiết · Khung tầng cao 3D (1/500)", "Detailed Plan · 3D Height Envelope (1:500)", "#d9ab62", "plan"},
  {"planning:compare", "Đối chiếu Hiện trạng ↔ Quy hoạch", "Existing vs. Future Planning Diff", "#e11d48", "plan"},
 };
 const QString query = diagramSearch_ ? diagramSearch_->text().trimmed().toLower() : QString();
 const QString activeKey = !activePlanningLevel_.isEmpty()
  ? ("planning:" + activePlanningLevel_)
  : (activeAnalytic_.isEmpty() ? diagram_ : (activeAnalytic_.startsWith("env:") ? activeAnalytic_ : ("analytic:" + activeAnalytic_)));
 for (const auto &r : rows) {
  if (diagramCategory_ != "all" && diagramCategory_ != QLatin1String(r.cat)) continue;
  const QString labelText = trText(r.vi, r.en);
  if (!query.isEmpty() && !labelText.toLower().contains(query) && !QString::fromLatin1(r.id).contains(query)) continue;
  auto *it = new QListWidgetItem(colorDotIcon(QColor(r.hex)), labelText, diagramList_);
  it->setData(Qt::UserRole, QString::fromLatin1(r.id));
  if (QString::fromLatin1(r.id) == activeKey) diagramList_->setCurrentItem(it);
 }
 // + 20 Curated Studio Diagram Kits (ga-diagram-*)
 if (diagramCategory_ == "all" || diagramCategory_ == "kit") {
  for (const auto &kit : ga::diagramPresets()) {
   const QString labelText = (english_ ? kit.en : kit.vi);
   if (!query.isEmpty() && !labelText.toLower().contains(query) && !kit.id.contains(query)) continue;
   auto *it = new QListWidgetItem(colorDotIcon(kit.palette.ink), labelText, diagramList_);
   it->setData(Qt::UserRole, kit.id);
   if (kit.id == activeKey) diagramList_->setCurrentItem(it);
  }
 }
}

void MainWindow::setLanguage(bool en) {
 english_ = en;
 setChromeLanguage(this, en);
 siteSelection_->setLanguage(en);
 const bool old = updating_;
 updating_ = true;
 if (langViBtn_ && langEnBtn_) {
  const QSignalBlocker b1(langViBtn_), b2(langEnBtn_);
  langViBtn_->setChecked(!en);
  langEnBtn_->setChecked(en);
 }
 for (const auto &fn : translations_) fn();
 viewport_->setLanguage(en);
 map_->setLanguage(en);
 if (liveSection_) liveSection_->setLanguage(en);

 const auto selectedStyle = project_.style, selectedDiagram = diagram_, projection = viewport_->camera().projection;
 styles_->clear();
 for (const auto &p : ga::palettes()) styles_->addItem(en ? p.en : p.vi, p.id);
 styles_->setCurrentIndex(styles_->findData(ga::paletteFor(selectedStyle).id));

 diagrams_->clear();
 for (const auto &row : QVector<QStringList>{
       {"context", "Bối cảnh tổng hợp", "Site context"}, {"figure", "Đặc–rỗng", "Figure–ground"},
       {"footprint", "Dấu chân công trình", "Building footprints"}, {"height", "Chiều cao công trình", "Building height"},
       {"floors", "Số tầng công trình", "Floor count"}, {"age", "Năm xây dựng", "Construction year"},
       {"use", "Công năng công trình", "Building use"}, {"roads", "Mạng giao thông", "Transport network"},
       {"walk", "Đường đi bộ", "Pedestrian routes"}, {"cycle", "Hạ tầng xe đạp", "Cycle infrastructure"},
       {"transit", "Giao thông công cộng", "Public transport"}, {"green", "Mảng xanh", "Green space"},
       {"water", "Mặt nước", "Water"}, {"park", "Công viên & vườn", "Parks & gardens"},
       {"amenity", "Tiện ích", "Amenities"}, {"school", "Giáo dục", "Education"},
       {"health", "Y tế", "Healthcare"}, {"heritage", "Di sản được gắn thẻ", "Tagged heritage"},
       {"trees", "Cây đơn lẻ", "Individual trees"}, {"landuse", "Sử dụng đất OSM", "OSM land use"},
       {"planning", "Quy hoạch đã nhập", "Imported planning"}, {"parcels", "Ranh thửa đã nhập", "Imported parcels"}}) {
  diagrams_->addItem(row[en ? 2 : 1], row[0]);
 }
 for (const auto &d : ga::diagramPresets()) diagrams_->addItem(en ? d.en : d.vi, d.id);
 diagrams_->setCurrentIndex(std::max(0, diagrams_->findData(selectedDiagram)));
 populateDiagramList();

 projections_->clear();
 for (const auto &row : QVector<QStringList>{
       {"axonometric", "Trục đo", "Axonometric"}, {"isometric", "Trục đo đẳng cự", "Isometric"},
       {"perspective", "Phối cảnh", "Perspective"}, {"one-point", "Phối cảnh 1 điểm tụ", "One-point perspective"},
       {"two-point", "Phối cảnh 2 điểm tụ", "Two-point perspective"}}) {
  projections_->addItem(row[en ? 2 : 1], row[0]);
 }
 projections_->setCurrentIndex(std::max(0, projections_->findData(projection)));

 if (sunSeason_) {
  const int curDay = sunSeason_->currentData().isValid() ? sunSeason_->currentData().toInt() : viewport_->camera().solar.dayOfYear;
  sunSeason_->clear();
  sunSeason_->addItem(trText("Hạ chí (21/06)", "Summer solstice (Jun 21)"), 172);
  sunSeason_->addItem(trText("Xuân / Thu phân (21/03)", "Equinox (Mar 21 / Sep 22)"), 80);
  sunSeason_->addItem(trText("Đông chí (21/12)", "Winter solstice (Dec 21)"), 355);
  sunSeason_->setCurrentIndex(std::max(0, sunSeason_->findData(curDay)));
 }
 syncSolarUi();
 syncPlotUi();
 syncMassingUi();

 const auto currentGroup = layerGroup_->currentData();
 {
  const QSignalBlocker block(layerGroup_);
  layerGroup_->clear();
  layerGroup_->addItem(trText("Tất cả nhóm lớp", "All layer groups"), QString());
  QSet<QString> seen;
  for (const auto &l : ga::defaultLayers()) {
   if (!l.groupEn.isEmpty() && !seen.contains(l.groupEn)) {
    seen.insert(l.groupEn);
    layerGroup_->addItem(en ? l.groupEn : l.groupVi, l.groupEn);
   }
  }
  layerGroup_->setCurrentIndex(std::max(0, layerGroup_->findData(currentGroup)));
 }
 sourceChoice_->setItemText(1, trText("Dữ liệu đang mở", "Current project data"));
 for (int i = 2; i < sourceChoice_->count(); ++i) {
  const auto entry = sourceChoice_->itemData(i, Qt::UserRole + 1).toJsonObject();
  sourceChoice_->setItemText(i, entry[en ? "en" : "vi"].toString());
 }
 syncSelection();
 notes_->setPlaceholderText(trText("Ý tưởng, giả định và ghi chú cho khu đất…", "Design ideas, assumptions and site notes…"));
 updating_ = old;
 syncImports();
 syncLayers();
 syncInspector(viewport_->selected());
 syncTitle();
 QTimer::singleShot(0, this, [this] {
  for (auto *track : segTracks_) if (track) track->syncImmediate();
 });
}

void MainWindow::syncLayers() {
 const QSignalBlocker blocker(layerTree_);
 const auto selected = layerTree_->currentItem() ? layerTree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
 layerTree_->clear();
 for (const auto &l : layers_) {
  auto *item = new QTreeWidgetItem(layerTree_);
  item->setText(0, english_ ? l.en : l.vi);
  item->setData(0, Qt::UserRole, l.id);
  item->setData(0, Qt::UserRole + 1, l.groupEn);
  item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
  item->setCheckState(0, l.visible ? Qt::Checked : Qt::Unchecked);
  item->setBackground(1, l.color);
  item->setText(2, QLocale(english_ ? "en_US" : "vi_VN").toString(detailedCounts_.value(l.id)));
  item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
  const auto hint = (english_ ? l.groupEn : l.groupVi) + " · " + l.id + QString(" · %1 %").arg(qRound(l.opacity * 100));
  item->setToolTip(0, hint);
  item->setHidden(!(item->text(0) + hint).contains(layerSearch_->text().trimmed(), Qt::CaseInsensitive));
  if (l.id == selected) layerTree_->setCurrentItem(item);
 }
 filterLayers();
}

void MainWindow::filterLayers() {
 if (!layerTree_ || !layerSearch_ || !layerGroup_) return;
 const auto query = layerSearch_->text().trimmed(), group = layerGroup_->currentData().toString();
 for (int i = 0; i < layerTree_->topLevelItemCount(); ++i) {
  auto *item = layerTree_->topLevelItem(i);
  item->setHidden(!(item->text(0) + item->toolTip(0)).contains(query, Qt::CaseInsensitive) || (!group.isEmpty() && item->data(0, Qt::UserRole + 1) != group));
 }
}

void MainWindow::editLayer() {
 auto *item = layerTree_->currentItem();
 if (!item) return;
 const auto id = item->data(0, Qt::UserRole).toString();
 for (auto &l : layers_) {
  if (l.id == id) {
   if (editLayerPresentation(l, english_, this)) {
    syncLayers();
    viewport_->setLayers(layers_);
    syncMap();
    markModified();
   }
   return;
  }
 }
}

void MainWindow::moveLayer(int delta) {
 auto *item = layerTree_->currentItem();
 if (!item) return;
 const auto id = item->data(0, Qt::UserRole).toString();
 for (int i = 0; i < layers_.size(); ++i) {
  if (layers_[i].id == id) {
   const int to = std::clamp(i + delta, 0, int(layers_.size()) - 1);
   layers_.move(i, to);
   syncLayers();
   viewport_->setLayers(layers_);
   syncMap();
   markModified();
   return;
  }
 }
}

void MainWindow::applyDiagram(const QString &id) {
 pushUndoState();
 diagram_ = id;
 if (id.startsWith("ga-diagram-")) {
  project_.style = id;
  layers_ = ga::presentationLayers(layers_, id, true);
  viewport_->setPalette(ga::paletteFor(id));
  const QSignalBlocker block(styles_);
  styles_->setCurrentIndex(-1);
 } else if (id == "context") {
  const auto defaults = ga::defaultLayers();
  for (auto &layer : layers_) for (const auto &d : defaults) if (d.id == layer.id) layer.visible = d.visible;
 } else {
  layers_ = ga::thematicLayers(layers_, id);
 }
 viewport_->setLayers(layers_);
 viewport_->setDiagram(id);
 if (geographic_) showArchitecturalView(viewport_->camera());
 syncLayers();
 syncMap();
 populateDiagramList();
 markModified();
 syncInspector(viewport_->selected());
 if (!ga::diagramAvailable(project_, id)) {
  statusBar()->showMessage(trText("Chưa có lớp dữ liệu cần thiết cho sơ đồ này ở khu đất đang mở.", "No required data for this diagram."), 10000);
 } else if (diagramList_ && diagramList_->currentItem()) {
  showToast(trText("Sơ đồ: ", "Diagram: ") + diagramList_->currentItem()->text(), QColor("#3b63fb"), 1900);
 }
}

void MainWindow::applyAnalytic(const QString &kind) {
 if (project_.features.isEmpty()) return;
 activeAnalytic_ = kind;
 QApplication::setOverrideCursor(Qt::WaitCursor);
 const auto overlay = ga::computeAnalyticalOverlay(
  project_, kind, viewport_->camera().solar, 135.0, {0, 0},
  ga::appDataPath("environment"), ga::appDataPath("climate.json"));
 QApplication::restoreOverrideCursor();
 viewport_->setAnalyticalOverlay(overlay);
 if (geographic_) showArchitecturalView(viewport_->camera());
 if (analyticSummary_) analyticSummary_->setText(english_ ? overlay.summaryEn : overlay.summaryVi);
 populateDiagramList();
 const QString title = english_ ? overlay.titleEn : overlay.titleVi;
 statusBar()->showMessage(title + " — " + (english_ ? overlay.summaryEn : overlay.summaryVi), 8000);
 showToast(title, QColor("#0284c7"), 2300);
}

void MainWindow::syncInspector(int index) {
 const auto s = project_.stats();
 const QLocale locale(english_ ? "en_US" : "vi_VN");
 const double unitMult = imperialUnits_ ? 3.28084 : 1.0;
 const QString unitLbl = imperialUnits_ ? " ft" : " m";
 projectName_->setText(project_.site.name);
 siteDetails_->setText(QString::number(project_.site.lat, 'f', 5) + ", " + QString::number(project_.site.lon, 'f', 5) + " · "
  + locale.toString(project_.site.radius * 2 * unitMult, 'f', 0) + unitLbl);
 if (metricValues_.size() == 4) {
  metricValues_[0]->setText(locale.toString(s.buildings));
  metricValues_[1]->setText(s.buildings ? locale.toString(100. * s.knownHeights / s.buildings, 'f', 0) + "%" : "—");
  metricValues_[2]->setText(s.knownHeights ? locale.toString(s.meanHeight * unitMult, 'f', 1) + unitLbl : "—");
  metricValues_[3]->setText(locale.toString(s.holeCount));
 }
 const auto quality = ga::dataQuality(project_)["height_groups"].toObject();
 statsText_->setText(locale.toString(s.features) + trText(" phần hình học\nChiều cao: ", " geometry parts\nHeights: ")
  + locale.toString(quality["recorded"].toInt()) + trText(" nguồn · ", " recorded · ")
  + locale.toString(quality["ai_estimate"].toInt()) + " AI · "
  + locale.toString(quality["floor_estimate"].toInt()) + trText(" tầng · ", " floors · ")
  + locale.toString(quality["unknown_flat"].toInt()) + trText(" thiếu", " unknown"));
 if (index < 0 || index >= project_.features.size()) {
  selectionText_->setText(trText("Bấm vào dấu chân công trình để xem thuộc tính.", "Click a building footprint to inspect attributes."));
 } else {
  const auto &f = project_.features[index];
  selectionText_->setText(f.tags["name"].toString(f.sourceId)
   + "\n" + trText("Công năng: ", "Use: ") + f.tags["building"].toString("—")
   + "\n" + trText("Dấu chân: ", "Footprint: ") + locale.toString(f.area(), 'f', 1) + " m²"
   + "\n" + trText("Chiều cao: ", "Height: ") + (f.knownHeight()
      ? (locale.toString(f.height * unitMult, 'f', 1) + unitLbl + (f.heightSource == "smart_estimate" ? trText(" (ước tính thông minh)", " (smart est.)") : QString()))
      : trText("Chưa có · giữ phẳng", "Unknown · flat")));
 }
 statusText_->setText(trText("Sẵn sàng · Dữ liệu xử lý trực tiếp trên máy (C++/OpenGL)", "Ready · Native local C++/OpenGL processing"));
}

void MainWindow::applyStyle(const QString &id, bool mark) {
 if (mark) pushUndoState();
 project_.style = id;
 layers_ = ga::presentationLayers(layers_, id, id.startsWith("ga-diagram-"));
 viewport_->setPalette(ga::paletteFor(id));
 if (mark) {
  diagram_ = "context";
  activeAnalytic_.clear();
  viewport_->setAnalyticalOverlay({});
  viewport_->setDiagram(diagram_);
  const QSignalBlocker block(diagrams_);
  diagrams_->setCurrentIndex(diagrams_->findData(diagram_));
  populateDiagramList();
 }
 viewport_->setLayers(layers_);
 syncMap();
 syncLayers();
 if (mark) {
  markModified();
  const auto pal = ga::paletteFor(id);
  showToast(trText("Phong cách: ", "Style: ") + (english_ ? pal.en : pal.vi), QColor("#3b63fb"), 2000);
 }
}

void MainWindow::syncProject() {
 const bool old = updating_;
 updating_ = true;
 activeAnalytic_.clear();
 viewport_->setAnalyticalOverlay({});
 viewport_->setProject(&project_);
 map_->setProject(&project_);
 liveDem_ = {};
 for (const auto &field : ga::environmentFields(ga::appDataPath("environment"), project_.site)) {
  if (field["id"].toString() == "gedtm30") { liveDem_ = ga::TerrainGrid::fromEnvironmentField(field); break; }
 }
 if (viewport_->isSectionCutActive()) updateLiveSection();
 contextStyle_->setChecked(project_.view["map"].toObject()["context_style"].toBool(true));
 map_->setContextEnabled(contextStyle_->isChecked());
 viewport_->setContextStyleEnabled(contextStyle_->isChecked());
 map_->cancelBoundaryDrawing();
 // Unlock boundary selection by default so users can immediately click anywhere on the global Map to pick a new site!
 siteSelection_->setSite(project_.site, false);
 if (project_.view["selection"].isObject()) siteSelection_->restore(project_.view["selection"].toObject());
 const auto sourceId = project_.view["selection_source"].toString(project_.features.isEmpty() ? "osm" : "current");
 sourceChoice_->setCurrentIndex(std::max(0, sourceChoice_->findData(sourceId)));
 importRole_->setCurrentIndex(std::max(0, importRole_->findData(project_.view["import_role"].toString("building"))));
 syncSelection();
 map_->restoreDraft(project_.view["selection_draft"].toObject());
 plot_ = project_.view["plot"].isObject() ? ga::PlotDesign::fromJson(project_.view["plot"].toObject()) : ga::PlotDesign{};
 massing_ = project_.view["massing"].isObject() ? ga::MassingConfig::fromJson(project_.view["massing"].toObject()) : ga::MassingConfig{};
 models3d_.clear();
 for (const auto &mv : project_.view["models3d"].toArray()) {
  auto loaded = ga::ImportedModel3D::fromJson(mv.toObject());
  if (loaded.valid()) models3d_.append(std::move(loaded));
 }
 viewport_->setPlot(plot_);
 viewport_->setMassing(massing_);
 viewport_->setModels3d(models3d_);
 viewport_->setSelectedModel3d(models3d_.isEmpty() ? -1 : 0);
 activePlanningLevel_ = project_.view["planning_level"].toString();
 planningOpacity_ = std::clamp(project_.view["planning_opacity"].toDouble(0.80), 0.15, 1.0);
 planningImage_ = project_.view["planning_image"].isObject() ? ga::PlanningImageOverlay::fromJson(project_.view["planning_image"].toObject()) : ga::PlanningImageOverlay{};
 viewport_->setPlanningImageOverlay(planningImage_);
 const auto hqObj = project_.view["hanoi_planning"].toObject();
 if (!hqObj.isEmpty()) {
  hanoiPlanEnabled_ = hqObj["enabled"].toBool(hanoiPlanEnabled_);
  const QString lid = hqObj["layer"].toString();
  if (lid == "qhpk" || lid == "khsdd" || lid == "qhct") hanoiPlanLayer_ = lid;
  hanoiPlanOpacity_ = std::clamp(hqObj["opacity"].toDouble(hanoiPlanOpacity_), 0.15, 1.0);
 }
 map_->setHanoiPlanning(hanoiPlanEnabled_, hanoiPlanLayer_, hanoiPlanOpacity_);
 viewport_->setHanoiPlanning(hanoiPlanEnabled_, hanoiPlanLayer_, hanoiPlanOpacity_);
 if (hanoiPlanEnabled_) refreshHanoiPlanningTiles();
 else if (hanoiTiles_) hanoiTiles_->cancel();
 syncPlotUi();
 syncMassingUi();
 syncModels3dUi();
 syncPlanningUi();
 if (!activePlanningLevel_.isEmpty()) applyFuturePlanning(activePlanningLevel_);
 else viewport_->setFuturePlanningOverlay({});
 detailedCounts_ = ga::layerCounts(project_.features);
 layers_ = ga::defaultLayers();
 applyStyle(project_.style, false);
 layers_ = ga::restoreLayerState(layers_, project_.layerState, project_.view["layer_order"].toArray());
 viewport_->setLayers(layers_);
 const auto camera = ga::Camera::fromJson(project_.view);
 viewport_->setCamera(camera);
 diagram_ = project_.view["diagram"].toString("context");
 viewport_->setDiagram(diagram_);
 syncMap();
 separation_->setValue(int(camera.separation));
 notes_->setPlainText(project_.notes);
 updating_ = old;
 setLanguage(english_);
 modified_ = false;
 syncTitle();
}

void MainWindow::markModified() {
 if (updating_ || switching_ || navigating_) return;
 modified_ = true;
 ++revision_;
 if (activeDocument_ >= 0 && activeDocument_ < documents_.size()) ++documents_[activeDocument_].contentRevision;
 syncTitle();
 syncTabs();
}

void MainWindow::syncTitle() {
 setWindowTitle((modified_ ? "● " : "") + project_.site.name + " — GroundAtlas Native 2.3.1");
}

bool MainWindow::maybeSave() {
 if (!modified_) return true;
 QMessageBox box(QMessageBox::Question, trText("Lưu thay đổi?", "Save changes?"), trText("Dự án có thay đổi chưa lưu.", "The project has unsaved changes."), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
 box.button(QMessageBox::Save)->setText(trText("Lưu", "Save"));
 box.button(QMessageBox::Discard)->setText(trText("Không lưu", "Discard"));
 box.button(QMessageBox::Cancel)->setText(trText("Hủy", "Cancel"));
 box.setDefaultButton(QMessageBox::Save);
 box.setEscapeButton(QMessageBox::Cancel);
 const auto result = box.exec();
 return result == QMessageBox::Save ? save() : result == QMessageBox::Discard;
}

bool MainWindow::loadPath(const QString &path, bool confirm) {
 Q_UNUSED(confirm);
 if (ga::isSupported3DModelFile(path)) {
  return import3DModel(path);
 }
 const auto canonical = QFileInfo(path).canonicalFilePath();
 for (int i = 0; i < documents_.size(); ++i) {
  if (!documents_[i].path.isEmpty() && QFileInfo(documents_[i].path).canonicalFilePath() == canonical && !canonical.isEmpty()) {
   tabs_->setCurrentIndex(i);
   return true;
  }
 }
 try {
  ga::Project next;
  if (path.endsWith(".gatlas", Qt::CaseInsensitive)) next = ga::Project::load(path);
  else {
   QFile file(path);
   if (!file.open(QIODevice::ReadOnly) || file.size() > 50 * 1024 * 1024) throw std::runtime_error("GeoJSON file could not be read or exceeds 50 MB");
   QJsonParseError error;
   const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
   if (error.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid GeoJSON JSON");
   next = ga::Project::fromGeoJson(doc.object(), QFileInfo(path).completeBaseName());
  }
  const auto sample = QDir::cleanPath(ga::appDataPath("hoankiem.gatlas"));
  const bool isBundledSample = (QDir::cleanPath(path) == sample || QFileInfo(path).fileName() == "hoankiem.gatlas");
  if (isBundledSample && next.layerState.isEmpty()) {
   next.style = "suushka-study";
   next.layerState["amenities"] = QJsonObject{{"id", "amenities"}, {"visible", false}};
  }
  addDocument(std::move(next), QDir::cleanPath(path) == sample || !path.endsWith(".gatlas", Qt::CaseInsensitive) ? QString() : path);
  return true;
 } catch (const std::exception &e) {
  QMessageBox::warning(this, trText("Không mở được tệp", "Could not open file"), ga::errorText(e));
  return false;
 }
}

void MainWindow::open() {
 const auto path = QFileDialog::getOpenFileName(this, trText("Mở dự án hoặc GeoJSON", "Open project or GeoJSON"), {}, "GroundAtlas / GeoJSON (*.gatlas *.geojson *.json)");
 if (!path.isEmpty()) loadPath(path);
}

void MainWindow::newProject() {
 ga::Project next;
 next.site.name = trText("Khu đất mới", "New site");
 next.site.boundary = ga::circleBoundary(next.site.radius);
 next.style = "suushka-study";
 next.layerState["amenities"] = QJsonObject{{"id", "amenities"}, {"visible", false}};
 next.source = {{"provider", "empty-user-project"}, {"attribution", "User project · no existing-condition data loaded"}};
 addDocument(std::move(next));
}

bool MainWindow::save(bool as) {
 QString path = currentPath_;
 if (as || path.isEmpty()) path = QFileDialog::getSaveFileName(this, trText("Lưu dự án GroundAtlas", "Save GroundAtlas project"), project_.site.name + ".gatlas", "GroundAtlas (*.gatlas)");
 if (path.isEmpty()) return false;
 if (!path.endsWith(".gatlas", Qt::CaseInsensitive)) path += ".gatlas";
 project_ = captureProject();
 try {
  project_.save(path);
  currentPath_ = path;
  modified_ = false;
  snapshotDocument();
  syncTitle();
  syncTabs();
  statusBar()->showMessage(trText("Đã lưu dự án", "Project saved"), 5000);
  showToast(trText("Đã lưu: ", "Saved: ") + QFileInfo(path).fileName(), QColor("#16a34a"), 2200);
  return true;
 } catch (const std::exception &e) {
  QMessageBox::warning(this, trText("Không lưu được", "Save failed"), ga::errorText(e));
  return false;
 }
}

void MainWindow::saveAutosave() {
 if (documents_.isEmpty() || !sessionStore_) return;
 snapshotDocument();
 SessionSnapshot snapshot;
 snapshot.active = activeDocument_;
 for (const auto &doc : documents_) snapshot.documents.append({doc.id, doc.path, doc.project, doc.modified, doc.contentRevision});
 sessionStore_->save(std::move(snapshot));
}

void MainWindow::restoreAutosave() {
 if (sessionStore_) sessionStore_->flush();
 try {
  QFile manifest(dataDirectory_ + "/session.json");
  if (manifest.open(QIODevice::ReadOnly)) {
   if (manifest.size() > 1024 * 1024) throw std::runtime_error("Session manifest is too large");
   const auto root = QJsonDocument::fromJson(manifest.readAll()).object();
   if (root["schema"].toInt() != 1) throw std::runtime_error("Unsupported session version");
   const auto rows = root["documents"].toArray();
   if (rows.size() > 32 || rows.size() + documents_.size() > 32) throw std::runtime_error("Too many open tabs");
   QVector<Document> recovered;
   for (const auto &v : rows) {
    const auto row = v.toObject();
    const auto file = row["file"].toString();
    if (file != QFileInfo(file).fileName() || !file.endsWith(".gatlas") || file.contains("..")) throw std::runtime_error("Invalid autosave path");
    recovered.append({QUuid::createUuid().toString(QUuid::WithoutBraces), row["path"].toString(), ga::Project::load(dataDirectory_ + "/sessions/" + file), row["modified"].toBool()});
   }
   const int first = documents_.size();
   for (auto &document : recovered) {
    addDocument(document.project, document.path);
    modified_ = document.modified;
    snapshotDocument();
   }
   if (!recovered.isEmpty()) tabs_->setCurrentIndex(first + std::clamp(root["active"].toInt(), 0, int(recovered.size()) - 1));
   syncTabs();
   return;
  }
  const auto file = dataDirectory_ + "/autosave.gatlas";
  if (!QFileInfo::exists(file)) {
   QMessageBox::information(this, "GroundAtlas", trText("Chưa có bản tự lưu.", "No autosave exists yet."));
   return;
  }
  if (loadPath(file)) { currentPath_.clear(); markModified(); }
 } catch (const std::exception &e) {
  QMessageBox::warning(this, "GroundAtlas", ga::errorText(e));
 }
}

bool MainWindow::exportProjectKit(const QString &outputDir) {
 if (project_.features.isEmpty() || outputDir.isEmpty()) return false;
 QDir dir(outputDir);
 if (!dir.mkpath(".")) return false;
 const auto pal = ga::paletteFor(project_.style);
 const auto curPlot = viewport_ ? viewport_->plot() : plot_;
 const auto curMass = viewport_ ? viewport_->massing() : massing_;
 const ga::PlotDesign *pPtr = curPlot.active ? &curPlot : nullptr;
 const ga::MassingConfig *mPtr = (curPlot.active && curMass.enabled) ? &curMass : nullptr;
 const ga::TerrainGrid *dPtr = liveDem_.valid ? &liveDem_ : nullptr;
 const QVector<ga::ImportedModel3D> *m3dPtr = models3d_.isEmpty() ? nullptr : &models3d_;

 bool ok = true;
 ok &= viewport_->exportPlanPng(dir.filePath("01_Plan_Suushka_4K.png"), 3840, 2160, false);
 ok &= viewport_->exportSun3dPng(dir.filePath("02_Sun3D_GoldenHour_4K.png"), 3840, 2160, false);
 {
  const QPointF a = viewport_->sectionCutA(), b = viewport_->sectionCutB();
  SectionDialog secDlg(project_, layers_, pal, english_, this, false, pPtr, mPtr, &a, &b, m3dPtr);
  ok &= secDlg.exportSvg(dir.filePath("03_Section_AA.svg"));
  ok &= secDlg.exportDxf(dir.filePath("04_Section_AA.dxf"));
 }
 ok &= ga::exportDxf(project_, layers_, pal, diagram_, dir.filePath("05_Site_Model_CAD.dxf"), pPtr, mPtr, dPtr, m3dPtr);
 ok &= ga::exportColladaDae(project_, layers_, pal, diagram_, dir.filePath("06_Site_Model_SketchUp.dae"), pPtr, mPtr, m3dPtr);
 ok &= ga::exportSiteReportPdf(project_, layers_, pal, ga::appDataPath("climate.json"), ga::appDataPath("environment"), english_, dir.filePath("07_Site_Report_A3.pdf"), pPtr, mPtr);
 ok &= viewport_->exportSunTimelapseGif(dir.filePath("08_Sun_Timelapse.gif"), 720, 450, 20, 9);
 return ok;
}

void MainWindow::exportFile(const QString &kind) {
 if (!requireProFeature("Xuất file sản xuất (CAD / SketchUp / 4K / PDF / GIF / Bộ 8 tệp)",
                        "Production Exports (CAD / SketchUp / 4K / PDF / GIF / 8-File Kit)")) {
  return;
 }
 if (project_.features.isEmpty()) {
  QMessageBox::information(this, "GroundAtlas", trText("Mở dữ liệu trước khi xuất.", "Load data before exporting."));
  return;
 }
 const auto pal = ga::paletteFor(project_.style);
 const auto curPlot = viewport_ ? viewport_->plot() : plot_;
 const auto curMass = viewport_ ? viewport_->massing() : massing_;
 const ga::PlotDesign *pPtr = curPlot.active ? &curPlot : nullptr;
 const ga::MassingConfig *mPtr = (curPlot.active && curMass.enabled) ? &curMass : nullptr;
 const ga::TerrainGrid *dPtr = liveDem_.valid ? &liveDem_ : nullptr;
 const QVector<ga::ImportedModel3D> *m3dPtr = models3d_.isEmpty() ? nullptr : &models3d_;

 if (kind == "project-kit") {
  const QString parentDir = QFileDialog::getExistingDirectory(this, trText("Chọn thư mục xuất Bộ hồ sơ Đồ án (8 tệp)", "Choose folder for 1-Click Presentation Kit (8 files)"));
  if (parentDir.isEmpty()) return;
  QString cleanName = project_.site.name;
  cleanName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
  const QString kitDir = QDir(parentDir).filePath(cleanName + "_GroundAtlas_Kit");
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool ok = exportProjectKit(kitDir);
  QApplication::restoreOverrideCursor();
  if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Một số tệp trong bộ hồ sơ chưa xuất được.", "Some files in the presentation kit could not be exported."));
  else {
   const QString msg = trText("Đã xuất trọn Bộ hồ sơ 8 tệp tại: ", "Exported complete 8-file Kit to: ") + QFileInfo(kitDir).fileName();
   statusBar()->showMessage(msg, 9000);
   showToast(msg, QColor("#16a34a"), 3200);
   QDesktopServices::openUrl(QUrl::fromLocalFile(kitDir));
  }
  return;
 }
 if (kind == "sun-gif" || kind == "turntable-gif") {
  const bool isSun = (kind == "sun-gif");
  const QString def = project_.site.name + (isSun ? "_SunTimelapse.gif" : "_Turntable360.gif");
  const QString file = QFileDialog::getSaveFileName(this, trText(isSun ? "Xuất Hoạt ảnh Nắng 1 ngày (.GIF)" : "Xuất Hoạt ảnh Xoay 360° (.GIF)",
                                                                 isSun ? "Export Full-Day Sun Timelapse (.GIF)" : "Export 360° Turntable (.GIF)"),
                                                    def, "Animated GIF (*.gif)");
  if (file.isEmpty()) return;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool ok = isSun ? viewport_->exportSunTimelapseGif(file, 960, 600, 28, 8)
                        : viewport_->exportTurntableGif(file, 960, 600, 36, 7);
  QApplication::restoreOverrideCursor();
  if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Không xuất được tệp hoạt ảnh GIF.", "Could not export animated GIF."));
  else {
   const QString msg = trText("Đã xuất hoạt ảnh GIF: ", "Exported animated GIF: ") + QFileInfo(file).fileName();
   statusBar()->showMessage(msg, 7000);
   showToast(msg, QColor("#16a34a"), 2800);
  }
  return;
 }

 if (kind == "png-4k" || kind == "png-8k-alpha") {
  const bool is8k = (kind == "png-8k-alpha");
  const QString suffix = is8k ? "_8K_transparent.png" : "_4K.png";
  const QString file = QFileDialog::getSaveFileName(this, trText(is8k ? "Xuất PNG 8K nền trong suốt" : "Xuất PNG 4K siêu nét", is8k ? "Export 8K Transparent PNG" : "Export 4K Render PNG"), project_.site.name + suffix, "PNG (*.png)");
  if (file.isEmpty()) return;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool ok = viewport_->exportHighResPng(file, is8k ? 7680 : 3840, is8k ? 4320 : 2160, is8k);
  QApplication::restoreOverrideCursor();
  if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Không render được ảnh độ phân giải cao.", "Could not render high-resolution PNG."));
  else {
   const QString msg = trText("Đã xuất ảnh %1: ", "Exported %1 PNG: ").arg(is8k ? "8K Alpha" : "4K") + QFileInfo(file).fileName();
   statusBar()->showMessage(msg, 6000);
   showToast(msg, QColor("#16a34a"), 2600);
  }
  return;
 }
 if (kind == "skp") {
  const QString file = QFileDialog::getSaveFileName(this, trText("Lưu mô hình SketchUp (.skp)", "Save SketchUp Model (.skp)"), project_.site.name + ".skp", "SketchUp (*.skp)");
  if (file.isEmpty()) return;
  const QString dae = file.left(file.size() - 4) + ".dae";
  if (!ga::exportColladaDae(project_, layers_, pal, diagram_, dae, pPtr, mPtr, m3dPtr)) { QMessageBox::warning(this, "GroundAtlas", trText("Không ghi được tệp .dae.", "Could not write .dae.")); return; }
  QString err;
  if (!ga::openInSketchUpWithSkpSave(dae, file, &err)) QMessageBox::information(this, "GroundAtlas", trText("Đã xuất .dae chuẩn mét.\n", "Exported metric .dae.\n") + err);
  else statusBar()->showMessage(trText("Đang mở SketchUp để nhập & lưu: ", "Launching SketchUp to save: ") + QFileInfo(file).fileName(), 8000);
  return;
 }
 if (kind == "blend") {
  const QString file = QFileDialog::getSaveFileName(this, trText("Lưu mô hình Blender (.blend)", "Save Blender Model (.blend)"), project_.site.name + ".blend", "Blender (*.blend)");
  if (file.isEmpty()) return;
  const QString glb = file.left(file.size() - 6) + ".glb";
  if (!ga::exportGlb(project_, layers_, pal, diagram_, glb, pPtr, mPtr, m3dPtr)) { QMessageBox::warning(this, "GroundAtlas", trText("Không ghi được .glb.", "Could not write .glb.")); return; }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString err;
  const bool ok = ga::convertGlbToBlendNative(glb, file, true, &err);
  QApplication::restoreOverrideCursor();
  if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Đã xuất .glb nhưng chưa chạy được Blender:\n", "Exported .glb, Blender error:\n") + err);
  else statusBar()->showMessage(trText("Đã tạo & mở file Blender thật: ", "Created & opened .blend: ") + QFileInfo(file).fileName(), 8000);
  return;
 }
 if (kind == "autocad") {
  const QString file = QFileDialog::getSaveFileName(this, trText("Lưu & Mở trong AutoCAD (.dxf)", "Save & Open in AutoCAD (.dxf)"), project_.site.name + ".dxf", "AutoCAD DXF (*.dxf)");
  if (file.isEmpty()) return;
  if (!ga::exportDxf(project_, layers_, pal, diagram_, file, pPtr, mPtr, dPtr, m3dPtr)) { QMessageBox::warning(this, "GroundAtlas", trText("Không ghi được DXF.", "Could not write DXF.")); return; }
  QString err;
  if (!ga::openInAutoCADNative(file, &err)) QMessageBox::information(this, "GroundAtlas", trText("Đã xuất DXF: ", "Exported DXF: ") + file + "\n" + err);
  else statusBar()->showMessage(trText("Đang mở AutoCAD: ", "Opening AutoCAD: ") + QFileInfo(file).fileName(), 8000);
  return;
 }
 if (kind == "report") {
  const QString file = QFileDialog::getSaveFileName(this, trText("Xuất Báo cáo Hồ sơ A3 (.PDF)", "Export 3-Page Site Report (.PDF)"), project_.site.name + "_Report_A3.pdf", "PDF (*.pdf)");
  if (file.isEmpty()) return;
  const bool ok = ga::exportSiteReportPdf(project_, layers_, pal, ga::appDataPath("climate.json"), ga::appDataPath("environment"), english_, file, pPtr, mPtr);
  if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Không xuất được báo cáo PDF.", "Could not export report PDF."));
  else statusBar()->showMessage(trText("Đã xuất Báo cáo 3 trang A3: ", "Exported 3-Page A3 Report: ") + QFileInfo(file).fileName(), 7000);
  return;
 }
 if (kind == "laser-dxf" || kind == "laser-svg") {
  const bool isSvg = (kind == "laser-svg");
  const QString ext = isSvg ? "svg" : "dxf";
  const QString file = QFileDialog::getSaveFileName(this, trText("Xuất bản vẽ Cắt Laser", "Export Laser Cut"), project_.site.name + "_laser." + ext, ext.toUpper() + " (*." + ext + ")");
  if (file.isEmpty()) return;
  ga::LaserCutOptions opts;
  opts.scaleDenom = laserScale_ ? laserScale_->currentData().toDouble() : 1000.0;
  opts.separateBaseplate = !laserSeparate_ || laserSeparate_->isChecked();
  opts.engraveRoads = !laserRoads_ || laserRoads_->isChecked();
  opts.includeWater = !laserWater_ || laserWater_->isChecked();
  const bool ok = isSvg ? ga::exportLaserCutSvg(project_, opts, file) : ga::exportLaserCutDxf(project_, opts, file);
  if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Không ghi được tệp Cắt Laser.", "Could not write laser file."));
  else statusBar()->showMessage(trText("Đã xuất Laser Cut: ", "Exported Laser Cut: ") + QFileInfo(file).fileName(), 6000);
  return;
 }

 const QString name = project_.site.name + "." + kind;
 const auto file = QFileDialog::getSaveFileName(this, trText("Xuất tệp", "Export file"), name, kind.toUpper() + " (*." + kind + ")");
 if (file.isEmpty()) return;
 bool ok = false;
 if (kind == "png") {
  map_->finishInteraction();
  viewport_->finishInteraction();
  ok = geographic_ ? map_->grabFramebuffer().save(file, "PNG") : viewport_->grabFramebuffer().save(file, "PNG");
 } else if (kind == "svg" || kind == "pdf") ok = viewport_->exportVector(file, kind == "pdf");
 else if (kind == "obj") ok = viewport_->exportObj(file);
 else if (kind == "dxf") ok = ga::exportDxf(project_, layers_, pal, diagram_, file, pPtr, mPtr, dPtr, m3dPtr);
 else if (kind == "dae") ok = ga::exportColladaDae(project_, layers_, pal, diagram_, file, pPtr, mPtr, m3dPtr);
 else if (kind == "glb") ok = ga::exportGlb(project_, layers_, pal, diagram_, file, pPtr, mPtr, m3dPtr);
 else if (kind == "stl") ok = ga::exportStl(project_, layers_, pal, diagram_, file, pPtr, mPtr, m3dPtr);
 else {
  QSaveFile out(file);
  if (out.open(QIODevice::WriteOnly)) {
   const auto bytes = QJsonDocument(project_.geoJson()).toJson();
   ok = out.write(bytes) == bytes.size() && out.commit();
  }
 }
 if (!ok) QMessageBox::warning(this, "GroundAtlas", trText("Không ghi được tệp xuất.", "Could not write export file."));
 else {
  const QString msg = trText("Đã xuất: ", "Exported: ") + QFileInfo(file).fileName();
  statusBar()->showMessage(msg, 5000);
  showToast(msg, QColor("#16a34a"), 2400);
 }
}

void MainWindow::showSource() {
 QMessageBox box(this);
 box.setTextFormat(Qt::PlainText);
 box.setWindowTitle(trText("Nguồn & giới hạn dữ liệu", "Sources & data limitations"));
 box.setIcon(QMessageBox::Information);
 box.setText(project_.source.value("attribution").toString());
 box.setInformativeText(trText(
  "GroundAtlas Native 2.2.0 chạy 100% bằng mã máy C++/Qt/OpenGL trên thiết bị của bạn (tự động tối ưu cấu hình & màn hình lên tới 500 Hz).\n\n"
  "• Quy hoạch tương lai Hà Nội: khung tham chiếu QĐ 1668/QĐ-TTg (2045–2065) & QĐ 6075/QĐ-UBND (Sông Hồng 1/5000), bảng màu TT 04/2022/TT-BXD, hỗ trợ nhập DXF hệ VN-2000 Hà Nội (múi 3°, 105°E) và phủ ảnh bản đồ phân khu 1/2000.\n"
  "• Nhập mô hình 3D trực tiếp: hỗ trợ SketchUp (.skp trực tiếp qua SketchUpAPI.dll nếu có cài SU, cùng .dae/.obj/.glb/.stl không cần phần mềm ngoài), kéo đặt trên khu đất, đổ bóng 4096px và cắt qua mặt cắt A–A'.\n"
  "• Bản đồ nền toàn cầu: MapLibre Native for Qt (OpenFreeMap Vector + Esri World Imagery Satellite).\n"
  "• Nguồn OSM toàn cầu: tự động xoay vòng đa máy chủ Overpass (overpass.openstreetmap.fr ưu tiên) + ước tính chiều cao thông minh tùy chọn.\n"
  "• Chiều cao công trình có thể từ nguồn OSM, ước tính AI Google 2023, số tầng × 3 m, hoặc ước tính thông minh (có gắn nhãn rõ).\n"
  "• Phân tích môi trường (Nắng, Bức xạ, UTCI, SVF, Gió, Tiếng ồn, Walkshed) được tính trực tiếp từ hình học khu đất và trạm khí hậu TMY đi kèm.",
  "GroundAtlas Native 2.1.0 runs 100% in compiled C++/Qt/OpenGL on your machine (auto hardware profiling up to 500 Hz).\n\n"
  "• Native 3D Model Import: supports SketchUp (.skp directly via installed SketchUpAPI.dll, plus zero-dependency .dae/.obj/.glb/.stl), drag placement, real 4096px GPU shadows & live Section A–A'.\n"
  "• Global basemap: MapLibre Native for Qt (OpenFreeMap Vector + Esri World Imagery Satellite).\n"
  "• Global OSM loader: automatic multi-mirror Overpass failover (overpass.openstreetmap.fr primary) + optional smart height estimation.\n"
  "• Building heights come from OSM, Google AI 2023 estimates, floor counts × 3 m, or labeled smart estimates.\n"
  "• Environmental analytics are computed natively from site geometry and bundled TMY stations."));
 QJsonObject detail{{"source", project_.source}, {"imports", ga::geoJsonImportSummaries(project_)}, {"quality", ga::dataQuality(project_)}};
 box.setDetailedText(QString::fromUtf8(QJsonDocument(detail).toJson(QJsonDocument::Indented)));
 box.exec();
}

void MainWindow::showAnalysis() { AnalysisDialog dialog(project_, english_, this, darkTheme_); dialog.exec(); }

void MainWindow::updateLiveSection() {
 if (!liveSection_ || !viewport_ || !viewport_->isSectionCutActive() || project_.features.isEmpty()) return;
 const auto mode = (terrain3d_ && liveDem_.valid) ? ga::SectionMode::TerrainDem : ga::SectionMode::FlatUrban;
 const auto curPlot = viewport_->plot();
 const auto curMass = viewport_->massing();
 const auto &curM3d = viewport_->models3d();
 const auto sec = ga::computeSection(
  project_, viewport_->sectionCutA(), viewport_->sectionCutB(), mode,
  liveDem_.valid ? &liveDem_ : nullptr, 120.0,
  (curPlot.active && curMass.enabled) ? &curPlot : nullptr,
  (curPlot.active && curMass.enabled) ? &curMass : nullptr,
  curM3d.isEmpty() ? nullptr : &curM3d);
 liveSection_->setSectionData(sec, project_.site.name);
}

void MainWindow::showSection() {
 if (project_.features.isEmpty()) {
  QMessageBox::information(this, "GroundAtlas", trText("Nạp dữ liệu khu đất trước khi mở Mặt cắt.", "Load site data before opening Section Studio."));
  return;
 }
 const QPointF a = viewport_->sectionCutA(), b = viewport_->sectionCutB();
 const auto curPlot = viewport_->plot();
 const auto curMass = viewport_->massing();
 const auto &curM3d = viewport_->models3d();
 SectionDialog dialog(project_, layers_, ga::paletteFor(project_.style), english_, this, darkTheme_,
                      (curPlot.active && curMass.enabled) ? &curPlot : nullptr,
                      (curPlot.active && curMass.enabled) ? &curMass : nullptr,
                      viewport_->isSectionCutActive() ? &a : nullptr,
                      viewport_->isSectionCutActive() ? &b : nullptr,
                      curM3d.isEmpty() ? nullptr : &curM3d);
 dialog.exec();
}

void MainWindow::showStyleGallery() {
 StyleGalleryDialog dlg(project_.style, diagram_, english_,
  [this](const QString &st) { applyStyle(st, true); },
  [this](const QString &kit) { applyDiagram(kit); },
  this);
 dlg.exec();
}

void MainWindow::showExportStudio() {
 if (!requireProFeature("Xưởng Xuất File Pro (CAD / 3D / 4K / Bộ 8 tệp)",
                        "Pro Export Studio (CAD / 3D / 4K / 8-File Kit)")) {
  return;
 }
 ExportStudioDialog dlg(project_.site.name, english_, [this](const QString &kind) { exportFile(kind); }, this);
 dlg.exec();
}

void MainWindow::showLicenseDialog(const QString &blockedFeature) {
 if (!license_) return;
 LicenseDialog dlg(license_, english_, darkTheme_, blockedFeature, this);
 dlg.exec();
 syncLicenseUi();
}

void MainWindow::showUpdateDialog(bool userInitiated) {
 if (!license_) return;
 if (userInitiated) license_->bootstrap();
 const auto &off = license_->offer();
 const QString curVer = QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("2.3.1") : QCoreApplication::applicationVersion();
 const bool hasUpdate = off.isUpdateAvailable(curVer);

 if (!hasUpdate) {
  if (!userInitiated) return;
  QMessageBox::information(
   this,
   trText("Kiểm tra cập nhật GroundAtlas Desktop", "Check for Updates"),
   trText("Bạn đang sử dụng phiên bản mới nhất: GroundAtlas Desktop %1.\n\n"
          "Mỗi khi có bản cập nhật mới dòng 2.x, ứng dụng sẽ tự động hiện thanh thông báo trên cửa sổ để bạn tải về miễn phí (giữ nguyên bản quyền Pro trên máy).",
          "You are already on the latest version: GroundAtlas Desktop %1.\n\n"
          "Whenever a newer 2.x update is released, GroundAtlas automatically shows an in-app update banner (free for all Lifetime 2.x seats).").arg(curVer));
  return;
 }

 const QString notes = off.updateNotes.trimmed().isEmpty()
  ? trText("Cải thiện hiệu năng đồ họa, độ ổn định và tính năng mới cho dòng 2.x.",
           "Performance improvements, stability fixes and new features for the 2.x release line.")
  : off.updateNotes.trimmed();
 const QString dlUrl = off.downloadUrl.trimmed().isEmpty() ? license_->serverUrl() : off.downloadUrl.trimmed();

 QMessageBox box(this);
 box.setIcon(QMessageBox::Information);
 box.setWindowTitle(trText("Có bản cập nhật mới — GroundAtlas Desktop %1", "Update Available — GroundAtlas Desktop %1").arg(off.latestVersion));
 box.setText(trText("Đã có phiên bản mới GroundAtlas Desktop %1 (máy bạn đang dùng bản %2).",
                    "A new version GroundAtlas Desktop %1 is available (current: %2).").arg(off.latestVersion, curVer));
 box.setInformativeText(
  trText("Điểm mới:\n• %1\n\nMiễn phí hoàn toàn cho bản quyền dòng 2.x. Khi chạy bộ cài bản mới, bản quyền Pro đã kích hoạt trên máy này sẽ được giữ nguyên (không tốn thêm Seat).",
         "What's new:\n• %1\n\nFree for all Lifetime 2.x licenses. Installing over your current version preserves your activated Pro seat on this PC.").arg(notes));
 auto *dlBtn = box.addButton(trText("Tải bản cập nhật ngay ↗", "Download Update ↗"), QMessageBox::AcceptRole);
 box.addButton(trText("Để sau", "Later"), QMessageBox::RejectRole);
 box.exec();
 if (box.clickedButton() == dlBtn) {
  QDesktopServices::openUrl(QUrl(dlUrl));
 }
}

void MainWindow::showOpenSourceLicenses() {
 OpenSourceLicensesDialog dlg(english_, darkTheme_, this);
 dlg.exec();
}

bool MainWindow::requireProFeature(const QString &viName, const QString &enName) {
 if (license_ && license_->isPro()) return true;
 showLicenseDialog(trText(viName.toUtf8().constData(), enName.toUtf8().constData()));
 return license_ && license_->isPro();
}

void MainWindow::syncLicenseUi() {
 if (!licensePillBtn_ || !license_) return;
 const auto &off = license_->offer();
 const QString curVer = QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("2.3.1") : QCoreApplication::applicationVersion();
 const bool hasUpdate = off.isUpdateAvailable(curVer);
 if (updatePillBtn_) {
  updatePillBtn_->setVisible(hasUpdate);
  if (hasUpdate) {
   updatePillBtn_->setText(QString::fromUtf8("↑ v%1").arg(off.latestVersion));
   updatePillBtn_->setToolTip(trText("Có bản cập nhật mới %1 (miễn phí dòng 2.x) · Bấm để xem & tải về",
                                     "Update %1 available (free for 2.x) · Click to view & download").arg(off.latestVersion));
  }
 }
 if (updateBanner_ && updateBannerLbl_) {
  if (hasUpdate && !updateBannerDismissed_) {
   const QString shortNote = off.updateNotes.trimmed().isEmpty()
    ? trText("Miễn phí cho bản quyền 2.x · cài đè giữ nguyên Seat trên máy", "Free for Lifetime 2.x · preserves your activated seat")
    : off.updateNotes.trimmed().left(110);
   updateBannerLbl_->setText(trText("↑ Có bản cập nhật mới GroundAtlas Desktop v%1 — %2",
                                    "↑ GroundAtlas Desktop v%1 is available — %2").arg(off.latestVersion, shortNote));
   updateBanner_->show();
  } else {
   updateBanner_->hide();
  }
 }

 const auto &inf = license_->info();
 if (inf.isPro()) {
  licenseOfferTick_.stop();
  licensePillBtn_->setText(QString::fromUtf8("✓ Pro 2.x"));
  licensePillBtn_->setToolTip(trText("Đã kích hoạt trọn đời 2.x · %1/%2 Seat · Offline còn %3 ngày",
                                     "Lifetime 2.x Active · %1/%2 Seat · %3 offline days left")
   .arg(inf.usedSeats).arg(inf.maxSeats).arg(inf.offlineDaysRemaining()));
  licensePillBtn_->setStyleSheet(darkTheme_
   ? "QPushButton{background:#142b23;color:#5fd9aa;border:1px solid #235945;border-radius:14px;padding:4px 11px;font-weight:600;font-size:11px;} QPushButton:hover{background:#19382d;}"
   : "QPushButton{background:#e8f7f0;color:#007a4d;border:1px solid #b4e5cf;border-radius:14px;padding:4px 11px;font-weight:600;font-size:11px;} QPushButton:hover{background:#d8f2e6;}");
  return;
 }
 const qint64 rem = license_->offer().welcomeRemainingSec();
 if (rem > 0) {
  if (!licenseOfferTick_.isActive()) licenseOfferTick_.start();
  const qint64 hh = rem / 3600, mm = (rem % 3600) / 60;
  licensePillBtn_->setText(QString::fromUtf8("⚡ 99k · %1:%2").arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0')));
  licensePillBtn_->setToolTip(trText("Ưu đãi trải nghiệm lần đầu 48 giờ · Bấm để mở khóa trọn đời",
                                     "48h First-Install Welcome Offer · Click to unlock Lifetime 2.x"));
  licensePillBtn_->setStyleSheet("QPushButton{background:#3b63fb;color:#ffffff;border:none;border-radius:14px;padding:4px 11px;font-weight:600;font-size:11px;} QPushButton:hover{background:#274dea;}");
 } else {
  licenseOfferTick_.stop();
  licensePillBtn_->setText(trText("Mở khóa Pro", "Unlock Pro"));
  licensePillBtn_->setToolTip(trText("Bản Free · Bấm để kích hoạt hoặc mua Seat trọn đời 2.x",
                                     "Free Edition · Click to activate or buy a Lifetime 2.x Seat"));
  licensePillBtn_->setStyleSheet(darkTheme_
   ? "QPushButton{background:#1f2842;color:#a8bfff;border:1px solid #3c58a6;border-radius:14px;padding:4px 11px;font-weight:600;font-size:11px;} QPushButton:hover{background:#283559;}"
   : "QPushButton{background:#eef2ff;color:#274dea;border:1px solid #cfdafe;border-radius:14px;padding:4px 11px;font-weight:600;font-size:11px;} QPushButton:hover{background:#e0e8ff;}");
 }
}

void MainWindow::applyHardwareProfile() {
 hwInfo_ = ga::probeHardware(viewport_ ? viewport_->renderer() : QString(), this);
 gfxConfig_ = ga::resolveGraphicsConfig(perfTier_, hwInfo_);
 const int tickMs = std::clamp(int(std::floor(1000.0 / std::clamp(gfxConfig_.targetFps, 60, 500))), 1, 16);
 sunAnimTimer_.setInterval(tickMs);
 if (viewport_) {
  viewport_->setGraphicsConfig(gfxConfig_);
  viewport_->setAutoDarkBasemap(autoDarkBasemap_);
  viewport_->setSmoothDamping(smoothDamping_ && ga::shouldPersistPreferences(dataDirectory_));
  viewport_->setTerrain3d(terrain3d_, ga::appDataPath("environment"));
 }
 if (terrain3dCheck_) {
  const QSignalBlocker b(terrain3dCheck_);
  terrain3dCheck_->setChecked(terrain3d_);
 }
 if (offlineText_) {
  const QString tierLbl = (gfxConfig_.effectiveTier == ga::PerfTier::Ultra) ? "Ultra" : (gfxConfig_.effectiveTier == ga::PerfTier::Low ? "Low" : "Balanced");
  offlineText_->setText(QString("C++20 · %1 · %2 Hz").arg(tierLbl).arg(gfxConfig_.targetFps));
 }
}

void MainWindow::persistPreferences() {
 ga::AppPreferences p;
 p.darkTheme = darkTheme_;
 p.english = english_;
 p.imperialUnits = imperialUnits_;
 p.contextStyle = contextStyle_ ? contextStyle_->isChecked() : true;
 p.perfTier = perfTier_;
 p.smoothDamping = smoothDamping_;
 p.autoDarkBasemap = autoDarkBasemap_;
 p.terrain3d = terrain3d_;
 p.leftDrawerOpen = !leftDrawer_ || leftDrawer_->isOpen();
 p.studioDrawerOpen = !studioDrawer_ || studioDrawer_->isOpen();
 p.windowGeometry = saveGeometry();
 ga::saveAppPreferences(dataDirectory_, p);
}

void MainWindow::pushUndoState() {
 if (updating_ || switching_) return;
 undoStack_.append({project_.style, diagram_, layers_, plot_, massing_});
 if (undoStack_.size() > 40) undoStack_.removeFirst();
 redoStack_.clear();
}

void MainWindow::undoAction() {
 if (undoStack_.isEmpty()) {
  showToast(trText("Không còn thao tác để hoàn tác", "Nothing to undo"), QColor("#64748b"), 1500);
  return;
 }
 redoStack_.append({project_.style, diagram_, layers_, plot_, massing_});
 const auto st = undoStack_.takeLast();
 const bool old = updating_;
 updating_ = true;
 project_.style = st.style;
 diagram_ = st.diagram;
 layers_ = st.layers;
 plot_ = st.plot;
 massing_ = st.massing;
 viewport_->setPalette(ga::paletteFor(project_.style));
 viewport_->setLayers(layers_);
 viewport_->setDiagram(diagram_);
 viewport_->setPlot(plot_);
 viewport_->setMassing(massing_);
 updating_ = old;
 syncMap();
 syncLayers();
 syncPlotUi();
 syncMassingUi();
 populateDiagramList();
 markModified();
 showToast(trText("Đã hoàn tác (Ctrl+Z)", "Undone (Ctrl+Z)"), QColor("#c45632"), 1500);
}

void MainWindow::redoAction() {
 if (redoStack_.isEmpty()) {
  showToast(trText("Không còn thao tác để làm lại", "Nothing to redo"), QColor("#64748b"), 1500);
  return;
 }
 undoStack_.append({project_.style, diagram_, layers_, plot_, massing_});
 const auto st = redoStack_.takeLast();
 const bool old = updating_;
 updating_ = true;
 project_.style = st.style;
 diagram_ = st.diagram;
 layers_ = st.layers;
 plot_ = st.plot;
 massing_ = st.massing;
 viewport_->setPalette(ga::paletteFor(project_.style));
 viewport_->setLayers(layers_);
 viewport_->setDiagram(diagram_);
 viewport_->setPlot(plot_);
 viewport_->setMassing(massing_);
 updating_ = old;
 syncMap();
 syncLayers();
 syncPlotUi();
 syncMassingUi();
 populateDiagramList();
 markModified();
 showToast(trText("Đã làm lại (Ctrl+Y)", "Redone (Ctrl+Y)"), QColor("#c45632"), 1500);
}

void MainWindow::showCommandPalette() {
 QVector<CommandPaletteDialog::Entry> entries;
 const QString catTool = trText("Lệnh", "Command"), catStyle = trText("Phong cách", "Style"), catDiag = trText("Sơ đồ", "Diagram");
 entries.append({trText("QH Chung Hà Nội 2045 · Metro & Hạ tầng (1/25.000)", "Hanoi Master Plan 2045 · Metro & Corridors (1:25k)"), "QĐ 1668", catTool, "quy hoach tuong lai ha noi metro 2045 qhc 1668", [this] { applyFuturePlanning("qhc-2045"); }});
 entries.append({trText("QH Phân khu · Sử dụng đất TT 04/2022 (1/2.000)", "Zoning Plan · TT 04/2022 Land Use (1:2k)"), "TT 04/2022", catTool, "quy hoach phan khu su dung dat qhpk 2000 tt04", [this] { applyFuturePlanning("qhpk-2000"); }});
 entries.append({trText("QH Chi tiết · Khung tầng cao 3D (1/500)", "Detailed Plan · 3D Height Envelope (1:500)"), "3D Envelope", catTool, "quy hoach chi tiet khung tang cao 3d 500 qhct", [this] { applyFuturePlanning("qhct-500"); }});
 entries.append({trText("Đối chiếu Hiện trạng ↔ Quy hoạch tương lai", "Existing vs. Future Planning Diff"), "Compare", catTool, "doi chieu hien trang quy hoach tuong lai compare", [this] { applyFuturePlanning("compare"); }});
 entries.append({trText("Nhập bản vẽ Quy hoạch Vector (DXF VN-2000 / GeoJSON)…", "Import Planning Vector (DXF VN-2000 / GeoJSON)…"), "VN-2000", catTool, "nhap quy hoach dxf vn2000 geojson", [this] { choosePlanningVectorFile(); }});
 entries.append({trText("Phủ ảnh bản đồ Quy hoạch (PNG / JPG)…", "Overlay Planning Map Image (PNG / JPG)…"), "Overlay", catTool, "phu anh ban do quy hoach png jpg overlay", [this] { choosePlanningImageFile(); }});
 entries.append({trText("Nhập mô hình 3D từ SketchUp (.skp / .dae / .obj / .glb / .stl)", "Import 3D Model from SketchUp (.skp / .dae / .obj / .glb / .stl)"), "Ctrl+Shift+I", catTool, "import 3d sketchup skp dae obj glb stl nhap mo hinh su", [this] { choose3DModelFile(); }});
 entries.append({trText("Dựng / Ẩn Khối công trình đề xuất (3D Massing · FAR/BCR)", "Toggle Proposed 3D Massing (FAR/BCR)"), "Massing", catTool, "massing khoi cong trinh de xuat far bcr gfa", [this] { if (massingEnabled_) massingEnabled_->toggle(); }});
 entries.append({trText("Bật / Tắt Nắng 3D (GPU PCF)", "Toggle Sun 3D (GPU PCF)"), "Ctrl+U", catTool, "sun nang bong do shadow 3d", [this] { actions_["sun"]->trigger(); selectStudioTab(7); }});
 entries.append({trText("Thước đo khoảng cách & diện tích", "Distance & Area Ruler"), "M", catTool, "ruler thuoc do khoang cach dien tich measure", [this] { if (geographic_) showArchitecturalView(viewport_->camera()); viewport_->setMeasuring(!viewport_->isMeasuring()); }});
 entries.append({trText("Xuất Bộ hồ sơ Đồ án 1-Click (8 tệp: 4K/CAD/3D/PDF/GIF)", "Export 1-Click Presentation Kit (8 files)"), "1-Click Kit", catTool, "kit bo ho so do an batch export all 8", [this] { exportFile("project-kit"); }});
 entries.append({trText("Xuất Hoạt ảnh Nắng 1 ngày (.GIF)", "Export Full-Day Sun Timelapse (.GIF)"), "GIF", catTool, "gif sun timelapse hoat anh nang video", [this] { exportFile("sun-gif"); }});
 entries.append({trText("Xuất Hoạt ảnh Xoay 360° Turntable (.GIF)", "Export 360° Orbital Turntable (.GIF)"), "GIF 360°", catTool, "gif turntable 360 xoay video", [this] { exportFile("turntable-gif"); }});
 entries.append({trText("Xuất ảnh PNG 4K siêu nét (3840×2160)", "Export 4K Render PNG (3840×2160)"), "PNG 4K", catTool, "export png 4k anh", [this] { exportFile("png-4k"); }});
 entries.append({trText("Xuất PNG 8K nền trong suốt (Photoshop)", "Export 8K Transparent PNG"), "PNG 8K Alpha", catTool, "export png 8k transparent alpha photoshop", [this] { exportFile("png-8k-alpha"); }});
 entries.append({trText("Xuất SketchUp (.SKP thật)", "Export Native SketchUp (.SKP)"), "3D Bridge", catTool, "export sketchup skp dae", [this] { exportFile("skp"); }});
 entries.append({trText("Xuất Blender (.BLEND thật)", "Export Native Blender (.BLEND)"), "3D Bridge", catTool, "export blender blend glb", [this] { exportFile("blend"); }});
 entries.append({trText("Xuất AutoCAD (.DXF)", "Export AutoCAD (.DXF)"), "CAD", catTool, "export autocad dxf", [this] { exportFile("autocad"); }});
 entries.append({trText("Mở Mặt cắt Studio đầy đủ", "Open Full Section Studio"), "Ctrl+K", catTool, "section mat cat studio", [this] { showSection(); }});
 entries.append({trText("Bật / Tắt Mặt cắt trực tiếp A–A' trên Viewport", "Toggle Live Section Cut A–A'"), "Cut A-A'", catTool, "live section mat cat truc tiep a-a", [this] { if (geographic_) showArchitecturalView(viewport_->camera()); viewport_->setSectionCutActive(!viewport_->isSectionCutActive()); }});
 entries.append({trText("Đổi Giao diện Sáng / Tối", "Toggle Light / Dark Theme"), "Theme", catTool, "dark light toi sang giao dien", [this] { setDarkTheme(!darkTheme_); persistPreferences(); }});
 entries.append({trText("Quản lý Bản quyền (Per-Seat) & Kích hoạt Key", "License & Per-Seat Activation"), "PRO 2.x", catTool, "license key pro seat ban quyen kich hoat", [this] { showLicenseDialog(); }});
 entries.append({trText("Kiểm tra bản cập nhật mới", "Check for Updates"), "v2.3.1", catTool, "update cap nhat phien ban moi", [this] { showUpdateDialog(true); }});
 entries.append({trText("Giấy phép Nguồn mở & Dữ liệu", "Open-Source & Data Licenses"), "LGPL/BSD", catTool, "oss license lgpl bsd odbl", [this] { showOpenSourceLicenses(); }});
 entries.append({trText("Cài đặt Hiệu năng & Màn hình (tới 500 Hz)", "Performance & Display Settings"), "Auto/Low/Med/Ultra", catTool, "settings cai dat hieu nang fps hz gpu", [this] { showSettings(); }});
 entries.append({trText("Bật / Tắt Địa hình 3D DEM", "Toggle 3D DEM Terrain"), "GEDTM30", catTool, "terrain dia hinh dem 3d", [this] { if (terrain3dCheck_) terrain3dCheck_->toggle(); }});
 entries.append({trText("Hoàn tác (Undo)", "Undo"), "Ctrl+Z", catTool, "undo hoan tac", [this] { undoAction(); }});
 entries.append({trText("Làm lại (Redo)", "Redo"), "Ctrl+Y", catTool, "redo lam lai", [this] { redoAction(); }});
 for (const auto &p : ga::palettes()) {
  entries.append({english_ ? p.en : p.vi, p.id, catStyle, p.vi + " " + p.en + " " + p.id, [this, id = p.id] { applyStyle(id, true); }});
 }
 if (diagramList_) {
  for (int i = 0; i < diagramList_->count(); ++i) {
   auto *it = diagramList_->item(i);
   const QString id = it->data(Qt::UserRole).toString();
   const QString txt = it->text();
   entries.append({txt, id, catDiag, txt + " " + id, [this, id] {
    if (id.startsWith("analytic:")) applyAnalytic(id.mid(9));
    else if (id.startsWith("env:")) applyAnalytic(id);
    else { activeAnalytic_.clear(); viewport_->setAnalyticalOverlay({}); applyDiagram(id); }
   }});
  }
 }
 CommandPaletteDialog dlg(entries, english_, this);
 dlg.exec();
}

void MainWindow::showSettings() {
 hwInfo_ = ga::probeHardware(viewport_ ? viewport_->renderer() : QString(), this);
 SettingsDialog::Values init{darkTheme_, english_, imperialUnits_, contextStyle_->isChecked(), perfTier_, smoothDamping_, autoDarkBasemap_, terrain3d_};
 SettingsDialog dlg(init, hwInfo_, [this](const SettingsDialog::Values &v) {
  perfTier_ = v.perfTier;
  smoothDamping_ = v.smoothDamping;
  autoDarkBasemap_ = v.autoDarkBasemap;
  terrain3d_ = v.terrain3d;
  imperialUnits_ = v.imperialUnits;
  setDarkTheme(v.darkTheme);
  contextStyle_->setChecked(v.contextStyle);
  language_->setCurrentIndex(v.english ? 1 : 0);
  applyHardwareProfile();
  persistPreferences();
  syncInspector(viewport_->selected());
  showToast(trText("Đã lưu cấu hình: ", "Saved profile: ") + ga::perfTierName(gfxConfig_.effectiveTier, english_), QColor("#16a34a"), 2400);
 }, this);
 dlg.exec();
}

void MainWindow::syncSolarUi() {
 if (!sunEnabled_ || !sunPath_ || !sunSeason_ || !sunHour_ || !sunReadout_) return;
 const auto sol = viewport_->camera().solar;
 const QSignalBlocker b1(sunEnabled_), b2(sunPath_), b3(sunSeason_), b4(sunHour_);
 sunEnabled_->setChecked(sol.enabled);
 sunPath_->setChecked(sol.showPath);
 if (sunGolden_) { const QSignalBlocker bg(sunGolden_); sunGolden_->setChecked(sol.goldenHour); }
 if (sunSkyDome_) { const QSignalBlocker bsd(sunSkyDome_); sunSkyDome_->setChecked(sol.skyDome); }
 if (trees3dCheck_) { const QSignalBlocker bt(trees3dCheck_); trees3dCheck_->setChecked(sol.trees3d); }
 if (sunShadow_) { const QSignalBlocker bs(sunShadow_); sunShadow_->setValue(std::clamp(int(qRound(sol.shadowStrength * 100.0)), 20, 100)); }
 const int sIdx = sunSeason_->findData(sol.dayOfYear);
 if (sIdx >= 0) sunSeason_->setCurrentIndex(sIdx);
 sunHour_->setValue(std::clamp(int(qRound(sol.hour * 60.0)), 360, 1080));
 const int hh = int(std::floor(sol.hour)), mm = int(std::floor((sol.hour - hh) * 60.0));
 const auto sp = ga::solarPosition(project_.site.lat, sol.dayOfYear, sol.hour);
 sunReadout_->setText(QString("%1:%2 · %3 %4° · %5 %6° · PCF %7px (%8%)")
  .arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0'))
  .arg(trText("Độ cao", "Alt")).arg(QString::number(sp.altitudeDeg, 'f', 1))
  .arg(trText("Phương vị", "Az")).arg(QString::number(sp.azimuthDeg, 'f', 1))
  .arg(gfxConfig_.shadowMapSize)
  .arg(qRound(sol.shadowStrength * 100.0)));
}

void MainWindow::applySolarLiveFromUi() {
 if (!sunEnabled_ || !sunPath_ || !sunSeason_ || !sunHour_) return;
 auto sol = viewport_->camera().solar;
 sol.enabled = sunEnabled_->isChecked();
 sol.showPath = sunPath_->isChecked();
 if (sunGolden_) sol.goldenHour = sunGolden_->isChecked();
 if (sunSkyDome_) sol.skyDome = sunSkyDome_->isChecked();
 if (trees3dCheck_) sol.trees3d = trees3dCheck_->isChecked();
 if (sunShadow_) sol.shadowStrength = sunShadow_->value() / 100.0;
 sol.dayOfYear = sunSeason_->currentData().toInt();
 sol.hour = sunHour_->value() / 60.0;
 sunAnimHour_ = sol.hour;
 if (!sol.enabled) {
  sol.enabled = true;
  const QSignalBlocker b(sunEnabled_);
  sunEnabled_->setChecked(true);
 }
 if (geographic_ || viewport_->camera().mode == "2d" || viewport_->camera().exploded) {
  auto c = viewport_->camera();
  c.solar = sol;
  c.mode = "3d";
  c.exploded = false;
  showArchitecturalView(c);
 } else {
  // Zero-rebuild ultra-fast GPU shadow + sun dome update (180–500 FPS!)
  viewport_->setSolarLive(sol);
 }
 syncSolarUi();
}

void MainWindow::applySolarFromUi() {
 if (!sunEnabled_ || !sunPath_ || !sunSeason_ || !sunHour_) return;
 auto c = viewport_->camera();
 c.solar.enabled = sunEnabled_->isChecked();
 c.solar.showPath = sunPath_->isChecked();
 if (sunGolden_) c.solar.goldenHour = sunGolden_->isChecked();
 if (sunSkyDome_) c.solar.skyDome = sunSkyDome_->isChecked();
 if (trees3dCheck_) c.solar.trees3d = trees3dCheck_->isChecked();
 if (sunShadow_) c.solar.shadowStrength = sunShadow_->value() / 100.0;
 c.solar.dayOfYear = sunSeason_->currentData().toInt();
 c.solar.hour = sunHour_->value() / 60.0;
 sunAnimHour_ = c.solar.hour;
 if (c.solar.enabled) { c.mode = "3d"; c.exploded = false; showArchitecturalView(c); }
 else viewport_->setCamera(c);
 syncSolarUi();
}

void MainWindow::syncPlotUi() {
 if (!plotWidth_ || !plotLength_ || !plotRotation_ || !plotDims_ || !plotStatus_) return;
 const QSignalBlocker b1(plotWidth_), b2(plotLength_), b3(plotRotation_), b4(plotDims_);
 plotWidth_->setValue(plot_.width);
 plotLength_->setValue(plot_.length);
 plotRotation_->setValue(plot_.rotationDeg);
 plotDims_->setChecked(plot_.showDimensions);
 if (plotModeRect_ && plotModePoly_ && plotRectPanel_ && plotPolyPanel_) {
  const QSignalBlocker br(plotModeRect_), bp(plotModePoly_);
  plotModeRect_->setChecked(!plot_.isPolygon);
  plotModePoly_->setChecked(plot_.isPolygon);
  plotRectPanel_->setVisible(!plot_.isPolygon);
  plotPolyPanel_->setVisible(plot_.isPolygon);
 }
 if (!plot_.active) {
  plotStatus_->setText(trText("Chưa đặt", "Unset"));
 } else if (plot_.isPolygon) {
  plotStatus_->setText(QString("%1 m²").arg(QLocale(english_ ? "en_US" : "vi_VN").toString(plot_.areaM2(), 'f', 0)));
 } else {
  plotStatus_->setText(QString("%1×%2 m").arg(qRound(plot_.width)).arg(qRound(plot_.length)));
 }
}

void MainWindow::applyPlotFromUi(bool active) {
 plot_.active = active;
 if (!plot_.isPolygon) {
  plot_.width = plotWidth_->value();
  plot_.length = plotLength_->value();
  plot_.rotationDeg = plotRotation_->value();
 }
 plot_.showDimensions = plotDims_->isChecked();
 viewport_->setPlot(plot_);
 if (active && geographic_) showArchitecturalView(viewport_->camera());
 syncPlotUi();
 syncMassingUi();
 markModified();
}

void MainWindow::syncMassingUi() {
 if (!massingEnabled_ || !massingPanel_ || !massingKpi_) return;
 const QSignalBlocker b0(massingEnabled_), b1(massSetback_), b2(massPodFloors_), b3(massTowFloors_),
                      b4(massTowCov_), b5(massPodH_), b6(massTowH_), b7(massTowRot_);
 massingEnabled_->setChecked(massing_.enabled);
 massingPanel_->setVisible(massing_.enabled);
 massSetback_->setValue(massing_.setbackM);
 massPodFloors_->setValue(massing_.podiumFloors);
 massTowFloors_->setValue(massing_.towerFloors);
 massTowCov_->setValue(qRound(massing_.towerCoverage * 100.0));
 massPodH_->setValue(massing_.podiumFloorHeight);
 massTowH_->setValue(massing_.towerFloorHeight);
 massTowRot_->setValue(massing_.towerRotationDeg);

 const auto m = ga::evaluateMassing(plot_, massing_);
 const QLocale loc(english_ ? "en_US" : "vi_VN");
 if (!m.valid) {
  massingKpi_->setText(trText("Đặt khu đất đồ án để xem chỉ tiêu BCR / FAR / GFA.", "Activate a design plot to compute BCR / FAR / GFA."));
 } else {
  massingKpi_->setText(QString("BCR (MĐXD): %1%  ·  FAR: %2\nGFA: %3 m²  ·  %4 m (%5 %6)")
   .arg(loc.toString(m.bcrPercent, 'f', 1))
   .arg(loc.toString(m.far, 'f', 2))
   .arg(loc.toString(m.totalGfaM2, 'f', 0))
   .arg(loc.toString(m.totalHeightM, 'f', 1))
   .arg(m.totalFloors)
   .arg(trText("tầng", "fl")));
 }
}

void MainWindow::applyMassingFromUi() {
 if (!massingEnabled_) return;
 massing_.enabled = massingEnabled_->isChecked();
 massing_.setbackM = massSetback_->value();
 massing_.podiumFloors = massPodFloors_->value();
 massing_.towerFloors = massTowFloors_->value();
 massing_.towerCoverage = massTowCov_->value() / 100.0;
 massing_.podiumFloorHeight = massPodH_->value();
 massing_.towerFloorHeight = massTowH_->value();
 massing_.towerRotationDeg = massTowRot_->value();
 viewport_->setMassing(massing_);
 syncMassingUi();
 if (viewport_->isSectionCutActive()) updateLiveSection();
 markModified();
}

void MainWindow::choose3DModelFile() {
 if (!requireProFeature("Nhập mô hình 3D trực tiếp (SketchUp .SKP / .DAE / .OBJ / .GLB)",
                        "Direct 3D Model Import (SketchUp .SKP / .DAE / .OBJ / .GLB)")) {
  return;
 }
 const QString filter = trText(
  "Mô hình 3D / SketchUp (*.skp *.dae *.obj *.glb *.gltf *.stl);;SketchUp trực tiếp (*.skp);;COLLADA SketchUp (*.dae);;Wavefront OBJ (*.obj);;glTF 2.0 (*.glb *.gltf);;STL (*.stl)",
  "3D Models / SketchUp (*.skp *.dae *.obj *.glb *.gltf *.stl);;SketchUp Native (*.skp);;COLLADA (*.dae);;Wavefront OBJ (*.obj);;glTF 2.0 (*.glb *.gltf);;STL (*.stl)");
 const QString path = QFileDialog::getOpenFileName(this, trText("Nhập mô hình 3D (SketchUp / DAE / OBJ / GLB / STL)", "Import 3D Model (SketchUp / DAE / OBJ / GLB / STL)"), {}, filter);
 if (!path.isEmpty()) import3DModel(path);
}

bool MainWindow::import3DModel(const QString &path) {
 QApplication::setOverrideCursor(Qt::WaitCursor);
 QString err;
 auto model = ga::load3DModelFile(path, &err);
 QApplication::restoreOverrideCursor();
 if (!model.valid()) {
  QMessageBox::warning(this, trText("Không nhập được mô hình 3D", "Could not import 3D model"),
                       err.isEmpty() ? trText("Định dạng 3D không hợp lệ hoặc tệp rỗng.", "Invalid or empty 3D file.") : err);
  return false;
 }
 // Smart auto-placement: snap to active Design Plot center (and match rotation), else site center (0, 0)
 const auto curPlot = viewport_ ? viewport_->plot() : plot_;
 if (curPlot.active) {
  const QPointF cen = curPlot.polygon().boundingRect().center();
  model.x = cen.x();
  model.y = cen.y();
  if (!curPlot.isPolygon) model.yawDeg = curPlot.rotationDeg;
 }
 models3d_.append(std::move(model));
 const int idx = models3d_.size() - 1;
 viewport_->setModels3d(models3d_);
 viewport_->setSelectedModel3d(idx);
 if (model3dList_) {
  syncModels3dUi();
  model3dList_->setCurrentRow(idx);
 }
 // Switch automatically into 3D view so the user immediately sees their SketchUp building with shadows!
 if (geographic_ || viewport_->camera().mode == "2d") {
  auto c = viewport_->camera();
  c.mode = "3d";
  c.exploded = false;
  showArchitecturalView(c);
 }
 if (viewport_->isSectionCutActive()) updateLiveSection();
 markModified();
 const auto &added = models3d_[idx];
 const auto sz = added.effectiveSizeM();
 const QLocale loc(english_ ? "en_US" : "vi_VN");
 const QString msg = trText("Đã nhập mô hình 3D: %1 (%2 tam giác · %3×%4×%5 m)",
                            "Imported 3D model: %1 (%2 tris · %3×%4×%5 m)")
                      .arg(added.name)
                      .arg(loc.toString(added.triangleCount()))
                      .arg(loc.toString(sz.x(), 'f', 1))
                      .arg(loc.toString(sz.y(), 'f', 1))
                      .arg(loc.toString(sz.z(), 'f', 1));
 statusBar()->showMessage(msg, 7000);
 showToast(msg, QColor("#d97706"), 3200);
 return true;
}

void MainWindow::syncModels3dUi() {
 if (!model3dList_ || !model3dEditor_) return;
 const bool prevUpd = updating_;
 updating_ = true;
 int cur = model3dList_->currentRow();
 if (cur < 0 && !models3d_.isEmpty()) cur = 0;
 if (cur >= models3d_.size()) cur = models3d_.size() - 1;

 model3dList_->clear();
 const QLocale loc(english_ ? "en_US" : "vi_VN");
 for (const auto &m : models3d_) {
  auto *it = new QListWidgetItem(QString("%1  [%2 · %3 tri]")
   .arg(m.name).arg(m.format.toUpper()).arg(loc.toString(m.triangleCount())), model3dList_);
  it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
  it->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
  it->setToolTip(m.sourcePath);
 }
 model3dList_->setVisible(!models3d_.isEmpty());
 if (cur >= 0 && cur < model3dList_->count()) model3dList_->setCurrentRow(cur);

 const bool hasSel = (cur >= 0 && cur < models3d_.size());
 model3dEditor_->setVisible(hasSel);
 if (hasSel) {
  const auto &m = models3d_[cur];
  const auto sz = m.effectiveSizeM();
  model3dMeta_->setText(QString("%1: %2 × %3 × %4 m  ·  %5 %6")
   .arg(trText("Kích thước", "Size"))
   .arg(loc.toString(sz.x(), 'f', 1))
   .arg(loc.toString(sz.y(), 'f', 1))
   .arg(loc.toString(sz.z(), 'f', 1))
   .arg(loc.toString(m.triangleCount()))
   .arg(trText("tam giác", "tris")));
  m3dX_->setValue(m.x);
  m3dY_->setValue(m.y);
  m3dZ_->setValue(m.z);
  m3dYaw_->setValue(m.yawDeg);
  m3dScale_->setValue(m.scale);
  m3dFlipYUp_->setChecked(m.flipYUp);
  m3dOverrideColor_->setChecked(m.overrideColor);
  m3dPlaceBtn_->setChecked(viewport_ && viewport_->isPlacingModel3d() && viewport_->selectedModel3d() == cur);
 }
 updating_ = prevUpd;
}

void MainWindow::applySelectedModel3dFromUi() {
 if (!model3dList_) return;
 const int row = model3dList_->currentRow();
 if (row < 0 || row >= models3d_.size()) return;
 auto &m = models3d_[row];
 m.x = m3dX_->value();
 m.y = m3dY_->value();
 m.z = m3dZ_->value();
 m.yawDeg = m3dYaw_->value();
 m.scale = m3dScale_->value();
 m.flipYUp = m3dFlipYUp_->isChecked();
 m.overrideColor = m3dOverrideColor_->isChecked();
 viewport_->setModels3d(models3d_);
 viewport_->setSelectedModel3d(row);
 const auto sz = m.effectiveSizeM();
 const QLocale loc(english_ ? "en_US" : "vi_VN");
 model3dMeta_->setText(QString("%1: %2 × %3 × %4 m  ·  %5 %6")
  .arg(trText("Kích thước", "Size"))
  .arg(loc.toString(sz.x(), 'f', 1))
  .arg(loc.toString(sz.y(), 'f', 1))
  .arg(loc.toString(sz.z(), 'f', 1))
  .arg(loc.toString(m.triangleCount()))
  .arg(trText("tam giác", "tris")));
 if (viewport_->isSectionCutActive()) updateLiveSection();
 markModified();
}

void MainWindow::removeSelectedModel3d() {
 if (!model3dList_) return;
 const int row = model3dList_->currentRow();
 if (row < 0 || row >= models3d_.size()) return;
 const QString removedName = models3d_[row].name;
 models3d_.removeAt(row);
 viewport_->setPlacingModel3d(false);
 viewport_->setModels3d(models3d_);
 viewport_->setSelectedModel3d(models3d_.isEmpty() ? -1 : std::min(row, int(models3d_.size()) - 1));
 syncModels3dUi();
 if (viewport_->isSectionCutActive()) updateLiveSection();
 markModified();
 showToast(trText("Đã xóa mô hình 3D: ", "Removed 3D model: ") + removedName, QColor("#64748b"), 1800);
}

void MainWindow::setHanoiPlanning(bool enabled, const QString &layerId, double opacity) {
 const bool wasEnabled = hanoiPlanEnabled_;
 const QString prevLayer = hanoiPlanLayer_;
 hanoiPlanEnabled_ = enabled;
 if (layerId == "qhpk" || layerId == "khsdd" || layerId == "qhct" || layerId == "custom") hanoiPlanLayer_ = layerId;
 if (opacity >= 0.05) hanoiPlanOpacity_ = std::clamp(opacity, 0.15, 1.0);

 if (map_) map_->setHanoiPlanning(hanoiPlanEnabled_, hanoiPlanLayer_, hanoiPlanOpacity_);
 if (viewport_) viewport_->setHanoiPlanning(hanoiPlanEnabled_, hanoiPlanLayer_, hanoiPlanOpacity_);

 if (hanoiPlanEnabled_) {
  const auto &curComp = viewport_ ? viewport_->hanoiTileComposite() : ga::HanoiTileComposite{};
  const bool needFetch = !curComp.valid()
   || curComp.layerId != hanoiPlanLayer_
   || std::abs(curComp.siteLat - project_.site.lat) > 1e-6
   || std::abs(curComp.siteLon - project_.site.lon) > 1e-6;
  if (needFetch) refreshHanoiPlanningTiles();
 } else {
  if (hanoiTiles_) hanoiTiles_->cancel();
  hanoiPlanStatus_.clear();
 }

 syncPlanningUi();
 markModified();
 if (wasEnabled != hanoiPlanEnabled_ || prevLayer != hanoiPlanLayer_) {
  showToast(hanoiPlanEnabled_
   ? trText("Đã bật lớp Quy hoạch sử dụng đất Hà Nội (quyhoach.hanoi.vn)", "Enabled Hanoi Land-Use Planning layer (quyhoach.hanoi.vn)")
   : trText("Đã tắt lớp Quy hoạch Hà Nội", "Disabled Hanoi Land-Use Planning layer"),
   hanoiPlanEnabled_ ? QColor("#c45632") : QColor("#64748b"), 2200);
 }
}

void MainWindow::refreshHanoiPlanningTiles() {
 if (!hanoiTiles_ || !hanoiPlanEnabled_) return;
 if (hanoiPlanLayer_ != "custom" && !ga::isLikelyInHanoiArea(project_.site.lat, project_.site.lon)) {
  if (viewport_) viewport_->setHanoiTileComposite({});
  hanoiPlanStatus_ = trText("Khu đất đang mở ngoài Hà Nội · bật trên Bản đồ để xem vùng Hà Nội.",
                            "Current site is outside Hanoi · switch to Map view to browse Hanoi tiles.");
  if (hanoiPlanStatusLbl_) hanoiPlanStatusLbl_->setText(hanoiPlanStatus_);
  return;
 }
 hanoiPlanStatus_ = trText("Đang tải ô bản đồ quyhoach.hanoi.vn…", "Loading quyhoach.hanoi.vn tiles…");
 if (hanoiPlanStatusLbl_) hanoiPlanStatusLbl_->setText(hanoiPlanStatus_);
 hanoiTiles_->fetchForSite(project_.site, hanoiPlanLayer_);
}

void MainWindow::applyFuturePlanning(const QString &levelId) {
 activePlanningLevel_ = levelId;
 if (levelId.isEmpty()) {
  viewport_->setFuturePlanningOverlay({});
  syncPlanningUi();
  populateDiagramList();
  markModified();
  return;
 }
 activeAnalytic_.clear();
 viewport_->setAnalyticalOverlay({});
 const QString planDir = ga::appDataPath("planning");
 const auto curPlot = viewport_ ? viewport_->plot() : plot_;
 const auto overlay = ga::computeFuturePlanningOverlay(
  project_, levelId, planDir,
  planningCustom_.empty() ? nullptr : &planningCustom_,
  curPlot.active ? &curPlot : nullptr,
  planningOpacity_);
 viewport_->setFuturePlanningOverlay(overlay);
 if (levelId == "qhct-500" && (geographic_ || viewport_->camera().mode == "2d")) {
  auto c = viewport_->camera();
  c.mode = "3d";
  c.exploded = false;
  showArchitecturalView(c);
 } else if (geographic_) {
  showArchitecturalView(viewport_->camera());
 }
 syncPlanningUi();
 populateDiagramList();
 markModified();
 const QString msg = english_ ? overlay.titleEn : overlay.titleVi;
 statusBar()->showMessage(msg + " — " + (english_ ? overlay.summaryEn : overlay.summaryVi), 7000);
 showToast(msg, QColor("#7c3aed"), 2500);
}

void MainWindow::syncPlanningUi() {
 const bool prevUpd = updating_;
 updating_ = true;
 if (hanoiPlanToggleBtn_) {
  const QSignalBlocker sb(hanoiPlanToggleBtn_);
  hanoiPlanToggleBtn_->setChecked(hanoiPlanEnabled_);
  hanoiPlanToggleBtn_->setProperty("accentActive", hanoiPlanEnabled_);
  hanoiPlanToggleBtn_->setStyleSheet(hanoiPlanEnabled_
   ? "QPushButton{background:#3b63fb;color:#ffffff;border:1px solid #274dea;border-radius:15px;font-weight:600;padding:5px 11px;} QPushButton:hover{background:#274dea;}"
   : QString());
  hanoiPlanToggleBtn_->setText(hanoiPlanEnabled_
   ? trText("✓ Đang đè lớp Quy hoạch Hà Nội", "✓ Hanoi Land-Use Layer Active")
   : trText("Bật lớp Quy hoạch Hà Nội", "Overlay Hanoi Land-Use Layer"));
 }
 if (hanoiPlanOpacitySlider_) {
  const QSignalBlocker sb(hanoiPlanOpacitySlider_);
  hanoiPlanOpacitySlider_->setValue(qRound(hanoiPlanOpacity_ * 100.0));
 }
 if (hanoiPlanOpacityLbl_) hanoiPlanOpacityLbl_->setText(QString::number(qRound(hanoiPlanOpacity_ * 100.0)) + "%");
 if (hanoiPlanLayerCombo_) {
  const QSignalBlocker sb(hanoiPlanLayerCombo_);
  const int idx = hanoiPlanLayerCombo_->findData(hanoiPlanLayer_);
  if (idx >= 0) hanoiPlanLayerCombo_->setCurrentIndex(idx);
 }
 if (hanoiPlanStatusLbl_) {
  hanoiPlanStatusLbl_->setText(hanoiPlanStatus_.isEmpty()
   ? trText("Bấm bật để đè trực tiếp lớp quy hoạch lên Bản đồ, 2D & 3D.",
            "Click to overlay Hanoi land-use zoning on Map, 2D & 3D.")
   : hanoiPlanStatus_);
 }
 if (!planSubdivLbl_) { updating_ = prevUpd; return; }
 const QString planDir = ga::appDataPath("planning");
 const auto sub = ga::detectHanoiSubdivision(project_.site.lat, project_.site.lon, planDir);
 if (sub.matched) {
  planSubdivLbl_->setText(QString::fromUtf8("● %1 (%2)\n%3")
   .arg(english_ ? sub.nameEn : sub.nameVi, sub.decision, sub.maxHeightNote));
 } else {
  planSubdivLbl_->setText(trText("Khu đất ngoài Hà Nội · tự phân loại TT 04/2022 & nhận DXF/GeoJSON quy hoạch.",
                                 "Site outside Hanoi · TT 04/2022 zoning + DXF/GeoJSON & image overlay ready."));
 }
 for (auto *b : planLevelBtns_) {
  const QSignalBlocker sb(b);
  b->setChecked(b->property("planId").toString() == activePlanningLevel_);
 }
 if (planOpacitySlider_) {
  const QSignalBlocker sb(planOpacitySlider_);
  planOpacitySlider_->setValue(qRound(planningOpacity_ * 100.0));
 }
 const bool hasCust = !planningCustom_.empty();
 if (planClearCustomBtn_) planClearCustomBtn_->setVisible(hasCust);
 if (planCustomLbl_) {
  planCustomLbl_->setVisible(hasCust);
  if (hasCust) {
   planCustomLbl_->setText(trText("Đã nhập: %1 (%2 ô đất · %3 tuyến)", "Loaded: %1 (%2 zones · %3 lines)")
    .arg(planningCustom_.sourceName).arg(planningCustom_.zones.size()).arg(planningCustom_.corridors.size()));
  }
 }
 if (planImgPanel_) {
  planImgPanel_->setVisible(planningImage_.valid());
  if (planningImage_.valid()) {
   planImgOpacity_->setValue(qRound(planningImage_.opacity * 100.0));
   planImgWidth_->setValue(planningImage_.widthM);
   planImgRot_->setValue(planningImage_.rotationDeg);
   planImgX_->setValue(planningImage_.centerX);
   planImgY_->setValue(planningImage_.centerY);
  }
 }
 updating_ = prevUpd;
}

void MainWindow::choosePlanningVectorFile() {
 const QString filter = trText(
  "Bản vẽ Quy hoạch (*.dxf *.geojson *.json);;AutoCAD DXF Quy hoạch (*.dxf);;GeoJSON Quy hoạch (*.geojson *.json)",
  "Planning Vector (*.dxf *.geojson *.json);;AutoCAD DXF (*.dxf);;GeoJSON (*.geojson *.json)");
 const QString path = QFileDialog::getOpenFileName(this, trText("Nhập bản vẽ Quy hoạch (DXF VN-2000 / GeoJSON)", "Import Planning Vector (DXF VN-2000 / GeoJSON)"), {}, filter);
 if (path.isEmpty()) return;
 const auto crs = planCrsCombo_ ? static_cast<ga::PlanningCrs>(planCrsCombo_->currentData().toInt()) : ga::PlanningCrs::Auto;
 importPlanningVector(path, crs);
}

bool MainWindow::importPlanningVector(const QString &path, ga::PlanningCrs crs) {
 QString err;
 auto loaded = ga::importPlanningVectorFile(path, project_.site, crs, &err);
 if (loaded.empty()) {
  QMessageBox::warning(this, trText("Không nhập được bản vẽ Quy hoạch", "Planning import failed"),
                       err.isEmpty() ? trText("Tệp không có đối tượng nằm trong khu đất.", "No planning features inside site bounds.") : err);
  return false;
 }
 planningCustom_ = std::move(loaded);
 syncPlanningUi();
 applyFuturePlanning(activePlanningLevel_.isEmpty() ? "qhpk-2000" : activePlanningLevel_);
 showToast(trText("Đã đồng bộ bản vẽ Quy hoạch: %1 (%2 ô đất)", "Synced planning vector: %1 (%2 zones)")
  .arg(planningCustom_.sourceName).arg(planningCustom_.zones.size()), QColor("#f28d42"), 2800);
 return true;
}

void MainWindow::choosePlanningImageFile() {
 const QString path = QFileDialog::getOpenFileName(this, trText("Chọn ảnh bản đồ Quy hoạch (PNG / JPG)", "Select Planning Map Image (PNG / JPG)"), {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
 if (path.isEmpty()) return;
 QImage img(path);
 if (img.isNull()) {
  QMessageBox::warning(this, "GroundAtlas", trText("Không đọc được tệp ảnh bản đồ.", "Could not read image file."));
  return;
 }
 planningImage_.enabled = true;
 planningImage_.imagePath = QFileInfo(path).absoluteFilePath();
 planningImage_.image = std::move(img);
 planningImage_.opacity = 0.56;
 planningImage_.centerX = 0.0;
 planningImage_.centerY = 0.0;
 planningImage_.widthM = std::max(200.0, project_.site.radius * 2.0);
 planningImage_.rotationDeg = 0.0;
 viewport_->setPlanningImageOverlay(planningImage_);
 if (geographic_) showArchitecturalView(viewport_->camera());
 syncPlanningUi();
 markModified();
 showToast(trText("Đã phủ ảnh bản đồ Quy hoạch · chỉnh độ mờ/tọa độ ở thẻ bên trái", "Planning map image draped · adjust opacity/alignment on left panel"), QColor("#7c3aed"), 2800);
}

void MainWindow::showHanoiPlanningSources() {
 QMessageBox box(this);
 box.setIcon(QMessageBox::Information);
 box.setWindowTitle(trText("Nguồn tra cứu Quy hoạch Hà Nội chính thống", "Official Hanoi Urban Planning Sources"));
 box.setText(trText("Cơ sở pháp lý & Cổng tra cứu Quy hoạch Thủ đô Hà Nội", "Legal Basis & Official Hanoi Planning Portals"));
 box.setInformativeText(trText(
  "• Điều chỉnh Quy hoạch chung Thủ đô Hà Nội đến 2045, tầm nhìn 2065: Quyết định 1668/QĐ-TTg (27/12/2024).\n"
  "• Điều chỉnh tổng thể Quy hoạch phân khu đô thị Sông Hồng 1/5000: Quyết định 6075/QĐ-UBND (03/11/2026).\n"
  "• Chuẩn ký hiệu & bảng màu sử dụng đất tích hợp sẵn: QCVN 01:2021/BXD & Thông tư 04/2022/TT-BXD.\n\n"
  "Các cổng chính thống để tải bản vẽ / đối chiếu:\n"
  "1. Viện Quy hoạch Xây dựng Hà Nội: https://vqh.hanoi.gov.vn\n"
  "2. Cổng thông tin Quy hoạch TP. Hà Nội: https://quyhoach.hanoi.vn\n"
  "3. Quy hoạch & Kế hoạch Sử dụng đất (Sở TN&MT): https://qhkhsdd.hanoi.gov.vn\n"
  "4. Cổng thông tin Quy hoạch Xây dựng VN: https://quyhoach.xaydung.gov.vn\n"
  "5. Cổng Dữ liệu mở TP. Hà Nội: https://opendata.hanoi.gov.vn\n\n"
  "Lưu ý: Khung QHC 2045 tích hợp sẵn phục vụ nghiên cứu – thiết kế kiến trúc, không thay thế văn bản chỉ giới đường đỏ pháp lý. Bạn có thể nhập trực tiếp file .DXF hệ VN-2000 Hà Nội (múi 3°, 105°E) hoặc phủ ảnh bản đồ phân khu 1/2000 từ các cổng trên.",
  "• Hanoi Capital Master Plan to 2045, Vision 2065: Decision 1668/QD-TTg (Dec 27, 2024).\n"
  "• Red River Urban Subdivision Plan 1:5000: Decision 6075/QD-UBND (Nov 3, 2026).\n"
  "• Built-in zoning color palette: QCVN 01:2021/BXD & Circular 04/2022/TT-BXD.\n\n"
  "Official portals for reference drawings:\n"
  "1. Hanoi Institute of Urban Planning: https://vqh.hanoi.gov.vn\n"
  "2. Hanoi Planning Information Portal: https://quyhoach.hanoi.vn\n"
  "3. Hanoi Land Use Planning (DONRE): https://qhkhsdd.hanoi.gov.vn\n"
  "4. Vietnam Construction Planning Portal: https://quyhoach.xaydung.gov.vn\n"
  "5. Hanoi Open Data Portal: https://opendata.hanoi.gov.vn\n\n"
  "Note: Built-in QHC 2045 envelopes are for architectural analysis only, not statutory red-line evidence. You can directly import VN-2000 Hanoi DXF/GeoJSON files or drape PNG/JPG zoning maps."));
 box.exec();
}

void MainWindow::closeEvent(QCloseEvent *event) {
 if (closing_) { event->ignore(); return; }
 data_->cancel();
 if (hanoiTiles_) hanoiTiles_->cancel();
 importing_ = false;
 importDocumentId_.clear();
 snapshotDocument();
 const int previous = activeDocument_;
 for (int i = 0; i < documents_.size(); ++i) {
  activateDocument(i);
  if (!maybeSave()) { event->ignore(); return; }
  snapshotDocument();
 }
 if (previous >= 0) activateDocument(previous);
 closing_ = true;
 persistPreferences();
 autosave_.stop();
 saveAutosave();
 if (sessionStore_->busy()) {
  QProgressDialog progress(trText("Đang hoàn tất bản khôi phục…", "Finishing local recovery…"), QString(), 0, 0, this);
  progress.setWindowTitle(trText("Đang lưu dự án", "Saving projects"));
  progress.setCancelButton(nullptr);
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(0);
  progress.show();
  sessionStore_->flush();
  progress.close();
 }
 map_->shutdown();
 viewport_->shutdown();
 event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
 if (e->mimeData()->hasUrls() && e->mimeData()->urls().size() == 1 && e->mimeData()->urls().first().isLocalFile()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e) {
 if (e->mimeData()->urls().size() == 1) {
  loadPath(e->mimeData()->urls().first().toLocalFile());
  e->acceptProposedAction();
 }
}

QJsonObject MainWindow::diagnostics() const {
 const auto s = project_.stats();
 return {
  {"application", "GroundAtlas Desktop"}, {"version", "2.3.1"}, {"native", true}, {"webview", false},
  {"qt_version", qVersion()}, {"open_documents", documents_.size()}, {"imports", ga::geoJsonImportSummaries(project_)},
  {"models_3d", models3d_.size()}, {"sketchup_direct_api", ga::sketchUpDirectImportAvailable()},
  {"planning_level", activePlanningLevel_}, {"planning_custom_zones", planningCustom_.zones.size()},
  {"hanoi_planning_tiles", hanoiPlanEnabled_}, {"hanoi_planning_layer", hanoiPlanLayer_},
  {"license", license_ ? license_->diagnostics() : QJsonObject{}},
  {"source_busy", loading_}, {"window_chrome", property("gaChrome").toBool() ? "macOS-style-native" : "system"},
  {"styles", ga::palettes().size()}, {"diagram_kits", ga::diagramPresets().size()},
  {"data_layers", ga::defaultLayers().size() - 1}, {"map_provider", "OpenFreeMap vector"},
  {"basemap", map_->basemapDiagnostics()}, {"scene_basemap", viewport_->basemapDiagnostics()},
  {"climate_stations", 36}, {"features", s.features}, {"buildings", s.buildings},
  {"known_heights", s.knownHeights}, {"holes", s.holeCount}, {"triangles", viewport_->triangleCount()},
  {"renderer", viewport_->renderer()}, {"graphics_error", viewport_->graphicsError()},
  {"camera", viewport_->camera().toJson()}, {"dark_theme", darkTheme_},
 };
}

void MainWindow::syncMap() { if (map_) map_->setPresentation(layers_, ga::paletteFor(project_.style), diagram_); }

void MainWindow::chooseSite(double lat, double lon, const QString &name) {
 siteSelection_->setSite(siteSelection_->site(), false);
 siteSelection_->setCenter(lat, lon, name.isEmpty() ? QString::number(lat, 'f', 5) + ", " + QString::number(lon, 'f', 5) : name);
 // Auto-switch source from "current" to "osm" whenever picking a location outside the currently open site!
 if (sourceChoice_ && sourceChoice_->currentData().toString() == "current" && !project_.features.isEmpty()) {
  const QPointF rel = ga::localPoint(lon, lat, project_.site);
  if (std::hypot(rel.x(), rel.y()) > std::max(80.0, project_.site.radius * 0.85)) {
   const int osmIdx = sourceChoice_->findData("osm");
   if (osmIdx >= 0) {
    const QSignalBlocker b(sourceChoice_);
    sourceChoice_->setCurrentIndex(osmIdx);
    showToast(trText("Đã chọn vị trí mới · Nguồn tự chuyển sang OpenStreetMap", "New site selected · Source set to OpenStreetMap"), QColor("#0284c7"), 2200);
   }
  }
 }
 syncSelection();
}

void MainWindow::syncSelection() {
 if (!siteSelection_ || !loadButton_ || !sourceChoice_) return;
 candidate_ = siteSelection_->site();
 map_->setCandidate(candidate_, siteSelection_->locked());
 candidateText_->setText(trText("Điểm nạp: ", "Load location: ") + candidate_.name);
 const auto issue = ga::selectionError(candidate_);
 loadButton_->setEnabled(!loading_ && issue.isEmpty());
 loadButton_->setToolTip(ga::localizedSiteError(issue, english_));
 enrichButton_->setEnabled(!loading_ && !project_.features.isEmpty());
 refreshSource_->setEnabled(!loading_);
 if (importButton_) {
  importButton_->setEnabled(!loading_);
  importRole_->setEnabled(!loading_);
  actions_["import"]->setEnabled(!loading_);
  removeImportButton_->setEnabled(!loading_ && importList_->currentItem());
 }
 const auto id = sourceChoice_->currentData().toString();
 sourceHint_->setText(id == "osm"
  ? trText("OSM công khai · chiều cao theo nguồn, phần thiếu giữ phẳng.", "Public OSM · recorded heights; unknowns stay flat.")
  : id == "current"
   ? trText("Cắt lại hình học đang mở.", "Reclip current geometry.")
   : sourceChoice_->currentData(Qt::UserRole + 1).toJsonObject()["source"].toString());
}

ga::Project MainWindow::captureProject() const {
 auto copy = project_;
 copy.view = viewport_->camera().toJson();
 copy.view["diagram"] = diagram_;
 copy.view["geographic"] = geographic_;
 copy.view["map"] = map_->view();
 copy.view["navigation"] = viewport_->geographicView();
 copy.view["selection"] = siteSelection_->state();
 copy.view["selection_draft"] = map_->draftState();
 copy.view["selection_source"] = sourceChoice_->currentData().toString();
 copy.view["import_role"] = importRole_->currentData().toString();
 copy.view["plot"] = plot_.toJson();
 copy.view["massing"] = massing_.toJson();
 QJsonArray m3dArr;
 for (const auto &m : models3d_) m3dArr.append(m.toJson());
 copy.view["models3d"] = m3dArr;
 copy.view["planning_level"] = activePlanningLevel_;
 copy.view["planning_opacity"] = planningOpacity_;
 copy.view["planning_image"] = planningImage_.toJson();
 copy.view["hanoi_planning"] = QJsonObject{{"enabled", hanoiPlanEnabled_}, {"layer", hanoiPlanLayer_}, {"opacity", hanoiPlanOpacity_}};
 copy.notes = notes_->toPlainText();
 copy.layerState = {};
 QJsonArray order;
 for (const auto &layer : layers_) {
  copy.layerState[layer.id] = ga::layerJson(layer);
  order.append(layer.id);
 }
 copy.view["layer_order"] = order;
 return copy;
}

void MainWindow::snapshotDocument() {
 if (activeDocument_ < 0 || activeDocument_ >= documents_.size()) return;
 auto &document = documents_[activeDocument_];
 document.project = captureProject();
 document.path = currentPath_;
 document.modified = modified_;
}

void MainWindow::addDocument(ga::Project next, const QString &path) {
 if (documents_.size() >= 32) {
  QMessageBox::information(this, "GroundAtlas", trText("Đang mở 32 dự án. Đóng bớt tab trước.", "32 tabs open. Close a tab first."));
  return;
 }
 snapshotDocument();
 switching_ = true;
 const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
 documents_.append({id, path, std::move(next), false});
 tabs_->addTab(documents_.last().project.site.name);
 activeDocument_ = -1;
 switching_ = false;
 activateDocument(documents_.size() - 1);
}

void MainWindow::activateDocument(int index) {
 if (index < 0 || index >= documents_.size() || index == activeDocument_) return;
 snapshotDocument();
 switching_ = true;
 const auto document = documents_[index];
 activeDocument_ = index;
 project_ = document.project;
 currentPath_ = document.path;
 geographic_ = project_.view["geographic"].toBool();
 syncProject();
 modified_ = document.modified;
 tabs_->setCurrentIndex(index);
 const auto mapView = project_.view["map"].toObject();
 if (mapView["lat"].isDouble() && mapView["lon"].isDouble() && mapView["zoom"].isDouble()) {
  map_->centerOn(mapView["lat"].toDouble(), mapView["lon"].toDouble(), mapView["zoom"].toDouble());
 }
 views_->setCurrentWidget(geographic_ ? static_cast<QWidget *>(map_) : static_cast<QWidget *>(viewport_));
 actions_["map"]->setChecked(geographic_);
 const auto navigation = project_.view["navigation"].toObject();
 if (navigation["lat"].isDouble() && navigation["lon"].isDouble() && navigation["zoom"].isDouble()) {
  viewport_->setGeographicView(navigation["lat"].toDouble(), navigation["lon"].toDouble(), navigation["zoom"].toDouble());
  map_->centerOn(navigation["lat"].toDouble(), navigation["lon"].toDouble(), navigation["zoom"].toDouble());
 }
 switching_ = false;
 syncNavigationFromActive();
 modified_ = document.modified;
 syncTabs();
 syncTitle();
}

bool MainWindow::closeDocument(int index) {
 if (index < 0 || index >= documents_.size()) return false;
 activateDocument(index);
 if (!maybeSave()) return false;
 snapshotDocument();
 switching_ = true;
 documents_.removeAt(index);
 tabs_->removeTab(index);
 activeDocument_ = -1;
 switching_ = false;
 if (documents_.isEmpty()) newProject();
 else activateDocument(std::min(index, int(documents_.size()) - 1));
 saveAutosave();
 return true;
}

void MainWindow::syncTabs() {
 if (!tabs_ || switching_) return;
 const QSignalBlocker block(tabs_);
 for (int i = 0; i < documents_.size(); ++i) {
  const bool active = i == activeDocument_;
  const auto name = active ? project_.site.name : documents_[i].project.site.name;
  const bool dirty = active ? modified_ : documents_[i].modified;
  tabs_->setTabText(i, (dirty ? QString("● ") : QString()) + name);
  const QString p = active ? currentPath_ : documents_[i].path;
  tabs_->setTabToolTip(i, p.isEmpty() ? trText("Dự án trên máy · chưa lưu tệp", "Local project · unsaved") : p);
 }
}

void MainWindow::syncNavigationFromActive() {
 if (updating_ || switching_ || navigating_) return;
 navigating_ = true;
 if (geographic_) {
  const auto v = map_->view();
  viewport_->setGeographicView(v["lat"].toDouble(), v["lon"].toDouble(), v["zoom"].toDouble());
 } else {
  const auto v = viewport_->geographicView();
  if (!v.isEmpty()) map_->centerOn(v["lat"].toDouble(), v["lon"].toDouble(), v["zoom"].toDouble());
 }
 navigating_ = false;
}

void MainWindow::showGeographicView() {
 if (!geographic_) syncNavigationFromActive();
 geographic_ = true;
 views_->setCurrentWidget(map_);
 if (liveSection_) liveSection_->hide();
 actions_["map"]->setChecked(true);
 actions_["view-2d"]->setChecked(false);
 actions_["view-3d"]->setChecked(false);
 for (auto *track : segTracks_) if (track) track->syncAnimated();
 markModified();
}

void MainWindow::showArchitecturalView(const ga::Camera &camera) {
 const auto v = geographic_ ? map_->view() : viewport_->geographicView();
 navigating_ = true;
 geographic_ = false;
 views_->setCurrentWidget(viewport_);
 if (liveSection_ && viewport_->isSectionCutActive()) { liveSection_->show(); updateLiveSection(); }
 actions_["map"]->setChecked(false);
 viewport_->setCamera(camera);
 if (!v.isEmpty()) viewport_->setGeographicView(v["lat"].toDouble(), v["lon"].toDouble(), v["zoom"].toDouble());
 navigating_ = false;
 syncNavigationFromActive();
}

void MainWindow::chooseGeoJsonOverlay() {
 if (loading_) return;
 const auto path = QFileDialog::getOpenFileName(this, trText("Thêm lớp GeoJSON WGS84", "Add WGS84 GeoJSON overlay"), {}, "GeoJSON (*.geojson *.json)");
 if (!path.isEmpty()) importGeoJson(path, importRole_->currentData().toString());
}

bool MainWindow::importGeoJson(const QString &path, const QString &role) {
 if (loading_ || activeDocument_ < 0) return false;
 importDocumentId_ = documents_[activeDocument_].id;
 importDataRevision_ = documents_[activeDocument_].dataRevision;
 importing_ = true;
 data_->importOverlay(captureProject(), path, role);
 return true;
}

bool MainWindow::removeImportedGeoJson(const QString &id) {
 if (loading_ || activeDocument_ < 0) return false;
 try {
  project_ = ga::removeGeoJsonOverlay(captureProject(), id);
  ++documents_[activeDocument_].dataRevision;
  refreshProjectGeometry();
  markModified();
  snapshotDocument();
  statusBar()->showMessage(trText("Đã gỡ file nhập.", "Imported file removed."), 8000);
  return true;
 } catch (const std::exception &e) {
  statusBar()->showMessage(ga::localizedImportError(ga::errorText(e), english_), 10000);
  return false;
 }
}

void MainWindow::refreshProjectGeometry() {
 const bool old = updating_;
 updating_ = true;
 const auto mapView = map_->view();
 const auto navigation = viewport_->geographicView();
 viewport_->setProject(&project_);
 map_->setProject(&project_);
 liveDem_ = {};
 for (const auto &field : ga::environmentFields(ga::appDataPath("environment"), project_.site)) {
  if (field["id"].toString() == "gedtm30") { liveDem_ = ga::TerrainGrid::fromEnvironmentField(field); break; }
 }
 if (viewport_->isSectionCutActive()) updateLiveSection();
 map_->centerOn(mapView["lat"].toDouble(), mapView["lon"].toDouble(), mapView["zoom"].toDouble());
 viewport_->setGeographicView(navigation["lat"].toDouble(), navigation["lon"].toDouble(), navigation["zoom"].toDouble());
 detailedCounts_ = ga::layerCounts(project_.features);
 syncMap();
 syncLayers();
 syncImports();
 syncInspector();
 updating_ = old;
}

void MainWindow::syncImports() {
 if (!importList_) return;
 const auto role = importRole_->currentData();
 {
  const QSignalBlocker blocked(importRole_);
  importRole_->clear();
  for (const auto &id : ga::geoJsonRoles()) importRole_->addItem(ga::geoJsonRoleName(id, english_), id);
  importRole_->setCurrentIndex(std::max(0, importRole_->findData(role)));
 }
 const auto selected = importList_->currentItem() ? importList_->currentItem()->data(Qt::UserRole).toString() : QString();
 const QSignalBlocker blocked(importList_);
 importList_->clear();
 for (const auto &value : project_.imports) {
  const auto entry = value.toObject();
  auto *item = new QListWidgetItem(entry["name"].toString() + "\n" + ga::geoJsonRoleName(entry["role"].toString(), english_) + trText(" · %1 phần", " · %1 parts").arg(entry["count"].toInt()), importList_);
  item->setData(Qt::UserRole, entry["id"]);
  if (entry["id"] == selected) importList_->setCurrentItem(item);
 }
 if (!importList_->currentItem() && importList_->count()) importList_->setCurrentRow(importList_->count() - 1);
 importList_->setVisible(importList_->count() > 0);
 removeImportButton_->setVisible(importList_->count() > 0);
 syncSelection();
}
