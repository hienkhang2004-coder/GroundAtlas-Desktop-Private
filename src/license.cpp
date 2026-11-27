#include "license.h"
#include "project.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QVersionNumber>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace ga {
namespace {

// Production Ed25519 Public Key matching license-server/.dev.vars (private seed is NEVER in client!)
static constexpr uint8_t kDefaultPublicKey[32] = {
 0x84, 0x8b, 0x30, 0xde, 0xc0, 0x04, 0xfc, 0xcf,
 0x79, 0xaf, 0x9a, 0x7d, 0x79, 0x9c, 0x63, 0x5c,
 0x33, 0x89, 0xee, 0xd3, 0x7a, 0x2a, 0x37, 0xb6,
 0x9f, 0x40, 0x98, 0xc2, 0x37, 0xe9, 0xf7, 0xbc
};

static constexpr const char *kDefaultLicenseServer = "https://groundatlas-license-server.groundatlas.workers.dev";
static constexpr qint64 kClockRollbackToleranceSec = 86400; // 24 hours tolerance for timezone/NTP drift

QByteArray b64UrlEncode(const QByteArray &raw) {
 return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray b64UrlDecode(const QByteArray &enc) {
 return QByteArray::fromBase64(enc, QByteArray::Base64UrlEncoding);
}

bool isAllowedServerUrl(const QString &value) {
 const QUrl u(value.trimmed());
 if (!u.isValid() || !u.userInfo().isEmpty() || !u.fragment().isEmpty()) return false;
 if (u.scheme() == "https") return !u.host().isEmpty();
 if (u.scheme() == "http" && (u.host() == "127.0.0.1" || u.host() == "localhost")) return true;
 return false;
}

} // namespace

int LicenseInfo::offlineDaysRemaining(qint64 nowUtc) const {
 if (expiresUtc <= 0) return 0;
 const qint64 now = nowUtc > 0 ? nowUtc : QDateTime::currentSecsSinceEpoch();
 return std::max(0, int((expiresUtc - now + 43200) / 86400));
}

qint64 WelcomeOfferInfo::welcomeRemainingSec(qint64 nowUtc) const {
 if (!welcomeOfferActive || welcomeOfferExpiresUtc <= 0) return 0;
 const qint64 now = nowUtc > 0 ? nowUtc : QDateTime::currentSecsSinceEpoch();
 return std::max<qint64>(0, welcomeOfferExpiresUtc - now);
}

bool WelcomeOfferInfo::isUpdateAvailable(const QString &currentVersion) const {
 const QString latest = latestVersion.trimmed();
 const QString current = currentVersion.trimmed();
 if (latest.isEmpty() || current.isEmpty()) return false;
 const QVersionNumber vLatest = QVersionNumber::fromString(latest);
 const QVersionNumber vCurrent = QVersionNumber::fromString(current);
 if (vLatest.isNull() || vCurrent.isNull()) return false;
 return QVersionNumber::compare(vLatest, vCurrent) > 0;
}

QByteArray defaultLicensePublicKey() {
 return QByteArray(reinterpret_cast<const char *>(kDefaultPublicKey), 32);
}

QString machineFingerprint() {
 QStringList parts;
 parts.append(QStringLiteral("ga-hw-v1"));
 parts.append(QSysInfo::currentCpuArchitecture());
 const QByteArray qId = QSysInfo::machineUniqueId();
 if (!qId.isEmpty()) parts.append(QString::fromLatin1(qId));

#ifdef _WIN32
 // Windows MachineGuid from HKLM\SOFTWARE\Microsoft\Cryptography (stable across VPN/Wi-Fi changes)
 HKEY hKey = nullptr;
 if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
  wchar_t buf[128]{};
  DWORD sz = sizeof(buf);
  DWORD type = 0;
  if (RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS && type == REG_SZ) {
   parts.append(QString::fromWCharArray(buf));
  }
  RegCloseKey(hKey);
 }
 // System volume serial number
 DWORD volSerial = 0;
 if (GetVolumeInformationW(L"C:\\", nullptr, 0, &volSerial, nullptr, nullptr, nullptr, 0)) {
  parts.append(QString::number(volSerial, 16));
 }
#elif defined(Q_OS_MACOS)
 // On macOS, QSysInfo::machineUniqueId() reads IOKit IOPlatformUUID directly.
 // Combine with hardware model identifier (e.g. Mac15,3) for rock-solid machine binding immune to Wi-Fi/VPN changes.
 char hwModel[128]{};
 size_t hwLen = sizeof(hwModel) - 1;
 if (sysctlbyname("hw.model", hwModel, &hwLen, nullptr, 0) == 0 && hwLen > 0) {
  parts.append(QStringLiteral("mac-hw:") + QString::fromLatin1(hwModel, int(strnlen(hwModel, sizeof(hwModel)))));
 }
#endif

