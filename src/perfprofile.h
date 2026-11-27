#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

class QWidget;

namespace ga {

enum class PerfTier {
 Auto = 0,
 Low = 1,
 Medium = 2,
 Ultra = 3,
};

// 4-tier universal hardware compatibility mode across NVIDIA, AMD, Intel, VM & RDP
enum class GraphicsCompatMode {
 Auto = 0,            // Auto-probe GPU capabilities + self-healing Watchdog
 HighPerformance = 1, // Discrete GPU · 4x MSAA · OpenGL 3.3 Compatibility
 Compatible = 2,      // Intel/AMD older iGPU · 0x MSAA · No SSAO · 1024px shadow
 RasterSafe = 3,      // Pure QPainter XYZ raster basemap (zero MapLibre GLSL)
 SoftwareSafe = 4,    // Mesa Software OpenGL (opengl32sw.dll · runs on OpenGL 1.1 / VM / RDP)
};

struct BootSafetyState {
 bool safeModeForcedByCli = false;
 bool safeModeTriggeredByCrash = false;
 int consecutiveCrashes = 0;
 GraphicsCompatMode requestedMode = GraphicsCompatMode::Auto;
 GraphicsCompatMode effectiveBootMode = GraphicsCompatMode::Auto;
 QString reason;

 bool useSoftwareOpenGL() const { return effectiveBootMode == GraphicsCompatMode::SoftwareSafe; }
 bool forceRasterBasemap() const { return effectiveBootMode == GraphicsCompatMode::RasterSafe; }
 bool disableMsaaAndSsao() const {
  return effectiveBootMode == GraphicsCompatMode::Compatible ||
         effectiveBootMode == GraphicsCompatMode::RasterSafe ||
         effectiveBootMode == GraphicsCompatMode::SoftwareSafe;
 }
 int desiredMsaaSamples() const { return disableMsaaAndSsao() ? 0 : 4; }
};

struct HardwareInfo {
 QString gpuRenderer;
 int cpuThreads = 4;
 int ramGb = 8;
 int displayHz = 60;
 PerfTier recommendedTier = PerfTier::Medium;
 QString glVersion;
 double dpiScale = 1.0;
 int maxTextureSize = 4096;
 int maxMsaaSamples = 4;

 QString summary(bool english) const;
 QString diagnosticReport(const QJsonObject &diag, GraphicsCompatMode activeCompat, bool english) const;
};

struct ResolvedGraphicsConfig {
 PerfTier effectiveTier = PerfTier::Medium;
 int shadowMapSize = 2048;
 int pcfQuality = 1; // 0 = 4-tap Low, 1 = 9-tap Medium, 2 = 16-tap Rotated Poisson Ultra
 bool architecturalCrease = true;
 bool ssaoEnabled = true;
 int ssaoSamples = 8;
 int targetFps = 60;
};

struct AppPreferences {
 bool darkTheme = false;
 bool english = false;
 bool imperialUnits = false;
 bool contextStyle = true;
 PerfTier perfTier = PerfTier::Auto;
 GraphicsCompatMode compatMode = GraphicsCompatMode::Auto;
 bool smoothDamping = true;
 bool autoDarkBasemap = true;
 bool terrain3d = false;
 bool leftDrawerOpen = true;
 bool studioDrawerOpen = true;
 QByteArray windowGeometry;
};

HardwareInfo probeHardware(const QString &gpuRenderer, const QWidget *window = nullptr,
                           const QString &glVersion = {}, int maxTex = 4096, int maxSamples = 4);
ResolvedGraphicsConfig resolveGraphicsConfig(PerfTier requested, const HardwareInfo &hw,
                                             GraphicsCompatMode compatMode = GraphicsCompatMode::Auto);
QString perfTierName(PerfTier tier, bool english);
QString compatModeName(GraphicsCompatMode mode, bool english);
QString compatModeId(GraphicsCompatMode mode);

bool softwareOpenGLDllAvailable(const QString &exeDir = {});
QString crashSentinelPath(const QString &dataDirectory);
BootSafetyState beginGraphicsBoot(const QString &dataDirectory, GraphicsCompatMode savedMode,
                                  bool cliSafeMode, bool cliCompatMode, bool cliRasterMode,
                                  const QString &exeDir = {});
void markGraphicsBootHealthy(const QString &dataDirectory);
void setGlobalBootSafety(const BootSafetyState &state);
const BootSafetyState &globalBootSafety();

bool shouldPersistPreferences(const QString &dataDirectory);
AppPreferences loadAppPreferences(const QString &dataDirectory);
void saveAppPreferences(const QString &dataDirectory, const AppPreferences &prefs);

} // namespace ga
