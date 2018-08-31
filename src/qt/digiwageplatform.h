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

    void Connect_updatePubAddress( QString UserName, QString Address, QString PubKey );

public Q_SLOTS:
    void updateEscrowList();
    void updatePendingDealsList();
    void showEscrowContextMenu(const QModelIndex&);
    void showSignatureContextMenu(const QModelIndex&);
    void ProcessSendReturn( const WalletModel::SendCoinsReturn& sendCoinsReturn, QString& msgArg);
    void SetEscrowTxId( QString TxId );
    void SetPaymentSignature1( QString PaymentSignature1 );

private slots:
    void on_addressBookButton_clicked();
    void on_userButton_clicked();
    void on_SendEscrowAction_clicked();
    void on_SignOffAction_clicked();

private:
    QMenu* EscrowContextMenu;
    QMenu* SignatureContextMenu;
    Ui::DigiwagePlatform *ui;
    ClientModel* clientModel;
    WalletModel* walletModel;

    QNetworkAccessManager *ConnectionManager;

    QString getPubKey( QString address );
    static const QString ENDPOINT;
    static const CAmount FEE;

    void insertPendingSignatureRows( QJsonArray jArr, PendingType OpType );
    void ProcessPendingResult( PendingType OpType );
    QJsonDocument callPending( QString api );

};

#endif // DIGIWAGEPLATFORM_H
