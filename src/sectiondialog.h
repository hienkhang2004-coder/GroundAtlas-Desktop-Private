#pragma once
#include "project.h"
#include "scene.h"
#include "section.h"
#include "massing.h"
#include "model3d.h"
#include <QDialog>

class SectionDialog : public QDialog {
 Q_OBJECT
public:
 explicit SectionDialog(const ga::Project &project,
                        const QVector<ga::Layer> &layers,
                        const ga::Palette &palette,
                        bool english,
                        QWidget *parent = nullptr,
                        bool darkTheme = false,
                        const ga::PlotDesign *plot = nullptr,
                        const ga::MassingConfig *massing = nullptr,
                        const QPointF *initialA = nullptr,
                        const QPointF *initialB = nullptr,
                        const QVector<ga::ImportedModel3D> *models3d = nullptr);
 ga::SectionData currentSection() const;
 bool exportSvg(const QString &path) const;
 bool exportPdf(const QString &path) const;
 bool exportPng(const QString &path) const;
 bool exportDxf(const QString &path) const;

private:
 struct Impl;
 Impl *impl_ = nullptr;
};

// Sleek collapsible bottom architectural section preview synced live to Viewport A-A' cut line
class LiveSectionDock : public QWidget {
 Q_OBJECT
public:
 explicit LiveSectionDock(QWidget *parent = nullptr);
 void setDarkTheme(bool dark);
 void setLanguage(bool english);
 void setSectionData(const ga::SectionData &data, const QString &siteName);
signals:
 void presetRequested(QString kind); // "ew", "ns", "flip"
 void openFullStudioRequested();
 void closeRequested();
private:
 struct Impl;
 Impl *impl_ = nullptr;
};
