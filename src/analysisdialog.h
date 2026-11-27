#pragma once
#include "project.h"
#include <QDialog>
class AnalysisDialog:public QDialog {
 Q_OBJECT
public:
 AnalysisDialog(const ga::Project &project,bool english,QWidget *parent=nullptr,bool darkTheme=false);
 // Runs the same native chart and report exporters as the visible workspace.
 void validateExports(const QString &directory);
};
