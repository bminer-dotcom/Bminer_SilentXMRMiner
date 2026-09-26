#include "logo.h"
#include "theme.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include <cmath>

namespace {

QFont badgeFont(qreal pixelSize, int weight)
{
    QFont f(QStringLiteral("Bahnschrift"));
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(qMax<int>(6, qRound(pixelSize)));
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

/* Lays text out along a circle. `top` = letters stand on the outside of the
 * arc (upper half); otherwise they hang from it (lower half), which is how
 * both halves stay readable left-to-right. */
void drawArcText(QPainter *p, const QPointF &center, qreal radius,
                 const QString &text, const QFont &font, const QColor &color,
                 bool top, qreal extraSpacingDeg)
{
    if (text.isEmpty() || radius <= 0.0)
        return;

    const QFontMetricsF fm(font);

    qreal total = 0.0;
    for (int i = 0; i < text.size(); ++i)
        total += qRadiansToDegrees(fm.horizontalAdvance(text.at(i)) / radius) + extraSpacingDeg;
    total -= extraSpacingDeg;

    p->save();
    p->setFont(font);
    p->setPen(color);
    p->setBrush(Qt::NoBrush);
    p->translate(center);

    qreal a = top ? -total / 2.0 : total / 2.0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const qreal w    = fm.horizontalAdvance(ch);
        const qreal step = qRadiansToDegrees(w / radius) + extraSpacingDeg;
        const qreal mid  = top ? a + step / 2.0 : a - step / 2.0;

        p->save();
        p->rotate(mid);
        p->translate(0.0, top ? -radius : radius);
        p->drawText(QPointF(-w / 2.0, 0.0), QString(ch));
        p->restore();

        a += top ? step : -step;
    }
    p->restore();
}

// Closed organic outline: points on a circle with per-vertex radius jitter,
// smoothed by running a quadratic through the vertex midpoints.
QPainterPath blobPath(const QPointF &c, qreal r, const QVector<qreal> &variation)
{
    const int n = variation.size();
    QVector<QPointF> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const qreal a  = 2.0 * M_PI * i / n - M_PI_2;
        const qreal rr = r * variation.at(i);
        pts << c + QPointF(rr * std::cos(a), rr * std::sin(a));
    }

    QPainterPath path;
    path.moveTo((pts.at(0) + pts.at(1)) / 2.0);
    for (int i = 1; i <= n; ++i) {
        const QPointF ctrl = pts.at(i % n);
        const QPointF end  = (pts.at(i % n) + pts.at((i + 1) % n)) / 2.0;
        path.quadTo(ctrl, end);
    }
    path.closeSubpath();
    return path;
}

