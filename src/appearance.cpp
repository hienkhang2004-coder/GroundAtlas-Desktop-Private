#include "appearance.h"
#include <QMap>
#include <QPainter>
#include <QSvgRenderer>

QIcon brandBadgeIcon() {
 // Crisp Adobe Spectrum 2 geometric brand mark (clean cobalt-slate architectural stack)
 const QByteArray svg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
  <defs>
   <linearGradient id="b" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#3b63fb"/><stop offset="1" stop-color="#1d44d8"/></linearGradient>
   <radialGradient id="l" cx=".2" cy=".1" r=".65"><stop stop-color="#93b4ff" stop-opacity=".42"/><stop offset="1" stop-color="#3b63fb" stop-opacity="0"/></radialGradient>
  </defs>
  <rect width="48" height="48" rx="12" fill="url(#b)"/>
  <rect width="48" height="48" rx="12" fill="url(#l)"/>
  <path transform="translate(6.4 6.4) scale(1.1)" d="m4 11 12-7 12 7-12 7-12-7Zm0 6 12 7 12-7M4 23l12 7 12-7" fill="none" stroke="#ffffff" stroke-width="1.95" stroke-linecap="round" stroke-linejoin="round"/>
 </svg>)SVG";
 QSvgRenderer renderer(svg);
 QPixmap pm(48, 48);
 pm.fill(Qt::transparent);
 QPainter p(&pm);
 renderer.render(&p);
 p.end();
 pm.setDevicePixelRatio(2);
 return QIcon(pm);
}

QIcon workspaceIcon(const QString &name, bool dark) {
 const QMap<QString, QString> paths = {
  {"new", "<path d='M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z'/><path d='M14 3v6h6M8 14h8m-4-4v8'/>"},
  {"open", "<path d='M3 7a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2l-2 9H5a2 2 0 0 1-2-2z'/><path d='M3 11h18'/>"},
  {"save", "<rect x='4' y='3' width='16' height='18' rx='3'/><path d='M8 3v6h8V3M8 21v-7h8v7'/>"},
  {"map", "<path d='m3 5 6-2 6 2 6-2v16l-6 2-6-2-6 2zM9 3v16M15 5v16'/>"},
  {"view-2d", "<rect x='4' y='4' width='16' height='16' rx='3'/><path d='M4 10h16M10 4v16'/>"},
  {"view-3d", "<path d='m12 3 9 5v8l-9 5-9-5V8zM3 8l9 5 9-5M12 13v8'/>"},
  {"diagram", "<rect x='3' y='3' width='18' height='18' rx='2'/><path d='M3 9h18M9 3v18M15 9v12'/>"},
  {"explode", "<path d='m12 3 10 5-10 5L2 8zM3 12l9 5 9-5M3 16l9 5 9-5'/>"},
  {"stats", "<path d='M12 3a9 9 0 1 0 9 9h-9z'/><path d='M15 3.5A9 9 0 0 1 20.5 9H15z'/>"},
  {"analysis", "<path d='M4 4v16h17M8 16v-5m5 5V7m5 9V3'/>"},
  {"laser", "<path d='M12 3v10m-3-3 3 3 3-3M4 17h16M6 21h12'/>"},
  {"section", "<path d='M3 16h18M5 16v-5h4v5m2 0V7h4v9m2 0v-3h2v3M3 20h18'/>"},
  {"report", "<path d='M6 3h9l4 4v14H6z'/><path d='M15 3v4h4M9 12h6M9 16h6M9 8h3'/>"},
  {"sun", "<circle cx='12' cy='12' r='4'/><path d='M12 2v2m0 16v2M4.93 4.93l1.41 1.41m11.32 11.32 1.41 1.41M2 12h2m16 0h2M4.93 19.07l1.41-1.41m11.32-11.32 1.41-1.41'/>"},
  {"moon", "<path d='M20 14.5A8.5 8.5 0 0 1 9.5 4 7 7 0 1 0 20 14.5z'/>"},
  {"studio", "<rect x='3' y='4' width='18' height='16' rx='2'/><path d='M14 4v16M14 10h7M14 15h7'/>"},
  {"grid", "<rect x='3' y='3' width='18' height='18' rx='3'/><path d='M9 3v18M15 3v18M3 9h18M3 15h18'/>"},
  {"reset", "<path d='M12 2v5m0 10v5M2 12h5m10 0h5'/><circle cx='12' cy='12' r='7'/>"},
  {"export-png", "<path d='M12 15V3m-4 4 4-4 4 4M5 13v6a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-6'/>"},
  {"search", "<circle cx='10.5' cy='10.5' r='6.5'/><path d='m16 16 5 5'/>"},
  {"circle", "<circle cx='12' cy='12' r='8'/><circle cx='12' cy='12' r='1.6'/>"},
  {"square", "<rect x='4' y='4' width='16' height='16' rx='2'/><circle cx='12' cy='12' r='1.4'/>"},
  {"polygon", "<path d='m12 3 8 6-3 11H7L4 9z'/>"},
  {"style", "<circle cx='12' cy='12' r='9'/><circle cx='9' cy='10' r='1.3'/><circle cx='14' cy='9' r='1.3'/><circle cx='15.5' cy='14' r='1.3'/>"},
  {"settings", "<circle cx='12' cy='12' r='3'/><path d='M12 2v3m0 14v3M4.9 4.9l2.1 2.1m10 10 2.1 2.1M2 12h3m14 0h3M4.9 19.1l2.1-2.1m10-10 2.1-2.1'/>"},
  {"source", "<circle cx='12' cy='12' r='9'/><path d='M12 11v6m0-10v.2'/>"},
 };
 const QString stroke = dark ? "#d8d8d8" : "#2c2c2c";
 const auto svg = QString("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'><g fill='none' stroke='%2' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'>%1</g></svg>")
                   .arg(paths.value(name, paths["source"]), stroke);
 QSvgRenderer renderer(svg.toUtf8());
 QPixmap pixmap(48, 48);
 pixmap.fill(Qt::transparent);
 QPainter painter(&pixmap);
 renderer.render(&painter);
 painter.end();
 pixmap.setDevicePixelRatio(2);
 return QIcon(pixmap);
}

