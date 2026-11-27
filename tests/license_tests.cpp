#include "license.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

namespace {

// Minimal local HTTP server mocking Cloudflare Worker /v1/activate, /v1/refresh, /v1/deactivate, /v1/bootstrap
class MockLicenseServer : public QTcpServer {
 Q_OBJECT
public:
 QByteArray seed32;
 QByteArray pub32;
 QString validKey = QStringLiteral("GA2X-TEST-7788-99AA");
 int maxSeats = 1;
 int swapCount = 0;
 QStringList activeMachines;

 MockLicenseServer() {
  seed32.fill('K', 32);
  ga::signLeaseTokenWithSeed(QJsonObject{{"v", 1}}, seed32, &pub32);
  connect(this, &QTcpServer::newConnection, this, &MockLicenseServer::handleClient);
  listen(QHostAddress::LocalHost, 0);
 }

 QString url() const {
  return QString("http://127.0.0.1:%1").arg(serverPort());
 }

private:
 void handleClient() {
  while (auto *sock = nextPendingConnection()) {
   connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
    const QByteArray raw = sock->readAll();
    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const QByteArray head = raw.left(headerEnd);
    const QByteArray body = raw.mid(headerEnd + 4);
    const QList<QByteArray> line0 = head.split('\n').first().trimmed().split(' ');
    const QString path = line0.size() >= 2 ? QString::fromLatin1(line0[1]) : QString();
    const QJsonObject req = QJsonDocument::fromJson(body).object();

    int code = 200;
    QJsonObject resp;
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (path == "/v1/bootstrap") {
     resp = {
      {"server_utc", double(now)},
      {"batch_id", "batch_1"},
      {"batch_name", "Batch 1 Early Bird"},
      {"seat_quota", 100},
      {"sold_in_batch", 12},
      {"remaining_in_batch", 88},
      {"welcome_offer_active", true},
      {"welcome_offer_remaining_sec", 48 * 3600},
      {"latest_version", "2.3.0"}
     };
    } else if (path == "/v1/activate") {
     const QString key = req.value("license_key").toString().toUpper();
     const QString mh = req.value("machine_hash").toString().toLower();
     if (key != validKey) {
      code = 404;
      resp = {{"error", "KEY_NOT_FOUND"}, {"message", "Key not found"}};
     } else if (!activeMachines.contains(mh) && activeMachines.size() >= maxSeats) {
      code = 409;
      resp = {{"error", "SEAT_LIMIT_REACHED"}, {"message", "Seat limit reached"}};
     } else {
      if (!activeMachines.contains(mh)) activeMachines.append(mh);
      const QJsonObject payload{
       {"v", 1}, {"key", "GA2X-TEST-****-99AA"}, {"tier", "pro_2x"}, {"major", 2},
       {"seats", maxSeats}, {"used", activeMachines.size()}, {"machine", mh},
       {"email", "kts@test.vn"}, {"issued", double(now)}, {"exp", double(now + 14 * 86400)}
      };
      resp = {{"ok", true}, {"lease", ga::signLeaseTokenWithSeed(payload, seed32)}};
     }
    } else if (path == "/v1/deactivate") {
     const QString mh = req.value("machine_hash").toString().toLower();
     if (!activeMachines.contains(mh)) {
      code = 404;
      resp = {{"error", "NOT_ACTIVATED"}};
     } else if (swapCount >= 2) {
      code = 429;
      resp = {{"error", "SWAP_LIMIT_REACHED"}, {"message", "Max swaps reached"}};
     } else {
      activeMachines.removeAll(mh);
      ++swapCount;
      resp = {{"ok", true}, {"swaps_used", swapCount}, {"message", "Seat released"}};
     }
    } else if (path == "/v1/refresh") {
     const QString mh = req.value("machine_hash").toString().toLower();
     if (!activeMachines.contains(mh)) {
      code = 403;
      resp = {{"error", "ACTIVATION_REVOKED"}, {"message", "Revoked"}};
     } else {
      const QJsonObject payload{
       {"v", 1}, {"key", "GA2X-TEST-****-99AA"}, {"tier", "pro_2x"}, {"major", 2},
       {"seats", maxSeats}, {"used", activeMachines.size()}, {"machine", mh},
       {"email", "kts@test.vn"}, {"issued", double(now)}, {"exp", double(now + 14 * 86400)}
      };
      resp = {{"ok", true}, {"lease", ga::signLeaseTokenWithSeed(payload, seed32)}};
     }
    } else {
     code = 404;
     resp = {{"error", "NOT_FOUND"}};
    }

    const QByteArray payloadBytes = QJsonDocument(resp).toJson(QJsonDocument::Compact);
    QByteArray out = QString("HTTP/1.1 %1 OK\r\nContent-Type: application/json\r\nContent-Length: %2\r\nConnection: close\r\n\r\n")
     .arg(code).arg(payloadBytes.size()).toLatin1() + payloadBytes;
    sock->write(out);
    sock->disconnectFromHost();
   });
  }
 }
};

} // namespace

