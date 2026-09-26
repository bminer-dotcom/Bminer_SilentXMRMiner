#include "widgets/toggleswitch.h"
#include "theme.h"

#include <QPainter>
#include <QPropertyAnimation>

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(sizeHint());

    m_anim = new QPropertyAnimation(this, "knob", this);
    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    // User activation can skip checkStateSet() inside nextCheckState().
    connect(this, &QAbstractButton::toggled, this, [this] { checkStateSet(); });

}

void ToggleSwitch::checkStateSet()
{
    QAbstractButton::checkStateSet();
    if (!m_anim) return;
    m_anim->stop();
    const qreal target = isChecked() ? 1.0 : 0.0;
    if (!m_animated || !isVisible() || signalsBlocked()) {
        setKnob(target);
        return;
    }
    m_anim->setStartValue(m_knob);
    m_anim->setEndValue(target);
    m_anim->start();
}

void ToggleSwitch::setAnimated(bool on)
{
    m_animated = on;
    if (!on) {
        m_anim->stop();
        setKnob(isChecked() ? 1.0 : 0.0);
    }
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(42, 22);
}

void ToggleSwitch::setKnob(qreal k)
{
    m_knob = qBound(0.0, k, 1.0);
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
        p.setOpacity(0.45);

    const qreal h = height();
    const qreal r = h / 2.0;

    QColor track = Theme::SurfaceAlt;
    QColor accent = Theme::accent();

    // track
    QColor mixed = track;
    if (m_knob > 0.0) {
        mixed = QColor::fromRgbF(
            track.redF()   + (accent.redF()   - track.redF())   * m_knob,
            track.greenF() + (accent.greenF() - track.greenF()) * m_knob,
            track.blueF()  + (accent.blueF()  - track.blueF())  * m_knob);
    }
    p.setPen(QPen(m_knob > 0.5 ? accent : Theme::Border, 1.0));
    p.setBrush(mixed);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, h - 1.0), r, r);

    // knob
    const qreal travel = width() - h;
    const qreal cx = r + travel * (layoutDirection() == Qt::RightToLeft ? 1.0 - m_knob : m_knob);
    p.setPen(Qt::NoPen);
    p.setBrush(isEnabled() ? Theme::Text : Theme::TextFaint);
    p.drawEllipse(QPointF(cx, r), r - 3.5, r - 3.5);

    if (hasFocus()) {
        QColor f = accent;
        f.setAlpha(120);
        p.setPen(QPen(f, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(1.0, 1.0, width() - 2.0, h - 2.0), r, r);
    }
}
