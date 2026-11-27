#include "appearance.h"
#include <QMap>
#include <QPainter>
#include <QSvgRenderer>

QIcon brandBadgeIcon(bool dark) {
 // Pure Adobe Spectrum monochrome brand emblem (#131313 in Light / #f5f5f5 in Dark)
 const QString bg = dark ? "#f5f5f5" : "#131313";
 const QString fg = dark ? "#131313" : "#ffffff";
 const QByteArray svg = QString(R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
  <rect width="48" height="48" rx="12" fill="%1"/>
  <path transform="translate(6.4 6.4) scale(1.1)" d="m4 11 12-7 12 7-12 7-12-7Zm0 6 12 7 12-7M4 23l12 7 12-7" fill="none" stroke="%2" stroke-width="2.0" stroke-linecap="round" stroke-linejoin="round"/>
 </svg>)SVG").arg(bg, fg).toUtf8();
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
 const QString stroke = dark ? "#e0e0e0" : "#1f1f1f";
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
 // Adobe Spectrum 2 Monochrome Design System (Pure Black & White Minimalism)
 // Mirrors spectrum.adobe.com: calm neutral surfaces + high-contrast ink pills
 // Cobalt blue (#3b63fb) strictly reserved for input focus & update notices
 // ═══════════════════════════════════════════════════════════════════════════
 if (dark) {
  return QString::fromUtf8(R"(
   QComboBox{combobox-popup:0}
   QWidget{font-family:'SF Pro Text','Segoe UI Variable Text','Segoe UI','Inter',sans-serif;font-size:12px;color:#ebebeb;selection-background-color:#3a3a3a;selection-color:#ffffff}
   QMainWindow,QDialog{background:#161616}
   QMainWindow::separator{width:1px;height:1px;background:#2a2a2a}

   QWidget#mac-window-top,QWidget#mac-titlebar{background:#141414;border-bottom:1px solid #262626}
   QMenuBar#mac-menu{background:transparent;color:#a8a8a8;border:0;padding:0px;font-size:11.5px;font-weight:500}
   QMenuBar#mac-menu::item{background:transparent;padding:4px 9px;border-radius:6px}
   QMenuBar#mac-menu::item:selected{background:#282828;color:#ffffff}
   QLabel#mac-window-title{font-size:11.5px;font-weight:500;color:#8a8a8a;background:transparent}
   QWidget#mac-dialog-body{background:#161616}

   QWidget#v2-header{background:#1b1b1b;border-bottom:1px solid #2a2a2a}
   QWidget#document-strip{background:#161616;border-bottom:1px solid #282828}
   QTabBar#document-tabs{background:transparent;border:0;qproperty-drawBase:0}
   QTabBar#document-tabs::tab{background:transparent;color:#8e8e8e;padding:5px 14px;border:0;border-right:1px solid #262626;border-bottom:2px solid transparent;min-width:96px;max-width:220px;font-size:11.5px;font-weight:500}
   QTabBar#document-tabs::tab:selected{background:#222222;color:#ffffff;font-weight:600;border-bottom:2px solid #f5f5f5}
   QTabBar#document-tabs::tab:hover{background:#1f1f1f;color:#e0e0e0}
   QTabBar#document-tabs::close-button{image:url(:/assets/tab-close.svg);width:12px;height:12px;border-radius:4px}
   QToolButton#new-document{padding:1px 10px;font-size:15px;color:#8e8e8e;border:0;border-radius:6px;background:transparent}
   QToolButton#new-document:hover{background:#282828;color:#ffffff}

   QLabel#brand{font-size:13.5px;font-weight:700;letter-spacing:-.35px;color:#ffffff;background:transparent}
   QLabel#v2-badge{font-size:9.5px;font-weight:600;color:#a8a8a8;background:#242424;border:1px solid #343434;border-radius:6px;padding:1px 6px}

   QPushButton#header-cmd{background:transparent;border:1px solid transparent;border-radius:13px;min-height:18px;padding:4px 10px;font-size:11.5px;font-weight:500;color:#c0c0c0}
   QPushButton#header-cmd:hover{background:#262626;color:#ffffff}
   QPushButton#header-cmd:pressed{background:#2f2f2f}
   QToolButton#header-icon{background:transparent;border:0;border-radius:13px;padding:4px}
   QToolButton#header-icon:hover{background:#262626}

   QPushButton#header-pill{background:#242424;border:1px solid #383838;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#ebebeb}
   QPushButton#header-pill:hover{background:#2e2e2e;border-color:#505050;color:#ffffff}
   QPushButton#header-pill:pressed{background:#202020}

   QPushButton#header-pill-dark{background:transparent;border:1px solid #363636;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#c8c8c8}
   QPushButton#header-pill-dark:hover{background:#282828;border-color:#4e4e4e;color:#ffffff}
   QPushButton#header-pill-dark:checked{background:#2c2c2c;border-color:#5c5c5c;color:#ffffff;font-weight:600}

   QPushButton#header-primary{background:#f5f5f5;border:1px solid #f5f5f5;border-radius:13px;min-height:18px;padding:4px 14px;font-size:11.5px;font-weight:600;color:#131313}
   QPushButton#header-primary:hover{background:#ffffff;border-color:#ffffff}
   QPushButton#header-primary:pressed{background:#dcdcdc;border-color:#dcdcdc}
   QPushButton#header-primary::menu-indicator{width:0px;height:0px;image:none}

   QWidget#seg-pill{background:transparent;border:0}
   QWidget#seg-pill QToolButton,QWidget#seg-pill QPushButton{background:transparent;border:0;border-radius:11px;padding:3px 10px;font-size:11px;font-weight:600;color:#969696}
   QWidget#seg-pill QToolButton:hover,QWidget#seg-pill QPushButton:hover{color:#f0f0f0}
   QWidget#seg-pill QToolButton:checked,QWidget#seg-pill QPushButton:checked{background:transparent;color:#ffffff}

   QWidget#left-panel{background:#161616;border-right:1px solid #282828}
   QWidget#studio-panel{background:#161616;border-left:1px solid #282828}
   QFrame#v2-card{background:#1f1f1f;border:1px solid #2e2e2e;border-radius:12px}
   QFrame#metric-card{background:#1f1f1f;border:1px solid #2e2e2e;border-radius:11px}
   QLabel#metric-value{font-size:19px;font-weight:700;letter-spacing:-.4px;color:#ffffff;background:transparent}
   QLabel#metric-caption{font-size:10.5px;color:#8e8e8e;background:transparent}
   QLabel#eyebrow,QLabel#section{font-size:10px;font-weight:600;letter-spacing:.06em;color:#a8a8a8;background:transparent}
   QLabel#muted,QLabel#hint{font-size:11px;color:#8e8e8e;background:transparent}
   QLabel#pro-tag{font-size:9.5px;font-weight:600;color:#b8b8b8;background:#282828;border:1px solid #383838;border-radius:8px;padding:2px 7px}
   QLabel#ready-badge{font-size:9.5px;font-weight:600;color:#5fd9aa;background:#162922;border:1px solid #245240;border-radius:8px;padding:2px 7px}

   QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox,QPlainTextEdit{background:#131313;border:1px solid #343434;border-radius:8px;padding:5px 9px;color:#ebebeb}
   QLineEdit:hover,QDoubleSpinBox:hover,QSpinBox:hover,QComboBox:hover,QPlainTextEdit:hover{border-color:#505050}
   QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus{border:1.5px solid #4b75ff;background:#171717}
   QLineEdit#place-search{background:#131313;border:1px solid #343434;border-radius:13px;min-height:18px;padding:5px 12px}
   QLineEdit#place-search:focus{border:1.5px solid #4b75ff;background:#171717}

   QPushButton{background:#262626;border:1px solid #3a3a3a;border-radius:8px;padding:5px 11px;color:#ebebeb;font-weight:500}
   QPushButton:hover{background:#303030;border-color:#545454;color:#ffffff}
   QPushButton:pressed{background:#202020}
   QPushButton:disabled{background:#1f1f1f;color:#5e5e5e;border-color:#2d2d2d}

   QPushButton#primary,QPushButton[accent="true"]{background:#f5f5f5;color:#131313;border:1px solid #f5f5f5;border-radius:14px;min-height:18px;font-weight:600;padding:6px 14px}
   QPushButton#primary:hover,QPushButton[accent="true"]:hover{background:#ffffff;border-color:#ffffff}
   QPushButton#primary:pressed,QPushButton[accent="true"]:pressed{background:#dcdcdc;border-color:#dcdcdc}
   QPushButton#primary:disabled,QPushButton[accent="true"]:disabled{background:#2d2d2d;border-color:#2d2d2d;color:#686868}

   QPushButton#secondary-pill{background:transparent;color:#dedede;border:1px solid #3e3e3e;border-radius:13px;min-height:18px;padding:5px 12px;font-weight:500}
   QPushButton#secondary-pill:hover{background:#2a2a2a;border-color:#585858;color:#ffffff}
   QPushButton#secondary-pill:pressed{background:#222222}

   QPushButton#quick-chip{background:#242424;border:1px solid #363636;border-radius:7px;padding:4px 6px;font-size:10.5px;font-weight:500;color:#d0d0d0}
   QPushButton#quick-chip:hover{background:#303030;border-color:#5a5a5a;color:#ffffff}

   QToolButton#shape-card{background:#181818;border:1px solid #303030;border-radius:9px;padding:6px 4px;color:#a8a8a8;font-size:10.5px;font-weight:500}
   QToolButton#shape-card:hover{background:#252525;border-color:#4c4c4c;color:#ffffff}
   QToolButton#shape-card:checked{background:#2a2a2a;border:1.5px solid #e8e8e8;color:#ffffff;font-weight:600}

   QToolButton#studio-tile{background:#1f1f1f;border:1px solid #2e2e2e;border-radius:9px;padding:5px 2px;color:#a8a8a8;font-size:10px;font-weight:500}
   QToolButton#studio-tile:hover{background:#292929;border-color:#484848;color:#ffffff}
   QToolButton#studio-tile:checked{background:#2c2c2c;border:1.5px solid #e8e8e8;color:#ffffff;font-weight:600}

   QToolButton#cat-pill{background:#202020;border:1px solid #303030;border-radius:7px;padding:4px 2px;color:#969696;font-size:10px;font-weight:500}
   QToolButton#cat-pill:hover{background:#2a2a2a;color:#f0f0f0}
   QToolButton#cat-pill:checked{background:#f5f5f5;border-color:#f5f5f5;color:#131313;font-weight:600}

   QListWidget#diagram-list{background:transparent;border:0;outline:0}
   QListWidget#diagram-list::item{padding:5px 9px;border-radius:7px;color:#d4d4d4;font-size:11.5px}
   QListWidget#diagram-list::item:hover{background:#282828;color:#ffffff}
   QListWidget#diagram-list::item:selected{background:#f5f5f5;color:#131313;font-weight:600}

   QListWidget{background:#1a1a1a;border:1px solid #2e2e2e;border-radius:8px;padding:4px}
   QListWidget::item{padding:5px 8px;border-radius:6px}
   QListWidget::item:selected{background:#323232;color:#ffffff}

   QTreeWidget{background:transparent;border:0;outline:0;font-size:11.5px}
   QTreeWidget::item{height:27px;border-radius:6px}
   QTreeWidget::item:hover{background:#262626}
   QTreeWidget::item:selected{background:#303030;color:#ffffff}
   QTreeWidget::indicator{width:14px;height:14px;border-radius:4px}
   QTreeWidget::indicator:unchecked{background:#131313;border:1.5px solid #505050}
   QTreeWidget::indicator:checked{background:#f0f0f0;border:1.5px solid #f0f0f0;image:url(:/assets/check-dark.svg)}

   QComboBox::drop-down{border:0;width:24px;background:transparent}
   QComboBox::down-arrow{image:url(:/assets/chevron-light.svg);width:11px;height:11px}
   QComboBox QAbstractItemView{background:#222222;border:1px solid #3a3a3a;color:#ebebeb;selection-background-color:#343434;selection-color:#ffffff;padding:4px;border-radius:8px}

   QCheckBox{spacing:7px;color:#e0e0e0;background:transparent}
   QCheckBox::indicator{width:15px;height:15px;border-radius:4px}
   QCheckBox::indicator:unchecked{background:#131313;border:1.5px solid #525252}
   QCheckBox::indicator:unchecked:hover{border-color:#9e9e9e}
   QCheckBox::indicator:checked{background:#f0f0f0;border:1.5px solid #f0f0f0;image:url(:/assets/check-dark.svg)}
   QCheckBox::indicator:checked:hover{background:#ffffff;border-color:#ffffff}

   QSlider::groove:horizontal{height:3px;background:#363636;border-radius:1.5px}
   QSlider::sub-page:horizontal{background:#e6e6e6;border-radius:1.5px}
   QSlider::handle:horizontal{width:14px;height:14px;background:#1f1f1f;border:2px solid #e6e6e6;margin:-6px 0;border-radius:7px}
   QSlider::handle:horizontal:hover{background:#2c2c2c;border-color:#ffffff}

   QTabWidget::pane{border:1px solid #2e2e2e;border-radius:10px;background:#1d1d1d;top:-1px}
   QTabBar::tab{background:transparent;color:#969696;padding:7px 14px;border:0;border-bottom:2px solid transparent;margin-right:4px;font-weight:500}
   QTabBar::tab:hover{color:#ebebeb}
   QTabBar::tab:selected{color:#ffffff;font-weight:600;border-bottom:2px solid #f5f5f5}

   QMenu{background:#222222;border:1px solid #3a3a3a;border-radius:10px;padding:5px;color:#ebebeb}
   QMenu::item{padding:6px 22px;border-radius:6px}
   QMenu::item:selected{background:#323232;color:#ffffff}
   QMenu::separator{height:1px;background:#323232;margin:4px 8px}

   QScrollArea{background:transparent;border:0}
   QScrollArea>QWidget>QWidget{background:transparent}
   QScrollBar:vertical{background:transparent;width:6px;margin:3px 0}
   QScrollBar::handle:vertical{background:#3e3e3e;min-height:30px;border-radius:3px}
   QScrollBar::handle:vertical:hover{background:#626262}
   QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}

   QStatusBar{background:#141414;border-top:1px solid #262626;color:#8a8a8a;font-size:11px;padding-left:8px;padding-right:10px}
   QStatusBar::item{border:0}

   QToolButton#edge-toggle{background:#1f1f1f;border:1px solid #323232;border-radius:6px;color:#969696;font-weight:700;padding:0}
   QToolButton#edge-toggle:hover{background:#2c2c2c;border-color:#666666;color:#ffffff}
  )");
 }

 // Spectrum 2 Monochrome Light (Pure White #ffffff · Soft Gray #f7f7f7 · Ink Black #131313)
 return QString::fromUtf8(R"(
  QComboBox{combobox-popup:0}
  QWidget{font-family:'SF Pro Text','Segoe UI Variable Text','Segoe UI','Inter',sans-serif;font-size:12px;color:#1f1f1f;selection-background-color:#e6e6e6;selection-color:#131313}
  QMainWindow,QDialog{background:#f7f7f7}
  QMainWindow::separator{width:1px;height:1px;background:#e6e6e6}

  QWidget#mac-window-top,QWidget#mac-titlebar{background:#f4f4f4;border-bottom:1px solid #e6e6e6}
  QMenuBar#mac-menu{background:transparent;color:#4a4a4a;border:0;padding:0px;font-size:11.5px;font-weight:500}
  QMenuBar#mac-menu::item{background:transparent;padding:4px 9px;border-radius:6px}
  QMenuBar#mac-menu::item:selected{background:#e6e6e6;color:#131313}
  QLabel#mac-window-title{font-size:11.5px;font-weight:500;color:#6e6e6e;background:transparent}
  QWidget#mac-dialog-body{background:#f7f7f7}

  QWidget#v2-header{background:#ffffff;border-bottom:1px solid #e8e8e8}
  QWidget#document-strip{background:#f5f5f5;border-bottom:1px solid #e6e6e6}
  QTabBar#document-tabs{background:transparent;border:0;qproperty-drawBase:0}
  QTabBar#document-tabs::tab{background:transparent;color:#6e6e6e;padding:5px 14px;border:0;border-right:1px solid #e8e8e8;border-bottom:2px solid transparent;min-width:96px;max-width:220px;font-size:11.5px;font-weight:500}
  QTabBar#document-tabs::tab:selected{background:#ffffff;color:#131313;font-weight:600;border-bottom:2px solid #131313}
  QTabBar#document-tabs::tab:hover{background:#fafafa;color:#1f1f1f}
  QTabBar#document-tabs::close-button{image:url(:/assets/tab-close.svg);width:12px;height:12px;border-radius:4px}
  QToolButton#new-document{padding:1px 10px;font-size:15px;color:#6e6e6e;border:0;border-radius:6px;background:transparent}
  QToolButton#new-document:hover{background:#e8e8e8;color:#131313}

  QLabel#brand{font-size:13.5px;font-weight:700;letter-spacing:-.35px;color:#131313;background:transparent}
  QLabel#v2-badge{font-size:9.5px;font-weight:600;color:#555555;background:#f2f2f2;border:1px solid #e2e2e2;border-radius:6px;padding:1px 6px}

  QPushButton#header-cmd{background:transparent;border:1px solid transparent;border-radius:13px;min-height:18px;padding:4px 10px;font-size:11.5px;font-weight:500;color:#4a4a4a}
  QPushButton#header-cmd:hover{background:#f0f0f0;color:#131313}
  QPushButton#header-cmd:pressed{background:#e6e6e6}
  QToolButton#header-icon{background:transparent;border:0;border-radius:13px;padding:4px}
  QToolButton#header-icon:hover{background:#f0f0f0}

  QPushButton#header-pill{background:#ffffff;border:1px solid #dcdcdc;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#1f1f1f}
  QPushButton#header-pill:hover{background:#f3f3f3;border-color:#bdbdbd;color:#131313}
  QPushButton#header-pill:pressed{background:#eaeaea}

  QPushButton#header-pill-dark{background:#ffffff;border:1px solid #dcdcdc;border-radius:13px;min-height:18px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#333333}
  QPushButton#header-pill-dark:hover{background:#f3f3f3;border-color:#bdbdbd}
  QPushButton#header-pill-dark:checked{background:#efefef;border-color:#131313;color:#131313;font-weight:600}

  QPushButton#header-primary{background:#131313;border:1px solid #131313;border-radius:13px;min-height:18px;padding:4px 14px;font-size:11.5px;font-weight:600;color:#ffffff}
  QPushButton#header-primary:hover{background:#2d2d2d;border-color:#2d2d2d}
  QPushButton#header-primary:pressed{background:#000000;border-color:#000000}
  QPushButton#header-primary::menu-indicator{width:0px;height:0px;image:none}

  QWidget#seg-pill{background:transparent;border:0}
  QWidget#seg-pill QToolButton,QWidget#seg-pill QPushButton{background:transparent;border:0;border-radius:11px;padding:3px 10px;font-size:11px;font-weight:600;color:#666666}
  QWidget#seg-pill QToolButton:hover,QWidget#seg-pill QPushButton:hover{color:#131313}
  QWidget#seg-pill QToolButton:checked,QWidget#seg-pill QPushButton:checked{background:transparent;color:#131313}

  QWidget#left-panel{background:#f7f7f7;border-right:1px solid #e6e6e6}
  QWidget#studio-panel{background:#f7f7f7;border-left:1px solid #e6e6e6}
  QFrame#v2-card{background:#ffffff;border:1px solid #eaeaea;border-radius:12px}
  QFrame#metric-card{background:#ffffff;border:1px solid #eaeaea;border-radius:11px}
  QLabel#metric-value{font-size:19px;font-weight:700;letter-spacing:-.4px;color:#131313;background:transparent}
  QLabel#metric-caption{font-size:10.5px;color:#6e6e6e;background:transparent}
  QLabel#eyebrow,QLabel#section{font-size:10px;font-weight:600;letter-spacing:.06em;color:#525252;background:transparent}
  QLabel#muted,QLabel#hint{font-size:11px;color:#6e6e6e;background:transparent}
  QLabel#pro-tag{font-size:9.5px;font-weight:600;color:#4a4a4a;background:#f2f2f2;border:1px solid #e2e2e2;border-radius:8px;padding:2px 7px}
  QLabel#ready-badge{font-size:9.5px;font-weight:600;color:#007a4d;background:#edf8f2;border:1px solid #bfe6d2;border-radius:8px;padding:2px 7px}

  QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox,QPlainTextEdit{background:#ffffff;border:1px solid #dcdcdc;border-radius:8px;padding:5px 9px;color:#1f1f1f}
  QLineEdit:hover,QDoubleSpinBox:hover,QSpinBox:hover,QComboBox:hover,QPlainTextEdit:hover{border-color:#b8b8b8}
  QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus{border:1.5px solid #3b63fb;background:#ffffff}
  QLineEdit#place-search{background:#ffffff;border:1px solid #dcdcdc;border-radius:13px;min-height:18px;padding:5px 12px}
  QLineEdit#place-search:focus{border:1.5px solid #3b63fb;background:#ffffff}

  QPushButton{background:#ffffff;border:1px solid #dcdcdc;border-radius:8px;padding:5px 11px;color:#1f1f1f;font-weight:500}
  QPushButton:hover{background:#f3f3f3;border-color:#b8b8b8;color:#131313}
  QPushButton:pressed{background:#eaeaea}
  QPushButton:disabled{background:#f4f4f4;color:#a8a8a8;border-color:#e6e6e6}

  QPushButton#primary,QPushButton[accent="true"]{background:#131313;color:#ffffff;border:1px solid #131313;border-radius:14px;min-height:18px;font-weight:600;padding:6px 14px}
  QPushButton#primary:hover,QPushButton[accent="true"]:hover{background:#2d2d2d;border-color:#2d2d2d}
  QPushButton#primary:pressed,QPushButton[accent="true"]:pressed{background:#000000;border-color:#000000}
  QPushButton#primary:disabled,QPushButton[accent="true"]:disabled{background:#c8c8c8;border-color:#c8c8c8;color:#ffffff}

  QPushButton#secondary-pill{background:#ffffff;color:#1f1f1f;border:1px solid #d6d6d6;border-radius:13px;min-height:18px;padding:5px 12px;font-weight:500}
  QPushButton#secondary-pill:hover{background:#f2f2f2;border-color:#b4b4b4;color:#131313}
  QPushButton#secondary-pill:pressed{background:#e8e8e8}

  QPushButton#quick-chip{background:#f8f8f8;border:1px solid #e2e2e2;border-radius:7px;padding:4px 6px;font-size:10.5px;font-weight:500;color:#333333}
  QPushButton#quick-chip:hover{background:#efefef;border-color:#131313;color:#131313}

  QToolButton#shape-card{background:#fafafa;border:1px solid #e6e6e6;border-radius:9px;padding:6px 4px;color:#555555;font-size:10.5px;font-weight:500}
  QToolButton#shape-card:hover{background:#f1f1f1;border-color:#c8c8c8;color:#131313}
  QToolButton#shape-card:checked{background:#f2f2f2;border:1.5px solid #131313;color:#131313;font-weight:600}

  QToolButton#studio-tile{background:#ffffff;border:1px solid #e8e8e8;border-radius:9px;padding:5px 2px;color:#525252;font-size:10px;font-weight:500}
  QToolButton#studio-tile:hover{background:#f3f3f3;border-color:#cccccc;color:#131313}
  QToolButton#studio-tile:checked{background:#f2f2f2;border:1.5px solid #131313;color:#131313;font-weight:600}

  QToolButton#cat-pill{background:#f2f2f2;border:1px solid #e4e4e4;border-radius:7px;padding:4px 2px;color:#555555;font-size:10px;font-weight:500}
  QToolButton#cat-pill:hover{background:#e8e8e8;color:#131313}
  QToolButton#cat-pill:checked{background:#131313;border-color:#131313;color:#ffffff;font-weight:600}

  QListWidget#diagram-list{background:transparent;border:0;outline:0}
  QListWidget#diagram-list::item{padding:5px 9px;border-radius:7px;color:#333333;font-size:11.5px}
  QListWidget#diagram-list::item:hover{background:#f0f0f0}
  QListWidget#diagram-list::item:selected{background:#131313;color:#ffffff;font-weight:600}

  QListWidget{background:#ffffff;border:1px solid #e6e6e6;border-radius:8px;padding:4px}
  QListWidget::item{padding:5px 8px;border-radius:6px}
  QListWidget::item:selected{background:#efefef;color:#131313}

  QTreeWidget{background:transparent;border:0;outline:0;font-size:11.5px}
  QTreeWidget::item{height:27px;border-radius:6px}
  QTreeWidget::item:hover{background:#f0f0f0}
  QTreeWidget::item:selected{background:#ebebeb;color:#131313}
  QTreeWidget::indicator{width:14px;height:14px;border-radius:4px}
  QTreeWidget::indicator:unchecked{background:#ffffff;border:1.5px solid #bdbdbd}
  QTreeWidget::indicator:checked{background:#131313;border:1.5px solid #131313;image:url(:/assets/check-white.svg)}

  QComboBox::drop-down{border:0;width:24px;background:transparent}
  QComboBox::down-arrow{image:url(:/assets/chevron-dark.svg);width:11px;height:11px}
  QComboBox QAbstractItemView{background:#ffffff;border:1px solid #dcdcdc;color:#1f1f1f;selection-background-color:#efefef;selection-color:#131313;padding:4px;border-radius:8px}

  QCheckBox{spacing:7px;color:#262626;background:transparent}
  QCheckBox::indicator{width:15px;height:15px;border-radius:4px}
  QCheckBox::indicator:unchecked{background:#ffffff;border:1.5px solid #bdbdbd}
  QCheckBox::indicator:unchecked:hover{border-color:#131313}
  QCheckBox::indicator:checked{background:#131313;border:1.5px solid #131313;image:url(:/assets/check-white.svg)}
  QCheckBox::indicator:checked:hover{background:#2e2e2e;border-color:#2e2e2e}

  QSlider::groove:horizontal{height:3px;background:#e2e2e2;border-radius:1.5px}
  QSlider::sub-page:horizontal{background:#1f1f1f;border-radius:1.5px}
  QSlider::handle:horizontal{width:14px;height:14px;background:#ffffff;border:2px solid #1f1f1f;margin:-6px 0;border-radius:7px}
  QSlider::handle:horizontal:hover{background:#ffffff;border-color:#000000}

  QTabWidget::pane{border:1px solid #e6e6e6;border-radius:10px;background:#ffffff;top:-1px}
  QTabBar::tab{background:transparent;color:#6e6e6e;padding:7px 14px;border:0;border-bottom:2px solid transparent;margin-right:4px;font-weight:500}
  QTabBar::tab:hover{color:#1f1f1f}
  QTabBar::tab:selected{color:#131313;font-weight:600;border-bottom:2px solid #131313}

  QMenu{background:#ffffff;border:1px solid #dfdfdf;border-radius:10px;padding:5px;color:#1f1f1f}
  QMenu::item{padding:6px 22px;border-radius:6px}
  QMenu::item:selected{background:#f2f2f2;color:#131313}
  QMenu::separator{height:1px;background:#ececec;margin:4px 8px}

  QScrollArea{background:transparent;border:0}
  QScrollArea>QWidget>QWidget{background:transparent}
  QScrollBar:vertical{background:transparent;width:6px;margin:3px 0}
  QScrollBar::handle:vertical{background:#cccccc;min-height:30px;border-radius:3px}
  QScrollBar::handle:vertical:hover{background:#9a9a9a}
  QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}

  QStatusBar{background:#f4f4f4;border-top:1px solid #e6e6e6;color:#6e6e6e;font-size:11px;padding-left:8px;padding-right:10px}
  QStatusBar::item{border:0}

  QToolButton#edge-toggle{background:#ffffff;border:1px solid #e0e0e0;border-radius:6px;color:#666666;font-weight:700;padding:0}
  QToolButton#edge-toggle:hover{background:#f0f0f0;border-color:#131313;color:#131313}
 )");
}
