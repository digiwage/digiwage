#include "digiwageplatform.h"
#include "ui_digiwageplatform.h"

#include "init.h"
#include "wallet_ismine.h"
#include "base58.h"
#include "addressbookpage.h"
#include "askpassphrasedialog.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "rpcserver.h"
#include "rpcclient.h"

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

#include <boost/algorithm/string.hpp>
using namespace std;
using namespace json_spirit;

// json_spirit helpers
Value CallRPC(std::string args);

const QString DigiwagePlatform::ENDPOINT = "http://dev.digiwage.org/api/wallet/";
const CAmount DigiwagePlatform::FEE = 3000;

DigiwagePlatform::DigiwagePlatform(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DigiwagePlatform),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    // Escrow table
    int columnDealIdWidth = 200;
    int columnAddressWidth = 0;
    int columnAmountWidth = 100;
    int columnDescriptionWidth = 240;
    int columnReceiverWidth = 120;
    int columnJobtypeWidth = 100;

    ui->escrowTable->setColumnWidth(0, columnDealIdWidth);
    ui->escrowTable->setColumnWidth(1, columnAmountWidth);
    ui->escrowTable->setColumnWidth(2, columnDescriptionWidth);
    ui->escrowTable->setColumnWidth(3, columnReceiverWidth);
    ui->escrowTable->setColumnWidth(4, columnAddressWidth);
    ui->escrowTable->setColumnHidden(4, true);
    ui->escrowTable->setColumnWidth(5, columnJobtypeWidth);

    QAction* SendEscrowAction = new QAction(tr("Send Escrow"), this);
    EscrowContextMenu = new QMenu();
    EscrowContextMenu->addAction(SendEscrowAction);
    connect(ui->escrowTable, SIGNAL(pressed(const QModelIndex&)), this, SLOT(showEscrowContextMenu(const QModelIndex&)));
    connect(SendEscrowAction, SIGNAL(triggered()), this, SLOT(on_SendEscrowAction_clicked()));

    // Pending signatures table
    int columnRefundWidth = 80;
    int columnStatusWidth = 140;

    ui->signatureTable->setColumnWidth(0, columnDealIdWidth);
    ui->signatureTable->setColumnWidth(1, columnAmountWidth);
    ui->signatureTable->setColumnWidth(2, columnRefundWidth);
    ui->signatureTable->setColumnWidth(3, columnDescriptionWidth);
    ui->signatureTable->setColumnWidth(4, columnStatusWidth);
    ui->signatureTable->setColumnWidth(5, columnJobtypeWidth);
    ui->signatureTable->setColumnWidth(6, 0);       // hidden EscrowTxId
    //ui->escrowTable->setColumnHidden(6, true);
    ui->signatureTable->setColumnWidth(7, 0);       // hidden RedeemScript
    //ui->escrowTable->setColumnHidden(7, true);
    ui->signatureTable->setColumnWidth(8, 0);       // hidden Signature1
    //ui->escrowTable->setColumnHidden(8, true);
    ui->signatureTable->setColumnWidth(9, 0);       // hidden sellerPubAddress
    //ui->escrowTable->setColumnHidden(9, true);
    ui->signatureTable->setColumnWidth(10, 0);       // hidden status code
    //ui->escrowTable->setColumnHidden(10, true);

    QAction* SignOffAction = new QAction(tr("Sign off deal"), this);
    SignatureContextMenu = new QMenu();
    SignatureContextMenu->addAction(SignOffAction);
    connect(ui->signatureTable, SIGNAL(pressed(const QModelIndex&)), this, SLOT(showSignatureContextMenu(const QModelIndex&)));
    connect(SignOffAction, SIGNAL(triggered()), this, SLOT(on_SignOffAction_clicked()));

    // managers and settings
    ConnectionManager = new QNetworkAccessManager(this);

    QSettings settings;

    QString username = settings.value("platformUsername").toString();
    QString useraddress = settings.value("platformUseraddress").toString();
    ui->usernameEdit->setText(username);
    ui->addressEdit->setText(useraddress);

    if (!username.isEmpty() && !useraddress.isEmpty())
    {
        on_userButton_clicked();        //autorefresh
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

void DigiwagePlatform::showEscrowContextMenu(const QModelIndex &index)
{
    EscrowContextMenu->exec(QCursor::pos());
}

void DigiwagePlatform::showSignatureContextMenu(const QModelIndex &index)
{
    SignatureContextMenu->exec(QCursor::pos());
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

QJsonDocument DigiwagePlatform::callPending( QString api )
{
    QString Address = ui->addressEdit->text();
    QString PubKey = getPubKey(ui->addressEdit->text());
    QString UserName = ui->usernameEdit->text();

    QUrl url( DigiwagePlatform::ENDPOINT + api );
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

    return json;
}

void DigiwagePlatform::updateEscrowList()
{
    // clean
    ui->escrowTable->setRowCount(0);

    QJsonDocument json = callPending( QString::fromStdString("getPendingEscrows") );

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
                AmountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    return;
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
    CallAPISet( "setEscrowTxId", ui->escrowTable, "EscrowTxId", TxId );
    updateEscrowList();
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

void DigiwagePlatform::updatePendingDealsList()
{
    // clean
    ui->signatureTable->setRowCount(0);

    ProcessPendingResult( PendingType::Buyer );
    ProcessPendingResult( PendingType::Seller );
    ProcessPendingResult( PendingType::Mediated );

    return;
}

void DigiwagePlatform::on_SignOffAction_clicked()
{
    // Find transaction info
    QItemSelectionModel* selectionModel = ui->signatureTable->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();

    QString strEscrowTxId = ui->signatureTable->item(nSelectedRow, 6)->text();
    QString strRedeemScript = ui->signatureTable->item(nSelectedRow, 7)->text();
    QString strSignature1 = ui->signatureTable->item(nSelectedRow, 8)->text();
    QString strSellerAddress = ui->signatureTable->item(nSelectedRow, 9)->text();
    PendingType StatusCode = (PendingType)ui->signatureTable->item(nSelectedRow, 10)->text().toInt();
    QString strAmount = ui->signatureTable->item(nSelectedRow, 1)->text();
    CAmount nAmount(0);
    QString strMyAddress = ui->addressEdit->text();

    BitcoinUnits::parse(walletModel->getOptionsModel()->getDisplayUnit(), strAmount, &nAmount);
    nAmount -= DigiwagePlatform::FEE;
    strAmount = BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), nAmount);

    //ui->userstatusLabel2->setText( QString::number(walletModel->getOptionsModel()->getDisplayUnit()) + QString::fromStdString( " / clicked " ) + strEscrowTxId
    //                    //+ QString::fromStdString(" ") + strRedeemScript
    //                    + QString::fromStdString(" ") + strSellerAddress);

    // pending transaction signature
    QString hex, hex2;
    Value hexValue, hexValue2;
    std::ostringstream cmd;

    switch ( StatusCode ) {
        case PendingType::Buyer:
            cmd << "createrawtransaction [{\"txid\":\"" << strEscrowTxId.toUtf8().constData() << "\",\"vout\":0}] {\"" << strSellerAddress.toUtf8().constData() << "\":" << strAmount.toUtf8().constData() << "}" ;
            hexValue = CallRPC( cmd.str() );
            hex = QString::fromStdString(hexValue.get_str());
            break;

        case PendingType::Seller:
            hex = strSignature1;
            break;

        case PendingType::Mediated:
            hex = strSignature1;
            break;
    }

    // obtain scriptPukKey
    Value scriptPubKey;
    try {
        cmd.str("");
        cmd << "getrawtransaction " << strEscrowTxId.toUtf8().constData() << " 1";
        Value jsonVal = CallRPC( cmd.str() );
        if ( jsonVal.type() == Value_type::str_type )       // some error happened, cannot continue
        {
            ui->userstatusLabel2->setText(QString::fromStdString(jsonVal.get_str()));
            return;
        }
        else if ( jsonVal.type() == Value_type::obj_type ){
            Value vout = find_value(jsonVal.get_obj(), "vout");
            Value vout0 = vout.get_array().at(0);
            Value scriptPubKeyObj = find_value(vout0.get_obj(),"scriptPubKey");
            scriptPubKey = find_value(scriptPubKeyObj.get_obj(),"hex");
            //ui->userstatusLabel2->setText(QString::fromStdString(scriptPubKey.get_str()));
        }
        else {
            ui->userstatusLabel2->setText(QString::fromStdString( "ERROR getrawtransaction: unknown response") );
            return;
        }
    }
    catch (Object& objError)
    {
        ui->userstatusLabel2->setText( QString::fromStdString(find_value(objError, "message").get_str()) );
        //return QString::fromStdString("ERROR");
    }
    catch ( std::runtime_error e)
    {
        ui->userstatusLabel2->setText( QString::fromStdString( "ERROR getrawtransaction" ) + QString::fromStdString( e.what() ));
        return;
    }

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
    }
    // already unlocked or not encrypted at all

    cmd.str("");
    cmd << "dumpprivkey " << strMyAddress.toUtf8().constData();
    Value privKeyVal = CallRPC( cmd.str() );
    if (privKeyVal.get_str().compare("Invalid Digiwage address") == 0)
    {
        ui->userstatusLabel2->setText( QString::fromStdString( "ERROR dumpprivkey, invalid user address" ));
        return;
    }

    cmd.str("");
    cmd << "signrawtransaction " << hex.toUtf8().constData() << " [{\"txid\":\"" << strEscrowTxId.toUtf8().constData() << "\",\"vout\":0,\"scriptPubKey\":\"" << scriptPubKey.get_str() << "\",\"redeemScript\":\"" << strRedeemScript.toUtf8().constData() << "\"}] [\"" << privKeyVal.get_str() << "\"]";
    Value signTxResult;
    bool complete;

    try {
        signTxResult = CallRPC( cmd.str() );
        if ( signTxResult.type() == Value_type::str_type )       // some error happened, cannot continue
        {
            ui->userstatusLabel2->setText(QString::fromStdString(signTxResult.get_str()));
            return;
        }
        else if ( signTxResult.type() == Value_type::obj_type ){
            hexValue2 = find_value(signTxResult.get_obj(), "hex");
            Value completeValue = find_value(signTxResult.get_obj(), "complete");
            complete = completeValue.get_bool();
            ui->userstatusLabel2->setText(QString::fromStdString(hexValue2.get_str()));
        }
        else {
            ui->userstatusLabel2->setText(QString::fromStdString( "ERROR signrawtransaction: unknown response") );
            return;
        }
    }
    catch (Object& objError)
    {
        ui->userstatusLabel2->setText( QString::fromStdString(find_value(objError, "message").get_str()) );
        return;
    }
    catch ( std::runtime_error e)
    {
        ui->userstatusLabel2->setText( QString::fromStdString( "ERROR signrawtransaction " ) + QString::fromStdString( e.what() ));
        return;
    }

    if ( complete )     // broadcast tx
    {
        cmd.str("");
        cmd << "sendrawtransaction " << hexValue2.get_str();
        Value TxIdValue = CallRPC( cmd.str() );
        if (TxIdValue.get_str().find("code") != std::string::npos)
        {
            ui->userstatusLabel2->setText( QString::fromStdString( "ERROR in sendrawtransaction " + TxIdValue.get_str() ));
            return;
        }
        SetPaymentTxId( QString::fromStdString( TxIdValue.get_str()) );
    }
    else {              // send partially signed tx to platform
        SetPaymentSignature1( QString::fromStdString(hexValue2.get_str()) );
    }

}

