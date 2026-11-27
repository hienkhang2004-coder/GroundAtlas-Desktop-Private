#include "siteselection.h"
#include "appearance.h"
#include "sitegeometry.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

QString SiteSelection::text(const char *vi,const char *en)const{return QString::fromUtf8(english_?en:vi);}

SiteSelection::SiteSelection(QWidget *parent):QWidget(parent){
 setObjectName("site-selection");
 auto *layout=new QVBoxLayout(this);
 layout->setContentsMargins(0,0,0,0);
 layout->setSpacing(7);

 // Visual 4-tile GroundAtlas v2 shape selector (Circle / Square / Rect / Freehand Polygon)
 auto *cardGrid=new QGridLayout();
 cardGrid->setSpacing(5);
 const QStringList ids={"circle","square","rectangle","polygon"};
 for(int i=0;i<ids.size();++i){
  auto *btn=new QToolButton(this);
  btn->setObjectName("shape-card");
  btn->setCheckable(true);
  btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  btn->setIconSize(QSize(16,16));
  btn->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
  btn->setMinimumWidth(0);
  btn->setMinimumHeight(46);
  btn->setProperty("shapeId",ids[i]);
  connect(btn,&QToolButton::clicked,this,[this,i]{
   shape_->setCurrentIndex(i);
   if(i==3&&!drawing_)emit drawRequested();
  });
  cardGrid->addWidget(btn,0,i);
  shapeCards_.append(btn);
 }
 layout->addLayout(cardGrid);

 // Keep synced QComboBox("site-shape") for tests & accessibility
 shape_=new QComboBox(this);
 shape_->setObjectName("site-shape");
 shape_->setMinimumWidth(0);
 shape_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
 for(const auto &id:ids)shape_->addItem(id,id);
 shape_->hide();
 layout->addWidget(shape_);

 auto *form=new QFormLayout();
 form->setContentsMargins(0,0,0,0);
 form->setHorizontalSpacing(8);
 form->setVerticalSpacing(5);
 form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
 layout->addLayout(form);

 const auto input=[&](const char *id,int min,int max,int value,const QString &suffix){
  auto *spin=new QDoubleSpinBox(this);
  spin->setObjectName(id);
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setMinimumWidth(0);
  spin->setDecimals(0);
  spin->setRange(min,max);
  spin->setSingleStep(100);
  spin->setValue(value);
  spin->setSuffix(suffix);
  return spin;
 };
 diameter_=input("site-diameter",400,3000,1000," m");
 width_=input("site-width",1,3000,1000," m");
 height_=input("site-height",1,3000,1000," m");
 rotation_=input("site-rotation",-180,180,0,"°");
 rotation_->setSingleStep(5);
 for(auto *field:{diameter_,width_,height_})field->setDecimals(2);
 diameterLabel_=new QLabel(this);
 widthLabel_=new QLabel(this);
 heightLabel_=new QLabel(this);
 rotationLabel_=new QLabel(this);
 form->addRow(diameterLabel_,diameter_);
 form->addRow(widthLabel_,width_);
 form->addRow(heightLabel_,height_);
 form->addRow(rotationLabel_,rotation_);

 polygonTools_=new QWidget(this);
 auto *polygonLayout=new QVBoxLayout(polygonTools_);
 polygonLayout->setContentsMargins(0,0,0,0);
 polygonLayout->setSpacing(5);
 auto *row=new QHBoxLayout();
 row->setSpacing(4);
 draw_=new QPushButton(polygonTools_);
 draw_->setObjectName("draw-boundary");
 draw_->setProperty("accent",true);
 finish_=new QPushButton(polygonTools_);
 finish_->setObjectName("finish-boundary");
 finish_->setProperty("accent",true);
 undo_=new QPushButton("↶",polygonTools_);
 undo_->setFixedWidth(34);
 cancel_=new QPushButton("×",polygonTools_);
 cancel_->setFixedWidth(34);
 row->addWidget(draw_,1);
 row->addWidget(finish_,1);
 row->addWidget(undo_);
 row->addWidget(cancel_);
 polygonLayout->addLayout(row);
 coordinates_=new QPushButton(polygonTools_);
 coordinates_->setObjectName("boundary-coordinates");
 polygonLayout->addWidget(coordinates_);
 layout->addWidget(polygonTools_);

 area_=new QLabel(this);
 area_->setObjectName("selection-area");
 area_->setWordWrap(true);
 layout->addWidget(area_);
 hint_=new QLabel(this);
 hint_->setObjectName("muted");
 hint_->setWordWrap(true);
 layout->addWidget(hint_);
 locked_=new QCheckBox(this);
 locked_->setObjectName("boundary-locked");
 layout->addWidget(locked_);

 connect(shape_,&QComboBox::currentIndexChanged,this,[this](int){
  if(updating_)return;
  const auto next=shape_->currentData().toString();
  if(site_.shape!=next){
   if(drawing_)emit cancelRequested();
   site_.shape=next;
   if(next=="polygon")site_.boundary.clear();
  }
  updateGeometry();
 });
 for(auto *spin:{diameter_,width_,height_,rotation_})connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{if(!updating_)updateGeometry();});
 connect(locked_,&QCheckBox::toggled,this,[this]{if(updating_)return;updateLabels();emit selectionChanged();});
 connect(draw_,&QPushButton::clicked,this,[this]{locked_->setChecked(false);emit drawRequested();});
 connect(finish_,&QPushButton::clicked,this,&SiteSelection::finishRequested);
 connect(undo_,&QPushButton::clicked,this,&SiteSelection::undoRequested);
 connect(cancel_,&QPushButton::clicked,this,&SiteSelection::cancelRequested);
 connect(coordinates_,&QPushButton::clicked,this,&SiteSelection::editCoordinates);

 site_.boundary=ga::primitiveBoundary(site_);
 setDarkTheme(false);
 setLanguage(false);
 syncControls();
}

