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

/** Developer helper (-uifunctest=<destAddress>,<amount>,<outFile>): drives the
 *  Receive page to get a fresh address, drives the Send page to pay
 *  <destAddress> <amount> DIGIWAGE end to end through the real GUI widgets and
 *  confirmation dialog, then writes "receive_address=...\nsend_txid=...\n" to
 *  <outFile> and exits. Proves Send/Receive work through the GUI, not RPC. */
void RunUiFunctionalTest(BitcoinGUI* window, const QString& destAddress, const QString& amount, const QString& outFile);

#endif // DIGIWAGE_QT_UITOUR_H