void paintPhage(QPainter *p, const QPointF &c, qreal s,
                const QColor &purple, const QColor &light)
{
    const qreal pen = qMax<qreal>(1.0, s * 0.115);

    // --- icosahedral head -------------------------------------------------
    QPolygonF hex;
    for (int k = 0; k < 6; ++k) {
        const qreal a = qDegreesToRadians(-90.0 + 60.0 * k);
        hex << c + QPointF(s * std::cos(a), s * std::sin(a));
    }

    QColor headFill = purple;
    headFill.setAlpha(52);
    p->setPen(QPen(purple, pen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(headFill);
    p->drawPolygon(hex);

    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(purple, pen * 0.72, Qt::SolidLine, Qt::RoundCap));
    p->drawPolygon(QPolygonF() << hex.at(0) << hex.at(2) << hex.at(4));
    p->drawPolygon(QPolygonF() << hex.at(1) << hex.at(3) << hex.at(5));

    // --- contractile sheath (the coil) ------------------------------------
    const QPointF neck = hex.at(3);                 // bottom vertex
    const qreal coilStep = s * 0.20;
    const int    coils = 5;
    p->setPen(QPen(light, pen * 0.85, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < coils; ++i) {
        const QPointF cc(neck.x(), neck.y() + coilStep * (i + 0.6));
        p->drawEllipse(cc, s * 0.30, s * 0.105);
    }

    // --- base plate + tail fibres -----------------------------------------
    const QPointF bp(neck.x(), neck.y() + coilStep * (coils + 0.25));
    p->setPen(QPen(purple, pen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawLine(bp + QPointF(-s * 0.30, 0), bp + QPointF(s * 0.30, 0));

    const qreal legs[4][4] = {
        { -0.60,  0.26, -0.82,  0.86 },
        { -0.26,  0.40, -0.38,  0.96 },
        {  0.28,  0.34,  0.20,  0.90 },
        {  0.62,  0.06,  0.92,  0.52 },
    };
    for (const auto &l : legs) {
        QPainterPath leg;
        leg.moveTo(bp + QPointF(l[0] * s * 0.35, 0.0));
        leg.lineTo(bp + QPointF(l[0] * s, l[1] * s));
        leg.lineTo(bp + QPointF(l[2] * s, l[3] * s));
        p->drawPath(leg);
    }
}

void paintCell(QPainter *p, const QPointF &c, qreal r,
               const QColor &pink, const QColor &purple, const QColor &light)
{
    const qreal pen = qMax<qreal>(1.0, r * 0.10);

    // an odd vertex count with uneven radii keeps the outline from settling
    // into a rounded rectangle
    static const QVector<qreal> variation{
        1.04, 0.82, 1.12, 0.90, 1.16, 0.80, 1.06, 0.94, 1.14, 0.84, 0.98
    };
    p->setPen(QPen(pink, pen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(blobPath(c, r, variation));

    // nucleus
    QColor nuc = purple;
    nuc.setAlpha(200);
    p->setPen(QPen(purple, pen * 0.8));
    p->setBrush(nuc);
    p->drawEllipse(c + QPointF(-r * 0.06, r * 0.02), r * 0.20, r * 0.14);

    // vacuoles, spread around the nucleus
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(light, pen * 0.75));
    const qreal vac[5][3] = {
        { -0.46, -0.30, 0.12 },
        { -0.10, -0.52, 0.10 },
        {  0.34, -0.34, 0.11 },
        {  0.52,  0.06, 0.09 },
        { -0.34,  0.34, 0.10 },
    };
    for (const auto &v : vac)
        p->drawEllipse(c + QPointF(v[0] * r, v[1] * r), v[2] * r, v[2] * r);

    // endoplasmic squiggle, kept well inside the membrane
    QPainterPath er;
    er.moveTo(c + QPointF(r * 0.02, r * 0.44));
    for (int i = 0; i < 3; ++i) {
        const qreal x = r * (0.02 + 0.16 * i);
        er.quadTo(c + QPointF(x + r * 0.08, r * (i % 2 ? 0.60 : 0.28)),
                  c + QPointF(x + r * 0.16, r * 0.44));
    }
    p->setPen(QPen(light, pen * 0.7, Qt::SolidLine, Qt::RoundCap));
    p->drawPath(er);

    // interior arc, echoing the hand-drawn plate
    p->setPen(QPen(pink, pen * 0.7, Qt::SolidLine, Qt::RoundCap));
    QPainterPath arc;
    arc.moveTo(c + QPointF(-r * 0.58, -r * 0.06));
    arc.quadTo(c + QPointF(-r * 0.36, -r * 0.62), c + QPointF(-r * 0.04, -r * 0.70));
    p->drawPath(arc);
}

void paintChain(QPainter *p, const QPointF &c, qreal unit, const QColor &teal)
{
    const int    n    = 7;
    const qreal  rad  = unit * 0.52;
    const qreal  step = unit * 0.78;
    const qreal  pen  = qMax<qreal>(1.0, unit * 0.20);

    p->setPen(QPen(teal, pen));
    p->setBrush(Qt::NoBrush);
    for (int i = 0; i < n; ++i) {
        const qreal x = c.x() + (i - (n - 1) / 2.0) * step;
        const qreal y = c.y() + ((i % 2) ? unit * 0.34 : -unit * 0.34);
        p->drawEllipse(QPointF(x, y), rad, rad);
    }
}

void paintWordmark(QPainter *p, const QPointF &baselineCenter, qreal pixelSize,
                   const QColor &pink, const QColor &white)
{
    QFont f = badgeFont(pixelSize, QFont::DemiBold);
    f.setLetterSpacing(QFont::PercentageSpacing, 98);
    const QFontMetricsF fm(f);

    const QString head = QStringLiteral("B");
    const QString tail = QStringLiteral("miner");
    const qreal wHead = fm.horizontalAdvance(head);
    const qreal wTail = fm.horizontalAdvance(tail);
    const qreal x0 = baselineCenter.x() - (wHead + wTail) / 2.0;

    p->setFont(f);
    p->setBrush(Qt::NoBrush);
    p->setPen(pink);
    p->drawText(QPointF(x0, baselineCenter.y()), head);
    p->setPen(white);
    p->drawText(QPointF(x0 + wHead, baselineCenter.y()), tail);
}

} // namespace

namespace Logo {

void paintBacterium(QPainter *p, const QPointF &center, qreal length,
                    qreal angleDeg, qreal phase, const QColor &color)
{
    const qreal w   = length;
    const qreal h   = length * 0.44;
    const qreal pen = qMax<qreal>(1.1, length * 0.055);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(center);
    p->rotate(angleDeg);

    // flagella trail behind the body (-x is the tail end)
    p->setPen(QPen(color, qMax<qreal>(0.9, pen * 0.60), Qt::SolidLine, Qt::RoundCap));
    p->setBrush(Qt::NoBrush);
    for (int i = -1; i <= 1; ++i) {
        QPainterPath fl;
        const qreal y0 = i * h * 0.24;
        const qreal x0 = -w * 0.44;
        fl.moveTo(x0, y0);
        const int   seg = 16;
        const qreal len = w * 0.92;
        for (int s = 1; s <= seg; ++s) {
            const qreal t = qreal(s) / seg;
            const qreal x = x0 - len * t;
            const qreal y = y0 + h * 0.46 * t * std::sin(phase + t * 6.5 + i * 1.25);
            fl.lineTo(x, y);
        }
        p->drawPath(fl);
    }

    // body
    QColor fill = color;
    fill.setAlpha(52);
    p->setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(fill);
    p->drawRoundedRect(QRectF(-w / 2.0, -h / 2.0, w, h), h / 2.0, h / 2.0);

    // cytoplasmic granules
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(color, qMax<qreal>(0.9, pen * 0.55), Qt::SolidLine, Qt::RoundCap));
    const qreal gran[5][2] = { { -0.26, -0.10 }, { -0.06, 0.12 }, { 0.10, -0.14 },
                               {  0.24,  0.08 }, { -0.16, 0.02 } };
    for (const auto &g : gran) {
        const QPointF a(g[0] * w, g[1] * h);
        p->drawLine(a, a + QPointF(w * 0.055, 0.0));
    }

    p->restore();
}

/* The shipped artwork, kept out of paintBadgeDrawn() so the procedural
 * version stays available if the resource is ever missing. */
const QPixmap &badgeSource()
{
    static const QPixmap src(QStringLiteral(":/logo.png"));
    return src;
}

QPixmap scaledBadge(int devicePx)
{
    static QHash<int, QPixmap> cache;
    const auto it = cache.constFind(devicePx);
    if (it != cache.constEnd())
        return it.value();

    const QPixmap &src = badgeSource();
    if (src.isNull())
        return QPixmap();

    if (cache.size() > 12)
        cache.clear();
    const QPixmap out = src.scaled(devicePx, devicePx, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    cache.insert(devicePx, out);
    return out;
}

void paintBadgeDrawn(QPainter *p, const QRectF &box, bool withPlate)
{
    const qreal   S = qMin(box.width(), box.height());
    if (S <= 2.0)
        return;
    const qreal   R = S / 2.0;
    const QPointF c = box.center();

    const QColor pink   = QColor(0xE6, 0x3F, 0x77);
    const QColor purple = QColor(0x82, 0x63, 0xB6);
    const QColor teal   = QColor(0x46, 0xC0, 0xA2);
    const QColor white  = QColor(0xF4, 0xF4, 0xF7);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);

    // --- plate -------------------------------------------------------------
    if (withPlate) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x07, 0x07, 0x09));
        p->drawEllipse(c, R, R);
    }

    // --- ring, broken at 3 and 9 o'clock -----------------------------------
    const qreal Rr  = R * 0.955;
    const qreal rpw = qMax<qreal>(1.0, R * 0.016);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(pink, rpw, Qt::SolidLine, Qt::RoundCap));
    const QRectF ringRect(c.x() - Rr, c.y() - Rr, Rr * 2.0, Rr * 2.0);
    p->drawArc(ringRect,  16 * 15, 16 * 150);      // top
    p->drawArc(ringRect, 16 * 195, 16 * 150);      // bottom

    for (int sgn = -1; sgn <= 1; sgn += 2) {
        p->drawLine(QPointF(c.x() + sgn * Rr, c.y() - R * 0.115),
                    QPointF(c.x() + sgn * Rr, c.y() + R * 0.115));
        p->setPen(Qt::NoPen);
        p->setBrush(pink);
        p->drawEllipse(QPointF(c.x() + sgn * R * 0.865, c.y()), R * 0.026, R * 0.026);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(pink, rpw, Qt::SolidLine, Qt::RoundCap));
    }

    // --- arc lettering ------------------------------------------------------
    const QFont arcFont = badgeFont(R * 0.118, QFont::Bold);
    drawArcText(p, c, R * 0.775, QStringLiteral("what doesn't kill you"),
                arcFont, white, true,  1.15);
    drawArcText(p, c, R * 0.895, QStringLiteral("mutates and tries again"),
                arcFont, white, false, 1.15);

    // --- culture -----------------------------------------------------------
    // Positions are tuned so nothing crosses the lettering ring: the rod's
    // flagella reach about 0.5R past its own centre, so it sits well inboard.
    paintPhage(p, c + QPointF(-R * 0.44, -R * 0.38), R * 0.170, purple, white);
    paintBacterium(p, c + QPointF(R * 0.20, -R * 0.44), R * 0.34, 190.0, 1.1, pink);
    paintCell(p, c + QPointF(R * 0.33, -R * 0.05), R * 0.205, pink, purple, white);
    paintChain(p, c + QPointF(0.0, R * 0.60), R * 0.115, teal);

    // free-floating dots, as on the plate
    const qreal dots[5][3] = {
        { -0.10, -0.72, 0.022 }, {  0.04, -0.78, 0.018 }, { -0.20, -0.52, 0.020 },
        { -0.32, -0.66, 0.016 }, { -0.02, -0.28, 0.018 },
    };
    p->setPen(Qt::NoPen);
    p->setBrush(pink);
    for (const auto &d : dots)
        p->drawEllipse(c + QPointF(d[0] * R, d[1] * R), d[2] * R, d[2] * R);

    // --- wordmark -----------------------------------------------------------
    paintWordmark(p, c + QPointF(0.0, R * 0.37), R * 0.32, pink, white);

    p->restore();
}

