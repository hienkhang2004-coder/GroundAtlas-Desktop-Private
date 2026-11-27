#include "windowchrome.h"
#include <QAbstractButton>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif
namespace {
class TrafficButton:public QAbstractButton {
public:
 enum Kind{Close,Minimize,Zoom};
 TrafficButton(Kind kind,QWidget *target,QWidget *parent):QAbstractButton(parent),kind_(kind),target_(target){
  setObjectName(kind==Close?"window-close":kind==Minimize?"window-minimize":"window-zoom");setFixedSize(22,24);setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);connect(this,&QAbstractButton::clicked,this,[this]{if(!target_)return;if(kind_==Close)target_->close();else if(kind_==Minimize)target_->showMinimized();else if(target_->isMaximized()||target_->isFullScreen())target_->showNormal();else target_->showMaximized();});
 }
 void language(bool english){const auto title=QString::fromUtf8(kind_==Close?(english?"Close window":"Đóng cửa sổ"):kind_==Minimize?(english?"Minimize":"Thu nhỏ"):(target_&&target_->isMaximized()?(english?"Restore window":"Khôi phục cửa sổ"):(english?"Maximize":"Phóng to")));setToolTip(title);setAccessibleName(title);}
protected:
 void enterEvent(QEnterEvent *event)override{update();QAbstractButton::enterEvent(event);}
 void leaveEvent(QEvent *event)override{update();QAbstractButton::leaveEvent(event);}
 void paintEvent(QPaintEvent *)override{
  QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF circle(width()/2.-6.1,height()/2.-6.1,12.2,12.2);const bool active=target_&&target_->isActiveWindow();QColor fill=kind_==Close?QColor("#ff6057"):kind_==Minimize?QColor("#ffbd2e"):QColor("#28c840");if(!isEnabled()||(!active&&!underMouse()))fill=QColor("#d0d0d0");if(isDown())fill=fill.darker(115);p.setBrush(fill);p.setPen(QPen(fill.darker(110),.65));p.drawEllipse(circle);if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#3b63fb"),1.5));p.drawEllipse(circle.adjusted(-2.5,-2.5,2.5,2.5));}
  if(isEnabled()&&(underMouse()||hasFocus())){const auto c=circle.center();p.setPen(QPen(kind_==Close?QColor("#7d2826"):kind_==Minimize?QColor("#875d12"):QColor("#126327"),1.15,Qt::SolidLine,Qt::RoundCap));if(kind_==Close){p.drawLine(c+QPointF(-2.6,-2.6),c+QPointF(2.6,2.6));p.drawLine(c+QPointF(-2.6,2.6),c+QPointF(2.6,-2.6));}else if(kind_==Minimize)p.drawLine(c+QPointF(-3,0),c+QPointF(3,0));else{p.drawLine(c+QPointF(-3,-.5),c+QPointF(-3,-3));p.drawLine(c+QPointF(-3,-3),c+QPointF(-.5,-3));p.drawLine(c+QPointF(3,.5),c+QPointF(3,3));p.drawLine(c+QPointF(3,3),c+QPointF(.5,3));}}}
private:
 Kind kind_;QPointer<QWidget> target_;
};
class TitleBar:public QWidget {
 Q_OBJECT
public:
 TitleBar(QWidget *target):QWidget(target),target_(target){setObjectName("mac-titlebar");setFixedHeight(31);auto layout=new QHBoxLayout(this);layout->setContentsMargins(12,3,12,3);layout->setSpacing(0);for(auto kind:{TrafficButton::Close,TrafficButton::Minimize,TrafficButton::Zoom}){auto button=new TrafficButton(kind,target,this);buttons_.append(button);layout->addWidget(button);}layout->addStretch();title_=new QLabel(this);title_->setObjectName("mac-window-title");title_->setAlignment(Qt::AlignCenter);title_->setAttribute(Qt::WA_TransparentForMouseEvents);layout->addWidget(title_,3);layout->addStretch();auto balance=new QWidget(this);balance->setFixedWidth(66);balance->setAttribute(Qt::WA_TransparentForMouseEvents);layout->addWidget(balance);target->installEventFilter(this);sync();}
 void sync(){if(!target_)return;const auto text=target_->windowTitle();title_->setText(fontMetrics().elidedText(text,Qt::ElideMiddle,std::max(120,width()-260)));const bool dialog=qobject_cast<QDialog*>(target_.data());buttons_[1]->setEnabled(!dialog);buttons_[2]->setEnabled(!dialog||target_->minimumSize()!=target_->maximumSize());const bool en=target_->property("gaEnglish").toBool();for(auto b:buttons_){b->language(en);b->update();}}
protected:
 bool eventFilter(QObject *object,QEvent *event)override{if(object==target_&&(event->type()==QEvent::WindowTitleChange||event->type()==QEvent::WindowStateChange||event->type()==QEvent::ActivationChange||event->type()==QEvent::DynamicPropertyChange))sync();return QWidget::eventFilter(object,event);}
 void resizeEvent(QResizeEvent *event)override{sync();QWidget::resizeEvent(event);}
 void mousePressEvent(QMouseEvent *event)override{if(event->button()==Qt::LeftButton&&target_&&target_->windowHandle()){target_->windowHandle()->startSystemMove();event->accept();}}
 void mouseDoubleClickEvent(QMouseEvent *event)override{if(event->button()==Qt::LeftButton&&buttons_[2]->isEnabled()){buttons_[2]->click();event->accept();}}
private:
 QPointer<QWidget> target_;QLabel *title_=nullptr;QVector<TrafficButton*> buttons_;
};
class ChromeManager:public QObject,public QAbstractNativeEventFilter {
public:
 explicit ChromeManager(QApplication &app):QObject(&app){app.installEventFilter(this);app.installNativeEventFilter(this);}
 ~ChromeManager(){if(qApp){qApp->removeEventFilter(this);qApp->removeNativeEventFilter(this);}}
 void main(QMainWindow *window){if(!window||window->property("gaChrome").toBool())return;prepare(window);auto container=new QWidget(window);container->setObjectName("mac-window-top");auto layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);layout->addWidget(new TitleBar(window));auto previous=window->menuBar();auto menu=new QMenuBar(container);menu->setObjectName("mac-menu");menu->setNativeMenuBar(false);for(auto action:previous->actions()){if(auto popup=action->menu())popup->setParent(menu,popup->windowFlags());if(action->parent()==previous)action->setParent(menu);menu->addAction(action);}layout->addWidget(menu);window->setMenuWidget(container);}
