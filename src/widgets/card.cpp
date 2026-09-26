#include "widgets/card.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

// ------------------------------------------------------------ StatCard ----

StatCard::StatCard(QWidget *parent)
    : QFrame(parent)
    , m_accent(Theme::TextFaint)
{
    buildUi();
}

StatCard::StatCard(const QString &label, const QString &unit,
                   const QColor &accent, QWidget *parent)
    : QFrame(parent)
    , m_accent(accent)
{
    buildUi();
    setLabel(label);
    setUnit(unit);
}

void StatCard::buildUi()
{
    // No setObjectName here on purpose: the background and border come from the
    // `StatCard` type selector in Theme::styleSheet(), which survives Qt
    // Designer renaming this widget to whatever the form calls it.
    setMinimumHeight(76);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(13, 8, 11, 8);
    lay->setSpacing(1);

    // The child labels below keep their objectNames: uic only renames the
    // promoted widget itself, never the children it does not know about.
    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("statLabel"));
    lay->addWidget(m_label);

    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    row->setContentsMargins(0, 1, 0, 0);

    m_value = new QLabel(this);
    m_value->setObjectName(QStringLiteral("statValue"));
    row->addWidget(m_value);

    m_unit = new QLabel(this);
    m_unit->setObjectName(QStringLiteral("statUnit"));
    m_unit->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
    m_unit->setContentsMargins(0, 0, 0, 6);
    m_unit->setVisible(false);
    row->addWidget(m_unit);

    row->addStretch(1);
    lay->addLayout(row);

    m_foot = new QLabel(this);
    m_foot->setObjectName(QStringLiteral("statFoot"));
    lay->addWidget(m_foot);
    lay->addStretch(1);
}

QString StatCard::label() const { return m_label->text(); }
QString StatCard::unit() const  { return m_unit->text(); }
QString StatCard::value() const { return m_value->text(); }

void StatCard::setLabel(const QString &l)   { m_label->setText(l.toUpper()); }
void StatCard::setValue(const QString &v)   { m_value->setText(v); }
void StatCard::setFootnote(const QString &f){ m_foot->setText(f); }

void StatCard::setUnit(const QString &u)
{
    m_unit->setText(u);
    m_unit->setVisible(!u.isEmpty());
}

void StatCard::setAccentColor(const QColor &c)
{
    m_accent = c;
    update();
}

void StatCard::paintEvent(QPaintEvent *e)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // accent rail, clipped to the card's rounded corner
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                        Theme::Metric::Radius, Theme::Metric::Radius);
    p.save();
    p.setClipPath(clip);
    p.fillRect(QRectF(0, 0, 3, height()), m_accent);
    p.restore();

    Q_UNUSED(e)
}

// --------------------------------------------------------- KeyValueRow ----

KeyValueRow::KeyValueRow(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

KeyValueRow::KeyValueRow(const QString &key, const QString &value, QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    setKey(key);
    setValue(value);
}

void KeyValueRow::buildUi()
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(10);

    m_key = new QLabel(this);
    m_key->setObjectName(QStringLiteral("kvKey"));   // styled in Theme::styleSheet()
    m_key->setMinimumWidth(118);
    lay->addWidget(m_key);

    m_val = new QLabel(this);
    m_val->setObjectName(QStringLiteral("kvVal"));
    m_val->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_val->setWordWrap(true);
    lay->addWidget(m_val, 1);
}

QString KeyValueRow::key() const   { return m_key->text(); }
QString KeyValueRow::value() const { return m_val->text(); }

void KeyValueRow::setKey(const QString &k)   { m_key->setText(k); }
void KeyValueRow::setValue(const QString &v) { m_val->setText(v); }

void KeyValueRow::setValueColor(const QColor &c)
{
    m_val->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(c.name()));
}
