#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkReply;

namespace ga {

enum class LicenseState {
 Free = 0,
 ProActive = 1,
 LeaseExpired = 2,
 ClockTampered = 3,
};

struct LicenseInfo {
 LicenseState state = LicenseState::Free;
 QString keyMasked;
 QString tier;
 QString buyerEmail;
 int majorVersion = 2;
 int maxSeats = 0;
 int usedSeats = 0;
 qint64 issuedUtc = 0;
 qint64 expiresUtc = 0;

 bool isPro() const { return state == LicenseState::ProActive; }
 int offlineDaysRemaining(qint64 nowUtc = 0) const;
};

struct WelcomeOfferInfo {
 bool loaded = false;
 QString batchId = QStringLiteral("batch_1");
 QString batchName = QStringLiteral("Đợt 1 · Early Bird (100 suất đầu)");
 int seatQuota = 100;
 int soldInBatch = 0;
 int remainingInBatch = 100;
 int totalSeatsSold = 0;
 int price1SeatVnd = 99000;
 int price3SeatsVnd = 249000;
 int price5SeatsVnd = 399000;
 int price10SeatsVnd = 690000;
 bool welcomeOfferActive = false;
 qint64 welcomeOfferExpiresUtc = 0;
 QString latestVersion;
 QString downloadUrl;
 QString updateNotes;

 qint64 welcomeRemainingSec(qint64 nowUtc = 0) const;
 bool isUpdateAvailable(const QString &currentVersion) const;
};

// Stable 64-char SHA-256 hardware fingerprint (Windows MachineGuid + CPU + system volume serial; immune to VPN/Wi-Fi MAC changes)
QString machineFingerprint();
QString machineDisplayName();

// Pure RFC 8032 Ed25519 (Monocypher) verification of compact lease tokens: "GA2L.<b64url(json)>.<b64url(sig64)>"
QByteArray defaultLicensePublicKey();
bool verifySignedLeaseToken(const QString &token,
                            const QByteArray &publicKey32,
                            const QString &expectedMachineHash,
                            LicenseInfo *outInfo,
                            QString *errorOut = nullptr,
                            qint64 nowUtcOverride = 0);

// Signs a lease payload using Monocypher Ed25519 (for unit tests & offline verification tests)
QString signLeaseTokenWithSeed(const QJsonObject &payload, const QByteArray &seed32, QByteArray *outPubKey32 = nullptr);

// Local machine-bound authenticated encryption: Windows DPAPI (GADP1:) or cross-platform Monocypher XChaCha20-Poly1305 AEAD (GAMC1:)
QByteArray protectLocalData(const QByteArray &plain);
QByteArray unprotectLocalData(const QByteArray &protectedBlob);
QByteArray protectLocalDataAead(const QByteArray &plain);
QByteArray unprotectLocalDataAead(const QByteArray &protectedBlob);

class LicenseManager : public QObject {
 Q_OBJECT
public:
 explicit LicenseManager(QString dataDirectory, QObject *parent = nullptr);
 ~LicenseManager() override;

 const LicenseInfo &info() const { return info_; }
 const WelcomeOfferInfo &offer() const { return offer_; }
 bool isPro() const { return info_.isPro(); }
 bool busy() const { return busy_; }
 QString machineHash() const { return machineHash_; }
 QString serverUrl() const { return serverUrl_; }
 QString buyUrl(int seats = 1) const;
 QJsonObject diagnostics() const;

 void setServerUrl(const QString &url);
 void setPublicKey(const QByteArray &publicKey32);
 void setMachineHashForTesting(const QString &hash64);

 // Loads & verifies local DPAPI-encrypted lease + anti-clock-rollback check
 void loadLocal();

 // Online operations (async, non-blocking)
 void bootstrap();
 void activate(const QString &licenseKey);
 void refresh();
 void deactivate();

 // Verifies and persists a signed Ed25519 lease token (used by activate/refresh & unit tests)
 bool installSignedLease(const QString &token, QString *errorOut = nullptr, qint64 nowUtcOverride = 0);
 void clearLocalLease();

signals:
 void statusChanged();
 void offerUpdated();
 void busyChanged(bool busy);
 void operationSucceeded(QString message);
 void operationFailed(QString message);

private:
 QString dataDirectory_;
 QString serverUrl_;
 QString machineHash_;
 QByteArray publicKey_;
 LicenseInfo info_;
 WelcomeOfferInfo offer_;
 QString rawToken_;
 qint64 maxSeenUtc_ = 0;
 bool busy_ = false;
 QNetworkAccessManager network_;
 QPointer<QNetworkReply> activeOp_;

 QString storagePath() const;
 void saveLocalBlob();
 void setBusy(bool b);
};

} // namespace ga
