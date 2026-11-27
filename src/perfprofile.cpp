#include "perfprofile.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QScreen>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <cstdint>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace ga {

namespace {
BootSafetyState g_bootSafety{};

int detectSystemRamGb() {
#ifdef Q_OS_WIN
 MEMORYSTATUSEX st{};
 st.dwLength = sizeof(st);
 if (GlobalMemoryStatusEx(&st)) {
  return std::max(1, int(std::round(double(st.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0))));
 }
#elif defined(Q_OS_MACOS)
 uint64_t memBytes = 0;
 size_t len = sizeof(memBytes);
 if (sysctlbyname("hw.memsize", &memBytes, &len, nullptr, 0) == 0 && memBytes > 0) {
  return std::max(1, int(std::round(double(memBytes) / (1024.0 * 1024.0 * 1024.0))));
 }
#elif defined(Q_OS_LINUX)
 const long pages = sysconf(_SC_PHYS_PAGES);
 const long pageSz = sysconf(_SC_PAGE_SIZE);
 if (pages > 0 && pageSz > 0) {
  return std::max(1, int(std::round((double(pages) * double(pageSz)) / (1024.0 * 1024.0 * 1024.0))));
 }
#endif
 return 8;
}

int detectRefreshRate(const QWidget *window) {
 QScreen *scr = nullptr;
 if (window && window->windowHandle() && window->windowHandle()->screen()) {
  scr = window->windowHandle()->screen();
 }
 if (!scr && window) scr = window->screen();
 if (!scr) scr = QGuiApplication::primaryScreen();
 if (!scr) return 60;
 const double hz = scr->refreshRate();
 if (!std::isfinite(hz) || hz < 30.0) return 60;
 return std::clamp(int(std::round(hz)), 60, 500);
}

double detectDpiScale(const QWidget *window) {
 if (window && window->devicePixelRatioF() > 0.5) return window->devicePixelRatioF();
 if (auto *scr = QGuiApplication::primaryScreen()) {
  if (scr->devicePixelRatio() > 0.5) return scr->devicePixelRatio();
 }
 return 1.0;
}

PerfTier classifyHardware(const QString &gpu, int threads, int ramGb, int maxTex, int maxSamples) {
 const QString g = gpu.toLower();
 if (g.contains("llvmpipe") || g.contains("swiftshader") || g.contains("microsoft basic") ||
     g.contains("gdi generic") || g.contains("software") || g.contains("vmware") ||
     g.contains("virtualbox") || maxTex < 2048 || maxSamples == 0) {
  return PerfTier::Low;
 }
 const bool highGpu = g.contains("rtx") || g.contains("gtx 16") || g.contains("gtx 107") || g.contains("gtx 108") ||
                      g.contains("rx 6") || g.contains("rx 7") || g.contains("rx 8") || g.contains("rx 9") ||
                      g.contains("radeon pro") || g.contains("quadro") || g.contains("arc a") || g.contains("arc b") ||
                      g.contains("apple m") || g.contains("apple gpu");
 const bool integratedGpu = g.contains("uhd") || g.contains("hd graphics") || g.contains("iris") ||
                            g.contains("radeon(tm) graphics") || g.contains("amd radeon graphics") ||
                            g.contains("vega") || g.contains("intel");
 if ((highGpu || (threads >= 12 && ramGb >= 16 && !integratedGpu)) && maxTex >= 4096) {
  return PerfTier::Ultra;
 }
 if (threads <= 4 || ramGb <= 6 || g.contains("hd graphics 4") || g.contains("hd graphics 5") ||
     g.contains("hd graphics 3") || g.contains("hd graphics 2")) {
  return PerfTier::Low;
 }
 return PerfTier::Medium;
}
} // namespace

QString perfTierName(PerfTier tier, bool english) {
 switch (tier) {
 case PerfTier::Auto: return QString::fromUtf8(english ? "Auto (Match hardware)" : "Tự động theo máy (Auto)");
 case PerfTier::Low: return QString::fromUtf8(english ? "Low / Power Saver (1024px · 60 Hz)" : "Máy cấu hình thấp / Tiết kiệm (1024px · 60 Hz)");
 case PerfTier::Medium: return QString::fromUtf8(english ? "Balanced (2048px · up to 144 Hz)" : "Cân bằng (2048px · tối đa 144 Hz)");
 case PerfTier::Ultra: return QString::fromUtf8(english ? "Ultra / High-End GPU (4096px · up to 500 Hz)" : "Tối đa / Máy cấu hình cao (4096px · tối đa 500 Hz)");
 }
 return {};
}

