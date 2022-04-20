// Copyright (c) 2019-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SEND_H
#define SEND_H

#include <QWidget>
#include <QPushButton>

#include "qt/digiwage/pwidget.h"
#include "qt/digiwage/contactsdropdown.h"
#include "qt/digiwage/sendmultirow.h"
#include "qt/digiwage/sendcustomfeedialog.h"
#include "walletmodel.h"
#include "coincontroldialog.h"
#include "qt/digiwage/tooltipmenu.h"

#include <atomic>

static const int MAX_SEND_POPUP_ENTRIES = 8;

class DIGIWAGEGUI;
class ClientModel;
class OperationResult;
class WalletModel;
class WalletModelTransaction;

namespace Ui {
class send;
class QPushButton;
}

class SendWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SendWidget(DIGIWAGEGUI* parent);
    ~SendWidget();

    void addEntry();

    void loadClientModel() override;
    void loadWalletModel() override;

Q_SIGNALS:
    /** Signal raised when a URI was entered or dragged to the GUI */
    void receivedURI(const QString& uri);

public Q_SLOTS:
    void onChangeAddressClicked();
    void onChangeCustomFeeClicked();
    void onCoinControlClicked();
    void onOpenUriClicked();
    void onShieldCoinsClicked();
    void onValueChanged();
    void refreshAmounts();
    void changeTheme(bool isLightTheme, QString &theme) override;
    void updateAmounts(const QString& titleTotalRemaining,
                       const QString& labelAmountSend,
                       const QString& labelAmountRemaining,
                       CAmount _delegationBalance);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

    void run(int type) override;
    void onError(QString error, int type) override;

private Q_SLOTS:
    void onWAGESelected(bool _isTransparent);
    void onSendClicked();
    void onContactsClicked(SendMultiRow* entry);
    void onMenuClicked(SendMultiRow* entry);
    void onAddEntryClicked();
    void clearEntries();
    void clearAll(bool fClearSettings = true);
    void onCheckBoxChanged();
    void onContactMultiClicked();
    void onDeleteClicked();
    void onEntryMemoClicked();
    void onSubtractFeeFromAmountChecked();
    void onResetCustomOptions(bool fRefreshAmounts);
    void onResetSettings();

private:
    Ui::send *ui;
    QPushButton *coinIcon;

    SendCustomFeeDialog* customFeeDialog = nullptr;
    bool isCustomFeeSelected = false;
    bool fDelegationsChecked = false;

    int nDisplayUnit;
    QList<SendMultiRow*> entries;
    CoinControlDialog *coinControlDialog = nullptr;

    // Cached tx
    WalletModelTransaction* ptrModelTx{nullptr};
    std::atomic<bool> isProcessing{false};
    Optional<QString> processingResultError{nullopt};
    std::atomic<bool> processingResult{false};

    // Balance update
    std::atomic<bool> isUpdatingBalance{false};

    ContactsDropdown *menuContacts = nullptr;
    TooltipMenu *menu = nullptr;
    // Current focus entry
    SendMultiRow* focusedEntry = nullptr;

    bool isTransparent = true;
    void resizeMenu();
    SendMultiRow* createEntry();
    void ProcessSend(QList<SendCoinsRecipient>& recipients, bool hasShieldedOutput,
                     const std::function<bool(QList<SendCoinsRecipient>&)>& func = nullptr);
    OperationResult prepareShielded(WalletModelTransaction* tx, bool fromTransparent);
    OperationResult prepareTransparent(WalletModelTransaction* tx);
    bool sendFinalStep();
    void setFocusOnLastEntry();
    void showHideCheckBoxDelegations(CAmount delegationBalance);
    void updateEntryLabels(const QList<SendCoinsRecipient>& recipients);
    void setCustomFeeSelected(bool isSelected, const CAmount& customFee = DEFAULT_TRANSACTION_FEE);
    void setCoinControlPayAmounts();
    void resetCoinControl();
    void resetChangeAddress();
    void hideContactsMenu();
    void tryRefreshAmounts();
};

#endif // SEND_H
