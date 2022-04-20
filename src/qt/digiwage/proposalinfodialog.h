// Copyright (c) 2019-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PROPOSALINFODIALOG_H
#define PROPOSALINFODIALOG_H

#include "qt/digiwage/focuseddialog.h"
#include "qt/digiwage/governancemodel.h"

struct ProposalInfo;
class SnackBar;
class WalletModel;

namespace Ui {
class ProposalInfoDialog;
}

class ProposalInfoDialog : public FocusedDialog
{
    Q_OBJECT

public:
    explicit ProposalInfoDialog(QWidget *parent = nullptr);
    ~ProposalInfoDialog();
    void setProposal(const ProposalInfo& info);

public Q_SLOTS:
    void accept() override;
    void reject() override;

private:
    Ui::ProposalInfoDialog* ui;
    SnackBar* snackBar{nullptr};
    ProposalInfo info;

    void inform(const QString& msg);
};

#endif // PROPOSALINFODIALOG_H
