// flaskwidget.h
//
// Procedurally drawn "bubbling flask" widget.
// Everything (glass, liquid, glow, bubbles, highlights) is rendered in
// paintEvent with QPainter primitives - no images, no SVG, no QML.
//
// The widget paints no background of its own: the surface is transparent, so
// the parent page's background shows through around and inside the flask.

#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QtMath>
#include <QString>
#include <cmath>
#include <vector>

class FlaskWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FlaskWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setMinimumSize(240, 240);

        seedBubbles();

        m_clock.start();
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
        m_timer.start();
    }

    QSize sizeHint() const override { return QSize(560, 520); }

    // API stubs used by CryptPage; visuals ignore them.
    void setPhase(const QString &) {}
    void setProgress(qreal)        {}
    void setActive(bool)           {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal t = m_clock.elapsed() / 1000.0;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const Geom g = computeGeom();

        paintAmbientBloom(p, g, t);
        paintGlassInterior(p, g);
        paintLiquid(p, g, t);
        paintRightFacet(p, g);
        paintOutline(p, g, t);
        paintGlassHighlights(p, g);
        paintRisingBubbles(p, g, t);
    }

private:
    static QColor bgCore()      { return QColor(0x2a, 0x1c, 0x50); }
    static QColor bgEdge()      { return QColor(0x17, 0x10, 0x2c); }
    static QColor strokeTop()   { return QColor(0x9d, 0x6b, 0xf0); }
    static QColor strokeMid()   { return QColor(0x8b, 0x4d, 0xd8); }
    static QColor strokeLow()   { return QColor(0xe0, 0x4b, 0xa8); }
    static QColor liqPink()     { return QColor(0xd8, 0x6c, 0xdc); }
    static QColor liqViolet()   { return QColor(0x7b, 0x5f, 0xdc); }
    static QColor liqBlue()     { return QColor(0x44, 0x62, 0xd8); }
    static QColor liqDeep()     { return QColor(0x5a, 0x46, 0xb4); }
    static QColor cyanGlow()    { return QColor(0x2c, 0xe4, 0xf2); }
    static QColor bubbleBlue()  { return QColor(0x3b, 0x76, 0xff); }

    struct Geom {
        qreal S = 0;
        qreal cx = 0;
        qreal yTop = 0;
        qreal yLipBot = 0;
        qreal yShoulder = 0;
        qreal yBot = 0;
        qreal hLip = 0, hNeck = 0, hBody = 0;
        qreal rLip = 0, rBot = 0;
        qreal stroke = 0;
        qreal yLiquid = 0;
        QPainterPath outline;
        QPainterPath inner;
    };

    Geom computeGeom() const
    {
        Geom g;
        g.S = qMin(width(), height());
        const qreal ox = (width()  - g.S) * 0.5;
        const qreal oy = (height() - g.S) * 0.5;

        g.cx        = ox + g.S * 0.500;
        g.yTop      = oy + g.S * 0.205;
        g.yLipBot   = oy + g.S * 0.272;
        g.yShoulder = oy + g.S * 0.455;
        g.yBot      = oy + g.S * 0.905;

        g.hLip   = g.S * 0.143;
        g.hNeck  = g.S * 0.095;
        g.hBody  = g.S * 0.300;
        g.rLip   = g.S * 0.026;
        g.rBot   = g.S * 0.046;
        g.stroke = g.S * 0.0115;

        g.yLiquid = g.yShoulder + (g.yBot - g.yShoulder) * 0.485;

        g.outline = buildFlask(g, 0.0);
        g.inner   = buildFlask(g, g.stroke * 0.5 + g.S * 0.002);
        return g;
    }

    static QPainterPath buildFlask(const Geom &g, qreal inset)
    {
        const qreal cx      = g.cx;
        const qreal hLip    = g.hLip  - inset;
        const qreal hNeck   = g.hNeck - inset;
        const qreal hBody   = g.hBody - inset;
        const qreal yTop    = g.yTop  + inset;
        const qreal yLipBot = g.yLipBot;
        const qreal yShldr  = g.yShoulder;
        const qreal yBot    = g.yBot - inset;
        const qreal rLip    = qMax<qreal>(1.0, g.rLip - inset * 0.5);
        const qreal rBot    = qMax<qreal>(1.0, g.rBot - inset);

        auto backOffSlant = [&](QPointF from, QPointF corner) {
            QPointF d = corner - from;
            const qreal len = std::hypot(d.x(), d.y());
            if (len < 1e-6) return corner;
            d /= len;
            return corner - d * rBot;
        };

        QPainterPath p;
        p.moveTo(cx - hLip + rLip, yTop);
        p.lineTo(cx + hLip - rLip, yTop);
        p.quadTo(cx + hLip, yTop, cx + hLip, yTop + rLip);
        p.lineTo(cx + hLip, yLipBot - rLip);
        p.quadTo(cx + hLip, yLipBot, cx + hLip - rLip, yLipBot);
        p.lineTo(cx + hNeck, yLipBot);
        p.lineTo(cx + hNeck, yShldr);
        {
            const QPointF corner(cx + hBody, yBot);
            const QPointF start(cx + hNeck, yShldr);
            p.lineTo(backOffSlant(start, corner));
            p.quadTo(corner, QPointF(cx + hBody - rBot, yBot));
        }
        p.lineTo(cx - hBody + rBot, yBot);
        {
            const QPointF corner(cx - hBody, yBot);
            const QPointF start(cx - hNeck, yShldr);
            p.quadTo(corner, backOffSlant(start, corner));
        }
        p.lineTo(cx - hNeck, yShldr);
        p.lineTo(cx - hNeck, yLipBot);
        p.lineTo(cx - hLip + rLip, yLipBot);
        p.quadTo(cx - hLip, yLipBot, cx - hLip, yLipBot - rLip);
        p.lineTo(cx - hLip, yTop + rLip);
        p.quadTo(cx - hLip, yTop, cx - hLip + rLip, yTop);
        p.closeSubpath();
        return p;
    }

    static qreal surfaceY(const Geom &g, qreal x, qreal t)
    {
        const qreal u = (x - g.cx) / g.hBody;
        const qreal a1 = g.S * 0.0225;
        const qreal a2 = g.S * 0.0105;
        const qreal bob = std::sin(t * 0.55) * g.S * 0.004;
        return g.yLiquid + bob
             + a1 * std::sin(u * 3.10 + t * 1.15)
             + a2 * std::sin(u * 5.40 - t * 0.83 + 1.7);
    }

    // Where the cyan core sits. Slow continuous drift instead of an argmax
    // over competing troughs (which would snap across the flask).
    static QPointF glowAnchor(const Geom &g, qreal t)
    {
        const qreal drift = 0.30 * std::sin(t * 0.27)
                          + 0.14 * std::sin(t * 0.41 + 2.1);
        const qreal x = g.cx + g.hBody * drift;
        return QPointF(x, surfaceY(g, x, t) + g.S * 0.055);
    }

    struct Bubble {
        qreal phase, speed, x, r, tint, wobF, wobA;
    };

    void seedBubbles()
    {
        QRandomGenerator rg(0xC0FFEEu);
        auto rnd = [&rg] { return rg.generateDouble(); };

        m_rising.clear();
        for (int i = 0; i < 13; ++i) {
            Bubble b;
            b.phase = rnd();
            b.speed = 0.17 + rnd() * 0.16;
            b.x     = rnd() * 2.0 - 1.0;
            b.r     = 0.0055 + rnd() * 0.0065;
            b.tint  = rnd();
            b.wobF  = 1.4 + rnd() * 2.6;
            b.wobA  = 0.004 + rnd() * 0.007;
            m_rising.push_back(b);
        }

        m_submerged.clear();
        for (int i = 0; i < 15; ++i) {
            Bubble b;
            b.phase = rnd();
            b.speed = 0.055 + rnd() * 0.075;
            b.x     = rnd() * 1.6 - 0.8;
            b.r     = 0.0055 + rnd() * 0.0075;
            b.tint  = rnd();
            b.wobF  = 0.9 + rnd() * 1.8;
            b.wobA  = 0.006 + rnd() * 0.010;
            m_submerged.push_back(b);
        }
    }

    void paintAmbientBloom(QPainter &p, const Geom &g, qreal t)
    {
        const qreal pulse = 0.82 + 0.18 * std::sin(t * 1.3);

        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Plus);

        QRadialGradient halo(QPointF(g.cx, (g.yShoulder + g.yBot) * 0.5),
                             g.S * 0.52);
        QColor h(0x7a, 0x2c, 0x9a);
        h.setAlphaF(0.30 * pulse);
        halo.setColorAt(0.0, h);
        h.setAlphaF(0.10 * pulse);
        halo.setColorAt(0.55, h);
        h.setAlpha(0);
        halo.setColorAt(1.0, h);
        p.fillRect(rect(), halo);

        QRadialGradient leak(QPointF(g.cx + g.S * 0.02, g.yLiquid + g.S * 0.09),
                             g.S * 0.30);
        QColor c = cyanGlow();
        c.setAlphaF(0.16 * pulse);
        leak.setColorAt(0.0, c);
        c.setAlpha(0);
        leak.setColorAt(1.0, c);
        p.fillRect(rect(), leak);

        p.restore();
    }

    void paintGlassInterior(QPainter &p, const Geom &g)
    {
        p.save();
        p.setClipPath(g.inner);
        QLinearGradient lg(g.cx, g.yTop, g.cx, g.yBot);
        lg.setColorAt(0.0, QColor(0x6a, 0x4f, 0xb4, 26));
        lg.setColorAt(0.5, QColor(0x3a, 0x28, 0x74, 40));
        lg.setColorAt(1.0, QColor(0x1d, 0x14, 0x40, 70));
        p.fillPath(g.inner, lg);
        p.restore();
    }

    void paintLiquid(QPainter &p, const Geom &g, qreal t)
    {
        const int samples = 96;
        const qreal x0 = g.cx - g.hBody;
        const qreal x1 = g.cx + g.hBody;

        QPainterPath surface;
        for (int i = 0; i <= samples; ++i) {
            const qreal x = x0 + (x1 - x0) * (qreal(i) / samples);
            const qreal y = surfaceY(g, x, t);
            if (i == 0) surface.moveTo(x, y);
            else        surface.lineTo(x, y);
        }

        QPainterPath body = surface;
        body.lineTo(x1, g.yBot + g.S * 0.02);
        body.lineTo(x0, g.yBot + g.S * 0.02);
        body.closeSubpath();

        p.save();
        p.setClipPath(g.inner);
        p.setClipPath(body, Qt::IntersectClip);

        const QRectF lr = g.inner.boundingRect();

        QLinearGradient base(lr.left(), g.yLiquid - g.S * 0.05,
                             lr.right(), g.yBot);
        base.setColorAt(0.00, liqPink());
        base.setColorAt(0.32, liqViolet());
        base.setColorAt(0.62, liqBlue());
        base.setColorAt(1.00, liqDeep());
        p.fillPath(body, base);

        QLinearGradient warm(g.cx, 0, x1, 0);
        QColor mv(0x9a, 0x6a, 0xd6, 0);
        warm.setColorAt(0.0, mv);
        mv.setAlpha(120);
        warm.setColorAt(1.0, mv);
        p.fillPath(body, warm);

        p.setCompositionMode(QPainter::CompositionMode_Plus);

        {
            const QPointF anchor = glowAnchor(g, t);
            const qreal radius = g.S * 0.185 * (1.0 + 0.06 * std::sin(t * 0.9));
            QRadialGradient core(anchor, radius);
            QColor c = cyanGlow();
            c.setAlphaF(0.90);
            core.setColorAt(0.00, c);
            c.setAlphaF(0.34);
            core.setColorAt(0.42, c);
            c.setAlpha(0);
            core.setColorAt(1.00, c);
            p.fillPath(body, core);
        }
        {
            QRadialGradient mag(QPointF(x0 + g.hBody * 0.30, g.yLiquid + g.S * 0.01),
                                g.S * 0.16);
            QColor c(0xe8, 0x5c, 0xd8);
            c.setAlphaF(0.34);
            mag.setColorAt(0.0, c);
            c.setAlpha(0);
            mag.setColorAt(1.0, c);
            p.fillPath(body, mag);
        }
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        {
            QLinearGradient sh(0, g.yBot - g.S * 0.10, 0, g.yBot);
            sh.setColorAt(0.0, QColor(0x14, 0x0b, 0x30, 0));
            sh.setColorAt(1.0, QColor(0x14, 0x0b, 0x30, 90));
            p.fillPath(body, sh);
        }

        paintSubmergedBubbles(p, g, t);

        QLinearGradient edge(x0, 0, x1, 0);
        edge.setColorAt(0.00, QColor(0xff, 0xc6, 0xf2, 130));
        edge.setColorAt(0.45, QColor(0xd8, 0xe6, 0xff, 90));
        edge.setColorAt(0.70, QColor(0xa8, 0xf4, 0xff, 150));
        edge.setColorAt(1.00, QColor(0xc9, 0xa8, 0xf0, 90));
        QPen mp(QBrush(edge), g.S * 0.0045);
        mp.setCapStyle(Qt::RoundCap);
        p.setBrush(Qt::NoBrush);
        p.setPen(mp);
        p.drawPath(surface);

        p.restore();
    }

    void paintSubmergedBubbles(QPainter &p, const Geom &g, qreal t)
    {
        p.setPen(Qt::NoPen);
        for (const Bubble &b : m_submerged) {
            qreal u = std::fmod(b.phase + t * b.speed, 1.0);
            const qreal x = g.cx + b.x * g.hBody * 0.72
                          + std::sin(t * b.wobF + b.phase * 6.28) * g.S * b.wobA;
            const qreal top = surfaceY(g, x, t) + g.S * 0.012;
            const qreal bottom = g.yBot - g.S * 0.02;
            const qreal y = bottom + (top - bottom) * u;
            const qreal r = g.S * b.r * (0.75 + 0.35 * u);

            qreal a = 1.0;
            if (u < 0.10) a = u / 0.10;
            if (u > 0.80) a = 1.0 - (u - 0.80) / 0.20;

            QRadialGradient rg(QPointF(x, y), r * 1.15,
                               QPointF(x - r * 0.3, y - r * 0.35));
            QColor deep(0x22, 0x14, 0x4e);
            deep.setAlphaF(0.46 * a);
            rg.setColorAt(0.0, deep);
            deep.setAlphaF(0.60 * a);
            rg.setColorAt(0.75, deep);
            QColor rim(0xc9, 0xd8, 0xff);
            rim.setAlphaF(0.22 * a);
            rg.setColorAt(1.0, rim);
            p.setBrush(rg);
            p.drawEllipse(QPointF(x, y), r, r);
        }
    }

    void paintRightFacet(QPainter &p, const Geom &g)
    {
        QPainterPath facet;
        facet.moveTo(g.cx + g.hNeck * 0.55, g.yShoulder - g.S * 0.03);
        facet.lineTo(g.cx - g.hBody * 0.28, g.yBot + g.S * 0.02);
        facet.lineTo(g.cx + g.hBody + g.S * 0.02, g.yBot + g.S * 0.02);
        facet.lineTo(g.cx + g.hBody + g.S * 0.02, g.yShoulder - g.S * 0.03);
        facet.closeSubpath();

        p.save();
        p.setClipPath(g.inner);
        QLinearGradient lg(g.cx, g.yShoulder, g.cx + g.hBody, g.yBot);
        lg.setColorAt(0.0, QColor(255, 255, 255, 26));
        lg.setColorAt(1.0, QColor(255, 255, 255, 8));
        p.fillPath(facet, lg);
        p.restore();
    }

    void paintOutline(QPainter &p, const Geom &g, qreal t)
    {
        auto strokeBrush = [&](qreal alpha) {
            QLinearGradient lg(g.cx - g.hBody, g.yTop, g.cx + g.hBody, g.yBot);
            QColor a = strokeTop(), b = strokeMid(), c = strokeLow();
            a.setAlphaF(alpha); b.setAlphaF(alpha); c.setAlphaF(alpha);
            lg.setColorAt(0.00, a);
            lg.setColorAt(0.35, b);
            lg.setColorAt(0.68, QColor(0xb0, 0x4b, 0xd0, int(255 * alpha)));
            lg.setColorAt(1.00, c);
            return QBrush(lg);
        };

        p.save();
        p.setBrush(Qt::NoBrush);

        const qreal pulse = 0.85 + 0.15 * std::sin(t * 1.3 + 0.6);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        for (int i = 3; i >= 1; --i) {
            QPen pen(strokeBrush(0.055 * pulse * i * 0.9),
                     g.stroke + g.stroke * 2.2 * i);
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.drawPath(g.outline);
        }
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        QPen pen(strokeBrush(1.0), g.stroke);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawPath(g.outline);

        QPen sheen(QColor(255, 255, 255, 30), g.stroke * 0.34);
        p.setPen(sheen);
        p.drawPath(g.inner);

        p.restore();
    }

    void paintGlassHighlights(QPainter &p, const Geom &g)
    {
        p.save();
        p.setPen(Qt::NoPen);

        auto bar = [&](QPointF center, qreal len, qreal w, qreal degrees, int alpha) {
            p.save();
            p.translate(center);
            p.rotate(degrees);
            QLinearGradient lg(0, -len / 2, 0, len / 2);
            lg.setColorAt(0.0, QColor(0xe6, 0xe8, 0xf4, int(alpha * 0.55)));
            lg.setColorAt(0.4, QColor(0xd2, 0xd4, 0xe6, alpha));
            lg.setColorAt(1.0, QColor(0xb4, 0xb8, 0xd0, int(alpha * 0.75)));
            p.setBrush(lg);
            p.drawRoundedRect(QRectF(-w / 2, -len / 2, w, len), w / 2, w / 2);
            p.restore();
        };

        const qreal nx = g.cx + g.hNeck * 0.48;
        bar(QPointF(nx, g.yLipBot + g.S * 0.062), g.S * 0.090, g.S * 0.018, 0, 190);
        bar(QPointF(nx, g.yLipBot + g.S * 0.135), g.S * 0.030, g.S * 0.018, 0, 120);

        const qreal dx = -(g.hBody - g.hNeck);
        const qreal dy =  (g.yBot - g.yShoulder);
        const qreal ang = qRadiansToDegrees(std::atan2(dx, dy));
        const qreal wx = g.cx - g.hBody * 0.62;
        bar(QPointF(wx + g.S * 0.030, g.yShoulder + g.S * 0.115), g.S * 0.115, g.S * 0.019, -ang, 165);
        bar(QPointF(wx - g.S * 0.005, g.yShoulder + g.S * 0.245), g.S * 0.048, g.S * 0.019, -ang, 130);

        p.restore();
    }

    void paintRisingBubbles(QPainter &p, const Geom &g, qreal t)
    {
        p.save();
        p.setPen(Qt::NoPen);

        const qreal yEnd = g.yTop - g.S * 0.115;

        for (const Bubble &b : m_rising) {
            const qreal u = std::fmod(b.phase + t * b.speed, 1.0);
            const qreal x = g.cx + b.x * g.hNeck * 0.52
                          + std::sin(u * b.wobF * 6.283 + b.phase * 6.283) * g.S * b.wobA;
            const qreal yStart = surfaceY(g, x, t) - g.S * 0.005;
            const qreal ease = u * u * (3.0 - 2.0 * u);
            const qreal y = yStart + (yEnd - yStart) * ease;
            const qreal r = g.S * b.r * (1.0 - 0.30 * u);

            qreal a = 1.0;
            if (u < 0.06) a = u / 0.06;
            if (u > 0.68) a = 1.0 - (u - 0.68) / 0.32;
            if (a <= 0.0) continue;

            QColor core = mix(bubbleBlue(), cyanGlow(), b.tint);

            p.setCompositionMode(QPainter::CompositionMode_Plus);
            QRadialGradient halo(QPointF(x, y), r * 3.4);
            QColor hc = core;
            hc.setAlphaF(0.42 * a);
            halo.setColorAt(0.0, hc);
            hc.setAlphaF(0.14 * a);
            halo.setColorAt(0.35, hc);
            hc.setAlpha(0);
            halo.setColorAt(1.0, hc);
            p.setBrush(halo);
            p.drawEllipse(QPointF(x, y), r * 3.4, r * 3.4);

            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            QRadialGradient rg(QPointF(x, y), r,
                               QPointF(x - r * 0.35, y - r * 0.4));
            QColor lit = core.lighter(135);
            lit.setAlphaF(a);
            QColor edge = core.darker(115);
            edge.setAlphaF(a * 0.95);
            rg.setColorAt(0.0, lit);
            rg.setColorAt(1.0, edge);
            p.setBrush(rg);
            p.drawEllipse(QPointF(x, y), r, r);
        }
        p.restore();
    }

    static QColor mix(const QColor &a, const QColor &b, qreal k)
    {
        return QColor(int(a.red()   + (b.red()   - a.red())   * k),
                      int(a.green() + (b.green() - a.green()) * k),
                      int(a.blue()  + (b.blue()  - a.blue())  * k));
    }

    QTimer m_timer;
    QElapsedTimer m_clock;
    std::vector<Bubble> m_rising;
    std::vector<Bubble> m_submerged;
};
