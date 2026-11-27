#include "dialogs_v2.h"
#include "exporter.h"
#include "uxmotion.h"
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QString bi(bool en, const char *vi, const char *enText) {
 return QString::fromUtf8(en ? enText : vi);
}

class SwatchPreview : public QWidget {
public:
 QVector<QColor> colors;
 explicit SwatchPreview(QVector<QColor> c, QWidget *parent = nullptr) : QWidget(parent), colors(std::move(c)) {
  setFixedHeight(28);
 }
protected:
 void paintEvent(QPaintEvent *) override {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  if (colors.isEmpty()) return;
  const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath clip;
  clip.addRoundedRect(r, 6.0, 6.0);
  p.save();
  p.setClipPath(clip);
  const double w = r.width() / colors.size();
  for (int i = 0; i < colors.size(); ++i) {
   p.setPen(Qt::NoPen);
   p.setBrush(colors[i]);
   p.drawRect(QRectF(r.left() + i * w, r.top(), w + 0.8, r.height()));
  }
  p.restore();
  p.setPen(QPen(QColor(0, 0, 0, 34), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(r, 6.0, 6.0);
 }
};
} // namespace

StyleGalleryDialog::StyleGalleryDialog(const QString &activeStyle,
                                       const QString &activeDiagram,
                                       bool english,
                                       std::function<void(const QString &)> onPickStyle,
                                       std::function<void(const QString &)> onPickDiagram,
                                       QWidget *parent) : QDialog(parent) {
 setWindowTitle(bi(english, "Thư viện Phong cách & Bộ sơ đồ — GroundAtlas", "Style & Diagram Library — GroundAtlas"));
 resize(880, 640);
 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(18, 16, 18, 16);
 root->setSpacing(10);

 auto *search = new QLineEdit(this);
 search->setPlaceholderText(bi(english, "Tìm trong 39 phong cách & 20 bộ sơ đồ…", "Search 39 architectural styles & 20 diagram kits…"));
 root->addWidget(search);

 auto *tabs = new QTabWidget(this);
 root->addWidget(tabs, 1);

 // Tab 1: 39 Styles
 auto *styleScroll = new QScrollArea(tabs);
 styleScroll->setWidgetResizable(true);
 auto *styleHost = new QWidget(styleScroll);
 auto *styleGrid = new QGridLayout(styleHost);
 styleGrid->setSpacing(10);
 styleScroll->setWidget(styleHost);
 tabs->addTab(styleScroll, bi(english, "39 Phong cách bản đồ", "39 Map Styles"));

 struct CardEntry { QWidget *card; QString searchText; };
 QVector<CardEntry> styleCards, kitCards;

 const auto pals = ga::palettes();
 for (int i = 0; i < pals.size(); ++i) {
  const auto &pal = pals[i];
  auto *card = new QFrame(styleHost);
  card->setObjectName("v2-card");
  if (pal.id == activeStyle) card->setStyleSheet("QFrame#v2-card{border:2px solid #3b63fb;}");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(10, 10, 10, 10);
  cl->setSpacing(6);
  cl->addWidget(new SwatchPreview({pal.paper, pal.building, pal.road, pal.green, pal.water, pal.ink}, card));
  auto *name = new QLabel(english ? pal.en : pal.vi, card);
  name->setStyleSheet("font-weight:600;");
  name->setWordWrap(true);
  cl->addWidget(name);
  auto *btn = new QPushButton(pal.id == activeStyle ? bi(english, "Đang dùng ✓", "Active ✓") : bi(english, "Áp dụng", "Apply"), card);
  if (pal.id == activeStyle) btn->setProperty("accent", true);
  connect(btn, &QPushButton::clicked, this, [this, id = pal.id, onPickStyle] {
   if (onPickStyle) onPickStyle(id);
   accept();
  });
  cl->addWidget(btn);
  styleGrid->addWidget(card, i / 3, i % 3);
  styleCards.append({card, (pal.vi + " " + pal.en + " " + pal.id).toLower()});
 }

 // Tab 2: 20 Diagram Kits
 auto *kitScroll = new QScrollArea(tabs);
 kitScroll->setWidgetResizable(true);
 auto *kitHost = new QWidget(kitScroll);
 auto *kitGrid = new QGridLayout(kitHost);
 kitGrid->setSpacing(10);
 kitScroll->setWidget(kitHost);
 tabs->addTab(kitScroll, bi(english, "20 Bộ sơ đồ đồ án", "20 Studio Diagram Kits"));

 const auto kits = ga::diagramPresets();
 for (int i = 0; i < kits.size(); ++i) {
  const auto &kit = kits[i];
  const auto &pal = kit.palette;
  auto *card = new QFrame(kitHost);
  card->setObjectName("v2-card");
  if (kit.id == activeDiagram) card->setStyleSheet("QFrame#v2-card{border:2px solid #3b63fb;}");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(10, 10, 10, 10);
  cl->setSpacing(6);
  cl->addWidget(new SwatchPreview({pal.paper, pal.building, pal.road, pal.green, pal.water, pal.ink}, card));
  auto *name = new QLabel(english ? kit.en : kit.vi, card);
  name->setStyleSheet("font-weight:600;");
  name->setWordWrap(true);
  cl->addWidget(name);
  auto *btn = new QPushButton(bi(english, "Áp dụng bộ sơ đồ", "Apply diagram kit"), card);
  connect(btn, &QPushButton::clicked, this, [this, id = kit.id, onPickDiagram] {
   if (onPickDiagram) onPickDiagram(id);
   accept();
  });
  cl->addWidget(btn);
  kitGrid->addWidget(card, i / 3, i % 3);
  kitCards.append({card, (kit.vi + " " + kit.en + " " + kit.id).toLower()});
 }

 connect(search, &QLineEdit::textChanged, this, [styleCards, kitCards](const QString &q) {
  const QString term = q.trimmed().toLower();
  for (const auto &e : styleCards) e.card->setVisible(term.isEmpty() || e.searchText.contains(term));
  for (const auto &e : kitCards) e.card->setVisible(term.isEmpty() || e.searchText.contains(term));
 });

 auto *foot = new QHBoxLayout();
 foot->addStretch();
 auto *closeBtn = new QPushButton(bi(english, "Đóng", "Close"), this);
 connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
 foot->addWidget(closeBtn);
 root->addLayout(foot);
 ux::attachDialogFadeIn(this);
}

ExportStudioDialog::ExportStudioDialog(const QString &siteName,
                                       bool english,
                                       std::function<void(const QString &)> onExportKind,
                                       QWidget *parent) : QDialog(parent) {
 setWindowTitle(bi(english, "Xuất Mô hình 3D, Ảnh 4K/8K & Bản vẽ — GroundAtlas", "Export 3D Models, 4K/8K PNG & CAD — GroundAtlas"));
 resize(820, 620);
 const auto dcc = ga::detectDccApps();

 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(20, 18, 20, 16);
 root->setSpacing(12);

 auto *title = new QLabel(siteName + " — " + bi(english, "Xuất trực tiếp trên máy (Native C++ / GPU)", "Direct Native Exports (C++ / GPU)"), this);
 title->setStyleSheet("font-size:16px;font-weight:600;");
 root->addWidget(title);

 QStringList detected;
 if (dcc.sketchup.found()) detected << dcc.sketchup.name;
 if (dcc.blender.found()) detected << dcc.blender.name;
 if (dcc.autocad.found()) detected << dcc.autocad.name;
 if (dcc.rhino.found()) detected << dcc.rhino.name;
 auto *dccHint = new QLabel(
  detected.isEmpty()
   ? bi(english, "Xuất trực tiếp file tiêu chuẩn CAD/3D và ảnh 4K/8K không giới hạn trình duyệt.", "Direct standard CAD/3D and 4K/8K exports without browser limits.")
   : bi(english, "Phần mềm CAD/3D đã sẵn sàng kết nối trực tiếp: ", "Detected local CAD/3D apps ready for 1-click bridge: ") + detected.join(" · "),
  this);
 dccHint->setObjectName("muted");
 root->addWidget(dccHint);

 auto *scroll = new QScrollArea(this);
 scroll->setWidgetResizable(true);
 auto *host = new QWidget(scroll);
 auto *grid = new QGridLayout(host);
 grid->setSpacing(10);
 scroll->setWidget(host);
 root->addWidget(scroll, 1);

 struct ExportItem { QString id, titleVi, titleEn, descVi, descEn; bool primary = false; };
 const QVector<ExportItem> items = {
  {"project-kit",  "Bộ hồ sơ Đồ án 1-Click (8 tệp)", "1-Click Presentation Kit (8 files)", "Tự xuất trọn thư mục: 4K Suushka + 4K Nắng 3D + Mặt cắt SVG/DXF + CAD 3D + SketchUp + PDF A3 + GIF Nắng!", "Auto-exports full folder: 4K Plan + 4K Sun 3D + Section SVG/DXF + CAD 3D + SketchUp + A3 PDF + Sun GIF!", true},
  {"sun-gif",      "Hoạt ảnh Nắng 1 ngày (.GIF)", "Full-Day Sun Timelapse (.GIF)", "Render trực tiếp từ GPU chuyển động nắng 06:30→17:30 thả thẳng vào slide/PowerPoint", "GPU-rendered 06:30→17:30 solar shadow study GIF ready for PowerPoint & Keynote", true},
  {"turntable-gif","Hoạt ảnh Xoay 360° (.GIF)", "360° Orbital Turntable (.GIF)", "Quay 360° mượt quanh khu đất & khối công trình đề xuất kèm bóng đổ 4096px + SSAO", "Smooth 360° orbit around site & proposed massing with 4096px shadows + SSAO", true},
  {"skp",          "SketchUp (.SKP thật)", "SketchUp (Native .SKP)", "Xuất COLLADA mét & tự gọi SketchUp lưu thành file .skp chuẩn", "Exports metric COLLADA & triggers SketchUp to save a true .skp", true},
  {"blend",        "Blender (.BLEND thật)", "Blender (Native .BLEND)", "Tự chạy Blender dựng đèn Sun, Sky, Camera 3/4 & lưu .blend thật", "Runs headless Blender to add Sun, Sky, Camera & save a true .blend", true},
  {"autocad",      "AutoCAD (.DXF mở ngay)", "AutoCAD (Open .DXF)", "Xuất DXF phân lớp chuẩn (GA_BUILDINGS, GA_PLOT, GA_PROPOSED, GA_CONTOURS) & mở AutoCAD", "Layered DXF (GA_BUILDINGS, GA_PLOT, GA_PROPOSED, GA_CONTOURS) & launch AutoCAD", true},
  {"png-4k",       "Ảnh PNG 4K (3840×2160)", "4K Render PNG (3840×2160)", "Render trực tiếp GPU 4K không dính HUD, nét căng cho dàn trang đồ án", "Clean HUD-free GPU 4K render ready for presentation boards", true},
  {"png-8k-alpha", "PNG 8K Nền trong suốt", "8K Transparent PNG", "Ảnh 7680×4320 tách nền alpha, thả thẳng vào Photoshop / InDesign", "7680×4320 alpha-transparent render for Photoshop / InDesign", true},
  {"report",       "Báo cáo Đồ án 3 trang (.PDF)", "3-Page Site Report (.PDF)", "Tự dàn trang A3: TMB + Số liệu BCR + 4 Sơ đồ + Mặt cắt + Khí hậu", "Auto-laid-out A3 board: Plan + BCR + 4 Diagrams + Section + Climate", true},
  {"dxf",          "2D/3D CAD (.DXF)", "2D/3D CAD (.DXF)", "Bản vẽ CAD phân lớp đơn vị mét cho AutoCAD / Revit / ArchiCAD", "Metric layered CAD drawing for AutoCAD / Revit / ArchiCAD", false},
  {"dae",          "SketchUp COLLADA (.DAE)", "SketchUp COLLADA (.DAE)", "Mô hình 3D Z-up 1:1 mét giữ nhóm vật liệu, nạp mượt vào SketchUp", "Z-up metric 3D model preserving material groups for SketchUp", false},
  {"glb",          "glTF 2.0 nhị phân (.GLB)", "Binary glTF 2.0 (.GLB)", "Chuẩn 3D hiện đại cho Blender, Lumion, Unreal, Twinmotion", "Modern 3D binary mesh for Blender, Lumion, Unreal, Twinmotion", false},
  {"stl",          "In 3D / Mô hình sa bàn (.STL)", "3D Print Mesh (.STL)", "Lưới tam giác nhị phân kèm đế khu đất cho máy in 3D & Rhino", "Binary triangle mesh with site plinth for 3D printing & Rhino", false},
  {"svg",          "Vector 2D (.SVG)", "2D Vector (.SVG)", "Bản vẽ nét vector chỉnh sửa trực tiếp trên Illustrator / Figma", "Editable vector linework for Illustrator / Figma", false},
  {"pdf",          "Bản vẽ A3 (.PDF)", "A3 Sheet (.PDF)", "Tờ bản vẽ 2D khổ A3 độ nét cao sẵn sàng in ấn", "High-resolution A3 architectural drawing sheet", false},
  {"laser-dxf",    "Cắt Laser 1:1000 (.DXF)", "Laser Cut 1:1000 (.DXF)", "Tách nét cắt đỏ (đế + khối nhà) & nét khắc xanh (đường/nước)", "Separated red cut lines (base + buildings) & blue score lines", false},
  {"geojson",      "Dữ liệu GIS (.GeoJSON)", "GIS Data (.GeoJSON)", "Toàn bộ thuộc tính WGS84 dùng cho QGIS / ArcGIS / Grasshopper", "Full WGS84 attributes for QGIS / ArcGIS / Grasshopper", false},
  {"png",          "Ảnh khung nhìn (.PNG)", "Viewport Snapshot (.PNG)", "Chụp nhanh khung nhìn hiện tại theo đúng tỷ lệ cửa sổ", "Quick snapshot matching the current viewport", false},
 };

 for (int i = 0; i < items.size(); ++i) {
  const auto &it = items[i];
  auto *card = new QFrame(host);
  card->setObjectName("v2-card");
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(11, 9, 11, 9);
  cl->setSpacing(4);
  auto *headRow = new QHBoxLayout();
  headRow->setSpacing(4);
  auto *h = new QLabel(bi(english, it.titleVi.toUtf8().constData(), it.titleEn.toUtf8().constData()), card);
  h->setStyleSheet("font-weight:600;font-size:12px;");
  headRow->addWidget(h, 1);
  const bool bridgeReady = (it.id == "skp" && dcc.sketchup.found()) || (it.id == "blend" && dcc.blender.found()) || (it.id == "autocad" && dcc.autocad.found());
  if (bridgeReady) {
   auto *badge = new QLabel(bi(english, "● Sẵn sàng", "● Ready"), card);
   badge->setObjectName("ready-badge");
   headRow->addWidget(badge);
  }
  auto *d = new QLabel(bi(english, it.descVi.toUtf8().constData(), it.descEn.toUtf8().constData()), card);
  d->setObjectName("muted");
  d->setWordWrap(true);
  auto *btn = new QPushButton(bi(english, "Xuất ngay", "Export"), card);
  if (it.primary) btn->setProperty("accent", true);
  connect(btn, &QPushButton::clicked, this, [this, id = it.id, onExportKind] {
   accept();
   if (onExportKind) onExportKind(id);
  });
  cl->addLayout(headRow);
  cl->addWidget(d, 1);
  cl->addWidget(btn);
  grid->addWidget(card, i / 3, i % 3);
 }

 auto *foot = new QHBoxLayout();
 foot->addStretch();
 auto *closeBtn = new QPushButton(bi(english, "Đóng", "Close"), this);
 connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
 foot->addWidget(closeBtn);
 root->addLayout(foot);
 ux::attachDialogFadeIn(this);
}

SettingsDialog::SettingsDialog(const Values &initial,
                               const ga::HardwareInfo &hardware,
                               std::function<void(const Values &)> onApply,
                               QWidget *parent) : QDialog(parent) {
 const bool en = initial.english;
 setWindowTitle(bi(en, "Cài đặt Hiệu năng & Không gian làm việc — GroundAtlas", "Performance & Workspace Settings — GroundAtlas"));
 resize(520, 510);

 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(20, 18, 20, 16);
 root->setSpacing(10);

 // Hardware Auto-Detection Banner Card
 auto *hwCard = new QFrame(this);
 hwCard->setObjectName("v2-card");
 auto *hwLay = new QVBoxLayout(hwCard);
 hwLay->setContentsMargins(12, 10, 12, 10);
 hwLay->setSpacing(4);
 auto *hwHead = new QHBoxLayout();
 auto *hwEyebrow = new QLabel(bi(en, "PHẦN CỨNG & MÀN HÌNH TỰ NHẬN DIỆN", "AUTO-DETECTED HARDWARE & DISPLAY"), hwCard);
 hwEyebrow->setObjectName("eyebrow");
 auto *hzBadge = new QLabel(QString("%1 Hz").arg(hardware.displayHz), hwCard);
 hzBadge->setObjectName("ready-badge");
 hwHead->addWidget(hwEyebrow);
 hwHead->addStretch();
 hwHead->addWidget(hzBadge);
 hwLay->addLayout(hwHead);
 auto *hwSummary = new QLabel(hardware.summary(en), hwCard);
 hwSummary->setObjectName("muted");
 hwSummary->setWordWrap(true);
 hwLay->addWidget(hwSummary);
 root->addWidget(hwCard);

 auto *perfLbl = new QLabel(bi(en, "CHẾ ĐỘ ĐỒ HỌA & HIỆU NĂNG (TỐI ĐA 500 Hz)", "GRAPHICS & PERFORMANCE PROFILE (UP TO 500 Hz)"), this);
 perfLbl->setObjectName("section");
 auto *perfBox = new QComboBox(this);
 for (auto tier : {ga::PerfTier::Auto, ga::PerfTier::Low, ga::PerfTier::Medium, ga::PerfTier::Ultra}) {
  perfBox->addItem(ga::perfTierName(tier, en), int(tier));
 }
 perfBox->setCurrentIndex(std::max(0, perfBox->findData(int(initial.perfTier))));

 auto *themeLbl = new QLabel(bi(en, "GIAO DIỆN", "APPEARANCE"), this);
 themeLbl->setObjectName("section");
 auto *themeBox = new QComboBox(this);
 themeBox->addItem(bi(en, "Giao diện Sáng (Spectrum Light)", "Light Theme (Spectrum Light)"), false);
 themeBox->addItem(bi(en, "Giao diện Tối (Spectrum Dark)", "Dark Theme (Spectrum Dark)"), true);
 themeBox->setCurrentIndex(initial.darkTheme ? 1 : 0);

 auto *row2 = new QHBoxLayout();
 row2->setSpacing(10);
 auto *langCol = new QVBoxLayout();
 auto *langLbl = new QLabel(bi(en, "NGÔN NGỮ", "LANGUAGE"), this);
 langLbl->setObjectName("section");
 auto *langBox = new QComboBox(this);
 langBox->addItem("Tiếng Việt (VI)", false);
 langBox->addItem("English (EN)", true);
 langBox->setCurrentIndex(initial.english ? 1 : 0);
 langCol->addWidget(langLbl);
 langCol->addWidget(langBox);

 auto *unitCol = new QVBoxLayout();
 auto *unitLbl = new QLabel(bi(en, "ĐƠN VỊ ĐO", "UNITS"), this);
 unitLbl->setObjectName("section");
 auto *unitBox = new QComboBox(this);
 unitBox->addItem(bi(en, "Hệ Mét (m / m²)", "Metric (m / m²)"), false);
 unitBox->addItem(bi(en, "Hệ Anh (ft / sq ft)", "Imperial (ft / sq ft)"), true);
 unitBox->setCurrentIndex(initial.imperialUnits ? 1 : 0);
 unitCol->addWidget(unitLbl);
 unitCol->addWidget(unitBox);
 row2->addLayout(langCol, 1);
 row2->addLayout(unitCol, 1);

 auto *dampingCheck = new QCheckBox(bi(en, "Bật quán tính mượt khi cuộn phóng & xoay 3D (theo tần số màn hình)", "Enable smooth inertia damping for wheel zoom & 3D orbit (display Hz)"), this);
 dampingCheck->setChecked(initial.smoothDamping);
 auto *darkMapCheck = new QCheckBox(bi(en, "Tự làm tối dịu bản đồ nền ngoài ranh khi bật Dark Mode", "Auto-soften outside vector basemap in Dark Mode"), this);
 darkMapCheck->setChecked(initial.autoDarkBasemap);
 auto *terrainCheck = new QCheckBox(bi(en, "Dựng địa hình 3D thật theo cao độ DEM (GEDTM30)", "Enable 3D terrain elevation from DEM (GEDTM30)"), this);
 terrainCheck->setChecked(initial.terrain3d);
 auto *ctxCheck = new QCheckBox(bi(en, "Đồng bộ phong cách bối cảnh bản đồ ngoài ranh", "Style vector basemap context outside site"), this);
 ctxCheck->setChecked(initial.contextStyle);

 root->addWidget(perfLbl);
 root->addWidget(perfBox);
 root->addWidget(themeLbl);
 root->addWidget(themeBox);
 root->addLayout(row2);
 root->addWidget(dampingCheck);
 root->addWidget(darkMapCheck);
 root->addWidget(terrainCheck);
 root->addWidget(ctxCheck);
 root->addStretch();

 auto *foot = new QHBoxLayout();
 foot->addStretch();
 auto *cancelBtn = new QPushButton(bi(en, "Hủy", "Cancel"), this);
 auto *okBtn = new QPushButton(bi(en, "Áp dụng & Lưu cấu hình", "Apply & Save"), this);
 okBtn->setProperty("accent", true);
 connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
 connect(okBtn, &QPushButton::clicked, this, [this, themeBox, langBox, unitBox, ctxCheck, perfBox, dampingCheck, darkMapCheck, terrainCheck, onApply] {
  Values v;
  v.darkTheme = themeBox->currentData().toBool();
  v.english = langBox->currentData().toBool();
  v.imperialUnits = unitBox->currentData().toBool();
  v.contextStyle = ctxCheck->isChecked();
  v.perfTier = static_cast<ga::PerfTier>(perfBox->currentData().toInt());
  v.smoothDamping = dampingCheck->isChecked();
  v.autoDarkBasemap = darkMapCheck->isChecked();
  v.terrain3d = terrainCheck->isChecked();
  if (onApply) onApply(v);
  accept();
 });
 foot->addWidget(cancelBtn);
 foot->addWidget(okBtn);
 root->addLayout(foot);
 ux::attachDialogFadeIn(this);
}

CommandPaletteDialog::CommandPaletteDialog(const QVector<Entry> &entries, bool english, QWidget *parent)
 : QDialog(parent) {
 setWindowTitle(bi(english, "Bảng lệnh nhanh (Ctrl+P) — GroundAtlas", "Command Palette (Ctrl+P) — GroundAtlas"));
 resize(560, 440);
 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(14, 14, 14, 14);
 root->setSpacing(8);

 auto *input = new QLineEdit(this);
 input->setObjectName("place-search");
 input->setPlaceholderText(bi(english, "Gõ tên sơ đồ, phong cách, hoặc lệnh (vd: suushka, nắng, 4K, sketchup, thước)…",
                              "Type a diagram, style, or command (e.g. suushka, sun, 4K, sketchup, ruler)…"));
 root->addWidget(input);

 auto *list = new QListWidget(this);
 list->setObjectName("diagram-list");
 root->addWidget(list, 1);

 auto *hint = new QLabel(bi(english, "↑↓ Chọn  ·  Enter Chạy ngay  ·  Esc Đóng", "↑↓ Select  ·  Enter Run  ·  Esc Close"), this);
 hint->setObjectName("muted");
 root->addWidget(hint);

 auto populate = [list, entries](const QString &q) {
  list->clear();
  const QString term = q.trimmed().toLower();
  int shown = 0;
  for (int i = 0; i < entries.size(); ++i) {
   const auto &e = entries[i];
   const QString hay = (e.title + " " + e.subtitle + " " + e.category + " " + e.keywords).toLower();
   if (!term.isEmpty() && !hay.contains(term)) continue;
   auto *item = new QListWidgetItem(QString("[%1]  %2").arg(e.category, e.title) + (e.subtitle.isEmpty() ? "" : "  —  " + e.subtitle), list);
   item->setData(Qt::UserRole, i);
   if (++shown >= 80) break;
  }
  if (list->count() > 0) list->setCurrentRow(0);
 };
 populate({});

 auto executeSelected = [this, list, entries] {
  auto *it = list->currentItem();
  if (!it) return;
  const int idx = it->data(Qt::UserRole).toInt();
  if (idx >= 0 && idx < entries.size() && entries[idx].run) {
   auto fn = entries[idx].run;
   accept();
   fn();
  }
 };

 connect(input, &QLineEdit::textChanged, this, populate);
 connect(input, &QLineEdit::returnPressed, this, executeSelected);
 connect(list, &QListWidget::itemActivated, this, [executeSelected](QListWidgetItem *) { executeSelected(); });

 class KeyNavFilter : public QObject {
 public:
  QListWidget *list;
  KeyNavFilter(QListWidget *l, QObject *p) : QObject(p), list(l) {}
 protected:
  bool eventFilter(QObject *obj, QEvent *ev) override {
   if (ev->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(ev);
    if (ke->key() == Qt::Key_Down && list->count() > 0) {
     list->setCurrentRow(std::min(list->count() - 1, list->currentRow() + 1));
     return true;
    }
    if (ke->key() == Qt::Key_Up && list->count() > 0) {
     list->setCurrentRow(std::max(0, list->currentRow() - 1));
     return true;
    }
   }
   return QObject::eventFilter(obj, ev);
  }
 };
 input->installEventFilter(new KeyNavFilter(list, input));
 input->setFocus();
 ux::attachDialogFadeIn(this);
}
