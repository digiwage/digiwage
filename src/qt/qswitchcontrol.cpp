#include "qswitchcontrol.h"

#include <qt/styleSheet.h>

#include <QPainter>
#include <QSettings>
#include <QVariantAnimation>

static const QSize TrackSize = QSize(44, 24);
static const qreal KnobMargin = 3;

static QColor Mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QSwitchControl::QSwitchControl(QWidget *parent):
    QAbstractButton(parent)
{
    setFixedSize(TrackSize);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);

    m_animation = new QVariantAnimation(this);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_position = value.toReal();
        update();
    });

    connect(this, &QSwitchControl::mouseClicked, this, &QSwitchControl::onStatusChanged);
    connect(StyleSheet::instance().notifier(), &StyleSheetNotifier::modeChanged, this, [this] { update(); });
    QAbstractButton::setChecked(false);
}

QSize QSwitchControl::sizeHint() const
{
    return TrackSize;
}

void QSwitchControl::setChecked(bool checked)
{
    m_animation->stop();
    m_position = checked ? 1 : 0;
    QAbstractButton::setChecked(checked);
    update();
}

void QSwitchControl::onStatusChanged()
{
    const bool checked = !isChecked();

    m_animation->stop();
    m_animation->setDuration(QSettings().value("ReduceMotion", false).toBool() ? 0 : 160);
    m_animation->setStartValue(m_position);
    m_animation->setEndValue(checked ? 1.0 : 0.0);
    QAbstractButton::setChecked(checked);
    m_animation->start();

    Q_EMIT clicked(checked);
}

void QSwitchControl::mousePressEvent(QMouseEvent *)
{
    Q_EMIT mouseClicked();
}

void QSwitchControl::paintEvent(QPaintEvent *)
{
    StyleSheet& style = StyleSheet::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.4);

    // Track: neutral grey (>= 3:1 against the page) when off, accent when on
    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(Mix(style.tokenColor("text-3-solid"), style.tokenColor("accent"), m_position));
    p.drawRoundedRect(track, track.height() / 2, track.height() / 2);

    // Knob
    const qreal d = height() - 2 * KnobMargin;
    const qreal x = KnobMargin + m_position * (width() - 2 * KnobMargin - d);
    const QRectF knob(x, KnobMargin, d, d);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawEllipse(knob.translated(0, 1));
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(knob);
}
