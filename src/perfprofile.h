#pragma once
#include <QByteArray>
#include <QString>

class QWidget;

namespace ga {

enum class PerfTier {
 Auto = 0,
 Low = 1,
 Medium = 2,
 Ultra = 3,
};

struct HardwareInfo {
 QString gpuRenderer;
 int cpuThreads = 4;
 int ramGb = 8;
 int displayHz = 60;
 PerfTier recommendedTier = PerfTier::Medium;

 QString summary(bool english) const;
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
 bool smoothDamping = true;
 bool autoDarkBasemap = true;
 bool terrain3d = false;
 bool leftDrawerOpen = true;
 bool studioDrawerOpen = true;
 QByteArray windowGeometry;
};

HardwareInfo probeHardware(const QString &gpuRenderer, const QWidget *window = nullptr);
ResolvedGraphicsConfig resolveGraphicsConfig(PerfTier requested, const HardwareInfo &hw);
QString perfTierName(PerfTier tier, bool english);

bool shouldPersistPreferences(const QString &dataDirectory);
AppPreferences loadAppPreferences(const QString &dataDirectory);
void saveAppPreferences(const QString &dataDirectory, const AppPreferences &prefs);

} // namespace ga
