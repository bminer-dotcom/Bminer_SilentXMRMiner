#pragma once

#include <QColor>
#include <QFrame>

class QLabel;

/*
 * The two small widgets that live INSIDE a card. The card itself is a plain
 * QGroupBox placed in a .ui form — see "Editing the UI in Qt Designer" in
 * README.md. There is no Card class; the stylesheet makes a group box look
 * like one, and unlike a custom widget you can drop things into it in Designer.
 *
 * Both classes below are styled by a TYPE selector in Theme::styleSheet()
 * (`StatCard { ... }`), never by objectName. That matters: Qt Designer
 * overwrites a widget's objectName with the name you give it in the form, so
 * any styling keyed to an objectName set in a constructor silently stops
 * applying the moment the widget is used in a .ui file.
 */

/*
 * One dashboard tile: a caption, a big value, and a footnote, with an accent
 * rail down the left edge.
 *
 * Designer: drop a QFrame -> right-click -> "Promote to..." -> class StatCard,
 * header widgets/card.h. Set `label` in the property panel. The value, the
 * footnote and the rail colour are filled in from C++ at runtime.
 */
class StatCard : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(QString label READ label WRITE setLabel)
    Q_PROPERTY(QString unit  READ unit  WRITE setUnit)
    Q_PROPERTY(QString value READ value WRITE setValue)
public:
    explicit StatCard(QWidget *parent = nullptr);
    StatCard(const QString &label, const QString &unit,
             const QColor &accent, QWidget *parent = nullptr);

    QString label() const;
    QString unit() const;
    QString value() const;

    void setLabel(const QString &l);
    void setUnit(const QString &u);
    void setValue(const QString &v);
    void setFootnote(const QString &f);
    void setAccentColor(const QColor &c);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    void buildUi();

    QLabel *m_label = nullptr;
    QLabel *m_unit  = nullptr;
    QLabel *m_value = nullptr;
    QLabel *m_foot  = nullptr;
    QColor  m_accent;
};

/*
 * One "Endpoint .......... not set" line.
 *
 * Designer: drop a QWidget -> "Promote to..." -> class KeyValueRow, header
 * widgets/card.h. Set `key` in the property panel; set the value from C++.
 */
class KeyValueRow : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString key   READ key   WRITE setKey)
    Q_PROPERTY(QString value READ value WRITE setValue)
public:
    explicit KeyValueRow(QWidget *parent = nullptr);
    KeyValueRow(const QString &key, const QString &value, QWidget *parent = nullptr);

    QString key() const;
    QString value() const;
    void setKey(const QString &k);
    void setValue(const QString &v);
    void setValueColor(const QColor &c);

private:
    void buildUi();

    QLabel *m_key = nullptr;
    QLabel *m_val = nullptr;
};
