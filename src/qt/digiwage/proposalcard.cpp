// Copyright (c) 2021 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "qt/digiwage/proposalcard.h"
#include "qt/digiwage/forms/ui_proposalcard.h"

#include "qt/digiwage/qtutils.h"

ProposalCard::ProposalCard(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ProposalCard)
{
    ui->setupUi(this);
    setStyleSheet(parent->styleSheet());
    setCssProperty(ui->btnVote, "btn-primary");
    setCssProperty(ui->card, "card-governance");
    setCssProperty(ui->labelPropName, "card-title");
    setCssProperty(ui->labelPropAmount, "card-amount");
    setCssProperty(ui->labelPropMonths, "card-time");
    setCssProperty(ui->labelStatus, "card-status-passing");
    setCssProperty(ui->btnVote, "card-btn-vote");
    setCssProperty(ui->btnLink, "btn-menu");
    setCssProperty(ui->containerVotes, "card-progress-box");
    ui->containerVotes->setContentsMargins(1,1,1,1);
    ui->containerVotes->layout()->setMargin(0);
    ui->votesBar->setMaximum(100);
    ui->votesBar->setMinimum(0);
    ui->votesBar->setTextVisible(false);
    setCssProperty(ui->votesBar, "vote-progress");
    ui->votesBar->setContentsMargins(0,0,0,0);

    connect(ui->btnVote, &QPushButton::clicked, [this](){ Q_EMIT voteClicked(proposalInfo); });
    connect(ui->btnLink, &QPushButton::clicked, this, &ProposalCard::onCopyUrlClicked);
}

void ProposalCard::setProposal(const ProposalInfo& _proposalInfo)
{
    proposalInfo = _proposalInfo;
    ui->labelPropName->setText(QString::fromStdString(proposalInfo.name));
    ui->labelPropName->setToolTip(QString::fromStdString(proposalInfo.name));
    ui->labelPropAmount->setText(GUIUtil::formatBalance(proposalInfo.amount));
    ui->labelPropMonths->setText(proposalInfo.remainingPayments < 0 ? tr("Inactive proposal") :
            proposalInfo.remainingPayments == 0 ? tr("Last month in course") :
            tr("%1 of %2 months left").arg(proposalInfo.remainingPayments).arg(proposalInfo.totalPayments));
    double totalVotes = _proposalInfo.votesYes + _proposalInfo.votesNo;
    double percentageNo = (totalVotes == 0) ? 0 :  (_proposalInfo.votesNo / totalVotes) * 100;
    double percentageYes = (totalVotes == 0) ? 0 : (_proposalInfo.votesYes / totalVotes) * 100;
    ui->labelNo->setText(QString::number(percentageNo) + "% " + tr("No"));
    ui->labelYes->setText(tr("Yes") + " " + QString::number(percentageYes) + "%");

    QString cssClassStatus;
    if (proposalInfo.status == ProposalInfo::WAITING_FOR_APPROVAL){
        cssClassStatus = "card-status-no-votes";
        setStatusAndVotes(tr("Waiting"), 50);
    } else if (proposalInfo.status == ProposalInfo::FINISHED) {
        cssClassStatus = "card-status-no-votes";
        setStatusAndVotes(tr("Finished"), 50);
    } else if (totalVotes == 0) {
        cssClassStatus = "card-status-no-votes";
        setStatusAndVotes(tr("No Votes"), 50);
    } else if (proposalInfo.status == ProposalInfo::NOT_PASSING ||
        proposalInfo.status == ProposalInfo::PASSING_NOT_FUNDED) {
        cssClassStatus = "card-status-not-passing";
        setStatusAndVotes(tr("Not Passing"), (int)percentageNo);
    } else if (proposalInfo.status == ProposalInfo::PASSING) {
        cssClassStatus = "card-status-passing";
        setStatusAndVotes(tr("Passing"), (int)percentageNo);
    }
    setCssProperty(ui->labelStatus, cssClassStatus, true);
}

void ProposalCard::setStatusAndVotes(const QString& msg, int value)
{
    ui->labelStatus->setText(msg);
    ui->votesBar->setValue(value);
}

void ProposalCard::onCopyUrlClicked()
{
    Q_EMIT onMenuClicked(this);
}

ProposalCard::~ProposalCard()
{
    delete ui;
}
