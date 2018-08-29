#include "digiwageplatform.h"
#include "ui_digiwageplatform.h"

#include "init.h"
#include "wallet_ismine.h"
#include "base58.h"
#include "addressbookpage.h"

#include <QLineEdit>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkReply>
#include "clientmodel.h"
#include "walletmodel.h"

const QString DigiwagePlatform::ENDPOINT = "http://dev.digiwage.org/api/wallet/";

DigiwagePlatform::DigiwagePlatform(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DigiwagePlatform),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    int columnDealIdWidth = 100;
    int columnAddressWidth = 0;
    int columnAmountWidth = 60;
    int columnDescriptionWidth = 120;
    int columnReceiverWidth = 80;
    int columnJobtypeWidth = 80;

    ui->escrowTable->setColumnWidth(0, columnDealIdWidth);
    ui->escrowTable->setColumnWidth(1, columnAmountWidth);
    ui->escrowTable->setColumnWidth(2, columnDescriptionWidth);
    ui->escrowTable->setColumnWidth(3, columnReceiverWidth);
    ui->escrowTable->setColumnWidth(4, columnAddressWidth);
    ui->escrowTable->setColumnWidth(5, columnJobtypeWidth);

    QAction* SendEscrowAction = new QAction(tr("Send Escrow"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(SendEscrowAction);
    connect(ui->escrowTable, SIGNAL(pressed(const QModelIndex&)), this, SLOT(showContextMenu(const QModelIndex&)));
    connect(SendEscrowAction, SIGNAL(triggered()), this, SLOT(on_SendEscrowAction_clicked()));


    ConnectionManager = new QNetworkAccessManager(this);

    QSettings settings;

    QString username = settings.value("platformUsername").toString();
    QString useraddress = settings.value("platformUseraddress").toString();
    ui->usernameEdit->setText(username);
    ui->addressEdit->setText(useraddress);

    if (!username.isEmpty() && !useraddress.isEmpty())
    {
        on_userButton_clicked();        //autologin
    }
}

DigiwagePlatform::~DigiwagePlatform()
{
    delete ui;
    delete ConnectionManager;
}

void DigiwagePlatform::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // connect for change events...
        //connect(clientModel, SIGNAL(strMasternodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void DigiwagePlatform::showContextMenu(const QModelIndex &index)
{
    int row = index.row();
    int column = index.column();
    contextMenu->exec(QCursor::pos());
}

void DigiwagePlatform::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void DigiwagePlatform::setAddress(const QString& address)
{
    setAddress(address, ui->addressEdit);
}

void DigiwagePlatform::setAddress(const QString& address, QLineEdit* addrEdit)
{
    addrEdit->setText(address);
    addrEdit->setFocus();
}

void DigiwagePlatform::updateEscrowList()
{
    QString Address = ui->addressEdit->text();
    QString PubKey = getPubKey(ui->addressEdit->text());
    QString UserName = ui->usernameEdit->text();

    QUrl url( DigiwagePlatform::ENDPOINT + "getPendingEscrows" );
    QUrlQuery urlQuery( url );
    urlQuery.addQueryItem("username", UserName);
    urlQuery.addQueryItem("publicKey", PubKey);
    urlQuery.addQueryItem("address", Address);
    url.setQuery( urlQuery );

    QNetworkRequest request( url );
    //QByteArray param;
    QNetworkReply *reply = ConnectionManager->get(request);
    QEventLoop loop;

    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();

    QByteArray data = reply->readAll();
    QJsonDocument json = QJsonDocument::fromJson(data);

    if (json.isObject() && json.object().contains("result") && json.object()["result"].isBool())
    {
        if (json.object()["result"].toBool() && json.object().contains("data") && json.object()["data"].isArray())
        {
            QJsonArray jArr = json.object()["data"].toArray();

            foreach (const QJsonValue & value, jArr) {
                QJsonObject obj = value.toObject();
                QString strDealId = obj["DealId"].toString();
                QString strAmount = QString::number(obj["EscrowAmount"].toDouble(), 'g', 8);
                QString strDescription = obj["JobTitle"].toString();
                QString strReceiver = obj["ReceiverUserName"].toString();
                QString strAddress = obj["EscrowAddress"].toString();
                QString strJobtype = obj["type"].toString();

                ui->escrowTable->insertRow(0);

                QTableWidgetItem* DealIdItem = new QTableWidgetItem(strDealId);
                QTableWidgetItem* AmountItem = new QTableWidgetItem(strAmount);
                QTableWidgetItem* DescriptionItem = new QTableWidgetItem(strDescription);
                QTableWidgetItem* ReceiverItem = new QTableWidgetItem(strReceiver);
                QTableWidgetItem* AddressItem = new QTableWidgetItem(strAddress);
                QTableWidgetItem* JobtypeItem = new QTableWidgetItem(strJobtype);

                ui->escrowTable->setItem(0, 0, DealIdItem);
                ui->escrowTable->setItem(0, 1, AmountItem);
                ui->escrowTable->setItem(0, 2, DescriptionItem);
                ui->escrowTable->setItem(0, 3, ReceiverItem);
                ui->escrowTable->setItem(0, 4, AddressItem);
                ui->escrowTable->setItem(0, 5, JobtypeItem);
            }
        }
        else
        {
            QString errMsg;
            if (json.object().contains("message") && json.object()["message"].isString())
            {
                errMsg = json.object()["message"].toString();
            }
            else {
                errMsg = "No data received";
            }
            ui->userstatusLabel2->setText( "ERROR: " + errMsg );
        }
    }
    else {
        ui->userstatusLabel2->setText( "no result" );
    }
    QString str(data);

}

void DigiwagePlatform::on_SendEscrowAction_clicked()
{
    // Find selected node alias
    ui->userstatusLabel2->setText( "clicked" );
    /*
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMasternodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMasternodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm masternode start"),
        tr("Are you sure you want to start masternode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
    */
}

void DigiwagePlatform::on_addressBookButton_clicked()
{
    if (walletModel && walletModel->getAddressTableModel()) {
        AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(walletModel->getAddressTableModel());
        if (dlg.exec())
            setAddress(dlg.getReturnValue(), ui->addressEdit);
    }
}

void DigiwagePlatform::on_userButton_clicked()
{
    ui->userstatusLabel2->setText("Connecting...");
    QString pubKey = getPubKey(ui->addressEdit->text());
    if (!pubKey.isEmpty())
    {
        Connect_updatePubAddress( ui->usernameEdit->text(), ui->addressEdit->text(), pubKey );
    }
    else
    {
        ui->userstatusLabel2->setText("ERROR: Address must belong to this wallet and be spendable");
    }

    updateEscrowList();
    //Connect_updatePubAddress();
}

void DigiwagePlatform::Connect_updatePubAddress( QString UserName, QString Address, QString PubKey )
{
    QUrl url( DigiwagePlatform::ENDPOINT + "updatePubAddress" );
    QUrlQuery urlQuery( url );
    urlQuery.addQueryItem("username", UserName);
    urlQuery.addQueryItem("publicKey", PubKey);
    urlQuery.addQueryItem("address", Address);
    url.setQuery( urlQuery );

    QNetworkRequest request( url );
    QByteArray param;
    QNetworkReply *reply = ConnectionManager->post(request, param);
    QEventLoop loop;

    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();

    QByteArray data = reply->readAll();
    QJsonDocument json = QJsonDocument::fromJson(data);

    if (json.isObject() && json.object().contains("result") && json.object()["result"].isBool())
    {
        if (json.object()["result"].toBool())
        {
            ui->userstatusLabel2->setText( "SUCCESS" );

            QSettings settings;
            settings.setValue("platformUsername", ui->usernameEdit->text());
            settings.setValue("platformUseraddress", ui->addressEdit->text());
        }
        else
        {
            QString errMsg;
            if (json.object().contains("message") && json.object()["message"].isString())
            {
                errMsg = json.object()["message"].toString();
            }
            ui->userstatusLabel2->setText( "ERROR: " + errMsg );
        }
    }
    QString str(data);

    //ui->userstatusLabel2->setText( str );
    return;
}

QString DigiwagePlatform::getPubKey( QString addressString )
{

    QString ret("");

    CBitcoinAddress address( addressString.toUtf8().constData() );
    bool isValid = address.IsValid();

    if (isValid) {
        CTxDestination dest = address.Get();
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        if (mine == ISMINE_SPENDABLE) {
            CKeyID keyID;
            if (address.GetKeyID( keyID ))
            {
                CPubKey vchPubKey;
                pwalletMain->GetPubKey(keyID, vchPubKey);
                ret = QString::fromStdString(HexStr(vchPubKey));
            }
        }
    }

    return ret;
}
