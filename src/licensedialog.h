#pragma once
#include "license.h"
#include <QDialog>
#include <QTimer>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class LicenseDialog : public QDialog {
 Q_OBJECT
public:
 LicenseDialog(ga::LicenseManager *manager,
               bool english,
               bool darkTheme,
               const QString &blockedFeature = {},
               QWidget *parent = nullptr);

private:
 ga::LicenseManager *manager_ = nullptr;
 bool english_ = false;
 bool dark_ = false;
 QLabel *statusBadge_ = nullptr;
 QLabel *statusDetail_ = nullptr;
 QLabel *batchLabel_ = nullptr;
 QProgressBar *batchBar_ = nullptr;
 QLabel *offerBanner_ = nullptr;
 QLabel *feedbackLbl_ = nullptr;
 QLineEdit *keyEdit_ = nullptr;
 QPushButton *activateBtn_ = nullptr;
 QPushButton *refreshBtn_ = nullptr;
 QPushButton *deactivateBtn_ = nullptr;
 QWidget *buyPanel_ = nullptr;
 QWidget *activePanel_ = nullptr;
 QTimer countdownTimer_;

 QString trText(const char *vi, const char *en) const;
 void syncUi();
 void updateCountdown();
};

class OpenSourceLicensesDialog : public QDialog {
 Q_OBJECT
public:
 OpenSourceLicensesDialog(bool english, bool darkTheme, QWidget *parent = nullptr);
};