bool SiteSelection::locked()const{return locked_->isChecked();}
void SiteSelection::setLanguage(bool english){english_=english;updateLabels();}
void SiteSelection::setDarkTheme(bool dark){
 dark_=dark;
 const QStringList icons={"circle","square","view-2d","polygon"};
 for(int i=0;i<shapeCards_.size()&&i<icons.size();++i)shapeCards_[i]->setIcon(workspaceIcon(icons[i],dark_));
}
void SiteSelection::setSite(const ga::Site &site,bool lock){
 updating_=true;site_=site;
 const auto b=site_.boundary.boundingRect();
 if(site_.shape=="rectangle"&&(site_.width<=0||site_.height<=0)){site_.width=b.width();site_.height=b.height();}
 if(site_.boundary.isEmpty()&&site_.shape!="polygon")site_.boundary=ga::primitiveBoundary(site_);
 locked_->setChecked(lock);
 syncControls();
 updating_=false;
 updateLabels();
 emit selectionChanged();
}
void SiteSelection::syncControls(){
 const QSignalBlocker b1(shape_),b2(diameter_),b3(width_),b4(height_),b5(rotation_);
 const int idx=std::max(0,shape_->findData(site_.shape));
 shape_->setCurrentIndex(idx);
 for(int i=0;i<shapeCards_.size();++i){const QSignalBlocker sb(shapeCards_[i]);shapeCards_[i]->setChecked(i==idx);}
 diameter_->setRange(std::min(400.,site_.radius*2),std::max(3000.,site_.radius*2));
 width_->setRange(std::min(1.,site_.width),std::max(3000.,site_.width));
 height_->setRange(std::min(1.,site_.height),std::max(3000.,site_.height));
 diameter_->setValue(site_.radius*2);
 width_->setValue(site_.width);
 height_->setValue(site_.height);
 rotation_->setValue(site_.rotation);
 updateLabels();
}
void SiteSelection::setCenter(double lat,double lon,const QString &name){
 site_.lat=lat;site_.lon=lon;site_.name=name;
 if(site_.shape!="polygon")site_.boundary=ga::primitiveBoundary(site_);
 updateLabels();
 emit selectionChanged();
}
void SiteSelection::updateGeometry(){
 site_.shape=shape_->currentData().toString();
 if(site_.shape=="circle"||site_.shape=="square"){site_.radius=diameter_->value()/2;site_.width=site_.height=diameter_->value();}
 else if(site_.shape=="rectangle"){site_.width=width_->value();site_.height=height_->value();site_.radius=std::max(site_.width,site_.height)/2;}
 site_.rotation=site_.shape=="circle"||site_.shape=="polygon"?0:rotation_->value();
 if(site_.shape!="polygon")site_.boundary=ga::primitiveBoundary(site_);
 const int idx=std::max(0,shape_->findData(site_.shape));
 for(int i=0;i<shapeCards_.size();++i){const QSignalBlocker sb(shapeCards_[i]);shapeCards_[i]->setChecked(i==idx);}
 updateLabels();
 emit selectionChanged();
}
void SiteSelection::updateLabels(){
 const QStringList labels={text("Tròn","Circle"),text("Vuông","Square"),text("Chữ nhật","Rect"),text("Vẽ tự do","Freehand")};
 for(int i=0;i<4;++i){
  shape_->setItemText(i,labels[i]);
  if(i<shapeCards_.size())shapeCards_[i]->setText(labels[i]);
 }
 const bool rectangle=site_.shape=="rectangle",polygon=site_.shape=="polygon";
 diameter_->setVisible(!rectangle&&!polygon);diameterLabel_->setVisible(!rectangle&&!polygon);
 diameterLabel_->setText(site_.shape=="square"?text("Cạnh","Side"):text("Đường kính","Diameter"));
 width_->setVisible(rectangle);widthLabel_->setVisible(rectangle);
 height_->setVisible(rectangle);heightLabel_->setVisible(rectangle);
 widthLabel_->setText(text("Chiều rộng","Width"));heightLabel_->setText(text("Chiều dài","Length"));
 rotation_->setVisible(site_.shape=="rectangle"||site_.shape=="square");
 rotationLabel_->setVisible(site_.shape=="rectangle"||site_.shape=="square");
 rotationLabel_->setText(text("Góc xoay","Rotation"));
 polygonTools_->setVisible(polygon);
 draw_->setText(text("Vẽ ranh tự do","Draw boundary"));
 finish_->setText(text("Hoàn tất","Finish"));
 finish_->setVisible(drawing_);undo_->setVisible(drawing_);cancel_->setVisible(drawing_);
 draw_->setVisible(!drawing_);
 undo_->setToolTip(text("Lùi một đỉnh · Backspace","Undo vertex · Backspace"));
 cancel_->setToolTip(text("Hủy vẽ · Esc","Cancel drawing · Esc"));
 coordinates_->setText(text("Chỉnh tọa độ đỉnh…","Edit vertex coordinates…"));
 coordinates_->setEnabled(!drawing_);
 locked_->setText(text("Khóa ranh trên bản đồ","Lock boundary on map"));
 const QLocale locale(english_?"en_US":"vi_VN");
 const double area=std::abs(ga::ringArea(site_.boundary));
 area_->setText(text("Diện tích: ","Area: ")+(area>=1000000?locale.toString(area/1000000,'f',3)+" km²":locale.toString(area,'f',0)+" m²"));
 hint_->setText(drawing_
  ?text("Bấm thêm đỉnh hoặc giữ Shift + kéo để vẽ nét tự do. Nhấp đúp/Enter để khép ranh.","Click vertices or hold Shift + drag freehand. Double-click/Enter to finish.")
  :polygon?text("Bấm Vẽ ranh tự do trên bản đồ hoặc chỉnh tọa độ. Mở khóa để kéo từng đỉnh.","Click Draw boundary on map or edit vertices. Unlock to drag vertices.")
  :text("Chọn tâm trên bản đồ. Ranh mới chỉ áp dụng khi nạp dữ liệu.","Choose a map center. The new boundary applies when data is loaded."));
}
void SiteSelection::setGeographicBoundary(const QPolygonF &points){
 try{
  auto next=ga::siteFromGeographicBoundary(points,site_.name);
  const auto error=ga::selectionError(next);
  if(!error.isEmpty())throw std::runtime_error(error.toUtf8().constData());
  setSite(next,false);
 }catch(const std::exception &e){
  emit message(text("Ranh chưa hợp lệ: ","Invalid boundary: ")+ga::localizedSiteError(ga::errorText(e),english_));
  emit selectionChanged();
 }
}
void SiteSelection::drawingState(bool active,int vertices){
 drawing_=active;updateLabels();
 if(active)area_->setText(QString::number(vertices)+text(" đỉnh đang vẽ"," vertices in draft"));
}
QJsonObject SiteSelection::state()const{
 QJsonArray ring;for(const auto &p:site_.boundary)ring.append(QJsonArray{p.x(),p.y()});
 return{{"shape",site_.shape},{"lat",site_.lat},{"lon",site_.lon},{"name",site_.name},{"radius",site_.radius},{"width",site_.width},{"height",site_.height},{"rotation",site_.rotation},{"locked",locked()},{"boundary",ring}};
}
void SiteSelection::restore(const QJsonObject &value){
 ga::Site next=site_;next.shape=value["shape"].toString(next.shape);
 if(!QStringList{"circle","square","rectangle","polygon"}.contains(next.shape))return;
 for(const auto &key:QStringList{"lat","lon","radius","width","height","rotation"})if(!value[key].isDouble()||!std::isfinite(value[key].toDouble()))return;
 next.lat=value["lat"].toDouble();next.lon=value["lon"].toDouble();next.radius=value["radius"].toDouble();
 next.width=value["width"].toDouble();next.height=value["height"].toDouble();next.rotation=value["rotation"].toDouble();
 next.name=value["name"].toString(next.name);next.boundary.clear();
 for(const auto &v:value["boundary"].toArray()){const auto p=v.toArray();if(p.size()!=2||!p[0].isDouble()||!p[1].isDouble())return;next.boundary.append({p[0].toDouble(),p[1].toDouble()});}
 if(next.shape=="polygon"&&next.boundary.isEmpty()){if(std::abs(next.lat)>85||std::abs(next.lon)>180)return;}
 else if(!ga::selectionError(next,100000).isEmpty())return;
 setSite(next,value["locked"].toBool());
}
void SiteSelection::editCoordinates(){
 QDialog dialog(this);dialog.setWindowTitle(text("Tọa độ ranh khu đất","Site boundary coordinates"));dialog.resize(500,430);
 auto *layout=new QVBoxLayout(&dialog);
 auto *explanation=new QLabel(text("Tọa độ WGS84 theo thứ tự khép ranh; không lặp đỉnh đầu ở cuối.","WGS84 coordinates in boundary order; do not repeat the first vertex."),&dialog);
 explanation->setWordWrap(true);layout->addWidget(explanation);
 auto *table=new QTableWidget(std::max(3,int(site_.boundary.size())),2,&dialog);
 table->setHorizontalHeaderLabels({text("Kinh độ","Longitude"),text("Vĩ độ","Latitude")});
 table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
 for(int i=0;i<site_.boundary.size();++i){const auto ll=ga::geographicPoint(site_.boundary[i],site_);table->setItem(i,0,new QTableWidgetItem(QString::number(ll.x(),'f',8)));table->setItem(i,1,new QTableWidgetItem(QString::number(ll.y(),'f',8)));}
 layout->addWidget(table,1);
 auto *row=new QHBoxLayout();
 auto *add=new QPushButton(text("Thêm đỉnh","Add vertex"),&dialog),*remove=new QPushButton(text("Xóa đỉnh","Remove vertex"),&dialog);
 row->addWidget(add);row->addWidget(remove);row->addStretch();layout->addLayout(row);
 connect(add,&QPushButton::clicked,&dialog,[=]{if(table->rowCount()<512)table->insertRow(table->rowCount());});
 connect(remove,&QPushButton::clicked,&dialog,[=]{if(table->rowCount()>3)table->removeRow(std::max(0,table->currentRow()));});
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
 buttons->button(QDialogButtonBox::Ok)->setText(text("Áp dụng","Apply"));
 buttons->button(QDialogButtonBox::Cancel)->setText(text("Hủy","Cancel"));
 layout->addWidget(buttons);
 connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
  QPolygonF points;
  for(int i=0;i<table->rowCount();++i){
   bool ok1=false,ok2=false;
   const double lon=table->item(i,0)?table->item(i,0)->text().toDouble(&ok1):0,lat=table->item(i,1)?table->item(i,1)->text().toDouble(&ok2):0;
   if(!ok1||!ok2){QMessageBox::warning(&dialog,"GroundAtlas",text("Nhập kinh/vĩ độ hợp lệ cho mọi đỉnh.","Enter valid longitude/latitude for every vertex."));return;}
   points.append({lon,lat});
  }
  try{
   const auto selection=ga::siteFromGeographicBoundary(points,site_.name);
   const auto error=ga::selectionError(selection);
   if(!error.isEmpty())throw std::runtime_error(error.toUtf8().constData());
   setSite(selection,false);dialog.accept();
  }catch(const std::exception &e){QMessageBox::warning(&dialog,"GroundAtlas",ga::localizedSiteError(ga::errorText(e),english_));}
 });
 dialog.exec();
}
