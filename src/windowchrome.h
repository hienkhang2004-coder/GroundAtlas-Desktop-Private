#pragma once
#include <QMainWindow>
class QApplication;
// Native Qt widgets and native OS move/resize operations; no browser surface.
void installApplicationChrome(QApplication &application);
void installWindowChrome(QMainWindow *window);
void setChromeLanguage(QWidget *window,bool english);
