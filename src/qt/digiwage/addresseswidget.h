// Copyright (c) 2019-2020 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ADDRESSESWIDGET_H
#define ADDRESSESWIDGET_H

#include "qt/digiwage/pwidget.h"
#include "addresstablemodel.h"
#include "furabstractlistitemdelegate.h"

#include <QWidget>

class AddressFilterProxyModel;
class TooltipMenu;
class DIGIWAGEGUI;
class WalletModel;

namespace Ui {
class AddressesWidget;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class AddressesWidget : public PWidget
{
    Q_OBJECT

public:
    explicit AddressesWidget(DIGIWAGEGUI* parent);
    ~AddressesWidget();

    void loadWalletModel() override;

private Q_SLOTS:
    void handleAddressClicked(const QModelIndex &index);
    void onStoreContactClicked();
    void onEditClicked();
    void onDeleteClicked();
    void onCopyClicked();
    void onAddContactShowHideClicked();
    void onSortChanged(int idx);
    void onSortOrderChanged(int idx);

    void changeTheme(bool isLightTheme, QString &theme) override;
private:
    Ui::AddressesWidget *ui;

    FurAbstractListItemDelegate* delegate = nullptr;
    AddressTableModel* addressTablemodel = nullptr;
    AddressFilterProxyModel *filter = nullptr;

    TooltipMenu* menu = nullptr;

    // Cached index
    QModelIndex index;

    // Cached sort type and order
    AddressTableModel::ColumnIndex sortType = AddressTableModel::Label;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;

    void updateListView();
    void sortAddresses();
};

#endif // ADDRESSESWIDGET_H
