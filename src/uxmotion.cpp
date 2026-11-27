#include "uxmotion.h"
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSlider>
#include <QTabBar>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace ux {

// ═══════════════════════════════════════════════════════════════════════════
// SlidingPillTrack
// ═══════════════════════════════════════════════════════════════════════════
SlidingPillTrack::SlidingPillTrack(QWidget *parent) : QWidget(parent) {
 setObjectName("seg-pill");
 setAttribute(Qt::WA_StyledBackground, false);
 setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
 layout_ = new QHBoxLayout(this);
 layout_->setContentsMargins(3, 3, 3, 3);
 layout_->setSpacing(2);

 anim_.setDuration(210);
 anim_.setEasingCurve(QEasingCurve::OutCubic);
 connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
  const double t = v.toDouble();
  currentRect_ = QRectF(
   startRect_.x() + (targetRect_.x() - startRect_.x()) * t,
   startRect_.y() + (targetRect_.y() - startRect_.y()) * t,
   startRect_.width() + (targetRect_.width() - startRect_.width()) * t,
   startRect_.height() + (targetRect_.height() - startRect_.height()) * t);
  update();
 });
}

void SlidingPillTrack::addSegment(QAbstractButton *button, int stretch) {
 if (!button) return;
 button->setCursor(Qt::PointingHandCursor);
 button->installEventFilter(this);
 layout_->addWidget(button, stretch);
 buttons_.append(button);
 connect(button, &QAbstractButton::toggled, this, [this](bool checked) {
  if (checked) animateToChecked(false);
 });
}

void SlidingPillTrack::setDarkTheme(bool dark) {
 dark_ = dark;
 update();
}

void SlidingPillTrack::syncImmediate() {
 animateToChecked(true);
}

void SlidingPillTrack::syncAnimated() {
 animateToChecked(false);
}

QAbstractButton *SlidingPillTrack::checkedButton() const {
 for (const auto &b : buttons_) {
  if (b && b->isChecked() && b->isVisible()) return b.data();
 }
 for (const auto &b : buttons_) {
  if (b && b->isChecked()) return b.data();
 }
 return nullptr;
}

void SlidingPillTrack::animateToChecked(bool instant) {
 QAbstractButton *active = checkedButton();
 if (!active || active->width() <= 0 || active->height() <= 0) {
  hasCurrent_ = false;
  update();
  return;
 }
 const QRectF next = QRectF(active->geometry());
 if (instant || !hasCurrent_ || !isVisible() || width() <= 0) {
  anim_.stop();
  startRect_ = currentRect_ = targetRect_ = next;
  hasCurrent_ = true;
  update();
  return;
 }
 if (std::abs(next.x() - targetRect_.x()) < 0.5 && std::abs(next.width() - targetRect_.width()) < 0.5) {
  return;
 }
 anim_.stop();
 startRect_ = currentRect_;
 targetRect_ = next;
 anim_.setStartValue(0.0);
 anim_.setEndValue(1.0);
 anim_.start();
}

bool SlidingPillTrack::eventFilter(QObject *watched, QEvent *event) {
 if (auto *btn = qobject_cast<QAbstractButton *>(watched)) {
  if (event->type() == QEvent::Enter && !btn->isChecked()) {
   hoverRect_ = QRectF(btn->geometry());
   update();
  } else if (event->type() == QEvent::Leave) {
   hoverRect_ = QRectF();
   update();
  } else if (event->type() == QEvent::ActionChanged) {
   QTimer::singleShot(0, this, [this] { animateToChecked(false); });
  }
 }
 return QWidget::eventFilter(watched, event);
}

void SlidingPillTrack::resizeEvent(QResizeEvent *event) {
 QWidget::resizeEvent(event);
 animateToChecked(true);
}

void SlidingPillTrack::showEvent(QShowEvent *event) {
 QWidget::showEvent(event);
 QTimer::singleShot(0, this, [this] { animateToChecked(true); });
}