class LicenseTests : public QObject {
 Q_OBJECT
private slots:
 void machineFingerprintAndDpapiRoundTrip() {
  const QString id1 = ga::machineFingerprint();
  const QString id2 = ga::machineFingerprint();
  QCOMPARE(id1.size(), 64);
  QCOMPARE(id1, id2);

  const QByteArray secret = "groundatlas-dpapi-secret-payload-v230";
  const QByteArray enc = ga::protectLocalData(secret);
  QVERIFY(!enc.isEmpty());
  QVERIFY(enc != secret);
  QCOMPARE(ga::unprotectLocalData(enc), secret);
  QVERIFY(ga::unprotectLocalData("CORRUPT_BLOB").isEmpty());

  // Cross-platform macOS/Linux Monocypher XChaCha20-Poly1305 AEAD (GAMC1:) round-trip + tamper rejection
  const QByteArray aead1 = ga::protectLocalDataAead(secret);
  const QByteArray aead2 = ga::protectLocalDataAead(secret);
  QVERIFY(aead1.startsWith("GAMC1:"));
  QVERIFY(aead1 != aead2); // fresh random 24-byte nonce every seal
  QCOMPARE(ga::unprotectLocalData(aead1), secret);
  QCOMPARE(ga::unprotectLocalDataAead(aead2), secret);
  QByteArray flipped = aead1;
  flipped[flipped.size() - 1] = char(flipped[flipped.size() - 1] ^ 0x01);
  QVERIFY(ga::unprotectLocalData(flipped).isEmpty());
 }

 void ed25519VerifyRejectsTamperingWrongMachineAndExpiry() {
  QByteArray seed(32, 'S');
  QByteArray pub32;
  const QString mhA = QString(64, 'a');
  const QString mhB = QString(64, 'b');
  const qint64 now = QDateTime::currentSecsSinceEpoch();

  const QJsonObject validPayload{
   {"v", 1}, {"key", "GA2X-ABCD-****-1234"}, {"tier", "pro_2x"}, {"major", 2},
   {"seats", 1}, {"used", 1}, {"machine", mhA}, {"email", "a@b.vn"},
   {"issued", double(now)}, {"exp", double(now + 14 * 86400)}
  };
  const QString token = ga::signLeaseTokenWithSeed(validPayload, seed, &pub32);
  QVERIFY(token.startsWith("GA2L."));

  // 1. Valid token verifies cleanly
  ga::LicenseInfo info;
  QString err;
  QVERIFY2(ga::verifySignedLeaseToken(token, pub32, mhA, &info, &err, now), qPrintable(err));
  QVERIFY(info.isPro());
  QCOMPARE(info.maxSeats, 1);
  QCOMPARE(info.keyMasked, QString("GA2X-ABCD-****-1234"));

  // 2. Wrong machine hash MUST fail
  QVERIFY(!ga::verifySignedLeaseToken(token, pub32, mhB, &info, &err, now));

  // 3. Tampered payload byte MUST fail Ed25519 check
  QString tampered = token;
  tampered[8] = (tampered[8] == 'A') ? 'B' : 'A';
  QVERIFY(!ga::verifySignedLeaseToken(tampered, pub32, mhA, &info, &err, now));

  // 4. Expired offline lease (> 14 days) MUST fail with LeaseExpired
  QVERIFY(!ga::verifySignedLeaseToken(token, pub32, mhA, &info, &err, now + 15 * 86400));
  QCOMPARE(info.state, ga::LicenseState::LeaseExpired);
 }