QString compatModeName(GraphicsCompatMode mode, bool english) {
 switch (mode) {
 case GraphicsCompatMode::Auto:
  return QString::fromUtf8(english ? "Auto Adaptive + Self-Healing Watchdog (Recommended)" : "Tự động thích ứng + Tự cứu lỗi GPU (Khuyên dùng)");
 case GraphicsCompatMode::HighPerformance:
  return QString::fromUtf8(english ? "High-Performance Discrete GPU (4x MSAA · OpenGL 3.3)" : "GPU rời hiệu năng cao (MSAA 4x · OpenGL 3.3)");
 case GraphicsCompatMode::Compatible:
  return QString::fromUtf8(english ? "Compatible GPU (Intel / AMD iGPU · 0x MSAA · No SSAO)" : "Tương thích Intel / AMD tích hợp (Không MSAA · Tắt SSAO)");
 case GraphicsCompatMode::RasterSafe:
  return QString::fromUtf8(english ? "CPU Raster Basemap (Bypass MapLibre GLSL · Anti-Black-Screen)" : "Bản đồ ảnh CPU (Bỏ qua shader MapLibre · Chống đen nền)");
 case GraphicsCompatMode::SoftwareSafe:
  return QString::fromUtf8(english ? "Safe Mode · Software OpenGL (Mesa LLVMpipe · VM / RDP)" : "Chế độ An toàn · Software OpenGL (Chạy mọi máy / VM / RDP)");
 }
 return {};
}

QString compatModeId(GraphicsCompatMode mode) {
 switch (mode) {
 case GraphicsCompatMode::Auto: return QStringLiteral("auto");
 case GraphicsCompatMode::HighPerformance: return QStringLiteral("high");
 case GraphicsCompatMode::Compatible: return QStringLiteral("compatible");
 case GraphicsCompatMode::RasterSafe: return QStringLiteral("raster");
 case GraphicsCompatMode::SoftwareSafe: return QStringLiteral("software");
 }
 return QStringLiteral("auto");
}

QString HardwareInfo::summary(bool english) const {
 const QString gpu = gpuRenderer.isEmpty() ? QString::fromUtf8(english ? "Detecting GPU…" : "Đang nhận diện GPU…") : gpuRenderer.section(" · ", 0, 0);
 const int dpiPct = qRound(dpiScale * 100.0);
 return QString::fromUtf8(english
  ? "Detected: %1 · %2 CPU threads · %3 GB RAM · %4 Hz (%5% DPI) → Recommended: %6"
  : "Cấu hình nhận diện: %1 · %2 luồng CPU · %3 GB RAM · Màn hình %4 Hz (DPI %5%) → Đề xuất: %6")
  .arg(gpu).arg(cpuThreads).arg(ramGb).arg(displayHz).arg(dpiPct).arg(perfTierName(recommendedTier, english));
}

