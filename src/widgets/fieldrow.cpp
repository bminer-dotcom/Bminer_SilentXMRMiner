#include "widgets/fieldrow.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// ---------------------------------------------------------- Validators ----

namespace Validators {

QString normalizeHex(const QString &raw)
{
    QString s = raw.trimmed();
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('-'));
    s.remove(QLatin1Char(':'));
    return s;
}

QString shorten(const QString &s, int head, int tail)
{
    const QString t = s.trimmed();
    if (t.size() <= head + tail + 1)
        return t;
    return t.left(head) + QString::fromUtf8("…") + t.right(tail);
}

QString endpointUrl(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return QString();

    if (s.contains(QLatin1Char(' ')))
        return QObject::tr("No spaces allowed in a URL.");

    const QUrl url = QUrl::fromUserInput(s);
    if (!url.isValid())
        return QObject::tr("Not a URL Qt can parse.");

    const QString scheme = url.scheme().toLower();
    static const QStringList allowed{ QStringLiteral("http"), QStringLiteral("https") };
    if (!allowed.contains(scheme))
        return QObject::tr("Scheme must be http or https — the client talks "
                           "plain HTTP to this URL — not \"%1\".")
                   .arg(scheme.isEmpty() ? QObject::tr("none") : scheme);

    if (url.host().isEmpty())
        return QObject::tr("Missing host name.");

    if (url.port() != -1 && (url.port() < 1 || url.port() > 65535))
        return QObject::tr("Port must be between 1 and 65535.");

    return QString();
}

QString toxId(const QString &raw)
{
    const QString s = normalizeHex(raw);
    if (s.isEmpty())
        return QString();

    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]*$"));
    if (!hex.match(s).hasMatch())
        return QObject::tr("A Tox ID is hexadecimal only (0-9, A-F).");
    if (s.size() != 76)
        return QObject::tr("A Tox ID is 76 characters; this has %1.").arg(s.size());
    return QString();
}

QString poolAddress(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty())
        return QString();

    if (s.contains(QLatin1Char(' ')))
        return QObject::tr("No spaces allowed in a pool address.");

    // an optional scheme, then host:port
    static const QStringList schemes{
        QStringLiteral("stratum+tcp"), QStringLiteral("stratum+ssl"),
        QStringLiteral("stratum+tls")
    };

    const int sep = s.indexOf(QStringLiteral("://"));
    if (sep > 0) {
        const QString scheme = s.left(sep).toLower();
        if (!schemes.contains(scheme))
            return QObject::tr("Unknown scheme \"%1\". Use stratum+tcp, stratum+ssl or stratum+tls.").arg(scheme);
        s = s.mid(sep + 3);
    }

    if (s.contains(QLatin1Char('/')))
        return QObject::tr("Remove the trailing path — use host:port only (e.g. pool.example.com:4444).");

    const int colon = s.lastIndexOf(QLatin1Char(':'));
    if (colon < 0)
        return QObject::tr("Add the port, for example pool.example.com:4444.");

    const QString host = s.left(colon);
    const QString port = s.mid(colon + 1);

    if (host.isEmpty())
        return QObject::tr("Missing host name.");

    bool ok = false;
    const int portNumber = port.toInt(&ok);
    if (!ok || portNumber < 1 || portNumber > 65535)
        return QObject::tr("Port must be a number between 1 and 65535.");

    static const QRegularExpression hostRe(
        QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$"));
    if (!hostRe.match(host).hasMatch())
        return QObject::tr("\"%1\" is not a valid host name.").arg(host);

    return QString();
}

QString moneroAddress(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return QString();

    // Base58 as Monero uses it: no 0, O, I or l
    static const QRegularExpression base58(
        QStringLiteral("^[1-9A-HJ-NP-Za-km-z]+$"));
    if (!base58.match(s).hasMatch())
        return QObject::tr("Contains characters a Monero address never uses "
                           "(0, O, I and l are excluded).");

    const QChar first = s.at(0);
    if (first == QLatin1Char('4') && s.size() == 95)
        return QString();                                   // standard
    if (first == QLatin1Char('8') && s.size() == 95)
        return QString();                                   // subaddress
    if (first == QLatin1Char('4') && s.size() == 106)
        return QString();                                   // integrated

    if (first != QLatin1Char('4') && first != QLatin1Char('8'))
        return QObject::tr("A Monero address starts with 4 or 8.");

    return QObject::tr("Expected 95 characters (or 106 for an integrated "
                       "address); this has %1.").arg(s.size());
}

} // namespace Validators

// ------------------------------------------------------ ValidatedField ----