void SlidingPillTrack::paintEvent(QPaintEvent *) {
 QPainter p(this);
 p.setRenderHint(QPainter::Antialiasing, true);

 const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
 const double outerR = outer.height() * 0.5;
 p.setPen(QPen(dark_ ? QColor("#36332e") : QColor("#e3dfd8"), 1.0));
 p.setBrush(dark_ ? QColor("#22201d") : QColor("#f2efe9"));
 p.drawRoundedRect(outer, outerR, outerR);

 // Subtle hover capsule on inactive segment
 if (hoverRect_.isValid() && hoverRect_.width() > 2.0) {
  const double hr = hoverRect_.height() * 0.5;
  p.setPen(Qt::NoPen);
  p.setBrush(dark_ ? QColor(255, 255, 255, 16) : QColor(26, 25, 24, 14));
  p.drawRoundedRect(hoverRect_, hr, hr);
 }

 // Gliding active capsule
 if (hasCurrent_ && currentRect_.width() > 2.0) {
  const double r = currentRect_.height() * 0.5;
  // Soft ambient lift shadow beneath the active pill
  p.setPen(Qt::NoPen);
  p.setBrush(dark_ ? QColor(0, 0, 0, 70) : QColor(26, 25, 24, 34));
  p.drawRoundedRect(currentRect_.translated(0.0, 1.0), r, r);

  p.setBrush(dark_ ? QColor("#f4f2ee") : QColor("#1a1918"));
  p.setPen(QPen(dark_ ? QColor(255, 255, 255, 90) : QColor(255, 255, 255, 26), 0.8));
  p.drawRoundedRect(currentRect_, r, r);
 }
}

// ═══════════════════════════════════════════════════════════════════════════
// AnimatedStackedWidget
// ═══════════════════════════════════════════════════════════════════════════
class AnimatedStackedWidget::Overlay : public QWidget {
public:
 QPixmap prevPix, nextPix;
 double progress = 1.0;
 double dir = 1.0;
 explicit Overlay(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  hide();
 }
protected:
 void paintEvent(QPaintEvent *) override {
  QPainter p(this);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  const double shift = 14.0;
  if (!prevPix.isNull() && progress < 1.0) {
   p.save();
   p.setOpacity(std::clamp(1.0 - progress, 0.0, 1.0));
   p.drawPixmap(QPointF(-dir * shift * progress, 0.0), prevPix);
   p.restore();
  }
  if (!nextPix.isNull() && progress > 0.0) {
   p.save();
   p.setOpacity(std::clamp(progress, 0.0, 1.0));
   p.drawPixmap(QPointF(dir * shift * (1.0 - progress), 0.0), nextPix);
   p.restore();
  }
 }
};

AnimatedStackedWidget::AnimatedStackedWidget(QWidget *parent) : QStackedWidget(parent) {
 overlay_ = new Overlay(this);
 anim_.setDuration(190);
 anim_.setEasingCurve(QEasingCurve::OutCubic);
 connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
  if (!overlay_) return;
  overlay_->progress = v.toDouble();
  overlay_->update();
 });
 connect(&anim_, &QVariantAnimation::finished, this, [this] {
  if (overlay_) {
   overlay_->hide();
   overlay_->prevPix = {};
   overlay_->nextPix = {};
  }
 });
}