QString workspaceStyle(bool dark) {
 Q_INIT_RESOURCE(ga_ux_assets);
 // ═══════════════════════════════════════════════════════════════════════════
 // Adobe Spectrum 2 Design System — Pure Achromatic Neutrals + Electric Cobalt
 // Calm canvas-first chrome so 3D models, sun shadows & urban maps pop cleanly
 // ═══════════════════════════════════════════════════════════════════════════
 if (dark) {
  // Spectrum 2 Dark: gray-25(#111111) · gray-50(#1b1b1b) · gray-75(#222222) · gray-100(#2c2c2c)
  // Accent: Spectrum Blue (#4b75ff / hover #6389ff) · Positive (#2d9d78) · Negative (#e34850)
  return QString::fromUtf8(R"(
   QComboBox{combobox-popup:0}
   QWidget{font-family:'SF Pro Text','Segoe UI Variable Text','Segoe UI','Inter',sans-serif;font-size:12px;color:#ebebeb;selection-background-color:#3b63fb;selection-color:#ffffff}
   QMainWindow,QDialog{background:#1b1b1b}
   QMainWindow::separator{width:1px;height:1px;background:#2f2f2f}

   QWidget#mac-window-top,QWidget#mac-titlebar{background:#171717;border-bottom:1px solid #2b2b2b}
   QMenuBar#mac-menu{background:#171717;color:#b4b4b4;border-bottom:1px solid #2b2b2b;padding:0px 10px;font-size:11px}
   QMenuBar#mac-menu::item{background:transparent;padding:3px 9px;border-radius:6px}
   QMenuBar#mac-menu::item:selected{background:#2c2c2c;color:#ffffff}
   QLabel#mac-window-title{font-size:11.5px;font-weight:500;color:#989898;background:transparent}
   QWidget#mac-dialog-body{background:#1b1b1b}

   QWidget#v2-header{background:#1f1f1f;border-bottom:1px solid #2f2f2f}
   QWidget#document-strip{background:#181818;border-bottom:1px solid #2c2c2c}
   QTabBar#document-tabs{background:transparent;border:0;qproperty-drawBase:0}
   QTabBar#document-tabs::tab{background:transparent;color:#969696;padding:5px 14px;border:0;border-right:1px solid #2b2b2b;border-bottom:2px solid transparent;min-width:100px;max-width:220px;font-size:11.5px;font-weight:500}
   QTabBar#document-tabs::tab:selected{background:#242424;color:#ffffff;font-weight:600;border-bottom:2px solid #4b75ff}
   QTabBar#document-tabs::tab:hover{background:#222222;color:#e0e0e0}
   QTabBar#document-tabs::close-button{image:url(:/assets/tab-close.svg);width:12px;height:12px;border-radius:4px}
   QToolButton#new-document{padding:1px 10px;font-size:15px;color:#969696;border:0;border-radius:6px;background:transparent}
   QToolButton#new-document:hover{background:#2c2c2c;color:#ffffff}

   QLabel#brand{font-size:13.5px;font-weight:700;letter-spacing:-.3px;color:#ffffff;background:transparent}
   QLabel#v2-badge{font-size:9.5px;font-weight:600;color:#a8bfff;background:#1f2842;border:1px solid #304478;border-radius:6px;padding:1px 6px}

   QPushButton#header-cmd{background:transparent;border:0;border-radius:7px;padding:5px 9px;font-size:11.5px;font-weight:500;color:#c8c8c8}
   QPushButton#header-cmd:hover{background:#2c2c2c;color:#ffffff}
   QPushButton#header-cmd:pressed{background:#343434}

   QPushButton#header-pill{background:#262626;border:1px solid #3c3c3c;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#ebebeb}
   QPushButton#header-pill:hover{background:#303030;border-color:#525252;color:#ffffff}
   QPushButton#header-pill:pressed{background:#222222}

   QPushButton#header-pill-dark{background:#262626;border:1px solid #3c3c3c;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:600;color:#d6d6d6}
   QPushButton#header-pill-dark:hover{background:#323232;border-color:#525252;color:#ffffff}
   QPushButton#header-pill-dark:checked{background:#223055;border-color:#4b75ff;color:#b8ccff}

   QPushButton#header-primary{background:#4b75ff;border:1px solid #4b75ff;border-radius:13px;min-height:18px;padding:4px 13px;font-size:11.5px;font-weight:600;color:#ffffff}
   QPushButton#header-primary:hover{background:#6389ff;border-color:#6389ff}
   QPushButton#header-primary:pressed{background:#3b63fb;border-color:#3b63fb}
   QPushButton#header-primary::menu-indicator{width:0px;height:0px;image:none}

   QWidget#seg-pill{background:transparent;border:0}
   QWidget#seg-pill QToolButton,QWidget#seg-pill QPushButton{background:transparent;border:0;border-radius:11px;padding:3px 10px;font-size:11px;font-weight:600;color:#9e9e9e}
   QWidget#seg-pill QToolButton:hover,QWidget#seg-pill QPushButton:hover{color:#f0f0f0}
   QWidget#seg-pill QToolButton:checked,QWidget#seg-pill QPushButton:checked{background:transparent;color:#ffffff}

   QWidget#left-panel{background:#1b1b1b;border-right:1px solid #2e2e2e}
   QWidget#studio-panel{background:#1b1b1b;border-left:1px solid #2e2e2e}
   QFrame#v2-card{background:#222222;border:1px solid #323232;border-radius:10px}
   QFrame#metric-card{background:#222222;border:1px solid #323232;border-radius:10px}
   QLabel#metric-value{font-size:19px;font-weight:700;letter-spacing:-.4px;color:#ffffff;background:transparent}
   QLabel#metric-caption{font-size:10.5px;color:#969696;background:transparent}
   QLabel#eyebrow,QLabel#section{font-size:10px;font-weight:600;letter-spacing:.06em;color:#b5b5b5;background:transparent}
   QLabel#muted,QLabel#hint{font-size:11px;color:#969696;background:transparent}
   QLabel#pro-tag{font-size:9.5px;font-weight:600;color:#a8bfff;background:#1f2842;border:1px solid #304478;border-radius:8px;padding:2px 7px}
   QLabel#ready-badge{font-size:9.5px;font-weight:600;color:#5fd9aa;background:#142b23;border:1px solid #235945;border-radius:8px;padding:2px 7px}

   QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox,QPlainTextEdit{background:#141414;border:1px solid #3a3a3a;border-radius:7px;padding:5px 9px;color:#ebebeb}
   QLineEdit:hover,QDoubleSpinBox:hover,QSpinBox:hover,QComboBox:hover,QPlainTextEdit:hover{border-color:#565656}
   QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus{border:1.5px solid #4b75ff;background:#181818}
   QLineEdit#place-search{background:#141414;border:1px solid #3a3a3a;border-radius:13px;min-height:18px;padding:5px 12px}
   QLineEdit#place-search:focus{border:1.5px solid #4b75ff;background:#181818}

   QPushButton{background:#292929;border:1px solid #3f3f3f;border-radius:7px;padding:5px 11px;color:#ebebeb;font-weight:500}
   QPushButton:hover{background:#333333;border-color:#595959;color:#ffffff}
   QPushButton:pressed{background:#222222}
   QPushButton:disabled{background:#222222;color:#626262;border-color:#303030}

   QPushButton#primary,QPushButton[accent="true"]{background:#4b75ff;color:#ffffff;border:1px solid #4b75ff;border-radius:14px;min-height:18px;font-weight:600;padding:6px 14px}
   QPushButton#primary:hover,QPushButton[accent="true"]:hover{background:#6389ff;border-color:#6389ff}
   QPushButton#primary:pressed,QPushButton[accent="true"]:pressed{background:#3b63fb;border-color:#3b63fb}
   QPushButton#primary:disabled,QPushButton[accent="true"]:disabled{background:#2c3244;border-color:#2c3244;color:#6b748f}

   QPushButton#secondary-pill{background:transparent;color:#e0e0e0;border:1px solid #444444;border-radius:13px;min-height:18px;padding:5px 12px;font-weight:500}
   QPushButton#secondary-pill:hover{background:#2e2e2e;border-color:#606060;color:#ffffff}
   QPushButton#secondary-pill:pressed{background:#242424}

   QPushButton#quick-chip{background:#262626;border:1px solid #3a3a3a;border-radius:7px;padding:4px 6px;font-size:10.5px;font-weight:500;color:#d6d6d6}
   QPushButton#quick-chip:hover{background:#223055;border-color:#4b75ff;color:#ffffff}

   QToolButton#shape-card{background:#1b1b1b;border:1px solid #363636;border-radius:8px;padding:6px 4px;color:#b4b4b4;font-size:10.5px;font-weight:500}
   QToolButton#shape-card:hover{background:#282828;border-color:#525252;color:#ffffff}
   QToolButton#shape-card:checked{background:#1e2948;border:1.5px solid #4b75ff;color:#ffffff;font-weight:600}

   QToolButton#studio-tile{background:#222222;border:1px solid #323232;border-radius:8px;padding:5px 2px;color:#b4b4b4;font-size:10px;font-weight:500}
   QToolButton#studio-tile:hover{background:#2c2c2c;border-color:#4b4b4b;color:#ffffff}
   QToolButton#studio-tile:checked{background:#1e2948;border:1.5px solid #4b75ff;color:#ffffff;font-weight:600}

   QToolButton#cat-pill{background:#222222;border:1px solid #323232;border-radius:7px;padding:4px 2px;color:#9e9e9e;font-size:10px;font-weight:500}
   QToolButton#cat-pill:hover{background:#2c2c2c;color:#f0f0f0}
   QToolButton#cat-pill:checked{background:#4b75ff;border-color:#4b75ff;color:#ffffff;font-weight:600}

   QListWidget#diagram-list{background:transparent;border:0;outline:0}
   QListWidget#diagram-list::item{padding:5px 9px;border-radius:7px;color:#d8d8d8;font-size:11.5px}
   QListWidget#diagram-list::item:hover{background:#2b2b2b;color:#ffffff}
   QListWidget#diagram-list::item:selected{background:#223055;color:#ffffff;font-weight:600}

   QListWidget{background:#1b1b1b;border:1px solid #323232;border-radius:8px;padding:4px}
   QListWidget::item{padding:5px 8px;border-radius:6px}
   QListWidget::item:selected{background:#223055;color:#ffffff}

   QTreeWidget{background:transparent;border:0;outline:0;font-size:11.5px}
   QTreeWidget::item{height:27px;border-radius:6px}
   QTreeWidget::item:hover{background:#292929}
   QTreeWidget::item:selected{background:#223055;color:#ffffff}
   QTreeWidget::indicator{width:14px;height:14px;border-radius:4px}
   QTreeWidget::indicator:unchecked{background:#141414;border:1.5px solid #525252}
   QTreeWidget::indicator:checked{background:#4b75ff;border:1.5px solid #4b75ff;image:url(:/assets/check-white.svg)}

   QComboBox::drop-down{border:0;width:24px;background:transparent}
   QComboBox::down-arrow{image:url(:/assets/chevron-light.svg);width:11px;height:11px}
   QComboBox QAbstractItemView{background:#242424;border:1px solid #3e3e3e;color:#ebebeb;selection-background-color:#223055;selection-color:#ffffff;padding:4px;border-radius:8px}

   QCheckBox{spacing:7px;color:#e0e0e0;background:transparent}
   QCheckBox::indicator{width:15px;height:15px;border-radius:4px}
   QCheckBox::indicator:unchecked{background:#141414;border:1.5px solid #555555}
   QCheckBox::indicator:unchecked:hover{border-color:#7c9eff}
   QCheckBox::indicator:checked{background:#4b75ff;border:1.5px solid #4b75ff;image:url(:/assets/check-white.svg)}
   QCheckBox::indicator:checked:hover{background:#6389ff;border-color:#6389ff}

   QSlider::groove:horizontal{height:3px;background:#3a3a3a;border-radius:1.5px}
   QSlider::sub-page:horizontal{background:#4b75ff;border-radius:1.5px}
   QSlider::handle:horizontal{width:14px;height:14px;background:#242424;border:2px solid #d6d6d6;margin:-6px 0;border-radius:7px}
   QSlider::handle:horizontal:hover{background:#2c2c2c;border-color:#ffffff}

   QTabWidget::pane{border:1px solid #323232;border-radius:10px;background:#1f1f1f;top:-1px}
   QTabBar::tab{background:transparent;color:#9e9e9e;padding:7px 14px;border:0;border-bottom:2px solid transparent;margin-right:4px;font-weight:500}
   QTabBar::tab:hover{color:#ebebeb}
   QTabBar::tab:selected{color:#ffffff;font-weight:600;border-bottom:2px solid #4b75ff}

   QMenu{background:#242424;border:1px solid #3e3e3e;border-radius:9px;padding:5px;color:#ebebeb}
   QMenu::item{padding:6px 22px;border-radius:6px}
   QMenu::item:selected{background:#333333;color:#ffffff}
   QMenu::separator{height:1px;background:#343434;margin:4px 8px}

   QScrollArea{background:transparent;border:0}
   QScrollArea>QWidget>QWidget{background:transparent}
   QScrollBar:vertical{background:transparent;width:6px;margin:3px 0}
   QScrollBar::handle:vertical{background:#424242;min-height:30px;border-radius:3px}
   QScrollBar::handle:vertical:hover{background:#666666}
   QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}

   QStatusBar{background:#181818;border-top:1px solid #2b2b2b;color:#969696;font-size:11px;padding-left:8px;padding-right:10px}
   QStatusBar::item{border:0}

   QToolButton#edge-toggle{background:#222222;border:1px solid #353535;border-radius:6px;color:#9e9e9e;font-weight:700;padding:0}
   QToolButton#edge-toggle:hover{background:#2e2e2e;border-color:#4b75ff;color:#ffffff}
  )");
 }

 // Spectrum 2 Light: gray-25(#ffffff) · gray-50(#f8f8f8) · gray-75(#f3f3f3) · gray-100(#e9e9e9)
 // Accent: Spectrum Blue (#3b63fb / hover #274dea) · Ink: #222222 · Muted: #6e6e6e
 return QString::fromUtf8(R"(
  QComboBox{combobox-popup:0}
  QWidget{font-family:'SF Pro Text','Segoe UI Variable Text','Segoe UI','Inter',sans-serif;font-size:12px;color:#222222;selection-background-color:#dce5ff;selection-color:#131313}
  QMainWindow,QDialog{background:#f8f8f8}
  QMainWindow::separator{width:1px;height:1px;background:#e1e1e1}

  QWidget#mac-window-top,QWidget#mac-titlebar{background:#f3f3f3;border-bottom:1px solid #e1e1e1}
  QMenuBar#mac-menu{background:#f3f3f3;color:#4b4b4b;border-bottom:1px solid #e1e1e1;padding:0px 10px;font-size:11px}
  QMenuBar#mac-menu::item{background:transparent;padding:3px 9px;border-radius:6px}
  QMenuBar#mac-menu::item:selected{background:#e5e5e5;color:#131313}
  QLabel#mac-window-title{font-size:11.5px;font-weight:500;color:#6e6e6e;background:transparent}
  QWidget#mac-dialog-body{background:#f8f8f8}

  QWidget#v2-header{background:#ffffff;border-bottom:1px solid #e1e1e1}
  QWidget#document-strip{background:#f3f3f3;border-bottom:1px solid #e1e1e1}
  QTabBar#document-tabs{background:transparent;border:0;qproperty-drawBase:0}
  QTabBar#document-tabs::tab{background:transparent;color:#6e6e6e;padding:5px 14px;border:0;border-right:1px solid #e5e5e5;border-bottom:2px solid transparent;min-width:100px;max-width:220px;font-size:11.5px;font-weight:500}
  QTabBar#document-tabs::tab:selected{background:#ffffff;color:#131313;font-weight:600;border-bottom:2px solid #3b63fb}
  QTabBar#document-tabs::tab:hover{background:#fafafa;color:#222222}
  QTabBar#document-tabs::close-button{image:url(:/assets/tab-close.svg);width:12px;height:12px;border-radius:4px}
  QToolButton#new-document{padding:1px 10px;font-size:15px;color:#6e6e6e;border:0;border-radius:6px;background:transparent}
  QToolButton#new-document:hover{background:#e6e6e6;color:#131313}

  QLabel#brand{font-size:13.5px;font-weight:700;letter-spacing:-.3px;color:#131313;background:transparent}
  QLabel#v2-badge{font-size:9.5px;font-weight:600;color:#3b63fb;background:#eef2ff;border:1px solid #cfdafe;border-radius:6px;padding:1px 6px}

  QPushButton#header-cmd{background:transparent;border:0;border-radius:7px;padding:5px 9px;font-size:11.5px;font-weight:500;color:#464646}
  QPushButton#header-cmd:hover{background:#eeeeee;color:#131313}
  QPushButton#header-cmd:pressed{background:#e4e4e4}

  QPushButton#header-pill{background:#ffffff;border:1px solid #d5d5d5;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#222222}
  QPushButton#header-pill:hover{background:#f3f3f3;border-color:#b6b6b6;color:#131313}
  QPushButton#header-pill:pressed{background:#e9e9e9}

  QPushButton#header-pill-dark{background:#ffffff;border:1px solid #d5d5d5;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:600;color:#333333}
  QPushButton#header-pill-dark:hover{background:#f3f3f3;border-color:#b6b6b6}
  QPushButton#header-pill-dark:checked{background:#eef2ff;border-color:#3b63fb;color:#274dea}

  QPushButton#header-primary{background:#3b63fb;border:1px solid #3b63fb;border-radius:13px;min-height:18px;padding:4px 13px;font-size:11.5px;font-weight:600;color:#ffffff}
  QPushButton#header-primary:hover{background:#274dea;border-color:#274dea}
  QPushButton#header-primary:pressed{background:#1d3ed4;border-color:#1d3ed4}
  QPushButton#header-primary::menu-indicator{width:0px;height:0px;image:none}

  QWidget#seg-pill{background:transparent;border:0}
  QWidget#seg-pill QToolButton,QWidget#seg-pill QPushButton{background:transparent;border:0;border-radius:11px;padding:3px 10px;font-size:11px;font-weight:600;color:#666666}
  QWidget#seg-pill QToolButton:hover,QWidget#seg-pill QPushButton:hover{color:#131313}
  QWidget#seg-pill QToolButton:checked,QWidget#seg-pill QPushButton:checked{background:transparent;color:#131313}

  QWidget#left-panel{background:#f8f8f8;border-right:1px solid #e1e1e1}
  QWidget#studio-panel{background:#f8f8f8;border-left:1px solid #e1e1e1}
  QFrame#v2-card{background:#ffffff;border:1px solid #e5e5e5;border-radius:10px}
  QFrame#metric-card{background:#ffffff;border:1px solid #e5e5e5;border-radius:10px}
  QLabel#metric-value{font-size:19px;font-weight:700;letter-spacing:-.4px;color:#131313;background:transparent}
  QLabel#metric-caption{font-size:10.5px;color:#6e6e6e;background:transparent}
  QLabel#eyebrow,QLabel#section{font-size:10px;font-weight:600;letter-spacing:.06em;color:#505050;background:transparent}
  QLabel#muted,QLabel#hint{font-size:11px;color:#6e6e6e;background:transparent}
  QLabel#pro-tag{font-size:9.5px;font-weight:600;color:#274dea;background:#eef2ff;border:1px solid #cfdafe;border-radius:8px;padding:2px 7px}
  QLabel#ready-badge{font-size:9.5px;font-weight:600;color:#007a4d;background:#e8f7f0;border:1px solid #b4e5cf;border-radius:8px;padding:2px 7px}

  QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox,QPlainTextEdit{background:#ffffff;border:1px solid #d5d5d5;border-radius:7px;padding:5px 9px;color:#222222}
  QLineEdit:hover,QDoubleSpinBox:hover,QSpinBox:hover,QComboBox:hover,QPlainTextEdit:hover{border-color:#b3b3b3}
  QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus{border:1.5px solid #3b63fb;background:#ffffff}
  QLineEdit#place-search{background:#ffffff;border:1px solid #d5d5d5;border-radius:13px;min-height:18px;padding:5px 12px}
  QLineEdit#place-search:focus{border:1.5px solid #3b63fb;background:#ffffff}

  QPushButton{background:#ffffff;border:1px solid #d5d5d5;border-radius:7px;padding:5px 11px;color:#222222;font-weight:500}
  QPushButton:hover{background:#f3f3f3;border-color:#b3b3b3;color:#131313}
  QPushButton:pressed{background:#e9e9e9}
  QPushButton:disabled{background:#f3f3f3;color:#a8a8a8;border-color:#e4e4e4}

  QPushButton#primary,QPushButton[accent="true"]{background:#3b63fb;color:#ffffff;border:1px solid #3b63fb;border-radius:14px;min-height:18px;font-weight:600;padding:6px 14px}
  QPushButton#primary:hover,QPushButton[accent="true"]:hover{background:#274dea;border-color:#274dea}
  QPushButton#primary:pressed,QPushButton[accent="true"]:pressed{background:#1d3ed4;border-color:#1d3ed4}
  QPushButton#primary:disabled,QPushButton[accent="true"]:disabled{background:#cfd6ec;border-color:#cfd6ec;color:#ffffff}

  QPushButton#secondary-pill{background:#ffffff;color:#222222;border:1px solid #d0d0d0;border-radius:13px;min-height:18px;padding:5px 12px;font-weight:500}
  QPushButton#secondary-pill:hover{background:#f2f2f2;border-color:#b0b0b0;color:#131313}
  QPushButton#secondary-pill:pressed{background:#e8e8e8}

  QPushButton#quick-chip{background:#f8f8f8;border:1px solid #e0e0e0;border-radius:7px;padding:4px 6px;font-size:10.5px;font-weight:500;color:#3b3b3b}
  QPushButton#quick-chip:hover{background:#eef2ff;border-color:#3b63fb;color:#274dea}

  QToolButton#shape-card{background:#f8f8f8;border:1px solid #e2e2e2;border-radius:8px;padding:6px 4px;color:#555555;font-size:10.5px;font-weight:500}
  QToolButton#shape-card:hover{background:#f0f0f0;border-color:#c6c6c6;color:#131313}
  QToolButton#shape-card:checked{background:#eef2ff;border:1.5px solid #3b63fb;color:#274dea;font-weight:600}

  QToolButton#studio-tile{background:#ffffff;border:1px solid #e5e5e5;border-radius:8px;padding:5px 2px;color:#505050;font-size:10px;font-weight:500}
  QToolButton#studio-tile:hover{background:#f3f3f3;border-color:#cbcbcb;color:#131313}
  QToolButton#studio-tile:checked{background:#eef2ff;border:1.5px solid #3b63fb;color:#274dea;font-weight:600}

  QToolButton#cat-pill{background:#f1f1f1;border:1px solid #e2e2e2;border-radius:7px;padding:4px 2px;color:#555555;font-size:10px;font-weight:500}
  QToolButton#cat-pill:hover{background:#e8e8e8;color:#131313}
  QToolButton#cat-pill:checked{background:#3b63fb;border-color:#3b63fb;color:#ffffff;font-weight:600}

  QListWidget#diagram-list{background:transparent;border:0;outline:0}
  QListWidget#diagram-list::item{padding:5px 9px;border-radius:7px;color:#333333;font-size:11.5px}
  QListWidget#diagram-list::item:hover{background:#eeeeee}
  QListWidget#diagram-list::item:selected{background:#eef2ff;color:#274dea;font-weight:600}

  QListWidget{background:#ffffff;border:1px solid #e5e5e5;border-radius:8px;padding:4px}
  QListWidget::item{padding:5px 8px;border-radius:6px}
  QListWidget::item:selected{background:#eef2ff;color:#274dea}

  QTreeWidget{background:transparent;border:0;outline:0;font-size:11.5px}
  QTreeWidget::item{height:27px;border-radius:6px}
  QTreeWidget::item:hover{background:#efefef}
  QTreeWidget::item:selected{background:#eef2ff;color:#274dea}
  QTreeWidget::indicator{width:14px;height:14px;border-radius:4px}
  QTreeWidget::indicator:unchecked{background:#ffffff;border:1.5px solid #bcbcbc}
  QTreeWidget::indicator:checked{background:#3b63fb;border:1.5px solid #3b63fb;image:url(:/assets/check-white.svg)}

  QComboBox::drop-down{border:0;width:24px;background:transparent}
  QComboBox::down-arrow{image:url(:/assets/chevron-dark.svg);width:11px;height:11px}
  QComboBox QAbstractItemView{background:#ffffff;border:1px solid #d5d5d5;color:#222222;selection-background-color:#eef2ff;selection-color:#274dea;padding:4px;border-radius:8px}

  QCheckBox{spacing:7px;color:#292929;background:transparent}
  QCheckBox::indicator{width:15px;height:15px;border-radius:4px}
  QCheckBox::indicator:unchecked{background:#ffffff;border:1.5px solid #bcbcbc}
  QCheckBox::indicator:unchecked:hover{border-color:#3b63fb}
  QCheckBox::indicator:checked{background:#3b63fb;border:1.5px solid #3b63fb;image:url(:/assets/check-white.svg)}
  QCheckBox::indicator:checked:hover{background:#274dea;border-color:#274dea}

  QSlider::groove:horizontal{height:3px;background:#e0e0e0;border-radius:1.5px}
  QSlider::sub-page:horizontal{background:#3b63fb;border-radius:1.5px}
  QSlider::handle:horizontal{width:14px;height:14px;background:#ffffff;border:2px solid #464646;margin:-6px 0;border-radius:7px}
  QSlider::handle:horizontal:hover{background:#ffffff;border-color:#3b63fb}

  QTabWidget::pane{border:1px solid #e1e1e1;border-radius:10px;background:#ffffff;top:-1px}
  QTabBar::tab{background:transparent;color:#6e6e6e;padding:7px 14px;border:0;border-bottom:2px solid transparent;margin-right:4px;font-weight:500}
  QTabBar::tab:hover{color:#222222}
  QTabBar::tab:selected{color:#131313;font-weight:600;border-bottom:2px solid #3b63fb}

  QMenu{background:#ffffff;border:1px solid #dadada;border-radius:9px;padding:5px;color:#222222}
  QMenu::item{padding:6px 22px;border-radius:6px}
  QMenu::item:selected{background:#f0f0f0;color:#131313}
  QMenu::separator{height:1px;background:#eaeaea;margin:4px 8px}

  QScrollArea{background:transparent;border:0}
  QScrollArea>QWidget>QWidget{background:transparent}
  QScrollBar:vertical{background:transparent;width:6px;margin:3px 0}
  QScrollBar::handle:vertical{background:#cccccc;min-height:30px;border-radius:3px}
  QScrollBar::handle:vertical:hover{background:#9e9e9e}
  QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}

  QStatusBar{background:#f3f3f3;border-top:1px solid #e1e1e1;color:#6e6e6e;font-size:11px;padding-left:8px;padding-right:10px}
  QStatusBar::item{border:0}

  QToolButton#edge-toggle{background:#ffffff;border:1px solid #dcdcdc;border-radius:6px;color:#666666;font-weight:700;padding:0}
  QToolButton#edge-toggle:hover{background:#eef2ff;border-color:#3b63fb;color:#274dea}
 )");
}