QString HardwareInfo::diagnosticReport(const QJsonObject &diag, GraphicsCompatMode activeCompat, bool english) const {
 const auto mapDiag = diag["basemap"].toObject();
 const auto sceneDiag = diag["scene_basemap"].toObject();
 const auto &boot = globalBootSafety();
 QStringList lines;
 lines << QStringLiteral("=== GroundAtlas Desktop Diagnostics ===");
 lines << QStringLiteral("App: v%1 (Qt %2) | OS: %3")
              .arg(QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("2.3.3") : QCoreApplication::applicationVersion(),
                   QString::fromLatin1(qVersion()),
                   QSysInfo::prettyProductName());
 lines << QStringLiteral("GPU: %1").arg(gpuRenderer.isEmpty() ? QStringLiteral("Unknown") : gpuRenderer);
 if (!glVersion.isEmpty()) {
  lines << QStringLiteral("OpenGL: %1 | MaxTex=%2 | MaxMSAA=%3").arg(glVersion).arg(maxTextureSize).arg(maxMsaaSamples);
 }
 lines << QStringLiteral("System: %1 threads | %2 GB RAM | Display %3 Hz | DPI %4%")
              .arg(cpuThreads).arg(ramGb).arg(displayHz).arg(qRound(dpiScale * 100.0));
 lines << QStringLiteral("Compat Mode: requested=%1 | active=%2 | boot_safe=%3 | crashes=%4")
              .arg(compatModeId(boot.requestedMode), compatModeId(activeCompat),
                   boot.useSoftwareOpenGL() ? QStringLiteral("software") : (boot.disableMsaaAndSsao() ? QStringLiteral("compat") : QStringLiteral("normal")))
              .arg(boot.consecutiveCrashes);
 lines << QStringLiteral("MapBasemap: engine=%1 | raster_fallback=%2 | gl_error=%3 | style=%4 | rendered=%5 | err=%6")
              .arg(mapDiag["renderer_engine"].toString(),
                   mapDiag["raster_fallback"].toBool() ? QStringLiteral("true") : QStringLiteral("false"))
              .arg(mapDiag["gl_error"].toInt())
              .arg(mapDiag["style_loaded"].toBool() ? QStringLiteral("ok") : QStringLiteral("no"),
                   mapDiag["fully_rendered"].toBool() ? QStringLiteral("yes") : QStringLiteral("no"),
                   mapDiag["error"].toString());
 lines << QStringLiteral("SceneBasemap: blit=%1 | gl_error=%2 | watchdog=%3 | shadow=%4 (%5px) | ssao=%6")
              .arg(sceneDiag["blit_mode"].toString(QStringLiteral("textured-quad")))
              .arg(sceneDiag["gl_error"].toInt())
              .arg(sceneDiag["watchdog_reason"].toString(),
                   sceneDiag["hardware_shadow_map"].toBool() ? QStringLiteral("on") : QStringLiteral("off"))
              .arg(sceneDiag["shadow_map_size"].toInt())
              .arg(sceneDiag["ssao_enabled"].toBool() ? QStringLiteral("on") : QStringLiteral("off"));
 if (!diag["graphics_error"].toString().isEmpty()) {
  lines << QStringLiteral("GraphicsError: %1").arg(diag["graphics_error"].toString());
 }
 if (!english) {
  lines << QStringLiteral("(Không chứa dữ liệu cá nhân hay License Key — dùng để hỗ trợ kỹ thuật GroundAtlas)");
 }
 return lines.join('\n');
}

HardwareInfo probeHardware(const QString &gpuRenderer, const QWidget *window,
                           const QString &glVersion, int maxTex, int maxSamples) {
 HardwareInfo info;
 info.gpuRenderer = gpuRenderer;
 info.glVersion = glVersion;
 info.cpuThreads = std::max(1, QThread::idealThreadCount());
 info.ramGb = detectSystemRamGb();
 info.displayHz = detectRefreshRate(window);
 info.dpiScale = detectDpiScale(window);
 info.maxTextureSize = std::max(512, maxTex);
 info.maxMsaaSamples = std::max(0, maxSamples);
 info.recommendedTier = classifyHardware(gpuRenderer, info.cpuThreads, info.ramGb, info.maxTextureSize, info.maxMsaaSamples);
 return info;
}

ResolvedGraphicsConfig resolveGraphicsConfig(PerfTier requested, const HardwareInfo &hw, GraphicsCompatMode compatMode) {
 PerfTier eff = (requested == PerfTier::Auto) ? hw.recommendedTier : requested;
 if (compatMode == GraphicsCompatMode::SoftwareSafe || compatMode == GraphicsCompatMode::Compatible) {
  eff = PerfTier::Low;
 } else if (compatMode == GraphicsCompatMode::RasterSafe && eff == PerfTier::Ultra) {
  eff = PerfTier::Medium;
 }
 ResolvedGraphicsConfig cfg;
 cfg.effectiveTier = eff;
 if (eff == PerfTier::Low) {
  cfg.shadowMapSize = std::min(1024, hw.maxTextureSize);
  cfg.pcfQuality = 0;
  cfg.architecturalCrease = (compatMode != GraphicsCompatMode::SoftwareSafe);
  cfg.ssaoEnabled = false;
  cfg.ssaoSamples = 0;
  cfg.targetFps = 60;
 } else if (eff == PerfTier::Medium) {
  cfg.shadowMapSize = std::min(2048, hw.maxTextureSize);
  cfg.pcfQuality = 1;
  cfg.architecturalCrease = true;
  cfg.ssaoEnabled = (hw.maxMsaaSamples > 0 && compatMode != GraphicsCompatMode::Compatible);
  cfg.ssaoSamples = cfg.ssaoEnabled ? 8 : 0;
  cfg.targetFps = std::clamp(hw.displayHz, 60, 144);
 } else {
  cfg.shadowMapSize = std::min(4096, hw.maxTextureSize);
  cfg.pcfQuality = 2;
  cfg.architecturalCrease = true;
  cfg.ssaoEnabled = (hw.maxMsaaSamples > 0);
  cfg.ssaoSamples = cfg.ssaoEnabled ? 16 : 0;
  cfg.targetFps = std::clamp(hw.displayHz, 60, 500);
 }
 return cfg;
}