void DigiwagePlatform::SetPaymentSignature1( QString PaymentSignature1 )
{
    CallAPISet( "setPaymentSignature1", ui->signatureTable, "PaymentSignature1", PaymentSignature1 );
    updatePendingDealsList();
    return;
}

void DigiwagePlatform::SetPaymentTxId( QString TxId )
{
    CallAPISet( "setPaymentTxId", ui->signatureTable, "PaymentTxId", TxId );
    updatePendingDealsList();
    return;
}

void DigiwagePlatform::ProcessPendingResult( PendingType OpType )
{
    QString strAPI;

    switch ( OpType ) {
        case PendingType::Buyer:
            strAPI = "getPendingBuyerSignature";
            break;

        case PendingType::Seller:
            strAPI = "getPendingSellerSignature";
            break;

        case PendingType::Mediated:
            strAPI = "getPendingMediatedDeals";
            break;
    }

    QJsonDocument json = callPending( strAPI );

    if (json.isObject() && json.object().contains("result") && json.object()["result"].isBool())
    {
        if (json.object()["result"].toBool() && json.object().contains("data") && json.object()["data"].isArray())
        {
            QJsonArray jArr = json.object()["data"].toArray();
            insertPendingSignatureRows( jArr, OpType);
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
            ui->userstatusLabel2->setText( strAPI + QString::fromStdString(" ERROR: ") + errMsg );
        }
    }
    else {
        ui->userstatusLabel2->setText( QString::fromStdString("ERROR: no result in ") + strAPI );
    }
}

