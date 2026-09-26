#include "widgets/titlebar.h"

#include "logo.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>

namespace {

class MarkWidget : public QWidget
{
public:
    explicit MarkWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(18, 18);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Logo::paintMark(&p, QRectF(rect()), false);
    }
};

QWidget *makeDot(QWidget *parent)
{
    auto *dot = new QWidget(parent);
    dot->setFixedSize(7, 7);
    dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    return dot;
}

QIcon titleButtonIcon(int kind)
{
    QPixmap pm(32, 32);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::Text, 1.2, Qt::SolidLine, Qt::SquareCap));
    if (kind == 0) p.drawLine(QPointF(3, 11), QPointF(13, 11));
    else if (kind == 1) p.drawRect(QRectF(3, 3, 10, 10));
    else if (kind == 2) {
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
    } else {
        p.drawRect(QRectF(5, 3, 8, 8));
        p.fillRect(QRectF(3, 5, 8, 8), Theme::BgAlt);
        p.drawRect(QRectF(3, 5, 8, 8));
    }
    return QIcon(pm);
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(Theme::Metric::TitleBarHeight);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(9, 0, 4, 0);
    lay->setSpacing(7);

    lay->addWidget(new MarkWidget(this));

    auto *title = new QLabel(QStringLiteral("Bminer"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(title);

    auto *sep = new QLabel(QStringLiteral("·"), this);
    sep->setObjectName(QStringLiteral("appTitleDim"));
    sep->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(sep);

    auto *sub = new QLabel(QStringLiteral("CULTURE CONTROL"), this);
    sub->setObjectName(QStringLiteral("appTitleDim"));
    sub->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(sub);

    lay->addStretch(1);

    m_statusDot = makeDot(this);
    lay->addWidget(m_statusDot);

    m_statusText = new QLabel(QStringLiteral("idle"), this);
    m_statusText->setObjectName(QStringLiteral("appTitleDim"));
    m_statusText->setTextFormat(Qt::PlainText);
    m_statusText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusText->setMinimumWidth(0);
    m_statusText->setMaximumWidth(280);
    m_statusText->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(m_statusText, 1);

    lay->addSpacing(10);

    struct BtnSpec { const char *glyph; const char *name; };
    const BtnSpec specs[3] = {
        { "─", "winBtn" },       // minimize
        { "□", "winBtn" },       // maximize
        { "✕", "winBtnClose" },  // close
    };

    QPushButton *buttons[3];
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(this);
        b->setIcon(titleButtonIcon(i));
        b->setIconSize(QSize(16, 16));
        b->setObjectName(QString::fromLatin1(specs[i].name));
        b->setProperty("winBtn", true);
        b->setFixedSize(38, 28);
        b->setCursor(Qt::ArrowCursor);
        b->setFocusPolicy(Qt::StrongFocus);
        const QString label = i == 0 ? tr("Minimize") : i == 1 ? tr("Maximize") : tr("Close");
        b->setToolTip(label);
        b->setAccessibleName(label);
        lay->addWidget(b);
        buttons[i] = b;
    }
    m_maxBtn = buttons[1];

    connect(buttons[0], &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(buttons[1], &QPushButton::clicked, this, &TitleBar::toggleMaximizeRequested);
    connect(buttons[2], &QPushButton::clicked, this, &TitleBar::closeRequested);

    setStatus(QStringLiteral("idle"), Theme::TextFaint);
}

void TitleBar::setStatus(const QString &text, const QColor &dot)
{
    m_status = text;
    m_statusText->setText(m_statusText->fontMetrics().elidedText(text, Qt::ElideRight, m_statusText->width()));
    m_statusText->setAccessibleName(text);
    setToolTip(text);
    m_statusDot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dot.name()));
}

void TitleBar::setMaximized(bool maximized)
{
    m_maxBtn->setIcon(titleButtonIcon(maximized ? 3 : 1));
    m_maxBtn->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
    m_maxBtn->setAccessibleName(m_maxBtn->toolTip());
}

void TitleBar::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_statusText->setText(m_statusText->fontMetrics().elidedText(m_status, Qt::ElideRight, m_statusText->width()));
}

void TitleBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        emit moveRequested();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        emit toggleMaximizeRequested();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void TitleBar::paintEvent(QPaintEvent *)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