bool softwareOpenGLDllAvailable(const QString &exeDir) {
#ifdef Q_OS_WIN
 QString dir = exeDir;
 if (dir.isEmpty()) {
  wchar_t buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
   dir = QFileInfo(QString::fromWCharArray(buf, int(n))).absolutePath();
  }
 }
 if (dir.isEmpty()) return false;
 return QFileInfo::exists(dir + QStringLiteral("/opengl32sw.dll"));
#else
 Q_UNUSED(exeDir);
 return false;
#endif
}

QString crashSentinelPath(const QString &dataDirectory) {
 return QDir(dataDirectory).filePath(QStringLiteral("gl-boot-sentinel.json"));
}

BootSafetyState beginGraphicsBoot(const QString &dataDirectory, GraphicsCompatMode savedMode,
                                  bool cliSafeMode, bool cliCompatMode, bool cliRasterMode,
                                  const QString &exeDir) {
 BootSafetyState st;
 st.requestedMode = savedMode;
 st.effectiveBootMode = savedMode;

 const bool hasSwDll = softwareOpenGLDllAvailable(exeDir);
 if (cliSafeMode) {
  st.safeModeForcedByCli = true;
  st.effectiveBootMode = hasSwDll ? GraphicsCompatMode::SoftwareSafe : GraphicsCompatMode::RasterSafe;
  st.reason = QStringLiteral("cli-safe-mode");
 } else if (cliRasterMode) {
  st.safeModeForcedByCli = true;
  st.effectiveBootMode = GraphicsCompatMode::RasterSafe;
  st.reason = QStringLiteral("cli-raster-mode");
 } else if (cliCompatMode) {
  st.safeModeForcedByCli = true;
  st.effectiveBootMode = GraphicsCompatMode::Compatible;
  st.reason = QStringLiteral("cli-compat-mode");
 } else if (savedMode == GraphicsCompatMode::SoftwareSafe && !hasSwDll) {
  st.effectiveBootMode = GraphicsCompatMode::RasterSafe;
  st.reason = QStringLiteral("software-dll-missing-fallback-raster");
 }

 // Two-strike Boot Crash Sentinel: detects hard GPU driver crashes during previous OpenGL startup
 if (shouldPersistPreferences(dataDirectory) && !dataDirectory.isEmpty()) {
  QDir().mkpath(dataDirectory);
  const QString path = crashSentinelPath(dataDirectory);
  int crashes = 0;
  QFile in(path);
  if (in.open(QIODevice::ReadOnly)) {
   const auto obj = QJsonDocument::fromJson(in.readAll()).object();
   if (obj["in_progress"].toBool()) {
    crashes = obj["crashes"].toInt() + 1;
   }
  }
  st.consecutiveCrashes = crashes;
  if (!st.safeModeForcedByCli && crashes > 0) {
   st.safeModeTriggeredByCrash = true;
   if (crashes >= 2 && hasSwDll) {
    st.effectiveBootMode = GraphicsCompatMode::SoftwareSafe;
    st.reason = QStringLiteral("crash-sentinel-software");
   } else if (crashes >= 2) {
    st.effectiveBootMode = GraphicsCompatMode::RasterSafe;
    st.reason = QStringLiteral("crash-sentinel-raster");
   } else if (st.effectiveBootMode == GraphicsCompatMode::Auto ||
              st.effectiveBootMode == GraphicsCompatMode::HighPerformance) {
    st.effectiveBootMode = GraphicsCompatMode::Compatible;
    st.reason = QStringLiteral("crash-sentinel-compatible");
   }
  }
  QFile out(path);
  if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
   const QJsonObject next{
    {"in_progress", true},
    {"crashes", crashes},
    {"mode", compatModeId(st.effectiveBootMode)},
    {"ts", QDateTime::currentSecsSinceEpoch()},
   };
   out.write(QJsonDocument(next).toJson(QJsonDocument::Compact));
   out.flush();
  }
 }
 setGlobalBootSafety(st);
 return st;
}