void DigiwagePlatform::insertPendingSignatureRows( QJsonArray jArr, PendingType OpType )
{
    foreach (const QJsonValue & value, jArr) {
        QJsonObject obj = value.toObject();
        QString strDealId = obj["DealId"].toString();
        QString strAmount = QString::number(obj["EscrowAmount"].toDouble(), 'g', 8);
        QString strRefund = obj["Refund"].toString();
        QString strDescription = obj["JobTitle"].toString();
        QString strStatus, strStatusCode = QString::number((int)OpType);
        QString strJobtype = obj["type"].toString();
        QString strEscrowTxId = obj["EscrowTxId"].toString();
        QString strRedeemScript = obj["RedeemScript"].toString();
        QString strSignature1 = obj["PaymentSignature1"].toString();
        QString strSellerAddress = obj["sellerPubAddress"].toString();

        switch ( OpType ) {
            case PendingType::Buyer:
                strStatus = tr("Pending Buyer");
                strRefund = tr("N/A");
                break;

            case PendingType::Seller:
                strStatus = tr("Pending Seller");
                strRefund = tr("N/A");
                break;

            case PendingType::Mediated:
                strStatus = tr("Mediated deal");
                break;
        }

        ui->signatureTable->insertRow(0);

        QTableWidgetItem* DealIdItem = new QTableWidgetItem(strDealId);
        QTableWidgetItem* AmountItem = new QTableWidgetItem(strAmount);
        AmountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QTableWidgetItem* RefundItem = new QTableWidgetItem(strRefund);
        AmountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QTableWidgetItem* DescriptionItem = new QTableWidgetItem(strDescription);
        QTableWidgetItem* StatusItem = new QTableWidgetItem(strStatus);
        QTableWidgetItem* JobtypeItem = new QTableWidgetItem(strJobtype);
        QTableWidgetItem* EscrowTxIdItem = new QTableWidgetItem(strEscrowTxId);
        QTableWidgetItem* RedeemScriptItem = new QTableWidgetItem(strRedeemScript);
        QTableWidgetItem* Signature1Item = new QTableWidgetItem(strSignature1);
        QTableWidgetItem* SellerAddressItem = new QTableWidgetItem(strSellerAddress);
        QTableWidgetItem* StatusCodeItem = new QTableWidgetItem(strStatusCode);

        ui->signatureTable->setItem(0, 0, DealIdItem);
        ui->signatureTable->setItem(0, 1, AmountItem);
        ui->signatureTable->setItem(0, 2, RefundItem);
        ui->signatureTable->setItem(0, 3, DescriptionItem);
        ui->signatureTable->setItem(0, 4, StatusItem);
        ui->signatureTable->setItem(0, 5, JobtypeItem);
        ui->signatureTable->setItem(0, 6, EscrowTxIdItem);
        ui->signatureTable->setItem(0, 7, RedeemScriptItem);
        ui->signatureTable->setItem(0, 8, Signature1Item);
        ui->signatureTable->setItem(0, 9, SellerAddressItem);
        ui->signatureTable->setItem(0, 10, StatusCodeItem);
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
        return;
    }

    updateEscrowList();
    updatePendingDealsList();
}

