#pragma once

#include <QAbstractButton>

class QPropertyAnimation;

/* iOS-style switch — reads better than a checkbox in a settings column. */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    qreal knob() const { return m_knob; }
    void  setKnob(qreal k);
    void  setAnimated(bool on);

protected:
    void paintEvent(QPaintEvent *e) override;
    void checkStateSet() override;

private:
    qreal               m_knob = 0.0;
    QPropertyAnimation *m_anim = nullptr;
    bool m_animated = true;
};
