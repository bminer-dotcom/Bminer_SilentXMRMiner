#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

/* Chrome for the frameless main window: mark, wordmark, live status pill and
 * the window buttons. Drag/double-click is forwarded to the shell. */
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setStatus(const QString &text, const QColor &dot);
    void setMaximized(bool maximized);

signals:
    void moveRequested();
    void toggleMaximizeRequested();
    void minimizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QLabel      *m_statusText = nullptr;
    QWidget     *m_statusDot  = nullptr;
    QPushButton *m_maxBtn     = nullptr;
    QString      m_status;
};
