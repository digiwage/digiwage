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
    void showContextMenu(const QModelIndex&);
    void ProcessSendReturn( const WalletModel::SendCoinsReturn& sendCoinsReturn, QString& msgArg);
    void SetEscrowTxId( QString TxId );

private slots:
    void on_addressBookButton_clicked();
    void on_userButton_clicked();
    void on_SendEscrowAction_clicked();

private:
    QMenu* contextMenu;
    Ui::DigiwagePlatform *ui;
    ClientModel* clientModel;
    WalletModel* walletModel;

    QNetworkAccessManager *ConnectionManager;

    QString getPubKey( QString address );
    static const QString ENDPOINT;


};

#endif // DIGIWAGEPLATFORM_H
