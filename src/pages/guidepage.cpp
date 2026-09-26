#include "pages/guidepage.h"

#include "appsettings.h"
#include "theme.h"

#include <QBrush>
#include <QEasingCurve>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QRadialGradient>
#include <QFile>
#include <QHideEvent>
#include <QMediaPlayer>
#include <QShowEvent>
#include <QSvgRenderer>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVariantAnimation>
#include <QVBoxLayout>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#endif

#include <cmath>
#include <functional>

namespace {

/* ------------------------------------------------------------------ *
 *  Content — the Builder's documentation, set as a printed handbook.  *
 * ------------------------------------------------------------------ */

struct SpecimenNote
{
    QString organ;
    QString text;
};

struct Specimen
{
    QString art;
    QString binomial;
    QString common;
    QList<SpecimenNote> notes;
};

struct GuideSection
{
    QString title;
    QString body;
};

struct GuideChapter
{
    QString title;
    QString deck;
    QString intro;
    Specimen specimen;
    QList<GuideSection> sections;
    QString noteTitle;
    QString note;
};

const QList<GuideChapter> &chapters()
{
    static const QList<GuideChapter> data = {
        {
            QStringLiteral("Start here"),
            QStringLiteral("A field guide to the Builder"),
            QStringLiteral("A visual handbook to what the app does, what its settings mean, and what to check before any client is run."),
            {
                QStringLiteral("inverted-microscope.svg"),
                QStringLiteral("Speculum"),
                QStringLiteral("the lens"),
                {
                    { QStringLiteral("Aspectus"), QStringLiteral("Look at the whole Builder before touching anything.") },
                    { QStringLiteral("Cautio"), QStringLiteral("Study only devices you own, administer, or may examine.") },
                }
            },
            {
                { QStringLiteral("Know the purpose"),
                  QStringLiteral("Bminer configures and builds Monero mining clients. A client can use CPU resources, connect to a mining pool, and communicate through the network settings selected for it.") },
                { QStringLiteral("Read with the device owner"),
                  QStringLiteral("Only use this software on devices you own or administer, with the owner's informed, explicit permission. Explain the mining workload, network connections, and how to stop and remove the client.") },
            },
            QStringLiteral("The guiding principle"),
            QStringLiteral("Be visible, be specific, and make sure the person responsible for each device understands what will run on it.")
        },
        {
            QStringLiteral("The workspace"),
            QStringLiteral("Find your way around"),
            QStringLiteral("The sidebar opens each area of the Builder. The Dashboard summarises configuration; the other pages focus on settings, build output, control, and project information."),
            {
                QStringLiteral("cell-group.svg"),
                QStringLiteral("Colonia"),
                QStringLiteral("the colony"),
                {
                    { QStringLiteral("Tabula"), QStringLiteral("The Dashboard is one cell: a summary, not the source of truth.") },
                    { QStringLiteral("Regio"), QStringLiteral("Each sidebar area is a neighbouring cell with its own job.") },
                }
            },
            {
                { QStringLiteral("Dashboard"),
                  QStringLiteral("A quick readout of configured addresses and selected mining behavior. Treat it as a summary; verify important values in their source settings before use.") },
                { QStringLiteral("Settings and Build"),
                  QStringLiteral("Settings holds the values used by the Builder. Build reports progress and writes its output to the displayed build location. Check the log and output before sharing or running anything.") },
                { QStringLiteral("Control and Protect"),
                  QStringLiteral("These areas can affect other running clients or change a built binary. Read each option carefully and use such controls only within an approved, documented test scope.") },
            },
            QStringLiteral("Take a moment"),
            QStringLiteral("If a label or outcome is unclear, pause and verify it before continuing.")
        },
        {
            QStringLiteral("Configuration basics"),
            QStringLiteral("Understand the important values"),
            QStringLiteral("A few terms explain most of the mining configuration. Confirm every destination and workload with the person responsible for the device."),
            {
                QStringLiteral("simple-cell.svg"),
                QStringLiteral("Cellula"),
                QStringLiteral("the basic unit"),
                {
                    { QStringLiteral("Nucleus"), QStringLiteral("Pool and wallet: the values that direct work.") },
                    { QStringLiteral("Vis"), QStringLiteral("CPU effort: the share of processor capacity.") },
                    { QStringLiteral("Vestis"), QStringLiteral("TLS: a covering, not proof of ownership.") },
                }
            },
            {
                { QStringLiteral("Pool and wallet"),
                  QStringLiteral("A pool is the service a miner connects to. A wallet address identifies where mining proceeds are directed. Verify both values carefully; a typo or an unexpected destination matters.") },
                { QStringLiteral("CPU effort"),
                  QStringLiteral("CPU effort controls how much processor capacity mining may use. More work can mean more heat, power use, and reduced responsiveness. Measure impact on the actual test device.") },
                { QStringLiteral("TLS"),
                  QStringLiteral("TLS encrypts a supported connection in transit. It does not validate who owns a pool, prove that a client is trustworthy, or replace checking the configured destination.") },
            },
            QStringLiteral("Keep it transparent"),
            QStringLiteral("Disclose resource use and network destinations before a device is included in a test.")
        },
        {
            QStringLiteral("Builds and files"),
            QStringLiteral("Know what leaves the workspace"),
            QStringLiteral("A build turns the saved configuration into an output file. The progress area and log help you understand whether the build completed and where to review its result."),
            {
                QStringLiteral("well-plate.svg"),
                QStringLiteral("Putei"),
                QStringLiteral("the assay plate"),
                {
                    { QStringLiteral("Fons"), QStringLiteral("Saved settings, read at build time.") },
                    { QStringLiteral("Exitus"), QStringLiteral("Build output, reviewed before it is shared.") },
                }
            },
            {
                { QStringLiteral("Saved settings"),
                  QStringLiteral("Build reads saved settings. If Settings contains unsaved edits, the Builder prompts you; save them first if they are intended to be part of this build.") },
                { QStringLiteral("Review the result"),
                  QStringLiteral("Inspect the build log, output path, and intended configuration. Keep a record of the version, destination, and approved test scope for each artifact.") },
                { QStringLiteral("Handle artifacts deliberately"),
                  QStringLiteral("Store build outputs securely, share them only with authorized reviewers, and remove test files when the approved exercise is complete.") },
            },
            QStringLiteral("A successful build is not an approval"),
            QStringLiteral("Build status only reports a technical result. It does not establish permission to run or distribute the output.")
        },
        {
            QStringLiteral("Impact and control"),
            QStringLiteral("Treat host changes as high impact"),
            QStringLiteral("Some Builder options can change startup behavior, require elevated privileges, affect security settings, or interact with other processes. These choices deserve extra review."),
            {
                QStringLiteral("bacterium.svg"),
                QStringLiteral("Bacterium"),
                QStringLiteral("the hardy culture"),
                {
                    { QStringLiteral("Calor"), QStringLiteral("Heat, power draw, and reduced responsiveness.") },
                    { QStringLiteral("Iussum"), QStringLiteral("Elevated privileges and startup changes.") },
                    { QStringLiteral("Clausura"), QStringLiteral("Keep potent options contained to an approved scope.") },
                }
            },
            {
                { QStringLiteral("Set a narrow scope"),
                  QStringLiteral("Use a dedicated test machine or an explicitly approved environment. Record which devices and people are in scope, and agree on a stop time and recovery plan.") },
                { QStringLiteral("Protect the device owner"),
                  QStringLiteral("Do not conceal activity, bypass endpoint protection, or change a device without the owner's knowledge. Keep high-impact options disabled unless they are specifically approved and necessary for a controlled test.") },
                { QStringLiteral("Monitor and stop"),
                  QStringLiteral("Watch resource use and device health during an approved test. Stop promptly if the owner withdraws permission, unexpected behavior appears, or the agreed limits are reached.") },
            },
            QStringLiteral("Keep security review intact"),
            QStringLiteral("Binary-protection features can make inspection harder and may trigger security tools. They are not a security guarantee and must never be used to hide software from an owner or reviewer.")
        },
        {
            QStringLiteral("Before you run"),
            QStringLiteral("A short review checklist"),
            QStringLiteral("Use this page as a final pause before an authorized lab exercise. If any answer is uncertain, do not run the client yet."),
            {
                QStringLiteral("bacteria-swim.svg"),
                QStringLiteral("Motus"),
                QStringLiteral("the moment before swimming"),
                {
                    { QStringLiteral("Permissio"), QStringLiteral("Owner authorization, given in writing.") },
                    { QStringLiteral("Revelatio"), QStringLiteral("Disclosure of pool, wallet, and duration.") },
                    { QStringLiteral("Restitutio"), QStringLiteral("A documented stop-and-remove plan.") },
                }
            },
            {
                { QStringLiteral("Permission"),
                  QStringLiteral("Do I own or administer this device, or have explicit authorization from the responsible owner? Does the owner understand the software and its purpose?") },
                { QStringLiteral("Disclosure"),
                  QStringLiteral("Have I explained the pool and wallet destinations, expected CPU and power use, network activity, duration, and how the software will be stopped and removed?") },
                { QStringLiteral("Recovery"),
                  QStringLiteral("Is there a documented stop-and-remove plan, a responsible contact, and a way to restore the test device if something goes wrong?") },
            },
            QStringLiteral("If the answer is no"),
            QStringLiteral("Pause. Get written approval, clarify the scope, or choose a harmless offline demonstration instead.")
        },
        {
            QStringLiteral("Glossary"),
            QStringLiteral("A few useful definitions"),
            QStringLiteral("Short definitions for the terms used throughout the Builder and this guide."),
            {
                QStringLiteral("tree-of-life.svg"),
                QStringLiteral("Stirps"),
                QStringLiteral("the lineage"),
                {
                    { QStringLiteral("XMR"), QStringLiteral("The cryptocurrency this workload describes.") },
                    { QStringLiteral("Hash rate"), QStringLiteral("Mining calculations performed over time.") },
                    { QStringLiteral("Tox"), QStringLiteral("Peer-to-peer messaging: mind who can send changes.") },
                }
            },
            {
                { QStringLiteral("XMR · Monero"),
                  QStringLiteral("The cryptocurrency associated with the mining workload described by this Builder.") },
                { QStringLiteral("Hash rate"),
                  QStringLiteral("A measure of how many mining calculations are performed over time. It varies with hardware, workload, temperature, and other activity.") },
                { QStringLiteral("Endpoint / pool"),
                  QStringLiteral("An endpoint is a configured service address. A mining pool coordinates work among participants and receives miner connections.") },
                { QStringLiteral("Tox"),
                  QStringLiteral("A peer-to-peer communications technology used by parts of this application. Before using any remote-control feature, understand who can send changes and who is accountable for them.") },
            },
            QStringLiteral("Need more detail?"),
            QStringLiteral("Keep the project README and release notes alongside this guide, and update this handbook whenever the interface or behavior changes.")
        }
    };
    return data;
}

/* ------------------------------------------------------------------ *
 *  Artwork — CC0 SVG plates, rendered once per size and cached.       *
 * ------------------------------------------------------------------ */

constexpr qreal kArtScale = 2.0;
constexpr qreal kPi = 3.14159265358979323846;

/* Handbook ink-and-wash treatment for mounted plates. */
const QColor kPlateInk(0x3B, 0x2E, 0x22);
const QColor kPlateWash(0xE7, 0xD9, 0xBE);
constexpr qreal kPlateHueKeep = 0.10;

void stylePlateArt(QImage &image)
{
    QImage styled = image.convertToFormat(QImage::Format_ARGB32);
    const int width = styled.width();
    const int height = styled.height();
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(styled.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = line[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0)
                continue;
            const int sourceR = qRed(pixel);
            const int sourceG = qGreen(pixel);
            const int sourceB = qBlue(pixel);
            // Knock out scanner-white page backgrounds and white filler so the
            // specimen floats on the mount instead of bringing its own paper.
            // Anti-aliased edge greys are kept, so outlines stay smooth.
            if (qMin(sourceR, qMin(sourceG, sourceB)) >= 247) {
                line[x] = qRgba(0, 0, 0, 0);
                continue;
            }
            const qreal luminance = (0.299 * sourceR + 0.587 * sourceG + 0.114 * sourceB) / 255.0;
            const qreal inkAmount = std::pow(1.0 - luminance, 1.12);
            qreal red = kPlateWash.red() + (kPlateInk.red() - kPlateWash.red()) * inkAmount;
            qreal green = kPlateWash.green() + (kPlateInk.green() - kPlateWash.green()) * inkAmount;
            qreal blue = kPlateWash.blue() + (kPlateInk.blue() - kPlateWash.blue()) * inkAmount;
            red = red * (1.0 - kPlateHueKeep) + sourceR * kPlateHueKeep;
            green = green * (1.0 - kPlateHueKeep) + sourceG * kPlateHueKeep;
            blue = blue * (1.0 - kPlateHueKeep) + sourceB * kPlateHueKeep;
            line[x] = qRgba(qBound(0, int(red + 0.5), 255),
                            qBound(0, int(green + 0.5), 255),
                            qBound(0, int(blue + 0.5), 255),
                            alpha);
        }
    }
    image = styled;
}