 if (parts.size() <= 2) {
  parts.append(QSysInfo::machineHostName());
 }
 const QByteArray digest = QCryptographicHash::hash(parts.join(QLatin1Char('|')).toUtf8(), QCryptographicHash::Sha256);
 return QString::fromLatin1(digest.toHex());
}

QString machineDisplayName() {
 const QString host = QSysInfo::machineHostName().trimmed();
#ifdef Q_OS_MACOS
 return host.isEmpty() ? QStringLiteral("Mac") : host.left(64);
#elif defined(Q_OS_LINUX)
 return host.isEmpty() ? QStringLiteral("Linux PC") : host.left(64);
#else
 return host.isEmpty() ? QStringLiteral("Windows PC") : host.left(64);
#endif
}

QByteArray protectLocalDataAead(const QByteArray &plain) {
 if (plain.isEmpty()) return {};
 const QByteArray keyMat = QStringLiteral("GroundAtlas-Desktop-AEAD-v2.3|")
  .append(machineFingerprint())
  .append(QLatin1Char('|'))
  .append(QDir::homePath())
  .toUtf8();
 QByteArray keyHash = QCryptographicHash::hash(keyMat, QCryptographicHash::Sha256);
 uint8_t key[32];
 std::memcpy(key, keyHash.constData(), 32);

 uint8_t nonce[24];
 quint32 rnd[6];
 QRandomGenerator::system()->fillRange(rnd);
 std::memcpy(nonce, rnd, sizeof(nonce));

 static const uint8_t kAd[] = "GAMC1-LICENSE";
 uint8_t mac[16]{};
 QByteArray cipher(plain.size(), Qt::Uninitialized);
 crypto_aead_lock(
  reinterpret_cast<uint8_t *>(cipher.data()),
  mac,
  key,
  nonce,
  kAd, sizeof(kAd) - 1,
  reinterpret_cast<const uint8_t *>(plain.constData()),
  size_t(plain.size()));
 crypto_wipe(key, sizeof(key));
 crypto_wipe(keyHash.data(), size_t(keyHash.size()));

 QByteArray out;
 out.reserve(6 + 24 + 16 + cipher.size());
 out.append("GAMC1:");
 out.append(reinterpret_cast<const char *>(nonce), 24);
 out.append(reinterpret_cast<const char *>(mac), 16);
 out.append(cipher);
 return out;
}