void markGraphicsBootHealthy(const QString &dataDirectory) {
 if (!shouldPersistPreferences(dataDirectory) || dataDirectory.isEmpty()) return;
 QFile out(crashSentinelPath(dataDirectory));
 if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
  const QJsonObject ok{
   {"in_progress", false},
   {"crashes", 0},
   {"ts", QDateTime::currentSecsSinceEpoch()},
  };
  out.write(QJsonDocument(ok).toJson(QJsonDocument::Compact));
  out.flush();
 }
}

void setGlobalBootSafety(const BootSafetyState &state) { g_bootSafety = state; }
const BootSafetyState &globalBootSafety() { return g_bootSafety; }

bool shouldPersistPreferences(const QString &dataDirectory) {
 const QString d = dataDirectory.toLower();
 if (d.contains("ui-tests-") || d.contains("presentation-ui-") || d.contains("mw-data") ||
     d.contains("/tmp/") || d.contains("\\temp\\") || d.contains("native_ui_tests") ||
     d.contains("smoke-") || d.contains("verify-")) {
  return false;
 }
 const QString appName = QGuiApplication::applicationName().toLower();
 if (appName.contains("test")) return false;
 return true;
}

AppPreferences loadAppPreferences(const QString &dataDirectory) {
 AppPreferences p;
 if (!shouldPersistPreferences(dataDirectory)) return p;
 QSettings s("GroundAtlas", "GroundAtlasDesktop");
 p.darkTheme = s.value("ui/darkTheme", false).toBool();
 p.english = s.value("ui/english", false).toBool();
 p.imperialUnits = s.value("ui/imperialUnits", false).toBool();
 p.contextStyle = s.value("ui/contextStyle", true).toBool();
 p.perfTier = static_cast<PerfTier>(std::clamp(s.value("perf/tier", 0).toInt(), 0, 3));
 p.compatMode = static_cast<GraphicsCompatMode>(std::clamp(s.value("perf/compatMode", 0).toInt(), 0, 4));
 p.smoothDamping = s.value("perf/smoothDamping", true).toBool();
 p.autoDarkBasemap = s.value("ui/autoDarkBasemap", true).toBool();
 p.terrain3d = s.value("perf/terrain3d", false).toBool();
 p.leftDrawerOpen = s.value("ui/leftDrawerOpen", true).toBool();
 p.studioDrawerOpen = s.value("ui/studioDrawerOpen", true).toBool();
 p.windowGeometry = s.value("ui/windowGeometry").toByteArray();
 return p;
}

void saveAppPreferences(const QString &dataDirectory, const AppPreferences &p) {
 if (!shouldPersistPreferences(dataDirectory)) return;
 QSettings s("GroundAtlas", "GroundAtlasDesktop");
 s.setValue("ui/darkTheme", p.darkTheme);
 s.setValue("ui/english", p.english);
 s.setValue("ui/imperialUnits", p.imperialUnits);
 s.setValue("ui/contextStyle", p.contextStyle);
 s.setValue("perf/tier", int(p.perfTier));
 s.setValue("perf/compatMode", int(p.compatMode));
 s.setValue("perf/smoothDamping", p.smoothDamping);
 s.setValue("ui/autoDarkBasemap", p.autoDarkBasemap);
 s.setValue("perf/terrain3d", p.terrain3d);
 s.setValue("ui/leftDrawerOpen", p.leftDrawerOpen);
 s.setValue("ui/studioDrawerOpen", p.studioDrawerOpen);
 if (!p.windowGeometry.isEmpty()) s.setValue("ui/windowGeometry", p.windowGeometry);
 s.sync();
}

} // namespace ga