QPixmap renderArt(const QString &name, const QSize &box)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1@%2x%3").arg(name).arg(box.width()).arg(box.height());
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pixmap;
    QSvgRenderer renderer(QStringLiteral(":/guide/%1").arg(name));
    if (renderer.isValid()) {
        QSizeF natural = renderer.defaultSize();
        if (natural.isEmpty())
            natural = renderer.viewBoxF().size();
        if (!natural.isEmpty()) {
            const QSize target = (natural * kArtScale).toSize().scaled(box * kArtScale, Qt::KeepAspectRatio);
            if (target.width() > 0 && target.height() > 0) {
                QImage image(target, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                renderer.render(&painter);
                painter.end();
                stylePlateArt(image);
                pixmap = QPixmap::fromImage(image);
            }
        }
    }
    cache.insert(key, pixmap);
    return pixmap;
}

QPixmap renderTintedArt(const QString &name, const QSize &box, const QColor &color)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1@%2x%3#%4")
                            .arg(name).arg(box.width()).arg(box.height())
                            .arg(color.rgba());
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pixmap;
    const QPixmap base = renderArt(name, box);
    if (!base.isNull()) {
        pixmap = QPixmap(base.size());
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.drawPixmap(0, 0, base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
    }
    cache.insert(key, pixmap);
    return pixmap;
}

/* Tight bounds of the visible ink, so a specimen can be centred by what is
   drawn rather than by the transparent padding around it. */
QRect contentBounds(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return QRect();
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                left = qMin(left, x);
                top = qMin(top, y);
                right = qMax(right, x);
                bottom = qMax(bottom, y);
            }
        }
    }
    if (right < left || bottom < top)
        return QRect();
    return QRect(left, top, right - left + 1, bottom - top + 1);
}

/* ------------------------------------------------------------------ *
 *  The desk — a dark surface with a faint drifting culture.           *
 * ------------------------------------------------------------------ */

struct CultureSpecimen
{
    const char *art;
    qreal x, y;
    qreal size;
    qreal rotation;
    qreal alpha;
    qreal drift;
};