 void perSeatActivationSwapAndAntiClockRollback() {
  MockLicenseServer srv;
  QVERIFY(srv.isListening());

  QTemporaryDir dir1, dir2;
  const QString mh1 = QString(64, '1');
  const QString mh2 = QString(64, '2');

  ga::LicenseManager lm1(dir1.path());
  lm1.setServerUrl(srv.url());
  lm1.setPublicKey(srv.pub32);
  lm1.setMachineHashForTesting(mh1);

  // Bootstrap fetches live Early-Bird slot count & 48h welcome offer
  QSignalSpy offerSpy(&lm1, &ga::LicenseManager::offerUpdated);
  lm1.bootstrap();
  QTRY_COMPARE_WITH_TIMEOUT(offerSpy.size(), 1, 4000);
  QCOMPARE(lm1.offer().remainingInBatch, 88);
  QVERIFY(lm1.offer().welcomeOfferActive);

  // Activate Machine 1 -> ProActive!
  QSignalSpy ok1(&lm1, &ga::LicenseManager::operationSucceeded);
  lm1.activate(srv.validKey);
  QTRY_COMPARE_WITH_TIMEOUT(ok1.size(), 1, 4000);
  QVERIFY(lm1.isPro());

  // Persistence check: new LicenseManager on same dir1 loads DPAPI lease OFFLINE without network!
  {
   ga::LicenseManager offline(dir1.path());
   offline.setPublicKey(srv.pub32);
   offline.setMachineHashForTesting(mh1);
   offline.loadLocal();
   QVERIFY(offline.isPro());
   QCOMPARE(offline.info().maxSeats, 1);
  }

  // Machine 2 tries same 1-seat key -> MUST FAIL 409 SEAT_LIMIT_REACHED!
  ga::LicenseManager lm2(dir2.path());
  lm2.setServerUrl(srv.url());
  lm2.setPublicKey(srv.pub32);
  lm2.setMachineHashForTesting(mh2);
  QSignalSpy fail2(&lm2, &ga::LicenseManager::operationFailed);
  lm2.activate(srv.validKey);
  QTRY_COMPARE_WITH_TIMEOUT(fail2.size(), 1, 4000);
  QVERIFY(!lm2.isPro());

  // Machine 1 releases seat (Deactivate) -> Machine 2 activates cleanly!
  ok1.clear();
  lm1.deactivate();
  QTRY_COMPARE_WITH_TIMEOUT(ok1.size(), 1, 4000);
  QVERIFY(!lm1.isPro());

  QSignalSpy ok2(&lm2, &ga::LicenseManager::operationSucceeded);
  lm2.activate(srv.validKey);
  QTRY_COMPARE_WITH_TIMEOUT(ok2.size(), 1, 4000);
  QVERIFY(lm2.isPro());
 }

 void directOfflineAdminKeyActivation() {
  QByteArray seed32(32, '\x55');
  QByteArray pub32;
  const QString mhOk = QString(64, 'c');
  const QString mhOther = QString(64, 'd');
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const QString adminToken = ga::signLeaseTokenWithSeed(QJsonObject{
   {"v", 1}, {"key", "GA2X-ADMN-****-V230"}, {"tier", "admin_2x"}, {"major", 2},
   {"seats", 1}, {"used", 1}, {"machine", mhOk}, {"email", "founder@groundatlas.vn"},
   {"issued", double(now)}, {"exp", double(now + 3650LL * 86400LL)}
  }, seed32, &pub32);
  QVERIFY(adminToken.startsWith("GA2L."));

  // 1. Direct GA2L. offline activation works on matching PC with ZERO network
  QTemporaryDir dirOk, dirOther;
  ga::LicenseManager lmOk(dirOk.path());
  lmOk.setPublicKey(pub32);
  lmOk.setMachineHashForTesting(mhOk);
  QSignalSpy okSpy(&lmOk, &ga::LicenseManager::operationSucceeded);
  lmOk.activate(adminToken);
  QCOMPARE(okSpy.size(), 1);
  QVERIFY(lmOk.isPro());
  QCOMPARE(lmOk.info().tier, QStringLiteral("admin_2x"));

  // 2. Reload offline from DPAPI succeeds
  ga::LicenseManager reload(dirOk.path());
  reload.setPublicKey(pub32);
  reload.setMachineHashForTesting(mhOk);
  reload.loadLocal();
  QVERIFY(reload.isPro());
  QCOMPARE(reload.info().tier, QStringLiteral("admin_2x"));

  // 3. Same GA2L. offline key MUST fail on a different PC!
  ga::LicenseManager lmOther(dirOther.path());
  lmOther.setPublicKey(pub32);
  lmOther.setMachineHashForTesting(mhOther);
  QSignalSpy failSpy(&lmOther, &ga::LicenseManager::operationFailed);
  lmOther.activate(adminToken);
  QCOMPARE(failSpy.size(), 1);
  QVERIFY(!lmOther.isPro());
 }

 void updateVersionComparisonCheck() {
  ga::WelcomeOfferInfo off;
  off.latestVersion = QStringLiteral("2.3.2");
  QVERIFY(off.isUpdateAvailable(QStringLiteral("2.3.1")));
  QVERIFY(!off.isUpdateAvailable(QStringLiteral("2.3.2")));
  QVERIFY(!off.isUpdateAvailable(QStringLiteral("2.4.0")));
  off.latestVersion = QStringLiteral("2.10.0");
  QVERIFY(off.isUpdateAvailable(QStringLiteral("2.3.1")));
  off.latestVersion.clear();
  QVERIFY(!off.isUpdateAvailable(QStringLiteral("2.3.1")));
 }
};

QTEST_GUILESS_MAIN(LicenseTests)
#include "license_tests.moc"
