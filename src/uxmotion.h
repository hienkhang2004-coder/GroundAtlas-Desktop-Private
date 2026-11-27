#pragma once
#include <QColor>
#include <QEasingCurve>
#include <QPointer>
#include <QRectF>
#include <QStackedWidget>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QDialog;
class QHBoxLayout;

namespace ux {

// Smooth sliding-capsule segmented control (macOS/Figma style) preserving real child QToolButtons/QActions
class SlidingPillTrack : public QWidget {
 Q_OBJECT
public:
 explicit SlidingPillTrack(QWidget *parent = nullptr);
 QHBoxLayout *trackLayout() const { return layout_; }
 void addSegment(QAbstractButton *button, int stretch = 0);
 void setDarkTheme(bool dark);
 void syncImmediate();
 void syncAnimated();

protected:
 void paintEvent(QPaintEvent *event) override;
 void resizeEvent(QResizeEvent *event) override;
 void showEvent(QShowEvent *event) override;
 bool eventFilter(QObject *watched, QEvent *event) override;

private:
 QHBoxLayout *layout_ = nullptr;
 QVector<QPointer<QAbstractButton>> buttons_;
 QVariantAnimation anim_;
 QRectF startRect_, currentRect_, targetRect_;
 QRectF hoverRect_;
 bool hasCurrent_ = false;
 bool dark_ = false;

 QAbstractButton *checkedButton() const;
 void animateToChecked(bool instant = false);
};

// Cross-fade + subtle directional micro-slide QStackedWidget for Studio drawer modules
class AnimatedStackedWidget : public QStackedWidget {
 Q_OBJECT
public:
 explicit AnimatedStackedWidget(QWidget *parent = nullptr);
 void slideToIndex(int nextIndex);

private:
 class Overlay;
 Overlay *overlay_ = nullptr;
 QVariantAnimation anim_;
};

// Smooth 60 FPS collapsible sidebar container (animates width without squishing inner content)
class AnimatedDrawer : public QWidget {
 Q_OBJECT
public:
 explicit AnimatedDrawer(int expandedWidth, QWidget *parent = nullptr);
 QWidget *contentHost() const { return content_; }
 bool isOpen() const { return open_; }
 void setOpen(bool open, bool animate = true);
 int expandedWidth() const { return expandedWidth_; }

protected:
 void resizeEvent(QResizeEvent *event) override;

private:
 int expandedWidth_ = 300;
 bool open_ = true;
 QWidget *content_ = nullptr;
 QVariantAnimation anim_;
};

// Sleek floating glassmorphic toast notification pill (custom alpha painting, GL-safe)
class FloatingToast : public QWidget {
 Q_OBJECT
public:
 explicit FloatingToast(QWidget *parent = nullptr);
 void setDarkTheme(bool dark);
 void showMessage(const QString &text, const QColor &accent = QColor("#c45632"), int durationMs = 2400);
 void reposition();

protected:
 void paintEvent(QPaintEvent *event) override;

private:
 QString text_;
 QColor accent_ = QColor("#c45632");
 bool dark_ = false;
 double progress_ = 0.0;
 QVariantAnimation anim_;
 QTimer hideTimer_;
};

// Apply tactile pointing-hand cursor across interactive controls in a subtree
void applyHandCursors(QWidget *root);

// Subtle 160ms fade-in entrance for modal dialogs (safe in offscreen mode)
void attachDialogFadeIn(QDialog *dialog);

} // namespace ux
