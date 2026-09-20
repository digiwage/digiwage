// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_THEMEDICON_H
#define BITCOIN_QT_THEMEDICON_H

#include <QIcon>
#include <QString>

/** Icons that follow the active appearance mode. The colour is resolved every time
 *  the icon is painted, so switching Light/Dark recolours them without recreating widgets. */
namespace ThemedIcon {
enum Role { Single, Text, Menu, NavBar, Button, ButtonLight };

QIcon create(const QString& resource, Role role, double opacity = 1.0);
}

#endif // BITCOIN_QT_THEMEDICON_H
