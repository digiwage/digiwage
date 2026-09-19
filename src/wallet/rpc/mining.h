#ifndef DIGIWAGE_WALLET_RPC_MINING_H
#define DIGIWAGE_WALLET_RPC_MINING_H

#include <span.h>

class CRPCCommand;

namespace wallet {
Span<const CRPCCommand> GetMiningRPCCommands();
} // namespace wallet

#endif // DIGIWAGE_WALLET_RPC_MINING_H
