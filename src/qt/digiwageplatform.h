#ifndef DIGIWAGEPLATFORM_H
#define DIGIWAGEPLATFORM_H

#include "walletmodel.h"

#include <QMenu>
#include <QWidget>

namespace Ui {
class DigiwagePlatform;
}

class ClientModel;
class WalletModel;
class QLineEdit;
class QTableWidget;
class QNetworkReply;
class QNetworkAccessManager;
class SendCoinsRecipient;

class DigiwagePlatform : public QWidget
{
    Q_OBJECT

    enum PendingType {
        Buyer = 0,
        Seller = 1,
        Mediated = 2
    };

public:
    explicit DigiwagePlatform(QWidget *parent = 0);
    ~DigiwagePlatform();
    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void setAddress(const QString& address);
    void setAddress(const QString& address, QLineEdit* addrEdit);
    void send(QList<SendCoinsRecipient> recipients);

    bool Connect_updatePubAddress( QString UserName, QString Address, QString PubKey );

public Q_SLOTS:
    void updateEscrowList();
    void updatePendingDealsList();
    void showEscrowContextMenu(const QModelIndex&);
    void showSignatureContextMenu(const QModelIndex&);
    void ProcessSendReturn( const WalletModel::SendCoinsReturn& sendCoinsReturn, QString& msgArg);
    void SetEscrowTxId( QString TxId );
    void SetPaymentSignature1( QString PaymentSignature1 );
    void SetPaymentTxId( QString TxId );

private slots:
    void on_addressBookButton_clicked();
    void on_userButton_clicked();
    void on_SendEscrowAction_clicked();
    void on_SignOffAction_clicked();
    void on_CopyDealIdEscrowAction_clicked();
    void on_CopyDealIdPendingAction_clicked();


private:
    QMenu* EscrowContextMenu;
    QMenu* SignatureContextMenu;
    Ui::DigiwagePlatform *ui;
    ClientModel* clientModel;
    WalletModel* walletModel;

    QNetworkAccessManager *ConnectionManager;

    QString getPubKey( QString address );
    QString ENDPOINT;
    CAmount FEE;

    void insertPendingSignatureRows( QJsonArray jArr, PendingType OpType );
    void ProcessPendingResult( PendingType OpType );
    void CallAPISet( std::string API, QTableWidget *QTW, const char *VarName, QString VarValue );
    void CopyDealId( QTableWidget *QTW );
    QJsonDocument callPending( QString api );

};

#endif // DIGIWAGEPLATFORM_H
