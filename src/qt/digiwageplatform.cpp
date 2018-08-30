#include "digiwageplatform.h"
#include "ui_digiwageplatform.h"

#include "init.h"
#include "wallet_ismine.h"
#include "base58.h"
#include "addressbookpage.h"
#include "askpassphrasedialog.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"

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

    int columnDealIdWidth = 200;
    int columnAddressWidth = 0;
    int columnAmountWidth = 80;
    int columnDescriptionWidth = 240;
    int columnReceiverWidth = 120;
    int columnJobtypeWidth = 100;

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
    // clean
    ui->escrowTable->setRowCount(0);

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
                AmountItem->setTextAlignment(Qt::AlignLeft);
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
    // Find escrow transaction info
    QItemSelectionModel* selectionModel = ui->escrowTable->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    QString strAddress = ui->escrowTable->item(nSelectedRow, 4)->text();
    QString strAmount = ui->escrowTable->item(nSelectedRow, 1)->text();
    QString strLabel("");
    QString strMessage("");
    CAmount nAmount(0);

    BitcoinUnits::parse(walletModel->getOptionsModel()->getDisplayUnit(), strAmount, &nAmount);

    ui->userstatusLabel2->setText( QString::fromStdString( "clicked " ) + strAddress + QString::fromStdString(" ") + strAmount );

    // Send escrow payment
    if (!walletModel || !walletModel->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    SendCoinsRecipient recipient( strAddress, strLabel, nAmount, strMessage);

    recipients.append(recipient);

    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();
    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(true));
        if (!ctx.isValid()) {
            // Unlock wallet was cancelled
            return;
        }
        send(recipients);
        return;
    }
    // already unlocked or not encrypted at all
    send(recipients);

}

void DigiwagePlatform::send(QList<SendCoinsRecipient> recipients)
{
    WalletModel *model = walletModel;
    QString msgError;

//ui->userstatusLabel2->setText( "send" );
    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;

    prepareStatus = model->prepareTransaction(currentTransaction);

    // process prepareStatus and on error generate message shown to user
    ProcessSendReturn(prepareStatus, msgError);

    if (prepareStatus.status != WalletModel::OK) {
        ui->userstatusLabel2->setText( QString::fromStdString("ERROR preparing transaction: ") + msgError );
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee"));
        questionString.append(" ");
        //questionString.append(strFee);

        // append transaction size
        questionString.append(" (" + QString::number((double)currentTransaction.getTransactionSize() / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    foreach (BitcoinUnits::Unit u, BitcoinUnits::availableUnits()) {
        if (u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatHtmlWithUnit(u, totalAmount));
    }

    // Show total amount + all alternative units
    questionString.append(tr("Total Amount = <b>%1</b><br />= %2")
                              .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount))
                              .arg(alternativeUnits.join("<br />= ")));


    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
        questionString.arg(""),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) {
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction);
    // process sendStatus and on error generate message shown to user
    ProcessSendReturn(sendStatus, msgError);

    if (sendStatus.status == WalletModel::OK) {
        CWalletTx *tx = currentTransaction.getTransaction();
        uint256 txhash = tx->GetHash();
        SetEscrowTxId( QString::fromStdString(txhash.GetHex()) );
    }
    else {
        ui->userstatusLabel2->setText( QString::fromStdString("ERROR sending: ") + msgError );
        return;
    }
}

void DigiwagePlatform::SetEscrowTxId( QString TxId )
{
    QString Address = ui->addressEdit->text();
    QString PubKey = getPubKey(ui->addressEdit->text());
    QString UserName = ui->usernameEdit->text();

    // Find escrow transaction info
    QItemSelectionModel* selectionModel = ui->escrowTable->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    QString type = ui->escrowTable->item(nSelectedRow, 5)->text();
    QString DealId = ui->escrowTable->item(nSelectedRow, 0)->text();

    QUrl url( DigiwagePlatform::ENDPOINT + "setEscrowTxId" );
    QUrlQuery urlQuery( url );
    urlQuery.addQueryItem("username", UserName);
    urlQuery.addQueryItem("publicKey", PubKey);
    urlQuery.addQueryItem("address", Address);
    urlQuery.addQueryItem("type", type);
    urlQuery.addQueryItem("DealId", DealId);
    urlQuery.addQueryItem("EscrowTxId", TxId);

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
    return;
}

void DigiwagePlatform::ProcessSendReturn( const WalletModel::SendCoinsReturn& sendCoinsReturn, QString& msgArg)
{
    switch (sendCoinsReturn.status) {
    case WalletModel::InvalidAddress:
        msgArg = tr("The recipient address is not valid, please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgArg = tr("The amount to pay must be larger than 0.");
        break;
    case WalletModel::AmountExceedsBalance:
        msgArg = tr("The amount exceeds your balance.");
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgArg = tr("The total exceeds your balance when the transaction fee is included.");
        break;
    case WalletModel::DuplicateAddress:
        msgArg = tr("Duplicate address found, can only send to each address once per send operation.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgArg = tr("Transaction creation failed!");
        break;
    case WalletModel::TransactionCommitFailed:
        msgArg = tr("The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
        break;

    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

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