QByteArray unprotectLocalDataAead(const QByteArray &blob) {
 if (!blob.startsWith("GAMC1:") || blob.size() <= 6 + 24 + 16) return {};
 const uint8_t *nonce = reinterpret_cast<const uint8_t *>(blob.constData() + 6);
 const uint8_t *mac = reinterpret_cast<const uint8_t *>(blob.constData() + 6 + 24);
 const uint8_t *cipher = reinterpret_cast<const uint8_t *>(blob.constData() + 6 + 24 + 16);
 const int cipherLen = blob.size() - (6 + 24 + 16);

 const QByteArray keyMat = QStringLiteral("GroundAtlas-Desktop-AEAD-v2.3|")
  .append(machineFingerprint())
  .append(QLatin1Char('|'))
  .append(QDir::homePath())
  .toUtf8();
 QByteArray keyHash = QCryptographicHash::hash(keyMat, QCryptographicHash::Sha256);
 uint8_t key[32];
 std::memcpy(key, keyHash.constData(), 32);

 static const uint8_t kAd[] = "GAMC1-LICENSE";
 QByteArray plain(cipherLen, Qt::Uninitialized);
 const int rc = crypto_aead_unlock(
  reinterpret_cast<uint8_t *>(plain.data()),
  mac,
  key,
  nonce,
  kAd, sizeof(kAd) - 1,
  cipher,
  size_t(cipherLen));
 crypto_wipe(key, sizeof(key));
 crypto_wipe(keyHash.data(), size_t(keyHash.size()));
 if (rc != 0) {
  crypto_wipe(plain.data(), size_t(plain.size()));
  return {};
 }
 return plain;
}