ValidatedField::ValidatedField(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

ValidatedField::ValidatedField(const QString &label, const QString &placeholder,
                               const QString &hint, bool optional, QWidget *parent)
    : QWidget(parent)
    , m_hint(hint)
    , m_optional(optional)
{
    buildUi();
    setLabel(label);
    setPlaceholder(placeholder);
    setOptional(optional);
    setHint(hint);
}

void ValidatedField::buildUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(3);

    m_caption = new QLabel(this);
    m_caption->setTextFormat(Qt::PlainText);
    Theme::applyText(m_caption, Theme::TextRole::FieldLabel);
    lay->addWidget(m_caption);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);

    m_edit = new QLineEdit(this);
    setFocusProxy(m_edit);
    m_caption->setBuddy(m_edit);
    m_edit->setClearButtonEnabled(true);
    row->addWidget(m_edit, 1);

    // Hidden unless the field opts in — see setCopyable().
    m_copy = new QPushButton(tr("Copy"), this);
    m_copy->setProperty("ghost", true);
    m_copy->setMinimumWidth(68);
    m_copy->setEnabled(false);
    m_copy->hide();
    row->addWidget(m_copy);
    lay->addLayout(row);

    // Hint when untouched, the reason when wrong. One line, not two.
    m_statusLine = new QLabel(this);
    m_statusLine->setTextFormat(Qt::PlainText);
    m_statusLine->setWordWrap(true);
    m_statusLine->setMinimumHeight(28);
    Theme::applyText(m_statusLine, Theme::TextRole::Hint);
    lay->addWidget(m_statusLine);

    connect(m_edit, &QLineEdit::textChanged, this, [this] {
        revalidate();
        emit edited();
    });

    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(text());
        m_copy->setText(tr("Copied"));
        QTimer::singleShot(1200, m_copy, [this] { m_copy->setText(tr("Copy")); });
    });

    revalidate();
}

QString ValidatedField::label() const       { return m_caption->text(); }
QString ValidatedField::placeholder() const { return m_edit->placeholderText(); }

void ValidatedField::setLabel(const QString &l)
{
    m_caption->setText(l);
    m_edit->setAccessibleName(l);
    m_copy->setAccessibleName(tr("Copy %1").arg(l));
}

void ValidatedField::setPlaceholder(const QString &p)
{
    m_edit->setPlaceholderText(p);
}

void ValidatedField::setHint(const QString &h)
{
    m_hint = h;
    m_edit->setAccessibleDescription(h);
    revalidate();
}

void ValidatedField::setOptional(bool optional)
{
    m_optional = optional;
    revalidate();
}

/* Lets a .ui file pick the check by name, so no C++ is needed to wire one up.
 * Add a case here when you add a function to Validators. */
void ValidatedField::setRule(const QString &rule)
{
    m_rule = rule.trimmed().toLower();

    if (m_rule == QLatin1String("url"))         setCheck(&Validators::endpointUrl);
    else if (m_rule == QLatin1String("pool"))   setCheck(&Validators::poolAddress);
    else if (m_rule == QLatin1String("monero")) setCheck(&Validators::moneroAddress);
    else if (m_rule == QLatin1String("tox"))    setCheck(&Validators::toxId);
    else                                        setCheck(Check());
}

/* A Copy button is worth its space on a 95-character wallet address and is
 * clutter on a port number, so each field opts in from its .ui form. */
void ValidatedField::setCopyable(bool on)
{
    m_copyable = on;
    m_copy->setVisible(on);
}

void ValidatedField::setCheck(const Check &check)
{
    m_check = check;
    revalidate();
}

void ValidatedField::setMaxLength(int n)
{
    m_edit->setMaxLength(n);
}

void ValidatedField::setMonospace(bool on)
{
    if (!on)
        return;
    QFont f = Theme::monoFont(9);
    m_edit->setFont(f);
}

void ValidatedField::setText(const QString &t)
{
    m_edit->setText(t);
}

QString ValidatedField::text() const
{
    return m_edit->text().trimmed();
}

bool ValidatedField::isAcceptable() const
{
    if (m_status == Valid)   return true;
    if (m_status == Invalid) return false;
    return m_optional;
}

void ValidatedField::revalidate()
{
    const QString value = text();
    m_error.clear();

    if (value.isEmpty()) {
        m_status = Empty;
        m_error  = m_optional ? QString() : tr("This field is required.");
    } else if (m_check) {
        m_error  = m_check(value);
        m_status = m_error.isEmpty() ? Valid : Invalid;
    } else {
        m_status = Valid;
    }

    m_copy->setEnabled(!value.isEmpty());

    // Say nothing when the value is good: the hint while empty, the reason
    // when wrong, and silence otherwise.
    QString message;
    QColor  color;
    switch (m_status) {
    case Empty:
        message = m_error.isEmpty() ? m_hint : m_error;
        color   = m_error.isEmpty() ? Theme::TextFaint : Theme::Red;
        break;
    case Valid:
        break;
    case Invalid:
        message = m_error;
        color   = Theme::Red;
        break;
    }

    // The label keeps its space even when it says nothing: hiding it would
    // change this widget's height, and every field below it would jump on the
    // keystroke that makes the value valid.
    m_statusLine->setText(message);
    if (!message.isEmpty())
        m_statusLine->setStyleSheet(
            QStringLiteral("color:%1;font-size:11px;").arg(color.name()));

    const QColor frame = (m_status == Invalid) ? Theme::Red : Theme::Border;
    m_edit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:%1;border:1px solid %2;border-radius:%3px;padding:4px 8px;}"
        "QLineEdit:focus{border:1px solid %4;}")
        .arg(Theme::SurfaceAlt.name(), frame.name())
        .arg(Theme::Metric::Radius)
        .arg(Theme::accent().name()));
}