const QList<CultureSpecimen> &cultureField()
{
    static const QList<CultureSpecimen> data = {
        { "bacterium.svg", 0.07, 0.14, 1.45, -22.0, 0.95, 7.0 },
        { "bacterium.svg", 0.86, 0.08, 1.00,  34.0, 0.80, 10.0 },
        { "bacterium.svg", 0.70, 0.60, 1.25, 118.0, 0.85, 8.0 },
        { "bacterium.svg", 0.13, 0.72, 0.90, -74.0, 0.72, 12.0 },
        { "bacterium.svg", 0.46, 0.38, 0.72, 205.0, 0.55, 6.0 },
        { "bacterium.svg", 0.93, 0.86, 1.10,  10.0, 0.68, 9.0 },
        { "bacterium.svg", 0.31, 0.94, 0.95, -140.0, 0.62, 11.0 },
    };
    return data;
}

void paintCultureField(QPainter &p, const QRect &area, qreal t, const QColor &accent,
                       qreal alpha, qreal sizeScale)
{
    if (area.isEmpty())
        return;

    p.save();
    p.setClipRect(area);

    QRadialGradient bloom(QPointF(area.left() + area.width() * 0.20,
                                  area.top() + area.height() * 0.06),
                          qMax(area.width(), area.height()) * 0.6);
    QColor glow = accent;
    glow.setAlphaF(alpha * 0.7);
    bloom.setColorAt(0.0, glow);
    glow.setAlpha(0);
    bloom.setColorAt(1.0, glow);
    p.fillRect(area, bloom);

    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const CultureSpecimen &s : cultureField()) {
        const int box = qRound(64.0 * s.size * sizeScale);
        if (box < 6)
            continue;
        const QPixmap art = renderTintedArt(QString::fromLatin1(s.art), QSize(box, box), accent);
        if (art.isNull())
            continue;

        const qreal dx = std::sin(t * 0.21 + s.x * 6.0) * s.drift;
        const qreal dy = std::cos(t * 0.17 + s.y * 5.0) * s.drift * 0.6;
        const QPointF centre(area.left() + area.width() * s.x + dx,
                             area.top() + area.height() * s.y + dy);

        p.save();
        p.setOpacity(qBound(0.0, s.alpha * alpha, 1.0));
        p.translate(centre);
        p.rotate(s.rotation + std::sin(t * 0.09 + s.y * 3.0) * 5.0);
        p.drawPixmap(QPointF(-art.width() / (2.0 * kArtScale), -art.height() / (2.0 * kArtScale)), art);
        p.restore();
    }
    p.restore();
}

/* ------------------------------------------------------------------ *
 *  Page rendering — each page of the book is painted to an image.     *
 * ------------------------------------------------------------------ */

constexpr int kPageW = 540;
constexpr int kPageH = 740;
constexpr qreal kMarginX = 46.0;
constexpr qreal kMarginTop = 46.0;
constexpr qreal kMarginBottom = 52.0;

// Print palette (the paper pages use ink colours, not the app theme).
const QColor kPaperHi(0xF4, 0xEE, 0xE0);
const QColor kPaperLo(0xE4, 0xDA, 0xC2);
const QColor kPaperEdge(0xC6, 0xB9, 0x9B);
const QColor kInk(0x2B, 0x24, 0x1A);
const QColor kInkDim(0x6E, 0x61, 0x4E);
const QColor kInkFaint(0x9A, 0x8D, 0x76);
const QColor kCrimson(0x8E, 0x3B, 0x46);
const QColor kTealInk(0x2F, 0x6F, 0x6A);

enum PageSide { LeftPage, RightPage };

QFont serifFont(int px, bool bold = false, bool italic = false)
{
    QFont f(QStringLiteral("Georgia"));
    f.setStyleHint(QFont::Serif);
    f.setPixelSize(px);
    f.setBold(bold);
    f.setItalic(italic);
    return f;
}