QByteArray protectLocalData(const QByteArray &plain) {
 if (plain.isEmpty()) return {};
#ifdef _WIN32
 static const char kEntropy[] = "GroundAtlas-Desktop-License-DPAPI-v2.3";
 DATA_BLOB inBlob{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
 DATA_BLOB entBlob{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
 DATA_BLOB outBlob{0, nullptr};
 if (CryptProtectData(&inBlob, L"GroundAtlasLicense", &entBlob, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
  QByteArray result(reinterpret_cast<const char *>(outBlob.pbData), int(outBlob.cbData));
  LocalFree(outBlob.pbData);
  return QByteArray("GADP1:") + result;
 }
#endif
 return protectLocalDataAead(plain);
}

QByteArray unprotectLocalData(const QByteArray &blob) {
 if (blob.isEmpty()) return {};
#ifdef _WIN32
 if (blob.startsWith("GADP1:")) {
  const QByteArray raw = blob.mid(6);
  static const char kEntropy[] = "GroundAtlas-Desktop-License-DPAPI-v2.3";
  DATA_BLOB inBlob{static_cast<DWORD>(raw.size()), reinterpret_cast<BYTE *>(const_cast<char *>(raw.constData()))};
  DATA_BLOB entBlob{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
  DATA_BLOB outBlob{0, nullptr};
  if (CryptUnprotectData(&inBlob, nullptr, &entBlob, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
   QByteArray plain(reinterpret_cast<const char *>(outBlob.pbData), int(outBlob.cbData));
   LocalFree(outBlob.pbData);
   return plain;
  }
  return {};
 }
#endif
 if (blob.startsWith("GAMC1:")) {
  return unprotectLocalDataAead(blob);
 }
 if (blob.startsWith("GAPL1:")) {
  return QByteArray::fromBase64(blob.mid(6));
 }
 return {};
}

QString signLeaseTokenWithSeed(const QJsonObject &payload, const QByteArray &seed32, QByteArray *outPubKey32) {
 if (seed32.size() != 32) return {};
 uint8_t seedCopy[32];
 std::memcpy(seedCopy, seed32.constData(), 32);
 uint8_t secretKey[64]{};
 uint8_t pubKey[32]{};
 crypto_ed25519_key_pair(secretKey, pubKey, seedCopy);
 if (outPubKey32) *outPubKey32 = QByteArray(reinterpret_cast<const char *>(pubKey), 32);

 const QByteArray payloadB64 = b64UrlEncode(QJsonDocument(payload).toJson(QJsonDocument::Compact));
 uint8_t sig[64]{};
 crypto_ed25519_sign(sig, secretKey, reinterpret_cast<const uint8_t *>(payloadB64.constData()), size_t(payloadB64.size()));
 crypto_wipe(secretKey, sizeof(secretKey));

 const QByteArray sigB64 = b64UrlEncode(QByteArray(reinterpret_cast<const char *>(sig), 64));
 return QStringLiteral("GA2L.") + QString::fromLatin1(payloadB64) + QLatin1Char('.') + QString::fromLatin1(sigB64);
}

bool verifySignedLeaseToken(const QString &token,
                            const QByteArray &publicKey32,
                            const QString &expectedMachineHash,
                            LicenseInfo *outInfo,
                            QString *errorOut,
                            qint64 nowUtcOverride) {
 if (publicKey32.size() != 32) {
  if (errorOut) *errorOut = QStringLiteral("Invalid public key length");
  return false;
 }
 const QStringList parts = token.trimmed().split(QLatin1Char('.'));
 if (parts.size() != 3 || parts[0] != QStringLiteral("GA2L")) {
  if (errorOut) *errorOut = QStringLiteral("Malformed license token");
  return false;
 }
 const QByteArray payloadB64 = parts[1].toLatin1();
 const QByteArray sigRaw = b64UrlDecode(parts[2].toLatin1());
 if (payloadB64.isEmpty() || sigRaw.size() != 64) {
  if (errorOut) *errorOut = QStringLiteral("Invalid signature encoding");
  return false;
 }
 const int check = crypto_ed25519_check(
  reinterpret_cast<const uint8_t *>(sigRaw.constData()),
  reinterpret_cast<const uint8_t *>(publicKey32.constData()),
  reinterpret_cast<const uint8_t *>(payloadB64.constData()),
  size_t(payloadB64.size()));
 if (check != 0) {
  if (errorOut) *errorOut = QStringLiteral("Ed25519 signature verification failed");
  return false;
 }

 QJsonParseError pe;
 const QJsonDocument doc = QJsonDocument::fromJson(b64UrlDecode(payloadB64), &pe);
 if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
  if (errorOut) *errorOut = QStringLiteral("Invalid license payload JSON");
  return false;
 }
 const QJsonObject obj = doc.object();
 if (obj.value("v").toInt() != 1 || obj.value("major").toInt() != 2) {
  if (errorOut) *errorOut = QStringLiteral("Unsupported license major version");
  return false;
 }
 const QString tokenMachine = obj.value("machine").toString().toLower();
 if (!expectedMachineHash.isEmpty() && tokenMachine != expectedMachineHash.toLower()) {
  if (errorOut) *errorOut = QStringLiteral("License is bound to a different machine");
  return false;
 }

 LicenseInfo parsed;
 parsed.keyMasked = obj.value("key").toString();
 parsed.tier = obj.value("tier").toString(QStringLiteral("pro_2x"));
 parsed.buyerEmail = obj.value("email").toString();
 parsed.majorVersion = obj.value("major").toInt(2);
 parsed.maxSeats = std::max(1, obj.value("seats").toInt(1));
 parsed.usedSeats = std::max(1, obj.value("used").toInt(1));
 parsed.issuedUtc = qint64(obj.value("issued").toDouble(0));
 parsed.expiresUtc = qint64(obj.value("exp").toDouble(0));

 const qint64 nowUtc = nowUtcOverride > 0 ? nowUtcOverride : QDateTime::currentSecsSinceEpoch();
 if (parsed.expiresUtc <= 0 || nowUtc > parsed.expiresUtc) {
  parsed.state = LicenseState::LeaseExpired;
  if (outInfo) *outInfo = parsed;
  if (errorOut) *errorOut = QStringLiteral("Offline lease expired; connect to internet to refresh");
  return false;
 }
 parsed.state = LicenseState::ProActive;
 if (outInfo) *outInfo = parsed;
 return true;
}

LicenseManager::LicenseManager(QString dataDirectory, QObject *parent)
 : QObject(parent),
   dataDirectory_(std::move(dataDirectory)),
   serverUrl_(QString::fromLatin1(kDefaultLicenseServer)),
   machineHash_(machineFingerprint()),
   publicKey_(defaultLicensePublicKey()) {
 // Allow optional clean server override from dataDirectory/license_server.json (for self-hosted Cloudflare Worker)
 for (const QString &cfgPath : {
       dataDirectory_ + "/license_server.json",
       ga::appDataPath("license_server.json")
      }) {
  QFile f(cfgPath);
  if (f.open(QIODevice::ReadOnly)) {
   const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
   const QString u = obj.value("api_url").toString().trimmed();
   if (isAllowedServerUrl(u)) {
    serverUrl_ = u;
    while (serverUrl_.endsWith(QLatin1Char('/'))) serverUrl_.chop(1);
    break;
   }
  }
 }
}

LicenseManager::~LicenseManager() {
 if (activeOp_) {
  disconnect(activeOp_, nullptr, this, nullptr);
  activeOp_->abort();
 }
}

QString LicenseManager::storagePath() const {
 return dataDirectory_ + "/license.dat";
}

QString LicenseManager::buyUrl(int seats) const {
 QUrl u(serverUrl_ + "/");
 QUrlQuery q;
 q.addQueryItem(QStringLiteral("m"), machineHash_);
 if (seats > 1) q.addQueryItem(QStringLiteral("seats"), QString::number(seats));
 u.setQuery(q);
 return u.toString();
}

void LicenseManager::setServerUrl(const QString &url) {
 if (!isAllowedServerUrl(url)) return;
 serverUrl_ = url.trimmed();
 while (serverUrl_.endsWith(QLatin1Char('/'))) serverUrl_.chop(1);
}

void LicenseManager::setPublicKey(const QByteArray &publicKey32) {
 if (publicKey32.size() == 32) publicKey_ = publicKey32;
}

void LicenseManager::setMachineHashForTesting(const QString &hash64) {
 if (hash64.size() == 64) machineHash_ = hash64.toLower();
}

void LicenseManager::setBusy(bool b) {
 if (busy_ == b) return;
 busy_ = b;
 emit busyChanged(busy_);
}

QJsonObject LicenseManager::diagnostics() const {
 return {
  {"state", info_.state == LicenseState::ProActive ? "pro" : info_.state == LicenseState::LeaseExpired ? "expired" : info_.state == LicenseState::ClockTampered ? "clock_tampered" : "free"},
  {"pro", info_.isPro()},
  {"key_masked", info_.keyMasked},
  {"tier", info_.tier},
  {"max_seats", info_.maxSeats},
  {"used_seats", info_.usedSeats},
  {"offline_days_remaining", info_.offlineDaysRemaining()},
  {"machine_id_short", machineHash_.left(12)},
  {"welcome_offer_active", offer_.welcomeOfferActive},
  {"batch_remaining", offer_.remainingInBatch},
  {"latest_version", offer_.latestVersion},
  {"update_available", offer_.isUpdateAvailable(QCoreApplication::applicationVersion())},
 };
}

void LicenseManager::loadLocal() {
 info_ = {};
 rawToken_.clear();
 maxSeenUtc_ = 0;

 const qint64 nowUtc = QDateTime::currentSecsSinceEpoch();
 QFile f(storagePath());
 if (!f.open(QIODevice::ReadOnly)) {
  emit statusChanged();
  return;
 }
 const QByteArray plain = unprotectLocalData(f.readAll());
 if (plain.isEmpty()) {
  emit statusChanged();
  return;
 }
 const QJsonObject obj = QJsonDocument::fromJson(plain).object();
 rawToken_ = obj.value("token").toString();
 maxSeenUtc_ = qint64(obj.value("max_seen_utc").toDouble(0));

 if (rawToken_.isEmpty()) {
  emit statusChanged();
  return;
 }

 // Anti-clock-rollback guard: reject offline lease if OS clock moved backwards >24h
 if (maxSeenUtc_ > 0 && nowUtc + kClockRollbackToleranceSec < maxSeenUtc_) {
  LicenseInfo parsed;
  verifySignedLeaseToken(rawToken_, publicKey_, machineHash_, &parsed, nullptr, maxSeenUtc_);
  parsed.state = LicenseState::ClockTampered;
  info_ = parsed;
  emit statusChanged();
  return;
 }

 LicenseInfo parsed;
 QString err;
 if (verifySignedLeaseToken(rawToken_, publicKey_, machineHash_, &parsed, &err, nowUtc)) {
  info_ = parsed;
  maxSeenUtc_ = std::max(maxSeenUtc_, nowUtc);
  saveLocalBlob();
 } else {
  // Keep masked info if merely expired so UI can show "Lease expired -> click Refresh"
  info_ = parsed;
 }
 emit statusChanged();
}

void LicenseManager::saveLocalBlob() {
 if (dataDirectory_.isEmpty()) return;
 QDir().mkpath(dataDirectory_);
 const qint64 nowUtc = QDateTime::currentSecsSinceEpoch();
 maxSeenUtc_ = std::max(maxSeenUtc_, nowUtc);
 const QJsonObject obj{
  {"token", rawToken_},
  {"max_seen_utc", double(maxSeenUtc_)},
 };
 const QByteArray blob = protectLocalData(QJsonDocument(obj).toJson(QJsonDocument::Compact));
 QSaveFile sf(storagePath());
 if (sf.open(QIODevice::WriteOnly)) {
  sf.write(blob);
  sf.commit();
 }
}

bool LicenseManager::installSignedLease(const QString &token, QString *errorOut, qint64 nowUtcOverride) {
 LicenseInfo parsed;
 QString err;
 const qint64 nowUtc = nowUtcOverride > 0 ? nowUtcOverride : QDateTime::currentSecsSinceEpoch();
 if (!verifySignedLeaseToken(token, publicKey_, machineHash_, &parsed, &err, nowUtc)) {
  if (errorOut) *errorOut = err;
  return false;
 }
 rawToken_ = token.trimmed();
 info_ = parsed;
 maxSeenUtc_ = std::max(nowUtc, parsed.issuedUtc);
 saveLocalBlob();
 emit statusChanged();
 return true;
}

void LicenseManager::clearLocalLease() {
 rawToken_.clear();
 info_ = {};
 QFile::remove(storagePath());
 emit statusChanged();
}

void LicenseManager::bootstrap() {
 QNetworkRequest req(QUrl(serverUrl_ + "/v1/bootstrap"));
 req.setTransferTimeout(8000);
 req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
 req.setRawHeader("User-Agent", "GroundAtlasDesktop/2.3");
 const QByteArray body = QJsonDocument(QJsonObject{
  {"machine_hash", machineHash_},
  {"app_version", QCoreApplication::applicationVersion()}
 }).toJson(QJsonDocument::Compact);

 QNetworkReply *reply = network_.post(req, body);
 connect(reply, &QNetworkReply::finished, this, [this, reply] {
  reply->deleteLater();
  if (reply->error() != QNetworkReply::NoError) return;
  const QJsonObject d = QJsonDocument::fromJson(reply->readAll()).object();
  if (d.isEmpty()) return;

  offer_.loaded = true;
  offer_.batchId = d.value("batch_id").toString(offer_.batchId);
  offer_.batchName = d.value("batch_name").toString(offer_.batchName);
  offer_.seatQuota = d.value("seat_quota").toInt(offer_.seatQuota);
  offer_.soldInBatch = d.value("sold_in_batch").toInt(offer_.soldInBatch);
  offer_.remainingInBatch = d.value("remaining_in_batch").toInt(offer_.remainingInBatch);
  offer_.totalSeatsSold = d.value("total_seats_sold").toInt(offer_.totalSeatsSold);
  offer_.welcomeOfferActive = d.value("welcome_offer_active").toBool(false);
  const qint64 remSec = qint64(d.value("welcome_offer_remaining_sec").toDouble(0));
  offer_.welcomeOfferExpiresUtc = offer_.welcomeOfferActive ? (QDateTime::currentSecsSinceEpoch() + remSec) : 0;
  offer_.latestVersion = d.value("latest_version").toString();
  offer_.downloadUrl = d.value("download_url").toString();
  offer_.updateNotes = d.value("update_notes").toString();

  for (const auto &pv : d.value("packages").toArray()) {
   const auto po = pv.toObject();
   const int s = po.value("seats").toInt();
   const int pr = po.value("price_vnd").toInt();
   if (s == 1 && pr > 0) offer_.price1SeatVnd = pr;
   else if (s == 3 && pr > 0) offer_.price3SeatsVnd = pr;
   else if (s == 5 && pr > 0) offer_.price5SeatsVnd = pr;
   else if (s == 10 && pr > 0) offer_.price10SeatsVnd = pr;
  }
  emit offerUpdated();

  // Silent background refresh if lease is expired, tampered, or older than 3 days (skip self-contained admin/offline Ed25519 leases)
  const bool isOfflineAdmin = (info_.tier == QStringLiteral("admin_2x") || info_.tier == QStringLiteral("offline_2x"));
  const qint64 nowUtc = QDateTime::currentSecsSinceEpoch();
  if (!rawToken_.isEmpty() && !isOfflineAdmin && (info_.state != LicenseState::ProActive || (nowUtc - info_.issuedUtc) > 3 * 86400)) {
   refresh();
  }
 });
}

void LicenseManager::activate(const QString &licenseKey) {
 const QString rawTrimmed = licenseKey.trimmed();
 // Direct Offline Ed25519 Signed Key (GA2L.<b64url-payload>.<b64url-sig>, case-sensitive, bound to machine_hash)
 if (rawTrimmed.startsWith(QStringLiteral("GA2L."))) {
  QString verifyErr;
  if (!installSignedLease(rawTrimmed, &verifyErr)) {
   emit operationFailed(QStringLiteral("Mã bản quyền offline không hợp lệ, đã hết hạn hoặc không khớp mã máy này (") + verifyErr + QStringLiteral(")."));
   return;
  }
  emit operationSucceeded(QStringLiteral("Đã kích hoạt thành công GroundAtlas Pro 2.x (Khóa ký Ed25519 trực tiếp trên máy)!"));
  return;
 }

 const QString key = rawTrimmed.toUpper();
 if (key.size() < 10) {
  emit operationFailed(QStringLiteral("Vui lòng nhập đầy đủ mã bản quyền (ví dụ: GA2X-XXXX-XXXX-XXXX)."));
  return;
 }
 if (activeOp_) activeOp_->abort();
 setBusy(true);

 QNetworkRequest req(QUrl(serverUrl_ + "/v1/activate"));
 req.setTransferTimeout(15000);
 req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
 req.setRawHeader("User-Agent", "GroundAtlasDesktop/2.3");
 const QByteArray body = QJsonDocument(QJsonObject{
  {"license_key", key},
  {"machine_hash", machineHash_},
  {"machine_name", machineDisplayName()},
  {"app_version", QCoreApplication::applicationVersion()}
 }).toJson(QJsonDocument::Compact);

 QNetworkReply *reply = network_.post(req, body);
 activeOp_ = reply;
 connect(reply, &QNetworkReply::finished, this, [this, reply] {
  reply->deleteLater();
  if (activeOp_ == reply) activeOp_ = nullptr;
  setBusy(false);

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QJsonObject d = QJsonDocument::fromJson(reply->readAll()).object();
  if (reply->error() != QNetworkReply::NoError || status != 200) {
   const QString msg = d.value("message").toString();
   emit operationFailed(!msg.isEmpty() ? msg : (status ? QString("Máy chủ bản quyền trả về lỗi HTTP %1.").arg(status)
                                                       : QString("Không kết nối được máy chủ bản quyền (%1).").arg(reply->errorString())));
   return;
  }

  QString verifyErr;
  if (!installSignedLease(d.value("lease").toString(), &verifyErr)) {
   emit operationFailed(QString("Chữ ký bản quyền không hợp lệ: ") + verifyErr);
   return;
  }
  emit operationSucceeded(QString("Đã kích hoạt thành công GroundAtlas Pro 2.x (%1/%2 máy)!")
   .arg(info_.usedSeats).arg(info_.maxSeats));
 });
}

void LicenseManager::refresh() {
 if (rawToken_.isEmpty() || busy_) return;
 if (info_.tier == QStringLiteral("admin_2x") || info_.tier == QStringLiteral("offline_2x")) {
  emit operationSucceeded(QStringLiteral("Bản quyền Admin / Offline (ký Ed25519) đang hợp lệ trên máy này, không cần gia hạn máy chủ."));
  return;
 }
 setBusy(true);

 QNetworkRequest req(QUrl(serverUrl_ + "/v1/refresh"));
 req.setTransferTimeout(12000);
 req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
 req.setRawHeader("User-Agent", "GroundAtlasDesktop/2.3");
 const QByteArray body = QJsonDocument(QJsonObject{
  {"machine_hash", machineHash_},
  {"key_masked", info_.keyMasked},
  {"app_version", QCoreApplication::applicationVersion()}
 }).toJson(QJsonDocument::Compact);

 QNetworkReply *reply = network_.post(req, body);
 activeOp_ = reply;
 connect(reply, &QNetworkReply::finished, this, [this, reply] {
  reply->deleteLater();
  if (activeOp_ == reply) activeOp_ = nullptr;
  setBusy(false);

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QJsonObject d = QJsonDocument::fromJson(reply->readAll()).object();
  if (status == 403 || status == 404) {
   clearLocalLease();
   emit operationFailed(d.value("message").toString(QStringLiteral("Bản quyền trên máy này đã bị gỡ hoặc thu hồi.")));
   return;
  }
  if (reply->error() != QNetworkReply::NoError || status != 200) return;

  QString err;
  if (installSignedLease(d.value("lease").toString(), &err)) {
   emit operationSucceeded(QStringLiteral("Đã làm mới bản quyền (offline thêm 14 ngày)."));
  }
 });
}

void LicenseManager::deactivate() {
 if (rawToken_.isEmpty()) return;
 if (info_.tier == QStringLiteral("admin_2x") || info_.tier == QStringLiteral("offline_2x")) {
  clearLocalLease();
  emit operationSucceeded(QStringLiteral("Đã gỡ khóa Admin / Offline khỏi máy này."));
  return;
 }
 if (activeOp_) activeOp_->abort();
 setBusy(true);

 QNetworkRequest req(QUrl(serverUrl_ + "/v1/deactivate"));
 req.setTransferTimeout(15000);
 req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
 req.setRawHeader("User-Agent", "GroundAtlasDesktop/2.3");
 const QByteArray body = QJsonDocument(QJsonObject{
  {"machine_hash", machineHash_},
  {"key_masked", info_.keyMasked}
 }).toJson(QJsonDocument::Compact);

 QNetworkReply *reply = network_.post(req, body);
 activeOp_ = reply;
 connect(reply, &QNetworkReply::finished, this, [this, reply] {
  reply->deleteLater();
  if (activeOp_ == reply) activeOp_ = nullptr;
  setBusy(false);

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QJsonObject d = QJsonDocument::fromJson(reply->readAll()).object();
  if (reply->error() != QNetworkReply::NoError || status != 200) {
   const QString msg = d.value("message").toString();
   emit operationFailed(!msg.isEmpty() ? msg : QStringLiteral("Chưa gỡ được seat trên máy chủ. Kiểm tra kết nối mạng."));
   return;
  }
  clearLocalLease();
  emit operationSucceeded(d.value("message").toString(QStringLiteral("Đã gỡ máy này và trả lại 1 seat.")));
 });
}

} // namespace ga
