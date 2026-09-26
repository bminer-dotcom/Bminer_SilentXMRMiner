#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;

/*
 * Input checks live here and nowhere else, so the settings page and the
 * dashboard always agree on what "valid" means.
 *
 * Each returns an empty string when the value is acceptable, otherwise the
 * reason to show under the field. Empty input is judged by the caller —
 * optional fields accept it, required ones do not.
 */
namespace Validators {
QString endpointUrl(const QString &raw);
QString toxId(const QString &raw);
QString poolAddress(const QString &raw);           // host:port, optional stratum scheme
QString moneroAddress(const QString &raw);         // standard, sub- or integrated

QString normalizeHex(const QString &raw);          // strips spaces and dashes
QString shorten(const QString &s, int head = 12, int tail = 10);
} // namespace Validators

/*
 * A labelled text field that reports its own state: caption, the input, and a
 * status line carrying the hint while empty and the reason while wrong. It
 * stays silent once the value is acceptable.
 */
class ValidatedField : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString label       READ label       WRITE setLabel)
    Q_PROPERTY(QString placeholder READ placeholder WRITE setPlaceholder)
    Q_PROPERTY(QString hint        READ hint        WRITE setHint)
    Q_PROPERTY(bool    optional    READ isOptional  WRITE setOptional)
    Q_PROPERTY(QString rule        READ rule        WRITE setRule)
    Q_PROPERTY(bool    copyable    READ isCopyable  WRITE setCopyable)
public:
    enum Status { Empty, Valid, Invalid };
    Q_ENUM(Status)

    using Check = std::function<QString(const QString &)>;

    explicit ValidatedField(QWidget *parent = nullptr);
    ValidatedField(const QString &label,
                   const QString &placeholder,
                   const QString &hint,
                   bool optional,
                   QWidget *parent = nullptr);

    // --- Designer-visible properties -------------------------------------
    QString label() const;
    QString placeholder() const;
    QString hint() const { return m_hint; }
    bool    isOptional() const { return m_optional; }
    /* Which Validators:: function to run, by name: "url", "pool", "monero",
     * "tox", "evm", or empty for no check. Settable in Designer. */
    QString rule() const { return m_rule; }
    /* Off by default. Turn on only for values too long to select by hand:
     * a wallet or a contract address, not a port or a password. */
    bool    isCopyable() const { return m_copyable; }

    void setLabel(const QString &l);
    void setPlaceholder(const QString &p);
    void setHint(const QString &h);
    void setOptional(bool optional);
    void setRule(const QString &rule);
    void setCopyable(bool on);

    void setCheck(const Check &check);
    void setMaxLength(int n);
    void setMonospace(bool on);

    void    setText(const QString &t);
    QString text() const;                  // trimmed
    Status  status() const { return m_status; }
    bool    isAcceptable() const;          // valid, or empty and optional
    QString errorText() const { return m_error; }

signals:
    void edited();

private:
    void buildUi();
    void revalidate();

    QLabel      *m_caption    = nullptr;
    QLabel      *m_statusLine = nullptr;
    QLineEdit   *m_edit       = nullptr;
    QPushButton *m_copy       = nullptr;

    Check   m_check;
    QString m_hint;
    QString m_error;
    QString m_rule;
    Status  m_status   = Empty;
    bool    m_optional = true;
    bool    m_copyable = false;
};
