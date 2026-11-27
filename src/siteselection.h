#pragma once
#include "project.h"
#include <QWidget>
class QComboBox;class QDoubleSpinBox;class QLabel;class QPushButton;class QCheckBox;class QFormLayout;class QToolButton;
class SiteSelection:public QWidget {
 Q_OBJECT
public:
 explicit SiteSelection(QWidget *parent=nullptr);
 ga::Site site() const{return site_;}
 bool locked() const;
 void setSite(const ga::Site &site,bool locked=false);
 void setLanguage(bool english);
 void setDarkTheme(bool dark);
 void setCenter(double lat,double lon,const QString &name);
 void setGeographicBoundary(const QPolygonF &points);
 void drawingState(bool active,int vertices=0);
 QJsonObject state() const;
 void restore(const QJsonObject &state);
signals:
 void selectionChanged();
 void drawRequested();
 void finishRequested();
 void undoRequested();
 void cancelRequested();
 void message(QString text);
private:
 ga::Site site_;
 bool english_=false,dark_=false,updating_=false,drawing_=false;
 QComboBox *shape_=nullptr;
 QVector<QToolButton*> shapeCards_;
 QDoubleSpinBox *diameter_=nullptr,*width_=nullptr,*height_=nullptr,*rotation_=nullptr;
 QLabel *diameterLabel_=nullptr,*widthLabel_=nullptr,*heightLabel_=nullptr,*rotationLabel_=nullptr,*area_=nullptr,*hint_=nullptr;
 QWidget *polygonTools_=nullptr;QPushButton *draw_=nullptr,*finish_=nullptr,*undo_=nullptr,*cancel_=nullptr,*coordinates_=nullptr;
 QCheckBox *locked_=nullptr;
 void updateGeometry();
 void syncControls();
 void updateLabels();
 void editCoordinates();
 QString text(const char *vi,const char *en) const;
};
