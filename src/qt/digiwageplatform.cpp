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
#include <QSettings>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkReply>
#include "clientmodel.h"
#include "walletmodel.h"

const QString DigiwagePlatform::ENDPOINT = "http://demo.digiwage.org/api/wallet/";

DigiwagePlatform::DigiwagePlatform(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DigiwagePlatform),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    ConnectionManager = new QNetworkAccessManager(this);

    QSettings settings;

    ui->usernameEdit->setText(settings.value("platformUsername").toString());
    ui->addressEdit->setText(settings.value("platformUseraddress").toString());
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

    //Connect_updatePubAddress();
}

void DigiwagePlatform::Connect_updatePubAddress( QString UserName, QString Address, QString PubKey )
{
    QUrl url( DigiwagePlatform::ENDPOINT + "user/updatePubAddress" );
    //QUrl url( "http://www.kesa.es" );
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
