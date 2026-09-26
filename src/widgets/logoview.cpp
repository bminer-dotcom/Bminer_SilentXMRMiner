#include "widgets/logoview.h"

#include "logo.h"
#include "theme.h"

#include <QPainter>
#include <QRadialGradient>
#include <QTimer>

#include <cmath>

LogoView::LogoView(QWidget *parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setMinimumSize(96, 96);
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_phase += 0.045;
        update();
    });
}

void LogoView::setAnimated(bool on)
{
    if (on && !m_timer->isActive())
        m_timer->start();
    else if (!on && m_timer->isActive())
        m_timer->stop();
    update();
}

void LogoView::setPlate(bool on)
{
    m_plate = on;
    update();
}

void LogoView::paintEvent(QPaintEvent *)
{
    const qreal s = qMin(width(), height());
    const QRectF box((width() - s) / 2.0, (height() - s) / 2.0, s, s);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_timer->isActive()) {
        const qreal k = 0.5 + 0.5 * std::sin(m_phase);
        QRadialGradient g(box.center(), s * 0.62);
        QColor c = Theme::accent();
        c.setAlpha(int(26 + 30 * k));
        g.setColorAt(0.55, c);
        c.setAlpha(0);
        g.setColorAt(1.0, c);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(box.adjusted(-s * 0.12, -s * 0.12, s * 0.12, s * 0.12));
    }

    Logo::paintBadge(&p, box, m_plate);
}
