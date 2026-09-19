#ifndef DIGIWAGEPUSHBUTTON_H
#define DIGIWAGEPUSHBUTTON_H
#include <QPushButton>
#include <QStyleOptionButton>
#include <QIcon>

class DigiWagePushButton : public QPushButton
{
public:
    explicit DigiWagePushButton(QWidget * parent = Q_NULLPTR);
    explicit DigiWagePushButton(const QString &text, QWidget *parent = Q_NULLPTR);

protected:
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE;

private:
    void updateIcon(QStyleOptionButton &pushbutton);

private:
    bool m_iconCached;
    QIcon m_downIcon;
};

#endif // DIGIWAGEPUSHBUTTON_H
