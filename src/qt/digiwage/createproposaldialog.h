// Copyright (c) 2021 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef CREATEPROPOSALDIALOG_H
#define CREATEPROPOSALDIALOG_H

#include <QDialog>

namespace Ui {
class CreateProposalDialog;
class QPushButton;
}

class ContactsDropdown;
class GovernanceModel;
class DIGIWAGEGUI;
class SnackBar;
class WalletModel;

class CreateProposalDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateProposalDialog(DIGIWAGEGUI* parent, GovernanceModel* _govModel, WalletModel* _walletModel);
    ~CreateProposalDialog() override;
protected:
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
public Q_SLOTS:
    void onNextClicked();
    void onBackClicked();
    void propNameChanged(const QString& newText);
    void propUrlChanged(const QString& newText);
    void propAmountChanged(const QString& newText);
    bool propaddressChanged(const QString& newText);
    void onAddrListClicked();
    void monthsEditDeselect(int i);

private:
    Ui::CreateProposalDialog *ui;
    GovernanceModel* govModel{nullptr};
    WalletModel* walletModel{nullptr};
    SnackBar* snackBar{nullptr};
    QPushButton* icConfirm1{nullptr};
    QPushButton* icConfirm2{nullptr};
    QPushButton* icConfirm3{nullptr};
    ContactsDropdown* menuContacts{nullptr};
    QAction* actAddrList{nullptr};
    int pos = 0;

    void loadSummary();
    void sendProposal();

    void setupPageOne();
    void setupPageTwo();
    void setupPageThree();

    bool validatePageOne();
    bool validatePageTwo();

    void inform(const QString& text);
};

#endif // CREATEPROPOSALDIALOG_H