void AnimatedStackedWidget::slideToIndex(int nextIndex) {
 if (nextIndex < 0 || nextIndex >= count()) return;
 const int oldIndex = currentIndex();
 if (nextIndex == oldIndex) return;
 if (!isVisible() || width() < 16 || height() < 16 || oldIndex < 0) {
  anim_.stop();
  if (overlay_) overlay_->hide();
  setCurrentIndex(nextIndex);
  return;
 }
 anim_.stop();
 QWidget *prevW = currentWidget();
 QPixmap prevPix = prevW ? prevW->grab() : QPixmap();
 setCurrentIndex(nextIndex);
 QWidget *nextW = currentWidget();
 if (nextW) nextW->ensurePolished();
 QPixmap nextPix = nextW ? nextW->grab() : QPixmap();

 overlay_->prevPix = prevPix;
 overlay_->nextPix = nextPix;
 overlay_->dir = (nextIndex > oldIndex) ? 1.0 : -1.0;
 overlay_->progress = 0.0;
 overlay_->setGeometry(rect());
 overlay_->show();
 overlay_->raise();

 anim_.setStartValue(0.0);
 anim_.setEndValue(1.0);
 anim_.start();
}

// ═══════════════════════════════════════════════════════════════════════════
// AnimatedDrawer
// ═══════════════════════════════════════════════════════════════════════════
AnimatedDrawer::AnimatedDrawer(int expandedWidth, QWidget *parent)
 : QWidget(parent), expandedWidth_(expandedWidth) {
 setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
 setFixedWidth(expandedWidth_);
 content_ = new QWidget(this);
 content_->setGeometry(0, 0, expandedWidth_, height());

 anim_.setDuration(220);
 anim_.setEasingCurve(QEasingCurve::OutCubic);
 connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
  const int w = std::max(0, v.toInt());
  setFixedWidth(w);
 });
 connect(&anim_, &QVariantAnimation::finished, this, [this] {
  if (!open_) hide();
  else setFixedWidth(expandedWidth_);
 });
}

void AnimatedDrawer::resizeEvent(QResizeEvent *event) {
 QWidget::resizeEvent(event);
 if (content_) content_->setGeometry(0, 0, expandedWidth_, height());
}

void AnimatedDrawer::setOpen(bool open, bool animate) {
 if (open_ == open && isVisible() == open && anim_.state() != QAbstractAnimation::Running) return;
 open_ = open;
 anim_.stop();

 const bool canAnimate = animate && parentWidget() && parentWidget()->isVisible();
 if (!canAnimate) {
  if (open_) {
   setFixedWidth(expandedWidth_);
   show();
  } else {
   setFixedWidth(0);
   hide();
  }
  return;
 }

 const int startW = isVisible() ? width() : 0;
 const int endW = open_ ? expandedWidth_ : 0;
 if (open_ && !isVisible()) {
  setFixedWidth(0);
  show();
 }
 anim_.setStartValue(startW);
 anim_.setEndValue(endW);
 anim_.start();
}

// ═══════════════════════════════════════════════════════════════════════════
// FloatingToast
// ═══════════════════════════════════════════════════════════════════════════
FloatingToast::FloatingToast(QWidget *parent) : QWidget(parent) {
 setAttribute(Qt::WA_TransparentForMouseEvents, true);
 setAttribute(Qt::WA_NoSystemBackground, true);
 setAttribute(Qt::WA_TranslucentBackground, true);
 hide();

 anim_.setEasingCurve(QEasingCurve::OutCubic);
 connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
  progress_ = v.toDouble();
  reposition();
  update();
 });
 connect(&anim_, &QVariantAnimation::finished, this, [this] {
  if (progress_ <= 0.01) hide();
 });

 hideTimer_.setSingleShot(true);
 connect(&hideTimer_, &QTimer::timeout, this, [this] {
  anim_.stop();
  anim_.setDuration(220);
  anim_.setEasingCurve(QEasingCurve::InCubic);
  anim_.setStartValue(progress_);
  anim_.setEndValue(0.0);
  anim_.start();
 });
}

void FloatingToast::setDarkTheme(bool dark) {
 dark_ = dark;
 update();
}

