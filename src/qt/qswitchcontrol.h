#ifndef QSWITCHCONTROL_H
#define QSWITCHCONTROL_H

#include <QAbstractButton>

class QVariantAnimation;

/** On/off switch, painted with the active appearance tokens (accent when on). */
class QSwitchControl : public QAbstractButton
{
    Q_OBJECT
public:
    QSwitchControl(QWidget *parent = nullptr);
    QSize sizeHint() const override;

public Q_SLOTS:
    void setChecked(bool);
    void onStatusChanged();

Q_SIGNALS:
    void mouseClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QVariantAnimation *m_animation;
    qreal m_position{0}; // knob position: 0 = off, 1 = on
};

#endif // QSWITCHCONTROL_H
