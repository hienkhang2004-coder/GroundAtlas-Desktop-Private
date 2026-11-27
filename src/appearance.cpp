#include "appearance.h"
#include <QMap>
#include <QPainter>
#include <QSvgRenderer>

QIcon brandBadgeIcon() {
 const QByteArray svg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
  <defs>
   <linearGradient id="b" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#a19c93"/><stop offset="1" stop-color="#9b958b"/></linearGradient>
   <radialGradient id="l" cx=".15" cy=".1" r=".57"><stop stop-color="#e9c8b5"/><stop offset="1" stop-color="#e9c8b5" stop-opacity="0"/></radialGradient>
  </defs>
  <rect width="48" height="48" rx="14" fill="url(#b)"/>
  <rect width="48" height="48" rx="14" fill="url(#l)"/>
  <path transform="translate(6.4 6.4) scale(1.1)" d="m4 11 12-7 12 7-12 7-12-7Zm0 6 12 7 12-7M4 23l12 7 12-7" fill="none" stroke="#fff" stroke-width="1.9" stroke-linejoin="round"/>
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
 const QString stroke = dark ? "#d7d3cc" : "#3a3834";
 const auto svg = QString("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'><g fill='none' stroke='%2' stroke-width='1.55' stroke-linecap='round' stroke-linejoin='round'>%1</g></svg>")
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
 if (dark) {
  return QString::fromUtf8(R"(
   QComboBox{combobox-popup:0}
   QWidget{font-family:'Geist','Segoe UI Variable Text','Segoe UI',sans-serif;font-size:12px;color:#f4f2ee;selection-background-color:#3a3732;selection-color:#ffffff}
   QMainWindow,QDialog{background:#141312}
   QMainWindow::separator{width:1px;height:1px;background:#2b2926}
   QWidget#mac-window-top,QWidget#mac-titlebar{background:#181715;border-bottom:1px solid #282623}
   QMenuBar#mac-menu{background:#181715;color:#b8b3aa;border-bottom:1px solid #282623;padding:0px 10px;font-size:11px}
   QMenuBar#mac-menu::item{background:transparent;padding:3px 9px;border-radius:5px}
   QMenuBar#mac-menu::item:selected{background:#2a2824;color:#ffffff}
   QLabel#mac-window-title{font-size:11.5px;font-weight:500;color:#a09b93;background:transparent}
   QWidget#mac-dialog-body{background:#141312}
   QWidget#v2-header{background:#1c1b19;border-bottom:1px solid #2d2b27}
   QWidget#document-strip{background:#171614;border-bottom:1px solid #292724}
   QTabBar#document-tabs{background:transparent;border:0;qproperty-drawBase:0}
   QTabBar#document-tabs::tab{background:transparent;color:#9a958d;padding:6px 14px;border:0;border-right:1px solid #292724;border-bottom:2px solid transparent;min-width:104px;max-width:225px;font-size:11.5px}
   QTabBar#document-tabs::tab:selected{background:#23211e;color:#f6f4f0;font-weight:600;border-bottom:2px solid #e09173}
   QTabBar#document-tabs::tab:hover{background:#1f1d1b;color:#e5e1d8}
   QTabBar#document-tabs::close-button{image:url(:/assets/tab-close.svg);width:12px;height:12px;border-radius:4px}
   QToolButton#new-document{padding:2px 10px;font-size:16px;color:#9e9991;border:0;border-radius:6px;background:transparent}
   QToolButton#new-document:hover{background:#2a2824;color:#ffffff}
   QLabel#brand{font-size:14.5px;font-weight:600;letter-spacing:-.4px;color:#f6f4f0;background:transparent}
   QLabel#v2-badge{font-size:9.5px;font-weight:700;color:#e09173;background:#29211d;border:1px solid #4a352b;border-radius:6px;padding:2px 6px}
   QPushButton#header-cmd{background:transparent;border:0;border-radius:7px;padding:5px 9px;font-size:11px;font-weight:600;letter-spacing:.035em;color:#c7c1b8}
   QPushButton#header-cmd:hover{background:#2a2824;color:#ffffff}
   QPushButton#header-pill{background:#24221f;border:1px solid #3a3732;border-radius:14px;padding:5px 13px;font-size:11.5px;font-weight:500;color:#f4f2ee}
   QPushButton#header-pill:hover{background:#2f2d29;border-color:#57524b;color:#ffffff}
   QPushButton#header-pill-dark{background:#f4f2ee;border:1px solid #f4f2ee;border-radius:14px;padding:5px 13px;font-size:11.5px;font-weight:600;color:#151413}
   QPushButton#header-pill-dark:hover{background:#ffffff}
   QPushButton#header-pill-dark:checked{background:#e09173;border-color:#e09173;color:#151413}
   QWidget#seg-pill{background:transparent;border:0}
   QWidget#seg-pill QToolButton,QWidget#seg-pill QPushButton{background:transparent;border:0;border-radius:11px;padding:4px 10px;font-size:11px;font-weight:600;color:#9e9991}
   QWidget#seg-pill QToolButton:hover,QWidget#seg-pill QPushButton:hover{color:#f4f2ee}
   QWidget#seg-pill QToolButton:checked,QWidget#seg-pill QPushButton:checked{background:transparent;color:#151413}
   QWidget#left-panel{background:#141312;border-right:1px solid #2b2926}
   QWidget#studio-panel{background:#141312;border-left:1px solid #2b2926}
   QFrame#v2-card{background:#1c1b19;border:1px solid #2e2c28;border-radius:13px}
   QFrame#metric-card{background:#1c1b19;border:1px solid #2e2c28;border-radius:11px}
   QLabel#metric-value{font-size:20px;font-weight:600;letter-spacing:-.5px;color:#f6f4f0;background:transparent}
   QLabel#metric-caption{font-size:10.5px;color:#9e9991;background:transparent}
   QLabel#eyebrow,QLabel#section{font-size:10px;font-weight:700;letter-spacing:.07em;color:#e8e3da;background:transparent}
   QLabel#muted,QLabel#hint{font-size:11px;color:#9e9991;background:transparent}
   QLabel#pro-tag{font-size:9.5px;font-weight:700;color:#e09173;background:#2a221e;border:1px solid #4e382d;border-radius:9px;padding:2px 7px}
   QLabel#ready-badge{font-size:9.5px;font-weight:700;color:#4ade80;background:#16291e;border:1px solid #235337;border-radius:9px;padding:2px 7px}
   QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox,QPlainTextEdit{background:#161513;border:1px solid #35322e;border-radius:9px;padding:6px 9px;color:#f4f2ee}
   QLineEdit:hover,QDoubleSpinBox:hover,QComboBox:hover{border-color:#4b4741}
   QLineEdit:focus,QDoubleSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus{border-color:#e09173;background:#1a1917}
   QLineEdit#place-search{background:#1c1b19;border:1px solid #35322e;border-radius:11px;padding:6px 10px}
   QPushButton{background:#23211e;border:1px solid #393631;border-radius:9px;padding:6px 10px;color:#f4f2ee;font-weight:500}
   QPushButton:hover{background:#2e2b27;border-color:#57524b;color:#ffffff}
   QPushButton:pressed{background:#1e1c19}
   QPushButton:disabled{opacity:.45;color:#6c6862}
   QPushButton#primary,QPushButton[accent="true"]{background:#f4f2ee;color:#151413;border:1px solid #f4f2ee;border-radius:16px;font-weight:600;padding:7px 13px}
   QPushButton#primary:hover,QPushButton[accent="true"]:hover{background:#ffffff;border-color:#ffffff}
   QPushButton#primary:pressed,QPushButton[accent="true"]:pressed{background:#e2dfd8}
   QPushButton#secondary-pill{background:#1d1c1a;color:#f4f2ee;border:1px solid #3a3732;border-radius:15px;padding:6px 11px}
   QPushButton#secondary-pill:hover{background:#2b2824;border-color:#5a554e}
   QPushButton#quick-chip{background:#171614;border:1px solid #322f2b;border-radius:8px;padding:5px 4px;font-size:10.5px;color:#d6d1c9}
   QPushButton#quick-chip:hover{background:#282521;border-color:#e09173;color:#ffffff}
   QToolButton#shape-card{background:#171614;border:1px solid #34312d;border-radius:11px;padding:8px 4px;color:#b8b3aa;font-size:10.5px;font-weight:500}
   QToolButton#shape-card:hover{background:#22201d;border-color:#524d46;color:#f4f2ee}
   QToolButton#shape-card:checked{background:#29211d;border:1.5px solid #e09173;color:#ffffff;font-weight:600}
   QToolButton#studio-tile{background:#1c1b19;border:1px solid #2e2c28;border-radius:10px;padding:6px 2px;color:#b8b3aa;font-size:10px;font-weight:500}
   QToolButton#studio-tile:hover{background:#262420;border-color:#48443e;color:#f4f2ee}
   QToolButton#studio-tile:checked{background:#2a221e;border:1.5px solid #e09173;color:#ffffff;font-weight:600}
   QToolButton#cat-pill{background:#1c1b19;border:1px solid #2e2c28;border-radius:9px;padding:4px 2px;color:#9e9991;font-size:10px;font-weight:500}
   QToolButton#cat-pill:hover{background:#262420;color:#f4f2ee}
   QToolButton#cat-pill:checked{background:#f4f2ee;border-color:#f4f2ee;color:#151413;font-weight:600}
   QListWidget#diagram-list{background:transparent;border:0;outline:0}
   QListWidget#diagram-list::item{padding:6px 9px;border-radius:8px;color:#d7d3cc;font-size:11.5px}
   QListWidget#diagram-list::item:hover{background:#24221f;color:#ffffff}
   QListWidget#diagram-list::item:selected{background:#f4f2ee;color:#151413;font-weight:600}
   QListWidget{background:#1c1b19;border:1px solid #2e2c28;border-radius:9px;padding:4px}
   QListWidget::item{padding:6px 8px;border-radius:6px}
   QListWidget::item:selected{background:#34312d;color:#ffffff}
   QTreeWidget{background:transparent;border:0;outline:0;font-size:11.5px}
   QTreeWidget::item{height:28px;border-radius:6px}
   QTreeWidget::item:hover{background:#24221f}
   QTreeWidget::item:selected{background:#34312d;color:#ffffff}
   QTreeWidget::indicator{width:13px;height:13px;border-radius:4px}
   QTreeWidget::indicator:unchecked{background:#161513;border:1px solid #4e4a44}
   QTreeWidget::indicator:checked{background:#e09173;border:1px solid #e09173;image:url(:/assets/check-dark.svg)}
   QComboBox::drop-down{border:0;width:24px;background:transparent}
   QComboBox::down-arrow{image:url(:/assets/chevron-light.svg);width:11px;height:11px}
   QComboBox QAbstractItemView{background:#1c1b19;border:1px solid #3a3732;color:#f4f2ee;selection-background-color:#34312d;padding:4px;border-radius:8px}
   QCheckBox{spacing:7px;color:#e5e1d8;background:transparent}
   QCheckBox::indicator{width:15px;height:15px;border-radius:4px}
   QCheckBox::indicator:unchecked{background:#161513;border:1.5px solid #4e4a44}
   QCheckBox::indicator:unchecked:hover{border-color:#e09173}
   QCheckBox::indicator:checked{background:#e09173;border:1.5px solid #e09173;image:url(:/assets/check-dark.svg)}
   QCheckBox::indicator:checked:hover{background:#eba388;border-color:#eba388}
   QSlider::groove:horizontal{height:4px;background:#2f2d29;border-radius:2px}
   QSlider::sub-page:horizontal{background:#e09173;border-radius:2px}
   QSlider::handle:horizontal{width:14px;height:14px;background:#f4f2ee;border:1.5px solid #e09173;margin:-5px 0;border-radius:7px}
   QSlider::handle:horizontal:hover{background:#ffffff;border-color:#f3a88a}
   QTabWidget::pane{border:1px solid #2e2c28;border-radius:11px;background:#171614;top:-1px}
   QTabBar::tab{background:#1c1b19;color:#9e9991;padding:7px 14px;border:1px solid #2e2c28;border-bottom:0;border-top-left-radius:8px;border-top-right-radius:8px;margin-right:4px;font-weight:500}
   QTabBar::tab:selected{background:#262420;color:#ffffff;font-weight:600;border-bottom:2px solid #e09173}
   QMenu{background:#1d1c1a;border:1px solid #3a3732;border-radius:10px;padding:5px;color:#f4f2ee}
   QMenu::item{padding:7px 24px;border-radius:6px}
   QMenu::item:selected{background:#2f2d29;color:#ffffff}
   QMenu::separator{height:1px;background:#2f2d29;margin:4px 10px}
   QScrollArea{background:transparent;border:0}
   QScrollArea>QWidget>QWidget{background:transparent}
   QScrollBar:vertical{background:transparent;width:6px;margin:3px 0}
   QScrollBar::handle:vertical{background:#3a3732;min-height:30px;border-radius:3px}
   QScrollBar::handle:vertical:hover{background:#666057}
   QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}
   QStatusBar{background:#171614;border-top:1px solid #292724;color:#9e9991;font-size:11px;padding-left:6px;padding-right:10px}
   QStatusBar::item{border:0}
   QToolButton#edge-toggle{background:#1d1c1a;border:1px solid #34312d;border-radius:8px;color:#b8b3aa;font-weight:700;padding:0}
   QToolButton#edge-toggle:hover{background:#2c2420;border-color:#e09173;color:#ffffff}
  )");
 }

 // Warm Architectural Editorial Light Theme — Elevated GroundAtlas 1.2 Native
 return QString::fromUtf8(R"(
  QComboBox{combobox-popup:0}
  QWidget{font-family:'Geist','Segoe UI Variable Text','Segoe UI',sans-serif;font-size:12px;color:#1a1918;selection-background-color:#e8e4dc;selection-color:#1a1918}
  QMainWindow,QDialog{background:#f8f7f4}
  QMainWindow::separator{width:1px;height:1px;background:#e8e5de}
  QWidget#mac-window-top,QWidget#mac-titlebar{background:#f3f1ec;border-bottom:1px solid #e6e3dc}
  QMenuBar#mac-menu{background:#f3f1ec;color:#52504c;border-bottom:1px solid #e6e3dc;padding:0px 10px;font-size:11px}
  QMenuBar#mac-menu::item{background:transparent;padding:3px 9px;border-radius:5px}
  QMenuBar#mac-menu::item:selected{background:#e5e1d8;color:#1a1918}
  QLabel#mac-window-title{font-size:11.5px;font-weight:500;color:#6e6b66;background:transparent}
  QWidget#mac-dialog-body{background:#f8f7f4}
  QWidget#v2-header{background:#ffffff;border-bottom:1px solid #e8e5de}
  QWidget#document-strip{background:#f3f1ec;border-bottom:1px solid #e4e0d8}
  QTabBar#document-tabs{background:transparent;border:0;qproperty-drawBase:0}
  QTabBar#document-tabs::tab{background:transparent;color:#75736f;padding:6px 14px;border:0;border-right:1px solid #e4e0d8;border-bottom:2px solid transparent;min-width:104px;max-width:225px;font-size:11.5px}
  QTabBar#document-tabs::tab:selected{background:#ffffff;color:#1a1918;font-weight:600;border-bottom:2px solid #c45632}
  QTabBar#document-tabs::tab:hover{background:#faf9f7;color:#1a1918}
  QTabBar#document-tabs::close-button{image:url(:/assets/tab-close.svg);width:12px;height:12px;border-radius:4px}
  QToolButton#new-document{padding:2px 10px;font-size:16px;color:#75736f;border:0;border-radius:6px;background:transparent}
  QToolButton#new-document:hover{background:#e6e2da;color:#1a1918}
  QLabel#brand{font-size:14.5px;font-weight:600;letter-spacing:-.4px;color:#1a1918;background:transparent}
  QLabel#v2-badge{font-size:9.5px;font-weight:700;color:#c45632;background:#fdf4f0;border:1px solid #f0d5c8;border-radius:6px;padding:2px 6px}
  QPushButton#header-cmd{background:transparent;border:0;border-radius:7px;padding:5px 9px;font-size:11px;font-weight:600;letter-spacing:.035em;color:#4f4c48}
  QPushButton#header-cmd:hover{background:#f2efe9;color:#1a1918}
  QPushButton#header-pill{background:#ffffff;border:1px solid #dcd8d0;border-radius:14px;padding:5px 13px;font-size:11.5px;font-weight:500;color:#1a1918}
  QPushButton#header-pill:hover{background:#f4f1eb;border-color:#c4bfb5}
  QPushButton#header-pill-dark{background:#1a1918;border:1px solid #1a1918;border-radius:14px;padding:5px 13px;font-size:11.5px;font-weight:600;color:#ffffff}
  QPushButton#header-pill-dark:hover{background:#32302d}
  QPushButton#header-pill-dark:checked{background:#c45632;border-color:#c45632;color:#ffffff}
  QWidget#seg-pill{background:transparent;border:0}
  QWidget#seg-pill QToolButton,QWidget#seg-pill QPushButton{background:transparent;border:0;border-radius:11px;padding:4px 10px;font-size:11px;font-weight:600;color:#6d6a64}
  QWidget#seg-pill QToolButton:hover,QWidget#seg-pill QPushButton:hover{color:#1a1918}
  QWidget#seg-pill QToolButton:checked,QWidget#seg-pill QPushButton:checked{background:transparent;color:#ffffff}
  QWidget#left-panel{background:#f8f7f4;border-right:1px solid #e8e5de}
  QWidget#studio-panel{background:#f8f7f4;border-left:1px solid #e8e5de}
  QFrame#v2-card{background:#ffffff;border:1px solid #e8e4dc;border-radius:13px}
  QFrame#metric-card{background:#ffffff;border:1px solid #e8e4dc;border-radius:11px}
  QLabel#metric-value{font-size:20px;font-weight:600;letter-spacing:-.5px;color:#1a1918;background:transparent}
  QLabel#metric-caption{font-size:10.5px;color:#75736f;background:transparent}
  QLabel#eyebrow,QLabel#section{font-size:10px;font-weight:700;letter-spacing:.07em;color:#2c2a27;background:transparent}
  QLabel#muted,QLabel#hint{font-size:11px;color:#75736f;background:transparent}
  QLabel#pro-tag{font-size:9.5px;font-weight:700;color:#9c4d32;background:#fbf3ef;border:1px solid #eed9cf;border-radius:9px;padding:2px 7px}
  QLabel#ready-badge{font-size:9.5px;font-weight:700;color:#15803d;background:#edf9f0;border:1px solid #bbf0c9;border-radius:9px;padding:2px 7px}
  QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox,QPlainTextEdit{background:#faf9f6;border:1px solid #dcd8d0;border-radius:9px;padding:6px 9px;color:#1a1918}
  QLineEdit:hover,QDoubleSpinBox:hover,QComboBox:hover{border-color:#c2bdb3}
  QLineEdit:focus,QDoubleSpinBox:focus,QComboBox:focus,QPlainTextEdit:focus{border-color:#c45632;background:#ffffff}
  QLineEdit#place-search{background:#ffffff;border:1px solid #e0dcd4;border-radius:11px;padding:6px 10px}
  QPushButton{background:#ffffff;border:1px solid #dcd8d0;border-radius:9px;padding:6px 10px;color:#1a1918;font-weight:500}
  QPushButton:hover{background:#f4f1eb;border-color:#c2bdb3}
  QPushButton:pressed{background:#eae6de}
  QPushButton:disabled{background:#f4f2ee;color:#a8a49d;border-color:#e6e3dd}
  QPushButton#primary,QPushButton[accent="true"]{background:#1a1918;color:#ffffff;border:1px solid #1a1918;border-radius:16px;font-weight:600;padding:7px 13px}
  QPushButton#primary:hover,QPushButton[accent="true"]:hover{background:#33302d;border-color:#33302d}
  QPushButton#primary:pressed,QPushButton[accent="true"]:pressed{background:#0d0c0b}
  QPushButton#primary:disabled,QPushButton[accent="true"]:disabled{background:#9c9892;border-color:#9c9892;color:#faf9f8}
  QPushButton#secondary-pill{background:#ffffff;color:#1a1918;border:1px solid #dcd8d0;border-radius:15px;padding:6px 11px}
  QPushButton#secondary-pill:hover{background:#f2efe9;border-color:#c2bdb3}
  QPushButton#quick-chip{background:#faf9f6;border:1px solid #e5e1d9;border-radius:8px;padding:5px 4px;font-size:10.5px;color:#3b3935}
  QPushButton#quick-chip:hover{background:#fdf4f0;border-color:#c45632;color:#9c4122}
  QToolButton#shape-card{background:#faf9f6;border:1px solid #e6e2da;border-radius:11px;padding:8px 4px;color:#6e6b66;font-size:10.5px;font-weight:500}
  QToolButton#shape-card:hover{background:#f3f0ea;border-color:#cdc8be;color:#1a1918}
  QToolButton#shape-card:checked{background:#fdf5f1;border:1.5px solid #c45632;color:#1a1918;font-weight:600}
  QToolButton#studio-tile{background:#ffffff;border:1px solid #e8e4dc;border-radius:10px;padding:6px 2px;color:#5e5b56;font-size:10px;font-weight:500}
  QToolButton#studio-tile:hover{background:#f5f2ec;border-color:#d2cdc3;color:#1a1918}
  QToolButton#studio-tile:checked{background:#fdf5f1;border:1.5px solid #c45632;color:#1a1918;font-weight:600}
  QToolButton#cat-pill{background:#f3f0ea;border:1px solid #e4e0d8;border-radius:9px;padding:4px 2px;color:#5e5b56;font-size:10px;font-weight:500}
  QToolButton#cat-pill:hover{background:#eae5dc;color:#1a1918}
  QToolButton#cat-pill:checked{background:#1a1918;border-color:#1a1918;color:#ffffff;font-weight:600}
  QListWidget#diagram-list{background:transparent;border:0;outline:0}
  QListWidget#diagram-list::item{padding:6px 9px;border-radius:8px;color:#3a3834;font-size:11.5px}
  QListWidget#diagram-list::item:hover{background:#f1eee8}
  QListWidget#diagram-list::item:selected{background:#1a1918;color:#ffffff;font-weight:600}
  QListWidget{background:#ffffff;border:1px solid #e8e4dc;border-radius:9px;padding:4px}
  QListWidget::item{padding:6px 8px;border-radius:6px}
  QListWidget::item:selected{background:#ebe7df;color:#1a1918}
  QTreeWidget{background:transparent;border:0;outline:0;font-size:11.5px}
  QTreeWidget::item{height:28px;border-radius:6px}
  QTreeWidget::item:hover{background:#f2efe9}
  QTreeWidget::item:selected{background:#e8e3da;color:#1a1918}
  QTreeWidget::indicator{width:13px;height:13px;border-radius:4px}
  QTreeWidget::indicator:unchecked{background:#ffffff;border:1px solid #c6c1b8}
  QTreeWidget::indicator:checked{background:#c45632;border:1px solid #c45632;image:url(:/assets/check-white.svg)}
  QComboBox::drop-down{border:0;width:24px;background:transparent}
  QComboBox::down-arrow{image:url(:/assets/chevron-dark.svg);width:11px;height:11px}
  QComboBox QAbstractItemView{background:#ffffff;border:1px solid #dcd8d0;color:#1a1918;selection-background-color:#ebe7df;padding:4px;border-radius:8px}
  QCheckBox{spacing:7px;color:#2c2a27;background:transparent}
  QCheckBox::indicator{width:15px;height:15px;border-radius:4px}
  QCheckBox::indicator:unchecked{background:#ffffff;border:1.5px solid #c6c1b8}
  QCheckBox::indicator:unchecked:hover{border-color:#c45632}
  QCheckBox::indicator:checked{background:#c45632;border:1.5px solid #c45632;image:url(:/assets/check-white.svg)}
  QCheckBox::indicator:checked:hover{background:#a84524;border-color:#a84524}
  QSlider::groove:horizontal{height:4px;background:#e4e0d8;border-radius:2px}
  QSlider::sub-page:horizontal{background:#c45632;border-radius:2px}
  QSlider::handle:horizontal{width:14px;height:14px;background:#ffffff;border:1.8px solid #c45632;margin:-5px 0;border-radius:7px}
  QSlider::handle:horizontal:hover{background:#fdf5f1;border-color:#a34323}
  QTabWidget::pane{border:1px solid #e6e2da;border-radius:11px;background:#ffffff;top:-1px}
  QTabBar::tab{background:#f3f0ea;color:#6d6a64;padding:7px 14px;border:1px solid #e6e2da;border-bottom:0;border-top-left-radius:8px;border-top-right-radius:8px;margin-right:4px;font-weight:500}
  QTabBar::tab:selected{background:#ffffff;color:#1a1918;font-weight:600;border-bottom:2px solid #c45632}
  QMenu{background:#ffffff;border:1px solid #e0dcd4;border-radius:10px;padding:5px;color:#1a1918}
  QMenu::item{padding:7px 24px;border-radius:6px}
  QMenu::item:selected{background:#f2efe9;color:#1a1918}
  QMenu::separator{height:1px;background:#ebe7df;margin:4px 10px}
  QScrollArea{background:transparent;border:0}
  QScrollArea>QWidget>QWidget{background:transparent}
  QScrollBar:vertical{background:transparent;width:6px;margin:3px 0}
  QScrollBar::handle:vertical{background:#d4cfc6;min-height:30px;border-radius:3px}
  QScrollBar::handle:vertical:hover{background:#aba59b}
  QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}
  QStatusBar{background:#f5f3ee;border-top:1px solid #e6e2da;color:#6e6b66;font-size:11px;padding-left:6px;padding-right:10px}
  QStatusBar::item{border:0}
  QToolButton#edge-toggle{background:#ffffff;border:1px solid #e0dcd4;border-radius:8px;color:#5a5752;font-weight:700;padding:0}
  QToolButton#edge-toggle:hover{background:#fdf4f0;border-color:#c45632;color:#c45632}
 )");
}