void FloatingToast::showMessage(const QString &text, const QColor &accent, int durationMs) {
 if (text.trimmed().isEmpty()) return;
 text_ = text;
 accent_ = accent;
 QFont f("Segoe UI", 9, QFont::DemiBold);
 QFontMetrics fm(f);
 const int w = std::clamp(fm.horizontalAdvance(text_) + 46, 150, 520);
 resize(w, 38);
 reposition();
 if (!parentWidget() || !parentWidget()->isVisible()) return;
 show();
 raise();
 hideTimer_.stop();
 anim_.stop();
 anim_.setDuration(190);
 anim_.setEasingCurve(QEasingCurve::OutCubic);
 anim_.setStartValue(progress_);
 anim_.setEndValue(1.0);
 anim_.start();
 hideTimer_.start(std::max(1000, durationMs));
}

void FloatingToast::reposition() {
 if (!parentWidget()) return;
 const int x = (parentWidget()->width() - width()) / 2;
 const int slideY = qRound((1.0 - progress_) * 14.0);
 const int y = std::max(12, parentWidget()->height() - 108 + slideY);
 move(x, y);
}

void FloatingToast::paintEvent(QPaintEvent *) {
 if (progress_ <= 0.01) return;
 QPainter p(this);
 p.setRenderHint(QPainter::Antialiasing, true);
 p.setOpacity(std::clamp(progress_, 0.0, 1.0));

 const QRectF card = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -4.0);
 const double r = card.height() * 0.5;

 // Soft elevation shadow
 p.setPen(Qt::NoPen);
 p.setBrush(dark_ ? QColor(0, 0, 0, 115) : QColor(26, 25, 24, 34));
 p.drawRoundedRect(card.translated(0.0, 2.5), r, r);

 // Frosted pill fill
 p.setBrush(dark_ ? QColor(28, 27, 24, 240) : QColor(26, 25, 24, 236));
 p.setPen(QPen(dark_ ? QColor(255, 255, 255, 28) : QColor(255, 255, 255, 24), 1.0));
 p.drawRoundedRect(card, r, r);

 // Accent dot with subtle outer halo
 const QPointF dot(card.left() + 18.0, card.center().y());
 QColor halo = accent_;
 halo.setAlpha(65);
 p.setPen(Qt::NoPen);
 p.setBrush(halo);
 p.drawEllipse(dot, 6.5, 6.5);
 p.setBrush(accent_);
 p.drawEllipse(dot, 3.6, 3.6);

 // Crisp label
 p.setPen(QColor("#faf8f5"));
 p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
 p.drawText(card.adjusted(31.0, 0.0, -14.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft,
            p.fontMetrics().elidedText(text_, Qt::ElideRight, int(card.width() - 46.0)));
}

// ═══════════════════════════════════════════════════════════════════════════
// Utility Polish
// ═══════════════════════════════════════════════════════════════════════════
void applyHandCursors(QWidget *root) {
 if (!root) return;
 for (auto *w : root->findChildren<QWidget *>()) {
  if (qobject_cast<QAbstractButton *>(w) ||
      qobject_cast<QComboBox *>(w) ||
      qobject_cast<QSlider *>(w) ||
      qobject_cast<QTabBar *>(w)) {
   w->setCursor(Qt::PointingHandCursor);
  }
 }
}

void attachDialogFadeIn(QDialog *dialog) {
 if (!dialog) return;
 applyHandCursors(dialog);
 class FadeFilter : public QObject {
 public:
  explicit FadeFilter(QDialog *d) : QObject(d), dlg_(d) {}
 protected:
  bool eventFilter(QObject *obj, QEvent *ev) override {
   if (obj == dlg_ && ev->type() == QEvent::Show && !shown_) {
    shown_ = true;
    dlg_->setWindowOpacity(0.0);
    auto *anim = new QPropertyAnimation(dlg_, "windowOpacity", dlg_);
    anim->setDuration(165);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
   }
   return QObject::eventFilter(obj, ev);
  }
 private:
  QPointer<QDialog> dlg_;
  bool shown_ = false;
 };
 dialog->installEventFilter(new FadeFilter(dialog));
}

} // namespace ux