bool DigiwagePlatform::Connect_updatePubAddress( QString UserName, QString Address, QString PubKey )
{
    bool bRet = false;

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
            bRet=true;
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
    return bRet;
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

void DigiwagePlatform::CallAPISet( string API, QTableWidget *QTW, const char *VarName, QString VarValue )
{
    QString Address = ui->addressEdit->text();
    QString PubKey = getPubKey(ui->addressEdit->text());
    QString UserName = ui->usernameEdit->text();

    // Find escrow transaction info
    QItemSelectionModel* selectionModel = QTW->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    QString type = QTW->item(nSelectedRow, 5)->text();
    QString DealId = QTW->item(nSelectedRow, 0)->text();

    QUrl url( DigiwagePlatform::ENDPOINT + QString::fromStdString(API) );
    QUrlQuery urlQuery( url );
    urlQuery.addQueryItem("username", UserName);
    urlQuery.addQueryItem("publicKey", PubKey);
    urlQuery.addQueryItem("address", Address);
    urlQuery.addQueryItem("type", type);
    urlQuery.addQueryItem("DealId", DealId);
    urlQuery.addQueryItem(VarName, VarValue);
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
            ui->userstatusLabel2->setText( "SUCCESS: " + json.object()["message"].toString() );
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

// RPC helper
Value CallRPC(string args)
{
    try {
        vector<string> vArgs;
        boost::split(vArgs, args, boost::is_any_of(" \t"));
        string strMethod = vArgs[0];
        vArgs.erase(vArgs.begin());
        Array params = RPCConvertValues(strMethod, vArgs);

        rpcfn_type method = tableRPC[strMethod]->actor;

        Value result = (*method)(params, false);
        return result;
        //return QString::fromStdString("nothing");
    }
    catch (Object& objError)
    {
        return find_value(objError, "message");
    }
}

