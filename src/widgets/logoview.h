#pragma once

#include <QWidget>

class QTimer;

/* Shows the badge at whatever size it is given, with an optional
 * slow bloom behind the plate. */
class LogoView : public QWidget
{
    Q_OBJECT
public:
    explicit LogoView(QWidget *parent = nullptr);

    void setAnimated(bool on);
    void setPlate(bool on);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QTimer *m_timer = nullptr;
    qreal   m_phase = 0.0;
    bool    m_plate = true;
};
