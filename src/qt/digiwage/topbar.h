// Copyright (c) 2019-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOPBAR_H
#define TOPBAR_H

#include <QWidget>
#include "qt/digiwage/pwidget.h"
#include "qt/digiwage/lockunlock.h"
#include "amount.h"
#include <QTimer>
#include <QProgressBar>

class DIGIWAGEGUI;
class WalletModel;
class ClientModel;

namespace Ui {
class TopBar;
}

class TopBar : public PWidget
{
    Q_OBJECT

public:
    explicit TopBar(DIGIWAGEGUI* _mainWindow, QWidget *parent = nullptr);
    ~TopBar();

    void showTop();
    void showBottom();

    void loadWalletModel() override;
    void loadClientModel() override;

    void openPassPhraseDialog(AskPassphraseDialog::Mode mode, AskPassphraseDialog::Context ctx);
    void encryptWallet();

    void unlockWallet();

public Q_SLOTS:
    void updateBalances(const CAmount& balance, const CAmount& lockedBalance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                        const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                        const CAmount& delegatedBalance, const CAmount& coldStakedBalance, const int& usdPrice);
    void updateDisplayUnit();

    void setNumConnections(int count);
    void setNumBlocks(int count);
    void setStakingStatusActive(bool fActive);
    void updateStakingStatus();
    void setHDStatus(bool fActive);
    void updateHDStatus();

Q_SIGNALS:
    void themeChanged(bool isLight);
    void walletSynced(bool isSync);
    void onShowHideColdStakingChanged(bool show);

protected:
    void resizeEvent(QResizeEvent *event) override;
private Q_SLOTS:
    void onBtnReceiveClicked();
    void onThemeClicked();
    void onBtnLockClicked();
    void lockDropdownMouseLeave();
    void lockDropdownClicked(const StateClicked&);
    void refreshStatus();
    void openLockUnlock();
    void onColdStakingClicked();
    void refreshProgressBarSize();
    void expandSync();
    void expandHardfork();
    void refreshHardforkSize();
private:
    Ui::TopBar *ui;
    LockUnlock *lockUnlockWidget = nullptr;
    QProgressBar* progressBar = nullptr;

    int nDisplayUnit = -1;
    int lastBlockCount = 0;
    std::vector<int> vBlocksPerSec;
    QTimer* timerStakingIcon = nullptr;
    QTimer* timerHDIcon = nullptr;
    bool isInitializing = true;

    // pointer to global unlock context (for multithread unlock/relock)
    WalletModel::UnlockContext* pctx = nullptr;

    void updateTorIcon();
};

#endif // TOPBAR_H