protected:
 bool eventFilter(QObject *object,QEvent *event)override{
  auto widget=qobject_cast<QWidget*>(object);if(!widget)return false;
  if(event->type()==QEvent::Polish){if(auto dialog=qobject_cast<QDialog*>(widget);dialog&&dialog->isWindow()&&!dialog->property("gaChrome").toBool()&&dialog->layout()){prepare(dialog);auto previous=dialog->layout();auto body=new QWidget(dialog);body->setObjectName("mac-dialog-body");body->setLayout(previous);auto layout=new QVBoxLayout(dialog);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);layout->addWidget(new TitleBar(dialog));layout->addWidget(body,1);}}
  if((event->type()==QEvent::Show||event->type()==QEvent::WinIdChange)&&widget->property("gaChrome").toBool())nativeFrame(widget);
  return false;
 }
 bool nativeEventFilter(const QByteArray &,void *message,qintptr *result)override{
#ifdef Q_OS_WIN
  auto msg=static_cast<MSG*>(message);auto window=windows_.value(quintptr(msg->hwnd));if(!window)return false;
  if(msg->message==WM_NCCALCSIZE&&msg->wParam){if(IsZoomed(msg->hwnd)){MONITORINFO monitor{};monitor.cbSize=sizeof(MONITORINFO);if(GetMonitorInfo(MonitorFromWindow(msg->hwnd,MONITOR_DEFAULTTONEAREST),&monitor))reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0]=monitor.rcWork;}*result=0;return true;}
  if(msg->message==WM_NCHITTEST){RECT r;GetWindowRect(msg->hwnd,&r);const int x=GET_X_LPARAM(msg->lParam),y=GET_Y_LPARAM(msg->lParam);const double dpr=window->devicePixelRatioF();const int border=std::max(5,qRound(6*dpr));const bool resizable=!window->isMaximized()&&!window->isFullScreen()&&window->minimumSize()!=window->maximumSize();if(resizable){const bool left=x<r.left+border,right=x>=r.right-border,top=y<r.top+border,bottom=y>=r.bottom-border;const auto hit=top&&left?HTTOPLEFT:top&&right?HTTOPRIGHT:bottom&&left?HTBOTTOMLEFT:bottom&&right?HTBOTTOMRIGHT:left?HTLEFT:right?HTRIGHT:top?HTTOP:bottom?HTBOTTOM:HTNOWHERE;if(hit!=HTNOWHERE){*result=hit;return true;}}
   const QPoint local(qRound((x-r.left)/dpr),qRound((y-r.top)/dpr));if(auto bar=window->findChild<TitleBar*>("mac-titlebar")){const auto point=bar->mapFrom(window,local);if(bar->rect().contains(point)){auto child=bar->childAt(point);bool button=false;for(auto p=child;p&&p!=bar;p=p->parentWidget())if(qobject_cast<QAbstractButton*>(p)){button=true;break;}if(!button){*result=HTCAPTION;return true;}}}}
#else
  Q_UNUSED(message);Q_UNUSED(result);
#endif
  return false;
 }
private:
 QHash<quintptr,QPointer<QWidget>> windows_;
 void prepare(QWidget *window){window->setProperty("gaChrome",true);if(window->parentWidget())window->setProperty("gaEnglish",window->parentWidget()->window()->property("gaEnglish"));window->setWindowFlag(Qt::FramelessWindowHint,true);window->setAttribute(Qt::WA_StyledBackground,true);}
 void nativeFrame(QWidget *window){
#ifdef Q_OS_WIN
  const auto hwnd=reinterpret_cast<HWND>(window->winId());windows_.insert(quintptr(hwnd),window);if(window->property("gaFrameReady").toBool())return;window->setProperty("gaFrameReady",true);const auto style=GetWindowLongPtr(hwnd,GWL_STYLE);SetWindowLongPtr(hwnd,GWL_STYLE,style|WS_THICKFRAME|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX);const MARGINS margin{1,1,1,1};DwmExtendFrameIntoClientArea(hwnd,&margin);const DWORD rounding=2;DwmSetWindowAttribute(hwnd,33,&rounding,sizeof(rounding));SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
#else
  Q_UNUSED(window);
#endif
 }
};
ChromeManager *manager=nullptr;
}
void installApplicationChrome(QApplication &application){
#ifdef Q_OS_MACOS
 Q_UNUSED(application);
 Q_UNUSED(manager);
#else
 if(!manager)manager=new ChromeManager(application);
#endif
}
void installWindowChrome(QMainWindow *window){
#ifdef Q_OS_MACOS
 if(window&&window->menuBar())window->menuBar()->setNativeMenuBar(true);
#else
 if(manager)manager->main(window);
#endif
}
void setChromeLanguage(QWidget *window,bool english){if(!window)return;window->setProperty("gaEnglish",english);if(auto bar=window->findChild<TitleBar*>("mac-titlebar"))bar->sync();}
#include "windowchrome.moc"
