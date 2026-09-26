#pragma once

#include <QIcon>
#include <QPixmap>
#include <QRectF>

class QPainter;

/*
 * Everything here is drawn with QPainter — the app ships no image assets,
 * so the badge stays crisp at any DPI and any size.
 */
namespace Logo {

// Full circular badge: ring, arc lettering, phage / bacterium / cell, wordmark.
void paintBadge(QPainter *p, const QRectF &box, bool withPlate = true);

// Compact mark for the title bar, the tray and the window icon.
void paintMark(QPainter *p, const QRectF &box, bool withPlate = true);

// A single bacterium, used by the badge, the mark and the desktop pet.
void paintBacterium(QPainter *p, const QPointF &center, qreal length,
                    qreal angleDeg, qreal wigglePhase, const QColor &color);

QPixmap badgePixmap(int size);
QPixmap markPixmap(int size);
QIcon   appIcon();

} // namespace Logo
