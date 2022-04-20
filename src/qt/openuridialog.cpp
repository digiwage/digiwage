// Copyright (c) 2019 The DIGIWAGE developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "openuridialog.h"
#include "ui_openuridialog.h"

#include "guiutil.h"
#include "walletmodel.h"
#include "qt/digiwage/qtutils.h"

#include <QUrl>
#include <QFile>

OpenURIDialog::OpenURIDialog(QWidget* parent) : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
                                                ui(new Ui::OpenURIDialog)
{
    ui->setupUi(this);
    this->setStyleSheet(parent->styleSheet());
    ui->uriEdit->setPlaceholderText("digiwage:");

    ui->labelSubtitle->setText("URI");
    setCssProperty(ui->labelSubtitle, "text-title2-dialog");
    setCssProperty(ui->frame, "container-dialog");
    setCssProperty(ui->labelTitle, "text-title-dialog");

    setCssBtnPrimary(ui->pushButtonOK);
    setCssProperty(ui->pushButtonCancel, "btn-dialog-cancel");

    initCssEditLine(ui->uriEdit, true);
    connect(ui->pushButtonOK, &QPushButton::clicked, this, &OpenURIDialog::accept);
    connect(ui->pushButtonCancel, &QPushButton::clicked, this, &OpenURIDialog::close);
}

void OpenURIDialog::showEvent(QShowEvent *event)
{
    ui->uriEdit->setFocus();
}

OpenURIDialog::~OpenURIDialog()
{
    delete ui;
}

QString OpenURIDialog::getURI()
{
    return ui->uriEdit->text();
}

void OpenURIDialog::accept()
{
    SendCoinsRecipient rcp;
    if (GUIUtil::parseBitcoinURI(getURI(), &rcp)) {
        /* Only accept value URIs */
        QDialog::accept();
    } else {
        setCssEditLineDialog(ui->uriEdit, false, true);
    }
}

void OpenURIDialog::inform(const QString& str) {
    if (!snackBar) snackBar = new SnackBar(nullptr, this);
    snackBar->setText(str);
    snackBar->resize(this->width(), snackBar->height());
    openDialog(snackBar, this);
}