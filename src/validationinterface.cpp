// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The DIGIWAGE developers // Assuming copyright needs update to 2023/2024?
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"
#include <boost/bind/bind.hpp> // Explicitly include for placeholders _1, _2 etc.

// It's good practice to explicitly include boost/bind.hpp when using boost::bind
// especially when placeholders like _1, _2 are involved.
// Make sure this doesn't conflict with other includes. Usually included via signals2.
// If you still have issues with placeholders, use boost::placeholders::_1, etc.
// For example: boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, boost::placeholders::_1)
// Using namespace boost::placeholders; can also work but is less explicit.

using boost::placeholders::_1;
using boost::placeholders::_2;


static CMainSignals g_signals;

CMainSignals& GetMainSignals()
{
    return g_signals;
}

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
    // Keep registration using boost::bind as before
    // XX42 g_signals.EraseTransaction.connect(boost::bind(&CValidationInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.UpdatedBlockTip.connect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1));
    g_signals.SyncTransaction.connect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2));
    g_signals.NotifyTransactionLock.connect(boost::bind(&CValidationInterface::NotifyTransactionLock, pwalletIn, _1));
    g_signals.UpdatedTransaction.connect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.SetBestChain.connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.Broadcast.connect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn)); // No placeholders needed for void()
    g_signals.BlockChecked.connect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
    // XX42    g_signals.ScriptForMining.connect(boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, _1));
    g_signals.BlockFound.connect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, _1));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {
    // --- CHANGE HERE ---
    // Disconnect using the object pointer. This disconnects all slots
    // associated with the object 'pwalletIn' for the given signal.
    g_signals.BlockFound.disconnect(pwalletIn);
    // XX42    g_signals.ScriptForMining.disconnect(pwalletIn);
    g_signals.BlockChecked.disconnect(pwalletIn);
    g_signals.Broadcast.disconnect(pwalletIn);
    g_signals.SetBestChain.disconnect(pwalletIn);
    g_signals.UpdatedTransaction.disconnect(pwalletIn);
    g_signals.NotifyTransactionLock.disconnect(pwalletIn);
    g_signals.SyncTransaction.disconnect(pwalletIn);
    g_signals.UpdatedBlockTip.disconnect(pwalletIn);
    // XX42    g_signals.EraseTransaction.disconnect(pwalletIn);
}

void UnregisterAllValidationInterfaces() {
    // This function is fine as it disconnects *all* slots,
    // regardless of the object or function they are bound to.
    g_signals.BlockFound.disconnect_all_slots();
    // XX42    g_signals.ScriptForMining.disconnect_all_slots();
    g_signals.BlockChecked.disconnect_all_slots();
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.UpdatedTransaction.disconnect_all_slots();
    g_signals.NotifyTransactionLock.disconnect_all_slots();
    g_signals.SyncTransaction.disconnect_all_slots();
    g_signals.UpdatedBlockTip.disconnect_all_slots();
    // XX42    g_signals.EraseTransaction.disconnect_all_slots();
}

void SyncWithWallets(const CTransaction &tx, const CBlock *pblock = NULL) {
    g_signals.SyncTransaction(tx, pblock);
}