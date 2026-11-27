#include "perfprofile.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
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

PerfTier classifyHardware(const QString &gpu, int threads, int ramGb) {
 const QString g = gpu.toLower();
 if (g.contains("llvmpipe") || g.contains("swiftshader") || g.contains("microsoft basic") || g.contains("software")) {
  return PerfTier::Low;
 }
 const bool highGpu = g.contains("rtx") || g.contains("gtx 16") || g.contains("gtx 107") || g.contains("gtx 108") ||
                      g.contains("rx 6") || g.contains("rx 7") || g.contains("rx 8") || g.contains("rx 9") ||
                      g.contains("radeon pro") || g.contains("quadro") || g.contains("arc a") || g.contains("arc b") ||
                      g.contains("apple m") || g.contains("apple gpu");
 const bool integratedGpu = g.contains("uhd") || g.contains("hd graphics") || g.contains("iris") ||
                            g.contains("radeon(tm) graphics") || g.contains("amd radeon graphics") ||
                            g.contains("vega");
 if (highGpu || (threads >= 12 && ramGb >= 16 && !integratedGpu)) {
  return PerfTier::Ultra;
 }
 if (threads <= 4 || ramGb <= 6 || g.contains("hd graphics 4") || g.contains("hd graphics 5")) {
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

QString HardwareInfo::summary(bool english) const {
 const QString gpu = gpuRenderer.isEmpty() ? QString::fromUtf8(english ? "Detecting GPU…" : "Đang nhận diện GPU…") : gpuRenderer.section(" · ", 0, 0);
 return QString::fromUtf8(english
  ? "Detected: %1 · %2 CPU threads · %3 GB RAM · %4 Hz display → Recommended: %5"
  : "Cấu hình nhận diện: %1 · %2 luồng CPU · %3 GB RAM · Màn hình %4 Hz → Đề xuất: %5")
  .arg(gpu).arg(cpuThreads).arg(ramGb).arg(displayHz).arg(perfTierName(recommendedTier, english));
}

HardwareInfo probeHardware(const QString &gpuRenderer, const QWidget *window) {
 HardwareInfo info;
 info.gpuRenderer = gpuRenderer;
 info.cpuThreads = std::max(1, QThread::idealThreadCount());
 info.ramGb = detectSystemRamGb();
 info.displayHz = detectRefreshRate(window);
 info.recommendedTier = classifyHardware(gpuRenderer, info.cpuThreads, info.ramGb);
 return info;
}

ResolvedGraphicsConfig resolveGraphicsConfig(PerfTier requested, const HardwareInfo &hw) {
 const PerfTier eff = (requested == PerfTier::Auto) ? hw.recommendedTier : requested;
 ResolvedGraphicsConfig cfg;
 cfg.effectiveTier = eff;
 if (eff == PerfTier::Low) {
  cfg.shadowMapSize = 1024;
  cfg.pcfQuality = 0;
  cfg.architecturalCrease = false;
  cfg.ssaoEnabled = false;
  cfg.ssaoSamples = 0;
  cfg.targetFps = 60;
 } else if (eff == PerfTier::Medium) {
  cfg.shadowMapSize = 2048;
  cfg.pcfQuality = 1;
  cfg.architecturalCrease = true;
  cfg.ssaoEnabled = true;
  cfg.ssaoSamples = 8;
  cfg.targetFps = std::clamp(hw.displayHz, 60, 144);
 } else {
  cfg.shadowMapSize = 4096;
  cfg.pcfQuality = 2;
  cfg.architecturalCrease = true;
  cfg.ssaoEnabled = true;
  cfg.ssaoSamples = 16;
  cfg.targetFps = std::clamp(hw.displayHz, 60, 500);
 }
 return cfg;
}

bool shouldPersistPreferences(const QString &dataDirectory) {
 const QString d = dataDirectory.toLower();
 if (d.contains("ui-tests-") || d.contains("presentation-ui-") || d.contains("mw-data") ||
     d.contains("/tmp/") || d.contains("\\temp\\") || d.contains("native_ui_tests")) {
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
 s.setValue("perf/smoothDamping", p.smoothDamping);
 s.setValue("ui/autoDarkBasemap", p.autoDarkBasemap);
 s.setValue("perf/terrain3d", p.terrain3d);
 s.setValue("ui/leftDrawerOpen", p.leftDrawerOpen);
 s.setValue("ui/studioDrawerOpen", p.studioDrawerOpen);
 if (!p.windowGeometry.isEmpty()) s.setValue("ui/windowGeometry", p.windowGeometry);
 s.sync();
}

} // namespace ga
