#include "mainwindow.h"
#include "license.h"
#include "viewport.h"
#include "analysisdialog.h"
#include "appearance.h"
#include "windowchrome.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_WIN
// Prefer discrete GPU on NVIDIA Optimus / AMD PowerXpress hybrid laptops
extern "C" {
 __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
 __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc,char **argv){
 QSurfaceFormat surface;
 surface.setVersion(3,3);
#ifdef Q_OS_MACOS
 surface.setProfile(QSurfaceFormat::CoreProfile);
#else
 // CompatibilityProfile ensures QMapLibre GLSL 1.20 shaders & GL_ALPHA glyph textures
 // compile natively on strict AMD Radeon & Intel UHD/Iris Xe Windows OpenGL drivers.
 surface.setProfile(QSurfaceFormat::CompatibilityProfile);
#endif
 surface.setDepthBufferSize(24);
 surface.setStencilBufferSize(8);
 surface.setSamples(4);
 surface.setSwapInterval(1);
 QSurfaceFormat::setDefaultFormat(surface);
#ifndef Q_OS_MACOS
 QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
#endif
 QApplication app(argc,argv);app.setApplicationName("GroundAtlas Desktop");app.setApplicationVersion("2.3.2");app.setOrganizationName("GroundAtlas");app.setStyleSheet(workspaceStyle());installApplicationChrome(app);
#ifdef Q_OS_WIN
 app.setStyle("Fusion");app.setFont(QFont("Segoe UI Variable Text",10));
#elif defined(Q_OS_MACOS)
 app.setFont(QFont("SF Pro Text",13));
#else
 app.setFont(QFont("Inter",10));
#endif
 ga::loadPresentationCatalog(ga::appDataPath("presentations.json"));
 QCommandLineParser parser;parser.setApplicationDescription("GroundAtlas Desktop · native C++/Qt/OpenGL workspace");parser.addHelpOption();parser.addVersionOption();parser.addOption({"data-dir","Local autosave directory","path"});parser.addOption({"diagnostics","Write native graphics diagnostics after startup","file"});parser.addOption({"validate-exports","Write sample SVG/PDF/OBJ exports for validation","directory"});parser.addOption({"validate-analysis","Export all native analysis charts, CSV and PDF","directory"});parser.addOption({"print-machine-id","Print this PC's deterministic machine fingerprint and exit"});parser.addOption({"activate-key","Verify & install a signed Ed25519 offline key (GA2L...) and exit","token"});parser.addPositionalArgument("project","Optional .gatlas project or GeoJSON file");parser.process(app);
 const auto directory=parser.isSet("data-dir")?parser.value("data-dir"):QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
 if(parser.isSet("print-machine-id")){QTextStream(stdout)<<ga::machineFingerprint()<<"\n";return 0;}
 if(parser.isSet("activate-key")){ga::LicenseManager lm(directory);QString err;if(lm.installSignedLease(parser.value("activate-key"),&err)){QTextStream(stdout)<<"ACTIVATED_OK "<<lm.info().keyMasked<<" tier="<<lm.info().tier<<" dir="<<directory<<"\n";return 0;}QTextStream(stderr)<<"ACTIVATION_FAILED: "<<err<<"\n";return 2;}
 MainWindow window(directory,parser.positionalArguments().value(0));installWindowChrome(&window);window.show();
 if(parser.isSet("diagnostics")||parser.isSet("validate-exports")||parser.isSet("validate-analysis")){QTimer::singleShot(2200,&window,[&]{if(parser.isSet("diagnostics")){QFile out(parser.value("diagnostics"));if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(window.diagnostics()).toJson(QJsonDocument::Indented));}if(parser.isSet("validate-exports")){QDir dir(parser.value("validate-exports"));dir.mkpath(".");window.viewport()->exportVector(dir.filePath("sample.svg"),false);window.viewport()->exportVector(dir.filePath("sample.pdf"),true);window.viewport()->exportObj(dir.filePath("sample.obj"));}if(parser.isSet("validate-analysis")){auto path=parser.positionalArguments().value(0,ga::appDataPath("hoankiem.gatlas"));AnalysisDialog dialog(ga::Project::load(path),false,&window);dialog.validateExports(parser.value("validate-analysis"));}});}
 return app.exec();
}
