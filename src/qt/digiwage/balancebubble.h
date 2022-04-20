// Copyright (c) 2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIWAGE_BALANCEBUBBLE_H
#define DIGIWAGE_BALANCEBUBBLE_H

#include <QWidget>
#include <QString>

namespace Ui {
    class BalanceBubble;
}

class BalanceBubble : public QWidget
{

public:
    explicit BalanceBubble(QWidget *parent = nullptr);
    ~BalanceBubble();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

    void updateValues(int64_t nTransparentBalance, int64_t nShieldedBalance, int unit);

public Q_SLOTS:
    void hideTimeout();

private:
    Ui::BalanceBubble *ui;
    QTimer* hideTimer{nullptr};
};

#endif //DIGIWAGE_BALANCEBUBBLE_H
