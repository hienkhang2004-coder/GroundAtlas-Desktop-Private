#include "licensedialog.h"
#include "uxmotion.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString formatVnd(int vnd) {
 return QLocale(QStringLiteral("vi_VN")).toString(vnd) + QStringLiteral(" đ");
}
} // namespace

QString LicenseDialog::trText(const char *vi, const char *en) const {
 return QString::fromUtf8(english_ ? en : vi);
}

LicenseDialog::LicenseDialog(ga::LicenseManager *manager,
                             bool english,
                             bool darkTheme,
                             const QString &blockedFeature,
                             QWidget *parent)
 : QDialog(parent), manager_(manager), english_(english), dark_(darkTheme) {
 setWindowTitle(trText("Bản quyền GroundAtlas Desktop 2.x (Per-Seat)", "GroundAtlas Desktop 2.x License (Per-Seat)"));
 setMinimumWidth(540);
 setMaximumWidth(620);

 const QString bg = dark_ ? "#161616" : "#f7f7f7";
 const QString cardBg = dark_ ? "#1f1f1f" : "#ffffff";
 const QString ink = dark_ ? "#ebebeb" : "#1f1f1f";
 const QString muted = dark_ ? "#8e8e8e" : "#6e6e6e";
 const QString border = dark_ ? "#303030" : "#e5e5e5";
 const QString primaryBg = dark_ ? "#f5f5f5" : "#131313";
 const QString primaryFg = dark_ ? "#131313" : "#ffffff";
 const QString primaryHov = dark_ ? "#ffffff" : "#2d2d2d";
 setStyleSheet(QString(
  "QDialog { background:%1; color:%2; }"
  "QFrame#lic-card { background:%3; border:1px solid %4; border-radius:12px; }"
  "QLabel#muted { color:%5; font-size:12px; }"
  "QLineEdit { background:%3; color:%2; border:1px solid %4; border-radius:8px; padding:8px 12px; font-family:Consolas,'SF Mono',monospace; font-size:13.5px; }"
  "QLineEdit:focus { border:1.5px solid #3b63fb; }"
  "QPushButton#accent-btn { background:%6; color:%7; border:none; border-radius:14px; min-height:18px; padding:7px 16px; font-weight:600; }"
  "QPushButton#accent-btn:hover { background:%8; }"
  "QPushButton#sec-btn { background:%3; color:%2; border:1px solid %4; border-radius:14px; min-height:18px; padding:6px 14px; font-weight:500; }"
  "QPushButton#sec-btn:hover { border-color:%6; }"
  "QPushButton#danger-btn { background:transparent; color:#d7373f; border:1px solid #d7373f; border-radius:14px; min-height:18px; padding:6px 14px; font-weight:600; }"
  "QPushButton#danger-btn:hover { background:rgba(215,55,63,0.10); }"
 ).arg(bg, ink, cardBg, border, muted, primaryBg, primaryFg, primaryHov));

 auto *root = new QVBoxLayout(this);
 root->setContentsMargins(18, 16, 18, 16);
 root->setSpacing(12);

 // Optional Pro feature gate banner
 if (!blockedFeature.isEmpty()) {
  auto *gate = new QFrame(this);
  gate->setObjectName("gate-banner");
  gate->setStyleSheet(QString("QFrame#gate-banner{background:%1;border:1px solid %2;border-radius:10px;} QFrame#gate-banner QLabel{border:none;background:transparent;}")
   .arg(dark_ ? "#242424" : "#f2f2f2", border));
  auto *gl = new QVBoxLayout(gate);
  gl->setContentsMargins(14, 10, 14, 10);
  gl->setSpacing(4);
  auto *gt = new QLabel(trText("🔒 Tính năng Pro: ", "🔒 Pro Feature: ") + blockedFeature, gate);
  gt->setStyleSheet(QString("font-weight:700;color:%1;font-size:13px;").arg(ink));
  auto *gs = new QLabel(trText(
   "Bản Free cho phép xem 2D/3D, nạp OSM và đè quy hoạch không giới hạn. Nhập mã bản quyền trọn đời (1 Seat = 1 Máy) để mở khóa toàn bộ xuất file CAD / SketchUp / 4K / PDF / Import .SKP.",
   "Free edition includes unlimited 2D/3D viewing, live OSM loading & planning overlay. Activate a Lifetime 2.x Seat key (1 Seat = 1 Machine) to unlock all CAD/SketchUp/4K/PDF exports & .SKP import."), gate);
  gs->setWordWrap(true);
  gs->setObjectName("muted");
  gl->addWidget(gt);
  gl->addWidget(gs);
  root->addWidget(gate);
 }

 // Status Card
 auto *statusCard = new QFrame(this);
 statusCard->setObjectName("lic-card");
 auto *sl = new QVBoxLayout(statusCard);
 sl->setContentsMargins(14, 12, 14, 12);
 sl->setSpacing(4);
 statusBadge_ = new QLabel(statusCard);
 statusBadge_->setStyleSheet("font-size:15px;font-weight:700;");
 statusDetail_ = new QLabel(statusCard);
 statusDetail_->setObjectName("muted");
 statusDetail_->setWordWrap(true);
 sl->addWidget(statusBadge_);
 sl->addWidget(statusDetail_);
 root->addWidget(statusCard);

 // Active Pro Controls Panel (Refresh + Self-Service Seat Swap)
 activePanel_ = new QWidget(this);
 auto *apl = new QHBoxLayout(activePanel_);
 apl->setContentsMargins(0, 0, 0, 0);
 apl->setSpacing(8);
 refreshBtn_ = new QPushButton(trText("Làm mới bản quyền", "Refresh Lease"), activePanel_);
 refreshBtn_->setObjectName("sec-btn");
 deactivateBtn_ = new QPushButton(trText("Gỡ máy này (Trả 1 Seat)", "Release This Seat"), activePanel_);
 deactivateBtn_->setObjectName("danger-btn");
 apl->addWidget(refreshBtn_);
 apl->addStretch();
 apl->addWidget(deactivateBtn_);
 root->addWidget(activePanel_);

 // Buy / Early-Bird Panel (shown when Free or Expired)
 buyPanel_ = new QFrame(this);
 buyPanel_->setObjectName("lic-card");
 auto *bpl = new QVBoxLayout(buyPanel_);
 bpl->setContentsMargins(14, 12, 14, 12);
 bpl->setSpacing(8);

 offerBanner_ = new QLabel(buyPanel_);
 offerBanner_->setWordWrap(true);
 offerBanner_->setStyleSheet("background:#3b63fb;color:#ffffff;padding:7px 12px;border-radius:8px;font-weight:600;font-size:12px;");
 offerBanner_->hide();
 bpl->addWidget(offerBanner_);

 batchLabel_ = new QLabel(buyPanel_);
 batchLabel_->setStyleSheet("font-weight:600;font-size:12.5px;");
 bpl->addWidget(batchLabel_);

 batchBar_ = new QProgressBar(buyPanel_);
 batchBar_->setFixedHeight(6);
 batchBar_->setTextVisible(false);
 batchBar_->setStyleSheet(dark_
  ? "QProgressBar{background:#2c2c2c;border:none;border-radius:3px;} QProgressBar::chunk{background:#f5f5f5;border-radius:3px;}"
  : "QProgressBar{background:#e8e8e8;border:none;border-radius:3px;} QProgressBar::chunk{background:#131313;border-radius:3px;}");
 bpl->addWidget(batchBar_);

 auto *pkgRow = new QGridLayout();
 pkgRow->setSpacing(6);
 const QVector<QPair<int, QString>> pkgs = {
  {1, trText("1 Seat · Cá nhân", "1 Seat · Solo")},
  {3, trText("3 Seats · Nhóm", "3 Seats · Team")},
  {5, trText("5 Seats · Studio", "5 Seats · Studio")},
  {10, trText("10 Seats · Công ty", "10 Seats · Office")},
 };
 for (int i = 0; i < pkgs.size(); ++i) {
  auto *pb = new QPushButton(buyPanel_);
  pb->setObjectName(i == 0 ? "accent-btn" : "sec-btn");
  pb->setProperty("pkgSeats", pkgs[i].first);
  connect(pb, &QPushButton::clicked, this, [this, s = pkgs[i].first] {
   if (manager_) QDesktopServices::openUrl(QUrl(manager_->buyUrl(s)));
  });
  pkgRow->addWidget(pb, i / 2, i % 2);
 }
 bpl->addLayout(pkgRow);

 auto *noteLbl = new QLabel(trText(
  "• Mua 1 lần dùng vĩnh viễn dòng 2.x · 1 Seat = 1 Máy · Offline 14 ngày · Tự chuyển máy tối đa 2 lần / 6 tháng.\n"
  "• Thanh toán VietQR tự động nhận mã bản quyền sau 5 giây.",
  "• One-time purchase for Lifetime 2.x · 1 Seat = 1 Machine · 14-day offline lease · Self-service seat swap (2x / 6 months)."), buyPanel_);
 noteLbl->setObjectName("muted");
 noteLbl->setWordWrap(true);
 bpl->addWidget(noteLbl);
 root->addWidget(buyPanel_);

 // Activation Input Row
 auto *actCard = new QFrame(this);
 actCard->setObjectName("lic-card");
 auto *acl = new QVBoxLayout(actCard);
 acl->setContentsMargins(14, 12, 14, 12);
 acl->setSpacing(8);
 auto *kl = new QLabel(trText("KÍCH HOẠT MÃ BẢN QUYỀN", "ACTIVATE LICENSE KEY"), actCard);
 kl->setObjectName("muted");
 kl->setStyleSheet("font-weight:700;letter-spacing:0.6px;");
 acl->addWidget(kl);
 auto *inpRow = new QHBoxLayout();
 inpRow->setSpacing(8);
 keyEdit_ = new QLineEdit(actCard);
 keyEdit_->setObjectName("license-key-input");
 keyEdit_->setPlaceholderText(QStringLiteral("GA2X-XXXX-XXXX-XXXX"));
 activateBtn_ = new QPushButton(trText("Kích hoạt", "Activate"), actCard);
 activateBtn_->setObjectName("accent-btn");
 inpRow->addWidget(keyEdit_, 1);
 inpRow->addWidget(activateBtn_);
 acl->addLayout(inpRow);

 feedbackLbl_ = new QLabel(actCard);
 feedbackLbl_->setWordWrap(true);
 feedbackLbl_->hide();
 acl->addWidget(feedbackLbl_);
 root->addWidget(actCard);

 // Footer: Machine ID + Close
 auto *foot = new QHBoxLayout();
 const QString shortId = manager_ ? manager_->machineHash().left(16) : QStringLiteral("—");
 auto *midLbl = new QLabel(trText("Mã máy: ", "Machine ID: ") + shortId + "…", this);
 midLbl->setObjectName("muted");
 auto *copyIdBtn = new QPushButton(trText("Chép mã máy", "Copy ID"), this);
 copyIdBtn->setObjectName("sec-btn");
 connect(copyIdBtn, &QPushButton::clicked, this, [this] {
  if (manager_) QApplication::clipboard()->setText(manager_->machineHash());
  feedbackLbl_->setStyleSheet("color:#16a34a;font-size:12px;");
  feedbackLbl_->setText(trText("Đã sao chép mã máy đầy đủ vào bộ nhớ tạm.", "Copied full Machine ID to clipboard."));
  feedbackLbl_->show();
 });
 auto *closeBtn = new QPushButton(trText("Đóng", "Close"), this);
 closeBtn->setObjectName("sec-btn");
 connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
 foot->addWidget(midLbl);
 foot->addWidget(copyIdBtn);
 foot->addStretch();
 foot->addWidget(closeBtn);
 root->addLayout(foot);

 // Signals
 connect(activateBtn_, &QPushButton::clicked, this, [this] {
  if (manager_) manager_->activate(keyEdit_->text());
 });
 connect(keyEdit_, &QLineEdit::returnPressed, activateBtn_, &QPushButton::click);
 connect(refreshBtn_, &QPushButton::clicked, this, [this] {
  if (manager_) manager_->refresh();
 });
 connect(deactivateBtn_, &QPushButton::clicked, this, [this] {
  if (!manager_) return;
  const auto ans = QMessageBox::question(
   this,
   trText("Gỡ kích hoạt máy này?", "Release Seat on This Machine?"),
   trText("Máy này sẽ chuyển về bản Free và trả lại 1 Seat để bạn kích hoạt trên máy tính mới.\n"
          "(Giới hạn tự chuyển máy: tối đa 2 lần trong 6 tháng). Tiếp tục?",
          "This machine will revert to Free mode and release 1 Seat so you can activate on another PC.\n"
          "(Self-service limit: 2 swaps per 6 months). Continue?"),
   QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (ans == QMessageBox::Yes) manager_->deactivate();
 });

 if (manager_) {
  connect(manager_, &ga::LicenseManager::statusChanged, this, &LicenseDialog::syncUi);
  connect(manager_, &ga::LicenseManager::offerUpdated, this, &LicenseDialog::syncUi);
  connect(manager_, &ga::LicenseManager::busyChanged, this, [this](bool b) {
   activateBtn_->setEnabled(!b);
   refreshBtn_->setEnabled(!b);
   deactivateBtn_->setEnabled(!b);
   activateBtn_->setText(b ? trText("Đang kiểm tra…", "Checking…") : trText("Kích hoạt", "Activate"));
  });
  connect(manager_, &ga::LicenseManager::operationSucceeded, this, [this](const QString &m) {
   feedbackLbl_->setStyleSheet("color:#16a34a;font-size:12.5px;font-weight:600;");
   feedbackLbl_->setText(m);
   feedbackLbl_->show();
   keyEdit_->clear();
  });
  connect(manager_, &ga::LicenseManager::operationFailed, this, [this](const QString &m) {
   feedbackLbl_->setStyleSheet("color:#dc2626;font-size:12.5px;font-weight:600;");
   feedbackLbl_->setText(m);
   feedbackLbl_->show();
  });
 }

 countdownTimer_.setInterval(1000);
 connect(&countdownTimer_, &QTimer::timeout, this, &LicenseDialog::updateCountdown);
 countdownTimer_.start();

 syncUi();
 ux::attachDialogFadeIn(this);
}

void LicenseDialog::syncUi() {
 if (!manager_) return;
 const auto &inf = manager_->info();
 const auto &off = manager_->offer();

 if (inf.state == ga::LicenseState::ProActive) {
  const bool isAdminOffline = (inf.tier == QStringLiteral("admin_2x") || inf.tier == QStringLiteral("offline_2x"));
  statusBadge_->setStyleSheet("font-size:15px;font-weight:700;color:#16a34a;");
  statusBadge_->setText(isAdminOffline
   ? trText("✓ GroundAtlas Pro · Admin / Founder 2.x (Đã kích hoạt)", "✓ GroundAtlas Pro · Admin / Founder 2.x (Active)")
   : trText("✓ GroundAtlas Pro · Trọn đời 2.x (Đã kích hoạt)", "✓ GroundAtlas Pro · Lifetime 2.x (Active)"));
  const QString offlineTxt = inf.offlineDaysRemaining() >= 365
   ? trText("Offline: Trọn đời (Ed25519)", "Offline: Lifetime (Ed25519)")
   : trText("Offline còn %1 ngày", "Offline lease: %1 days").arg(inf.offlineDaysRemaining());
  statusDetail_->setText(
   trText("Mã: %1  ·  Số máy: %2/%3 Seat  ·  %4%5",
          "Key: %1  ·  Seats: %2/%3  ·  %4%5")
    .arg(inf.keyMasked)
    .arg(inf.usedSeats)
    .arg(inf.maxSeats)
    .arg(offlineTxt)
    .arg(inf.buyerEmail.isEmpty() ? QString() : ("  ·  " + inf.buyerEmail)));
 } else if (inf.state == ga::LicenseState::LeaseExpired) {
  statusBadge_->setStyleSheet("font-size:15px;font-weight:700;color:#d97706;");
  statusBadge_->setText(trText("⚠ Cần kết nối mạng để làm mới bản quyền", "⚠ Online Refresh Required"));
  statusDetail_->setText(trText("Chu kỳ offline 14 ngày của mã %1 đã hết. Bấm 'Làm mới bản quyền' khi có mạng.",
                                "The 14-day offline lease for %1 expired. Click 'Refresh Lease' while online.").arg(inf.keyMasked));
 } else if (inf.state == ga::LicenseState::ClockTampered) {
  statusBadge_->setStyleSheet("font-size:15px;font-weight:700;color:#dc2626;");
  statusBadge_->setText(trText("⚠ Phát hiện sai lệch đồng hồ hệ thống", "⚠ System Clock Mismatch Detected"));
  statusDetail_->setText(trText("Vui lòng đồng bộ lại ngày giờ Windows và bấm 'Làm mới bản quyền'.",
                                "Please sync your Windows clock and click 'Refresh Lease'."));
 } else {
  statusBadge_->setStyleSheet(QString("font-size:15px;font-weight:700;color:%1;").arg(dark_ ? "#f4f2ee" : "#1a1918"));
  statusBadge_->setText(trText("Bản miễn phí (Free) · Xem 2D/3D & Nạp OSM không giới hạn", "Free Edition · Unlimited 2D/3D & Live OSM"));
  statusDetail_->setText(trText("Kích hoạt Key trọn đời 2.x để mở khóa xuất CAD DXF, SketchUp DAE, 4K PNG, PDF A3, GIF, Import .SKP & Massing 3D.",
                                "Activate a Lifetime 2.x Seat key to unlock CAD DXF, SketchUp DAE, 4K PNG, A3 PDF, GIF, .SKP Import & 3D Massing."));
 }

 activePanel_->setVisible(inf.state != ga::LicenseState::Free);
 buyPanel_->setVisible(!inf.isPro());

 // Update Early-Bird real slot bar & package buttons
 if (off.seatQuota > 0) {
  batchLabel_->setText(QString("%1  —  %2 %3/%4 %5")
   .arg(off.batchName,
        trText("Còn", "Left"))
   .arg(off.remainingInBatch)
   .arg(off.seatQuota)
   .arg(trText("suất", "slots")));
  batchBar_->setRange(0, std::max(1, off.seatQuota));
  batchBar_->setValue(std::clamp(off.soldInBatch, 0, off.seatQuota));
 } else {
  batchLabel_->setText(off.batchName);
  batchBar_->setRange(0, 100);
  batchBar_->setValue(100);
 }

 for (auto *btn : buyPanel_->findChildren<QPushButton *>()) {
  const int s = btn->property("pkgSeats").toInt();
  if (s == 1) btn->setText(trText("1 Seat · ", "1 Seat · ") + formatVnd(off.price1SeatVnd));
  else if (s == 3) btn->setText(trText("3 Seats · ", "3 Seats · ") + formatVnd(off.price3SeatsVnd));
  else if (s == 5) btn->setText(trText("5 Seats · ", "5 Seats · ") + formatVnd(off.price5SeatsVnd));
  else if (s == 10) btn->setText(trText("10 Seats · ", "10 Seats · ") + formatVnd(off.price10SeatsVnd));
 }
 updateCountdown();
}

void LicenseDialog::updateCountdown() {
 if (!manager_ || !offerBanner_) return;
 const qint64 rem = manager_->offer().welcomeRemainingSec();
 if (rem > 0 && !manager_->isPro()) {
  const qint64 hh = rem / 3600;
  const qint64 mm = (rem % 3600) / 60;
  const qint64 ss = rem % 60;
  offerBanner_->setText(
   trText("⚡ Ưu đãi trải nghiệm lần đầu cho máy này: chỉ %1 · còn %2:%3:%4",
          "⚡ First-install welcome offer on this PC: %1 · %2:%3:%4 left")
    .arg(formatVnd(manager_->offer().price1SeatVnd))
    .arg(hh, 2, 10, QChar('0'))
    .arg(mm, 2, 10, QChar('0'))
    .arg(ss, 2, 10, QChar('0')));
  offerBanner_->show();
 } else {
  offerBanner_->hide();
 }
}

// ═══════════════════════════════════════════════════════════════════════════
// Open-Source & Data Attribution Licenses Dialog (Commercial LGPLv3 / BSD / ODbL Compliance)
// ═══════════════════════════════════════════════════════════════════════════
OpenSourceLicensesDialog::OpenSourceLicensesDialog(bool english, bool darkTheme, QWidget *parent)
 : QDialog(parent) {
 setWindowTitle(QString::fromUtf8(english ? "Open-Source & Data Licenses" : "Giấy phép Nguồn mở & Dữ liệu"));
 resize(640, 500);
 const QString bg = darkTheme ? "#1b1b1b" : "#f8f8f8";
 const QString card = darkTheme ? "#222222" : "#ffffff";
 const QString ink = darkTheme ? "#ebebeb" : "#222222";
 const QString border = darkTheme ? "#343434" : "#e1e1e1";
 setStyleSheet(QString("QDialog{background:%1;color:%2;} QPlainTextEdit{background:%3;color:%2;border:1px solid %4;border-radius:10px;padding:10px;font-family:Consolas,monospace;font-size:12px;}")
  .arg(bg, ink, card, border));

 auto *lay = new QVBoxLayout(this);
 lay->setContentsMargins(16, 14, 16, 14);
 lay->setSpacing(10);

 auto *head = new QLabel(QString::fromUtf8(english
  ? "GroundAtlas Desktop 2.3.1 — Third-Party Software & Map Data Notices"
  : "GroundAtlas Desktop 2.3.1 — Thông báo Bản quyền Thư viện & Dữ liệu Bản đồ"), this);
 head->setStyleSheet("font-size:14px;font-weight:700;");
 lay->addWidget(head);

 auto *box = new QPlainTextEdit(this);
 box->setReadOnly(true);
 box->setPlainText(QString::fromUtf8(
  "1. Qt 6.8.3 (LGPL v3)\n"
  "   Copyright (C) The Qt Company Ltd.\n"
  "   Dynamically linked via shared libraries (Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll,\n"
  "   Qt6OpenGL.dll, Qt6OpenGLWidgets.dll, Qt6Network.dll, Qt6Svg.dll, Qt6Concurrent.dll).\n"
  "   Licensed under GNU Lesser General Public License v3 (https://www.gnu.org/licenses/lgpl-3.0.html).\n"
  "   Users may replace the dynamically linked Qt libraries with compatible LGPLv3 builds.\n\n"
  "2. MapLibre Native for Qt 3.0 (BSD 2-Clause)\n"
  "   Copyright (c) MapLibre contributors & Mapbox, Inc.\n"
  "   Licensed under the 2-Clause BSD License (https://github.com/maplibre/maplibre-native-qt).\n\n"
  "3. Monocypher 4.0.2 + RFC 8032 Ed25519 (CC0 1.0 / BSD 2-Clause)\n"
  "   Copyright (c) 2017-2023 Loup Vaillant.\n"
  "   Dual-licensed under CC0-1.0 and BSD-2-Clause (https://monocypher.org).\n\n"
  "4. Mapbox Earcut (ISC License)\n"
  "   Copyright (c) 2015, Mapbox.\n"
  "   Licensed under the ISC License (https://github.com/mapbox/earcut.hpp).\n\n"
  "5. OpenStreetMap Data & OpenFreeMap Tiles (ODbL 1.0)\n"
  "   Map data © OpenStreetMap contributors, available under the Open Database License (ODbL 1.0).\n"
  "   https://www.openstreetmap.org/copyright\n\n"
  "6. SketchUp C API Integration\n"
  "   Direct .SKP import loads the locally installed SketchUpAPI.dll from the user's own\n"
  "   SketchUp installation at runtime. GroundAtlas does not redistribute Trimble binaries.\n\n"
  "7. Custom XYZ Planning Tile Overlays\n"
  "   User-configured XYZ raster tile overlays load directly from the URL specified by the user.\n"
  "   Users are responsible for ensuring appropriate rights to any third-party tile sources they configure.\n"
 ));
 lay->addWidget(box, 1);

 auto *closeBtn = new QPushButton(QString::fromUtf8(english ? "Close" : "Đóng"), this);
 closeBtn->setFixedWidth(100);
 connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
 auto *row = new QHBoxLayout();
 row->addStretch();
 row->addWidget(closeBtn);
 lay->addLayout(row);
 ux::attachDialogFadeIn(this);
}
