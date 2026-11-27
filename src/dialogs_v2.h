#pragma once
#include "perfprofile.h"
#include "scene.h"
#include <QDialog>
#include <QString>
#include <QVector>
#include <functional>

class StyleGalleryDialog : public QDialog {
 Q_OBJECT
public:
 StyleGalleryDialog(const QString &activeStyle,
                    const QString &activeDiagram,
                    bool english,
                    std::function<void(const QString &)> onPickStyle,
                    std::function<void(const QString &)> onPickDiagram,
                    QWidget *parent = nullptr);
};

class ExportStudioDialog : public QDialog {
 Q_OBJECT
public:
 ExportStudioDialog(const QString &siteName,
                    bool english,
                    std::function<void(const QString &)> onExportKind,
                    QWidget *parent = nullptr);
};

class SettingsDialog : public QDialog {
 Q_OBJECT
public:
 struct Values {
  bool darkTheme = false;
  bool english = false;
  bool imperialUnits = false;
  bool contextStyle = true;
  ga::PerfTier perfTier = ga::PerfTier::Auto;
  bool smoothDamping = true;
  bool autoDarkBasemap = true;
  bool terrain3d = false;
 };
 SettingsDialog(const Values &initial,
                const ga::HardwareInfo &hardware,
                std::function<void(const Values &)> onApply,
                QWidget *parent = nullptr);
};

class CommandPaletteDialog : public QDialog {
 Q_OBJECT
public:
 struct Entry {
  QString title;
  QString subtitle;
  QString category;
  QString keywords;
  std::function<void()> run;
 };
 CommandPaletteDialog(const QVector<Entry> &entries, bool english, QWidget *parent = nullptr);
};