QFont capsFont(int px, qreal spacing = 1.6, bool bold = false)
{
    QFont f = serifFont(px, bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    return f;
}

/* Draw wrapped text; returns the height used. */
qreal drawWrapped(QPainter &p, const QString &text, const QRectF &rect,
                  const QFont &font, const QColor &color,
                  int flags = Qt::AlignLeft | Qt::AlignTop)
{
    p.setFont(font);
    p.setPen(color);
    QRectF bounds;
    p.drawText(rect, flags | Qt::TextWordWrap, text, &bounds);
    return bounds.height();
}

qreal wrappedHeight(const QString &text, qreal width, const QFont &font)
{
    const QFontMetricsF fm(font);
    return fm.boundingRect(QRectF(0, 0, width, 10000), Qt::TextWordWrap, text).height();
}

QString roman(int n)
{
    static const char *numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    return (n >= 1 && n <= 7) ? QString::fromLatin1(numerals[n - 1])
                              : QString::number(n);
}

/* The paper itself: warm stock, a soft vignette, and faint grain. */
void paintPaper(QPainter &p, PageSide side)
{
    const QRectF page(0, 0, kPageW, kPageH);

    QLinearGradient stock(0, 0, 0, kPageH);
    stock.setColorAt(0.0, kPaperHi);
    stock.setColorAt(1.0, kPaperLo);
    p.fillRect(page, stock);

    // Vignette so the sheet feels printed, not flat.
    QRadialGradient vignette(QPointF(kPageW / 2.0, kPageH / 2.0), kPageH * 0.75);
    vignette.setColorAt(0.0, QColor(90, 70, 40, 0));
    vignette.setColorAt(1.0, QColor(90, 70, 40, 22));
    p.fillRect(page, vignette);

    // A whisper of grain.
    p.setPen(Qt::NoPen);
    quint32 seed = 4369;
    for (int i = 0; i < 90; ++i) {
        seed = seed * 1103515245u + 12345u;
        const qreal x = (seed >> 16) % kPageW;
        seed = seed * 1103515245u + 12345u;
        const qreal y = (seed >> 16) % kPageH;
        seed = seed * 1103515245u + 12345u;
        const int a = 4 + (seed >> 16) % 7;
        p.setBrush(QColor(70, 55, 30, a));
        p.drawEllipse(QPointF(x, y), 0.8, 0.8);
    }

    // The spine side falls into shadow.
    QLinearGradient gutter;
    if (side == LeftPage) {
        gutter = QLinearGradient(kPageW - 56, 0, kPageW, 0);
        gutter.setColorAt(0.0, QColor(60, 45, 25, 0));
        gutter.setColorAt(1.0, QColor(60, 45, 25, 46));
    } else {
        gutter = QLinearGradient(0, 0, 56, 0);
        gutter.setColorAt(0.0, QColor(60, 45, 25, 46));
        gutter.setColorAt(1.0, QColor(60, 45, 25, 0));
    }
    p.fillRect(page, gutter);

    // Trim edge.
    p.setPen(QPen(kPaperEdge, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(page.adjusted(0.5, 0.5, -0.5, -0.5));
}

/* Running head, folio, and a faint printed bacterium. */
void paintFurniture(QPainter &p, PageSide side, const QString &head, int folio)
{
    const bool left = (side == LeftPage);
    const QRectF headRect(left ? kMarginX : kPageW / 2.0, 20.0,
                          kPageW / 2.0 - kMarginX, 14.0);
    p.setFont(capsFont(9, 1.4));
    p.setPen(kInkFaint);
    p.drawText(headRect, (left ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
               head.toUpper());

    p.setPen(QPen(kPaperEdge, 1));
    p.drawLine(QPointF(kMarginX, 40.0), QPointF(kPageW - kMarginX, 40.0));

    const QRectF folioRect(left ? kMarginX : kPageW - kMarginX - 60.0,
                           kPageH - 34.0, 60.0, 14.0);
    p.setFont(serifFont(10));
    p.setPen(kInkDim);
    p.drawText(folioRect, (left ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
               QString::number(folio));

    // A small culture mark printed into the lower gutter, like a stamp.
    const QPixmap mark = renderTintedArt(QStringLiteral("bacterium.svg"),
                                         QSize(44, 44), kCrimson);
    if (!mark.isNull()) {
        p.save();
        p.setOpacity(0.10);
        p.translate(left ? kPageW - 66.0 : 66.0, kPageH - 52.0);
        p.rotate(left ? -24.0 : 24.0);
        p.drawPixmap(QPointF(-mark.width() / (2.0 * kArtScale),
                             -mark.height() / (2.0 * kArtScale)), mark);
        p.restore();
    }
}

void paintTitlePage(QPainter &p)
{
    paintPaper(p, RightPage);

    // A round seal with a specimen inside.
    const QPointF sealC(kPageW / 2.0, 218.0);
    p.setPen(QPen(kCrimson, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(sealC, 64, 64);
    p.setPen(QPen(kCrimson, 0.8));
    p.drawEllipse(sealC, 58, 58);
    const QPixmap specimen = renderTintedArt(QStringLiteral("phage.svg"),
                                             QSize(86, 86), kCrimson);
    if (!specimen.isNull()) {
        p.save();
        p.setOpacity(0.85);
        QRect ink = contentBounds(specimen);
        if (ink.isNull())
            ink = specimen.rect();
        const qreal fit = 92.0; // logical px inside the 58px inner ring
        const qreal scale = fit / qMax(ink.width() / kArtScale, ink.height() / kArtScale);
        const qreal sealW = ink.width() / kArtScale * scale;
        const qreal sealH = ink.height() / kArtScale * scale;
        p.drawPixmap(QRectF(sealC.x() - sealW / 2.0, sealC.y() - sealH / 2.0, sealW, sealH),
                     specimen, QRectF(ink));
        p.restore();
    }

    qreal y = 322.0;
    p.setFont(capsFont(13, 4.0));
    p.setPen(kInkDim);
    p.drawText(QRectF(0, y, kPageW, 18), Qt::AlignHCenter, QStringLiteral("B M I N E R"));
    y += 34.0;

    p.setFont(serifFont(30, true));
    p.setPen(kInk);
    p.drawText(QRectF(0, y, kPageW, 40), Qt::AlignHCenter, QStringLiteral("A Field Guide"));
    y += 40.0;
    p.setFont(serifFont(30, true));
    p.drawText(QRectF(0, y, kPageW, 40), Qt::AlignHCenter, QStringLiteral("to the Builder"));
    y += 56.0;

    // Rule with a diamond.
    p.setPen(QPen(kCrimson, 1));
    p.drawLine(QPointF(kPageW / 2.0 - 70, y), QPointF(kPageW / 2.0 - 10, y));
    p.drawLine(QPointF(kPageW / 2.0 + 10, y), QPointF(kPageW / 2.0 + 70, y));
    p.setBrush(kCrimson);
    QPainterPath diamond;
    diamond.moveTo(kPageW / 2.0, y - 4.5);
    diamond.lineTo(kPageW / 2.0 + 4.5, y);
    diamond.lineTo(kPageW / 2.0, y + 4.5);
    diamond.lineTo(kPageW / 2.0 - 4.5, y);
    diamond.closeSubpath();
    p.drawPath(diamond);
    y += 26.0;

    p.setFont(serifFont(12, false, true));
    p.setPen(kInkDim);
    p.drawText(QRectF(0, y, kPageW, 18), Qt::AlignHCenter,
               QStringLiteral("Being a documentation of the mining client builder,"));
    p.drawText(QRectF(0, y + 18, kPageW, 18), Qt::AlignHCenter,
               QStringLiteral("its settings, and its proper use."));

    p.setFont(capsFont(9, 1.8));
    p.setPen(kInkFaint);
    p.drawText(QRectF(0, kPageH - 118.0, kPageW, 14), Qt::AlignHCenter,
               QStringLiteral("SEVEN CHAPTERS  ·  SEVEN PLATES"));
    p.drawText(QRectF(0, kPageH - 96.0, kPageW, 14), Qt::AlignHCenter,
               QStringLiteral("SPECIMEN EDITION"));
    p.setFont(serifFont(9, false, true));
    p.drawText(QRectF(0, kPageH - 66.0, kPageW, 14), Qt::AlignHCenter,
               QStringLiteral("For authorized, transparent use only."));
}

/* The contents page; records where each entry sits for mouse jumps. */
void paintTocPage(QPainter &p, QVector<QRectF> &hitRects)
{
    paintPaper(p, LeftPage);
    paintFurniture(p, LeftPage, QStringLiteral("A Field Guide to the Builder"), 2);

    qreal y = 84.0;
    p.setFont(serifFont(24, true));
    p.setPen(kInk);
    p.drawText(QRectF(kMarginX, y, kPageW - 2 * kMarginX, 32),
               Qt::AlignLeft, QStringLiteral("Contents"));
    y += 52.0;

    const QFont numFont = capsFont(10, 1.2, true);
    const QFont entryFont = serifFont(13);
    const QFont pageFont = serifFont(11);
    const QFontMetricsF entryMetrics(entryFont);
    const QFontMetricsF pageMetrics(pageFont);
    const QFontMetricsF dotMetrics(serifFont(11));

    hitRects.clear();
    const auto &book = chapters();
    for (int i = 0; i < book.size(); ++i) {
        const QRectF row(kMarginX, y, kPageW - 2 * kMarginX, 26.0);
        hitRects.append(row);

        p.setFont(numFont);
        p.setPen(kCrimson);
        const QString num = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        p.drawText(QRectF(row.left(), row.top(), 26, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, num);

        p.setFont(entryFont);
        p.setPen(kInk);
        const qreal titleX = row.left() + 34.0;
        p.drawText(QRectF(titleX, row.top(), row.width() - 34.0, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, book.at(i).title);

        const QString folio = QString::number(3 + 2 * i);
        const qreal folioW = pageMetrics.horizontalAdvance(folio);
        p.setFont(pageFont);
        p.setPen(kInkDim);
        p.drawText(QRectF(row.right() - folioW, row.top(), folioW, row.height()),
                   Qt::AlignRight | Qt::AlignVCenter, folio);

        // Dot leaders between title and folio.
        const qreal titleW = entryMetrics.horizontalAdvance(book.at(i).title);
        const qreal dotsFrom = titleX + titleW + 8.0;
        const qreal dotsTo = row.right() - folioW - 8.0;
        if (dotsTo > dotsFrom) {
            const qreal dotW = dotMetrics.horizontalAdvance(QStringLiteral(" ."));
            const int count = int((dotsTo - dotsFrom) / dotW);
            if (count > 0) {
                p.setFont(serifFont(11));
                p.setPen(kInkFaint);
                p.drawText(QRectF(dotsFrom, row.top(), dotsTo - dotsFrom, row.height()),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QString(QStringLiteral(" .")).repeated(count));
            }
        }
        y += 30.0;
    }

    y += 18.0;
    p.setPen(QPen(kPaperEdge, 1));
    p.drawLine(QPointF(kMarginX, y), QPointF(kPageW - kMarginX, y));
    y += 16.0;
    p.setFont(serifFont(10, false, true));
    p.setPen(kInkDim);
    p.drawText(QRectF(kMarginX, y, kPageW - 2 * kMarginX, 60),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QStringLiteral("Turn the leaves with a click on either page, the corner "
                              "of a leaf, the arrow keys, or the buttons below the book. "
                              "Touch a chapter above to open it directly."));
}

/* Left page of a chapter spread: the mounted specimen plate. */
void paintPlatePage(QPainter &p, int chapterIndex, int folio)
{
    const GuideChapter &chapter = chapters().at(chapterIndex);
    const Specimen &s = chapter.specimen;

    paintPaper(p, LeftPage);
    paintFurniture(p, LeftPage, chapter.title, folio);

    qreal y = 62.0;
    p.setFont(capsFont(10, 2.0, true));
    p.setPen(kCrimson);
    p.drawText(QRectF(kMarginX, y, kPageW - 2 * kMarginX, 14),
               Qt::AlignLeft, QStringLiteral("PLATE %1").arg(roman(chapterIndex + 1)));
    y += 22.0;

    p.setFont(serifFont(22, false, true));
    p.setPen(kInk);
    p.drawText(QRectF(kMarginX, y, kPageW - 2 * kMarginX, 28), Qt::AlignLeft, s.binomial);
    y += 30.0;

    p.setFont(serifFont(12, false, true));
    p.setPen(kInkDim);
    p.drawText(QRectF(kMarginX, y, kPageW - 2 * kMarginX, 16), Qt::AlignLeft,
               QStringLiteral("— ") + s.common);
    y += 26.0;

    // The mount: a lighter card with a hairline frame.
    const QRectF mount(kMarginX, y, kPageW - 2 * kMarginX, 300.0);
    p.setPen(QPen(kPaperEdge, 1));
    p.setBrush(QColor(0xFA, 0xF6, 0xEA));
    p.drawRect(mount);
    p.setPen(QPen(QColor(0xD8, 0xCC, 0xAE), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(mount.adjusted(4.5, 4.5, -4.5, -4.5));

    const QSize artBox(int(mount.width()) - 40, int(mount.height()) - 40);
    const QPixmap art = renderArt(s.art, artBox);
    if (!art.isNull()) {
        const qreal w = art.width() / kArtScale;
        const qreal h = art.height() / kArtScale;
        p.drawPixmap(QRectF(mount.center().x() - w / 2.0, mount.center().y() - h / 2.0, w, h),
                     art, QRectF(art.rect()));
    }

    // Scale bar under the mount, right hand side.
    const qreal barY = mount.bottom() + 12.0;
    const qreal barX = mount.right() - 64.0;
    p.setPen(QPen(kInkDim, 1.4));
    p.drawLine(QPointF(barX, barY), QPointF(barX + 52.0, barY));
    p.drawLine(QPointF(barX, barY - 3), QPointF(barX, barY + 3));
    p.drawLine(QPointF(barX + 52.0, barY - 3), QPointF(barX + 52.0, barY + 3));
    p.setFont(serifFont(8));
    p.setPen(kInkDim);
    p.drawText(QRectF(barX - 12.0, barY + 5.0, 76.0, 12), Qt::AlignHCenter,
               QStringLiteral("1 : 1"));

    // Annotations with dot leaders.
    qreal ay = mount.bottom() + 34.0;
    const QFont organFont = capsFont(9, 1.2, true);
    const QFont noteFont = serifFont(11);
    const QFontMetricsF organMetrics(organFont);
    for (const SpecimenNote &note : s.notes) {
        p.setFont(organFont);
        p.setPen(kCrimson);
        p.drawText(QRectF(kMarginX, ay, 120.0, 14), Qt::AlignLeft, note.organ.toUpper());

        const qreal organW = organMetrics.horizontalAdvance(note.organ.toUpper());
        const qreal textX = kMarginX + 130.0;
        const qreal textW = kPageW - kMarginX - textX;
        drawWrapped(p, note.text, QRectF(textX, ay - 1.0, textW, 40.0), noteFont, kInk);

        // Leader dots between the organ name and the note.
        const qreal dotsFrom = kMarginX + organW + 6.0;
        const qreal dotsTo = textX - 6.0;
        if (dotsTo > dotsFrom) {
            p.setFont(serifFont(10));
            p.setPen(kInkFaint);
            const qreal dotW = QFontMetricsF(serifFont(10)).horizontalAdvance(QStringLiteral("."));
            const int count = int((dotsTo - dotsFrom) / dotW);
            if (count > 0)
                p.drawText(QRectF(dotsFrom, ay, dotsTo - dotsFrom, 14),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QString(QStringLiteral(".")).repeated(count));
        }
        ay += 34.0;
    }

    // Figure caption along the bottom.
    p.setFont(serifFont(10, false, true));
    p.setPen(kInkDim);
    p.drawText(QRectF(kMarginX, kPageH - kMarginBottom - 26.0, kPageW - 2 * kMarginX, 30),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QStringLiteral("Fig. %1 — %2, %3. After the specimen in the collection.")
                   .arg(chapterIndex + 1).arg(s.binomial, s.common));
}

/* Right page of a chapter spread: the field notes. */
void paintTextPage(QPainter &p, int chapterIndex, int folio)
{
    const GuideChapter &chapter = chapters().at(chapterIndex);

    paintPaper(p, RightPage);
    paintFurniture(p, RightPage, chapter.title, folio);

    const qreal textW = kPageW - 2 * kMarginX;
    qreal y = 62.0;

    p.setFont(capsFont(10, 2.0, true));
    p.setPen(kCrimson);
    p.drawText(QRectF(kMarginX, y, textW, 14), Qt::AlignLeft,
               QStringLiteral("CHAPTER %1").arg(chapterIndex + 1));
    y += 24.0;

    y += drawWrapped(p, chapter.title, QRectF(kMarginX, y, textW, 60),
                     serifFont(23, true), kInk) + 6.0;
    y += drawWrapped(p, chapter.deck, QRectF(kMarginX, y, textW, 34),
                     serifFont(12, false, true), kInkDim) + 10.0;

    p.setPen(QPen(kPaperEdge, 1));
    p.drawLine(QPointF(kMarginX, y), QPointF(kPageW - kMarginX, y));
    y += 12.0;

    y += drawWrapped(p, chapter.intro, QRectF(kMarginX, y, textW, 80),
                     serifFont(11), kInk) + 12.0;

    for (const GuideSection &section : chapter.sections) {
        p.setFont(capsFont(10, 1.4, true));
        p.setPen(kCrimson);
        p.drawText(QRectF(kMarginX, y, textW, 14), Qt::AlignLeft, section.title.toUpper());
        y += 18.0;
        y += drawWrapped(p, section.body, QRectF(kMarginX, y, textW, 90),
                         serifFont(11), kInk) + 12.0;
    }

    // The marginal note, set on a tinted slip.
    const qreal noteTitleH = 16.0;
    const qreal noteBodyH = wrappedHeight(chapter.note, textW - 24.0, serifFont(10));
    const qreal noteH = noteTitleH + noteBodyH + 22.0;
    const qreal maxY = kPageH - kMarginBottom;
    if (y + noteH > maxY)
        y = maxY - noteH;
    const QRectF slip(kMarginX, y, textW, noteH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x8E, 0x3B, 0x46, 16));
    p.drawRoundedRect(slip, 4, 4);
    p.setBrush(kCrimson);
    p.drawRect(QRectF(slip.left(), slip.top() + 4, 3, slip.height() - 8));

    p.setFont(capsFont(9, 1.2, true));
    p.setPen(kCrimson);
    p.drawText(QRectF(slip.left() + 12, slip.top() + 8, textW - 24, noteTitleH),
               Qt::AlignLeft, chapter.noteTitle.toUpper());
    drawWrapped(p, chapter.note,
                QRectF(slip.left() + 12, slip.top() + 8 + noteTitleH, textW - 24, noteBodyH + 4),
                serifFont(10), kInk);
}

void paintColophonPage(QPainter &p, int folio)
{
    paintPaper(p, LeftPage);
    paintFurniture(p, LeftPage, QStringLiteral("Colophon"), folio);

    qreal y = 84.0;
    p.setFont(serifFont(22, true));
    p.setPen(kInk);
    p.drawText(QRectF(kMarginX, y, kPageW - 2 * kMarginX, 30), Qt::AlignLeft,
               QStringLiteral("Colophon"));
    y += 48.0;

    const qreal textW = kPageW - 2 * kMarginX;
    y += drawWrapped(p, QStringLiteral("This handbook is drawn by the application itself. "
                                       "The specimen plates mount public-domain scientific artwork "
                                       "from the Bioicons collection (CC0 1.0):"),
                     QRectF(kMarginX, y, textW, 90), serifFont(11), kInk) + 10.0;
    y += drawWrapped(p, QStringLiteral("Microscope — Divakar-Badal\n"
                                       "Cell group and simple cell — JhonnyXC, Marnie-Maddock\n"
                                       "Assay plate — OpenClipart\n"
                                       "Bacteria, phage, and lineage — Pauline Franz, James Lloyd, Geomicrobio"),
                     QRectF(kMarginX + 14, y, textW - 14, 90), serifFont(10, false, true), kInkDim) + 10.0;
    y += drawWrapped(p, QStringLiteral("No attribution is required by the licence; it is given "
                                       "here anyway, as a field guide should. Type is Georgia; "
                                       "the desk, the culture, and the binding are Qt."),
                     QRectF(kMarginX, y, textW, 90), serifFont(11), kInk) + 16.0;

    p.setPen(QPen(kPaperEdge, 1));
    p.drawLine(QPointF(kMarginX, y), QPointF(kPageW - kMarginX, y));
    y += 14.0;
    p.setFont(capsFont(9, 1.4, true));
    p.setPen(kCrimson);
    p.drawText(QRectF(kMarginX, y, textW, 14), Qt::AlignLeft,
               QStringLiteral("A REMINDER"));
    y += 18.0;
    drawWrapped(p, QStringLiteral("This software is for devices you own or administer, with the "
                                  "owner's informed, explicit permission. Be visible, be specific, "
                                  "and keep security review intact."),
                QRectF(kMarginX, y, textW, 70), serifFont(10), kInk);
}

void paintClosingPage(QPainter &p, int folio)
{
    paintPaper(p, RightPage);
    paintFurniture(p, RightPage, QStringLiteral("A Field Guide to the Builder"), folio);

    const QPixmap art = renderTintedArt(QStringLiteral("bacterium.svg"),
                                        QSize(120, 120), kTealInk);
    if (!art.isNull()) {
        p.save();
        p.setOpacity(0.8);
        p.drawPixmap(QRectF(kPageW / 2.0 - 60, 268.0, 120, 120),
                     art, QRectF(art.rect()));
        p.restore();
    }

    p.setFont(capsFont(12, 4.0));
    p.setPen(kInkDim);
    p.drawText(QRectF(0, 412.0, kPageW, 18), Qt::AlignHCenter, QStringLiteral("F I N I S"));
    p.setFont(serifFont(11, false, true));
    p.setPen(kInkDim);
    p.drawText(QRectF(0, 438.0, kPageW, 16), Qt::AlignHCenter,
               QStringLiteral("End of the field guide."));
}

} // namespace

/* ------------------------------------------------------------------ *
 *  FlipBook — the open book, the page-turn, and the desk.             *
 * ------------------------------------------------------------------ */

class FlipBook final : public QWidget
{
public:
    std::function<void(int spread, int spreadCount)> onSpreadChanged;

    explicit FlipBook(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumSize(560, 460);

        m_motion = AppSettings::i()->getBool(Keys::UiAnimations);

        m_driftTimer = new QTimer(this);
        m_driftTimer->setInterval(80);
        connect(m_driftTimer, &QTimer::timeout, this, [this] {
            m_driftTime += 0.08;
            if (!m_turning)
                update();
        });
    }

    int spread() const { return m_spread; }
    int spreadCount() const { return (pageCount() + 1) / 2; }
    bool canNext() const { return m_spread < spreadCount() - 1; }
    bool canPrevious() const { return m_spread > 0; }

    void next() { turnTo(m_spread + 1); }
    void previous() { turnTo(m_spread - 1); }

    void goToSpread(int target)
    {
        target = qBound(0, target, spreadCount() - 1);
        if (target == m_spread || m_turning)
            return;
        if (std::abs(target - m_spread) == 1) {
            turnTo(target);
            return;
        }
        m_spread = target;
        emitChanged();
        update();
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (m_motion)
            m_driftTimer->start();
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        m_driftTimer->stop();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateGeometryCache();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Space) {
            next();
            return;
        }
        if (event->key() == Qt::Key_Left) {
            previous();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const Corner hover = cornerAt(event->position());
        if (hover != m_hover) {
            m_hover = hover;
            setCursor(m_hover == NoCorner ? Qt::ArrowCursor : Qt::PointingHandCursor);
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hover != NoCorner) {
            m_hover = NoCorner;
            unsetCursor();
            update();
        }
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        setFocus();
        if (m_turning)
            return;

        const QPointF pos = event->position();

        // Contents page: chapter entries jump straight to a spread.
        if (m_spread == 0 && m_rightRect.contains(pos)) {
            const QPointF inPage = (pos - m_rightRect.topLeft()) / m_bookScale;
            for (int i = 0; i < m_tocHits.size(); ++i) {
                if (m_tocHits.at(i).contains(inPage)) {
                    goToSpread(i + 1);
                    return;
                }
            }
        }

        if (m_rightRect.contains(pos)) {
            next();
            return;
        }
        if (m_leftRect.contains(pos)) {
            previous();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        // The desk.
        p.fillRect(rect(), Theme::Bg);
        paintCultureField(p, rect(), m_driftTime, Theme::accent(), 0.06, 1.0);
        {
            QRadialGradient shade(rect().center(), qMax(width(), height()) * 0.72);
            shade.setColorAt(0.0, QColor(0, 0, 0, 0));
            shade.setColorAt(1.0, QColor(0, 0, 0, 70));
            p.fillRect(rect(), shade);
        }

        if (m_leftRect.isNull())
            return;

        drawBookShadow(p);
        drawCover(p);
        drawPageBlockEdges(p);

        // Static leaves beneath any turning sheet.
        const int leftIndex = m_turning && m_direction < 0 ? 2 * m_spread - 2 : 2 * m_spread;
        const int rightIndex = m_turning && m_direction > 0 ? 2 * m_spread + 3 : 2 * m_spread + 1;
        drawPage(p, leftIndex, m_leftRect);
        drawPage(p, rightIndex, m_rightRect);
        drawSpineShadow(p);

        if (m_turning)
            drawTurningSheet(p);
        else
            drawCornerHint(p);
    }

private:
    enum Corner { NoCorner, BottomLeft, BottomRight };

    static int pageCount() { return 2 + 2 * chapters().size() + 2; } // title+toc, plates+text, colophon+finis

    void emitChanged()
    {
        if (onSpreadChanged)
            onSpreadChanged(m_spread, spreadCount());
    }

    void updateGeometryCache()
    {
        const qreal availW = width() - 56.0;
        const qreal availH = height() - 40.0;
        m_bookScale = qMin(availW / (2.0 * kPageW), availH / qreal(kPageH));
        m_bookScale = qBound(0.4, m_bookScale, 1.4);

        const qreal pageW = kPageW * m_bookScale;
        const qreal pageH = kPageH * m_bookScale;
        const qreal ox = (width() - 2.0 * pageW) / 2.0;
        const qreal oy = (height() - pageH) / 2.0 - 4.0;
        m_leftRect = QRectF(ox, oy, pageW, pageH);
        m_rightRect = QRectF(ox + pageW, oy, pageW, pageH);
        m_spineX = ox + pageW;

        const qreal target = qBound(1.0, m_bookScale * devicePixelRatioF(), 2.5);
        if (qAbs(target - m_renderScale) > 0.15) {
            m_renderScale = target;
            m_pageImages.clear();
        }
        update();
    }

    const QImage &pageImage(int index)
    {
        auto it = m_pageImages.constFind(index);
        if (it != m_pageImages.constEnd())
            return it.value();

        QImage image(int(kPageW * m_renderScale), int(kPageH * m_renderScale),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.scale(m_renderScale, m_renderScale);

            const int chapterCount = chapters().size();
            if (index == 0) {
                paintTitlePage(p);
            } else if (index == 1) {
                paintTocPage(p, m_tocHits);
            } else if (index >= 2 && index < 2 + 2 * chapterCount) {
                const int chapter = (index - 2) / 2;
                if ((index - 2) % 2 == 0)
                    paintPlatePage(p, chapter, index + 1);
                else
                    paintTextPage(p, chapter, index + 1);
            } else if (index == 2 + 2 * chapterCount) {
                paintColophonPage(p, index + 1);
            } else {
                paintClosingPage(p, index + 1);
            }
        }
        return *m_pageImages.insert(index, image);
    }

    void drawBookShadow(QPainter &p)
    {
        const QRectF book = m_leftRect.united(m_rightRect);
        for (int i = 0; i < 5; ++i) {
            const qreal grow = 6.0 + i * 7.0;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 16 - i * 2));
            p.drawRoundedRect(book.adjusted(-grow, -grow * 0.4, grow, grow + 10.0), 14, 14);
        }
    }

    void drawCover(QPainter &p)
    {
        const QRectF book = m_leftRect.united(m_rightRect);
        const QRectF cover = book.adjusted(-13, -11, 13, 13);

        QLinearGradient leather(cover.topLeft(), cover.bottomLeft());
        leather.setColorAt(0.0, QColor(0x21, 0x20, 0x27));
        leather.setColorAt(1.0, QColor(0x15, 0x14, 0x1A));
        p.setPen(QPen(QColor(0x33, 0x32, 0x3D), 1));
        p.setBrush(leather);
        p.drawRoundedRect(cover, 8, 8);

        // Embossed frame on the cover edge.
        p.setPen(QPen(QColor(255, 255, 255, 14), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(cover.adjusted(4.5, 4.5, -4.5, -4.5), 6, 6);

        // Hinge crease along the spine.
        p.setPen(QPen(QColor(0, 0, 0, 90), 1.4));
        p.drawLine(QPointF(m_spineX, cover.top() + 4), QPointF(m_spineX, cover.bottom() - 4));
    }

    void drawPageBlockEdges(QPainter &p)
    {
        // The thickness of the paper block at the fore-edge and tail.
        p.setPen(Qt::NoPen);
        for (int i = 1; i <= 3; ++i) {
            p.setBrush(QColor(0xD9, 0xCE, 0xB2).darker(100 + i * 6));
            p.drawRect(QRectF(m_rightRect.right() + i * 1.6, m_rightRect.top() + 2.0 + i,
                              1.6, m_rightRect.height() - 2.0 - i));
            p.drawRect(QRectF(m_leftRect.left() - (i + 1) * 1.6, m_leftRect.top() + 2.0 + i,
                              1.6, m_leftRect.height() - 2.0 - i));
        }
        for (int i = 1; i <= 2; ++i) {
            p.setBrush(QColor(0xD9, 0xCE, 0xB2).darker(104 + i * 6));
            p.drawRect(QRectF(m_leftRect.left() + 2.0, m_leftRect.bottom() + i * 1.6,
                              m_leftRect.width() * 2 - 4.0, 1.6));
        }
    }

    void drawPage(QPainter &p, int index, const QRectF &rect)
    {
        if (index < 0 || index >= pageCount() || rect.isNull())
            return;
        p.drawImage(rect, pageImage(index));
    }

    void drawSpineShadow(QPainter &p)
    {
        const qreal w = 34.0 * m_bookScale;
        QLinearGradient left(m_spineX - w, 0, m_spineX, 0);
        left.setColorAt(0.0, QColor(40, 28, 12, 0));
        left.setColorAt(1.0, QColor(40, 28, 12, 58));
        p.fillRect(QRectF(m_spineX - w, m_leftRect.top(), w, m_leftRect.height()), left);

        QLinearGradient right(m_spineX, 0, m_spineX + w, 0);
        right.setColorAt(0.0, QColor(40, 28, 12, 58));
        right.setColorAt(1.0, QColor(40, 28, 12, 0));
        p.fillRect(QRectF(m_spineX, m_rightRect.top(), w, m_rightRect.height()), right);
    }

    void drawTurningSheet(QPainter &p)
    {
        const qreal theta = m_turnProgress * kPi;      // 0 → π
        const qreal c = std::cos(theta);
        const qreal s = std::sin(theta);
        const qreal W = m_rightRect.width();
        const qreal top = m_rightRect.top();
        const qreal bottom = m_rightRect.bottom();
        const qreal lift = s * W * 0.055;

        // The leaf casts a soft shadow on the page beneath it.
        const qreal edgeX = m_spineX + W * c;
        const qreal shadowW = W * 0.5 * s;
        if (shadowW > 1.0) {
            QLinearGradient cast(edgeX - shadowW, 0, edgeX + shadowW, 0);
            cast.setColorAt(0.0, QColor(30, 20, 8, 0));
            cast.setColorAt(0.5, QColor(30, 20, 8, int(64 * s)));
            cast.setColorAt(1.0, QColor(30, 20, 8, 0));
            p.fillRect(QRectF(edgeX - shadowW, top, shadowW * 2, bottom - top), cast);
        }

        // Map the leaf image onto the turned quad.
        const bool frontVisible = (theta <= kPi / 2.0);
        const int frontIndex = m_direction > 0 ? 2 * m_spread + 1 : 2 * m_spread - 1;
        const int backIndex = m_direction > 0 ? 2 * m_spread + 2 : 2 * m_spread;
        const QImage &image = pageImage(frontVisible ? frontIndex : backIndex);

        QPolygonF src;
        src << QPointF(0, 0) << QPointF(image.width(), 0)
            << QPointF(image.width(), image.height()) << QPointF(0, image.height());

        QPolygonF dst;
        if (frontVisible) {
            dst << QPointF(m_spineX, top)
                << QPointF(edgeX, top + lift)
                << QPointF(edgeX, bottom - lift)
                << QPointF(m_spineX, bottom);
        } else {
            dst << QPointF(edgeX, top + lift)
                << QPointF(m_spineX, top)
                << QPointF(m_spineX, bottom)
                << QPointF(edgeX, bottom - lift);
        }

        QTransform transform;
        if (QTransform::quadToQuad(src, dst, transform)) {
            p.save();
            p.setTransform(transform, true);
            p.drawImage(0, 0, image);
            p.restore();
        }

        // Shade the leaf: crease darkening near the spine, a sheen at the edge.
        QPainterPath clip;
        clip.addPolygon(dst);
        clip.closeSubpath();
        p.save();
        p.setClipPath(clip);

        const qreal shade = 0.42 * s;
        QLinearGradient crease;
        if (frontVisible) {
            crease = QLinearGradient(m_spineX, 0, qMax(m_spineX + 1.0, edgeX), 0);
            crease.setColorAt(0.0, QColor(40, 28, 12, int(150 * shade)));
            crease.setColorAt(0.6, QColor(40, 28, 12, int(40 * shade)));
            crease.setColorAt(1.0, QColor(40, 28, 12, 0));
        } else {
            crease = QLinearGradient(qMin(m_spineX - 1.0, edgeX), 0, m_spineX, 0);
            crease.setColorAt(0.0, QColor(40, 28, 12, 0));
            crease.setColorAt(0.4, QColor(40, 28, 12, int(40 * shade)));
            crease.setColorAt(1.0, QColor(40, 28, 12, int(150 * shade)));
        }
        p.fillRect(QRectF(QPointF(qMin(m_spineX, edgeX), top),
                          QPointF(qMax(m_spineX, edgeX), bottom)), crease);

        const qreal sheenW = 26.0 * m_bookScale * s;
        if (sheenW > 1.0) {
            QLinearGradient sheen(edgeX - sheenW, 0, edgeX, 0);
            sheen.setColorAt(0.0, QColor(255, 252, 240, 0));
            sheen.setColorAt(1.0, QColor(255, 252, 240, int(46 * s)));
            p.fillRect(QRectF(edgeX - sheenW, top, sheenW, bottom - top), sheen);
        }
        p.restore();
    }

    void drawCornerHint(QPainter &p)
    {
        if (m_hover == NoCorner)
            return;
        if (m_hover == BottomRight && !canNext())
            return;
        if (m_hover == BottomLeft && !canPrevious())
            return;

        const bool right = (m_hover == BottomRight);
        const QRectF &page = right ? m_rightRect : m_leftRect;
        const qreal cornerX = right ? page.right() : page.left();
        const qreal dir = right ? -1.0 : 1.0;
        const qreal size = 30.0 + 6.0 * (0.5 + 0.5 * std::sin(m_driftTime * 2.2));

        // Soft shadow under the lifted corner.
        QPainterPath shadow;
        shadow.moveTo(cornerX, page.bottom());
        shadow.lineTo(cornerX + dir * size * 1.15, page.bottom());
        shadow.lineTo(cornerX, page.bottom() - size * 1.15);
        shadow.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 40));
        p.drawPath(shadow);

        // The lifted dog-ear: page back is a shade deeper than the front.
        QPainterPath fold;
        fold.moveTo(cornerX, page.bottom());
        fold.lineTo(cornerX + dir * size, page.bottom());
        fold.quadTo(QPointF(cornerX + dir * size * 0.22, page.bottom() - size * 0.22),
                    QPointF(cornerX, page.bottom() - size));
        fold.closeSubpath();
        QLinearGradient back(right ? page.right() - size : page.left(), page.bottom(),
                             cornerX, page.bottom() - size);
        back.setColorAt(0.0, kPaperLo.darker(108));
        back.setColorAt(1.0, kPaperHi);
        p.setBrush(back);
        p.setPen(QPen(kPaperEdge, 1));
        p.drawPath(fold);
    }

    Corner cornerAt(const QPointF &pos) const
    {
        const qreal zone = 90.0 * m_bookScale;
        if (canNext()) {
            const QRectF hot(m_rightRect.right() - zone, m_rightRect.bottom() - zone, zone, zone);
            if (hot.contains(pos))
                return BottomRight;
        }
        if (canPrevious()) {
            const QRectF hot(m_leftRect.left(), m_leftRect.bottom() - zone, zone, zone);
            if (hot.contains(pos))
                return BottomLeft;
        }
        return NoCorner;
    }

    void turnTo(int target)
    {
        target = qBound(0, target, spreadCount() - 1);
        if (target == m_spread || m_turning)
            return;

        if (!m_motion) {
            m_spread = target;
            emitChanged();
            update();
            return;
        }

        m_turning = true;
        m_direction = (target > m_spread) ? 1 : -1;
        m_turnProgress = 0.0;
        m_hover = NoCorner;
        unsetCursor();

        auto *animation = new QVariantAnimation(this);
        animation->setDuration(520);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::InOutSine);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_turnProgress = value.toReal();
            update();
        });
        connect(animation, &QVariantAnimation::finished, this, [this, target, animation] {
            m_spread = target;
            m_turning = false;
            m_turnProgress = 0.0;
            animation->deleteLater();
            emitChanged();
            update();
        });
        animation->start();
    }

    QRectF m_leftRect;
    QRectF m_rightRect;
    qreal m_spineX = 0.0;
    qreal m_bookScale = 1.0;
    qreal m_renderScale = 1.0;

    QHash<int, QImage> m_pageImages;
    QVector<QRectF> m_tocHits;

    int m_spread = 0;
    bool m_turning = false;
    int m_direction = 1;
    qreal m_turnProgress = 0.0;

    bool m_motion = false;
    QTimer *m_driftTimer = nullptr;
    qreal m_driftTime = 0.0;
    Corner m_hover = NoCorner;
};

/* ------------------------------------------------------------------ *
 *  GuidePage — the desk with the book and a slim control bar.         *
 * ------------------------------------------------------------------ */

GuidePage::GuidePage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(8);

    m_book = new FlipBook(this);
    root->addWidget(m_book, 1);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(10);

    auto *caption = new QLabel(QStringLiteral("Click a page or a corner to turn the leaf · ← → keys work too"), this);
    caption->setObjectName(QStringLiteral("guideCaption"));
    bar->addWidget(caption, 1);

    m_previousButton = new QPushButton(QStringLiteral("‹  Previous"), this);
    m_previousButton->setObjectName(QStringLiteral("guidePrevious"));
    m_previousButton->setProperty("ghost", true);
    m_previousButton->setCursor(Qt::PointingHandCursor);
    m_previousButton->setFocusPolicy(Qt::NoFocus);
    bar->addWidget(m_previousButton);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("guideProgress"));
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    m_progress->setFixedSize(130, 4);
    bar->addWidget(m_progress, 0, Qt::AlignVCenter);

    m_spreadLabel = new QLabel(this);
    m_spreadLabel->setObjectName(QStringLiteral("guideSpread"));
    m_spreadLabel->setAlignment(Qt::AlignCenter);
    m_spreadLabel->setMinimumWidth(70);
    bar->addWidget(m_spreadLabel);

    m_nextButton = new QPushButton(QStringLiteral("Next  ›"), this);
    m_nextButton->setObjectName(QStringLiteral("guideNext"));
    m_nextButton->setProperty("primary", true);
    m_nextButton->setCursor(Qt::PointingHandCursor);
    m_nextButton->setFocusPolicy(Qt::NoFocus);
    bar->addWidget(m_nextButton);

    root->addLayout(bar);

    // Ambient music, resource-only loop: plays while the Documentation page
    // is visible, pauses when the user navigates away. Track lives at
    // :/guide/ambience.mp3 (assets/guide/"music that i love.mp3" via resources.qrc).
    // Silent when the resource is absent, so nothing breaks before it ships.
    m_music = new QMediaPlayer(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(0.2f);
    m_music->setAudioOutput(m_audio);
    m_music->setLoops(QMediaPlayer::Infinite);
#else
    m_music->setVolume(20);
#endif
    {
        if (QFile::exists(QStringLiteral(":/guide/ambience.mp3"))) {
            m_music->setSource(QUrl(QStringLiteral("qrc:/guide/ambience.mp3")));
            m_hasMusic = true;
        }
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 has no setLoops(): replay at end of media for a seamless loop.
    connect(m_music, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia && m_hasMusic && isVisible())
            m_music->play();
    });
#endif

    connect(m_previousButton, &QPushButton::clicked, this, [this] { m_book->previous(); });
    connect(m_nextButton, &QPushButton::clicked, this, [this] { m_book->next(); });

    m_book->onSpreadChanged = [this](int spread, int count) {
        m_spreadLabel->setText(QStringLiteral("%1 / %2").arg(spread + 1).arg(count));
        m_progress->setValue(qRound((spread + 1) * 100.0 / count));
        m_previousButton->setEnabled(spread > 0);
        m_nextButton->setEnabled(spread < count - 1);
    };
    m_book->onSpreadChanged(0, m_book->spreadCount());
}

void GuidePage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::Bg);
}

void GuidePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_hasMusic && m_music)
        m_music->play();
}

void GuidePage::hideEvent(QHideEvent *event)
{
    if (m_music)
        m_music->pause();
    QWidget::hideEvent(event);
}
