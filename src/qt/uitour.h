// Copyright (c) 2026 The DigiWage developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIWAGE_QT_UITOUR_H
#define DIGIWAGE_QT_UITOUR_H

#include <QString>

class BitcoinGUI;

/** Developer helper (-uitour=<dir>): walks through every page and dialog in
 *  both appearance modes, saves a PNG of each into <dir>, then exits. */
void RunUiTour(BitcoinGUI* window, const QString& outDir);

#endif // DIGIWAGE_QT_UITOUR_H