void paintBadge(QPainter *p, const QRectF &box, bool withPlate)
{
    const qreal S = qMin(box.width(), box.height());
    if (S <= 2.0)
        return;

    const QPointF c = box.center();
    const QRectF  square(c.x() - S / 2.0, c.y() - S / 2.0, S, S);

    // Scale to device pixels, not layout pixels, so the badge stays sharp on
    // fractional-DPI screens; the results are cached per size.
    const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
    const QPixmap art = scaledBadge(qMax(16, qRound(S * dpr)));

    if (art.isNull()) {
        paintBadgeDrawn(p, box, withPlate);      // resource missing — draw it
        return;
    }

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // the artwork is a black square; clip it to its own ring
    QPainterPath clip;
    clip.addEllipse(square);
    p->setClipPath(clip);
    p->drawPixmap(square, art, QRectF(art.rect()));
    p->restore();
}

void paintMark(QPainter *p, const QRectF &box, bool withPlate)
{
    const qreal S = qMin(box.width(), box.height());
    if (S <= 1.0)
        return;
    const qreal   R = S / 2.0;
    const QPointF c = box.center();
    const QColor  pink = QColor(0xE6, 0x3F, 0x77);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    if (withPlate) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x07, 0x07, 0x09));
        p->drawEllipse(c, R, R);
    }

    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(pink, qMax<qreal>(1.0, R * 0.10), Qt::SolidLine, Qt::RoundCap));
    const qreal Rr = R * 0.90;
    const QRectF ringRect(c.x() - Rr, c.y() - Rr, Rr * 2.0, Rr * 2.0);
    p->drawArc(ringRect,  16 * 20, 16 * 140);
    p->drawArc(ringRect, 16 * 200, 16 * 140);

    paintBacterium(p, c + QPointF(R * 0.18, 0.0), R * 0.92, 182.0, 0.8, pink);

    p->restore();
}

QPixmap badgePixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintBadge(&p, QRectF(0, 0, size, size));
    return pm;
}

QPixmap markPixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintMark(&p, QRectF(0, 0, size, size));
    return pm;
}

QIcon appIcon()
{
    QIcon icon;

    // Below ~40 px the full badge turns to mush, so the small sizes use the
    // simplified mark and the large ones use the artwork.
    for (int s : { 16, 20, 24, 32 })
        icon.addPixmap(markPixmap(s));

    const bool haveArt = !badgeSource().isNull();
    for (int s : { 48, 64, 128, 256 })
        icon.addPixmap(haveArt ? badgePixmap(s) : markPixmap(s));

    return icon;
}

} // namespace Logo
