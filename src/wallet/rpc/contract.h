#ifndef DIGIWAGE_WALLET_RPC_CONTRACT_H
#define DIGIWAGE_WALLET_RPC_CONTRACT_H

#include <span.h>

class CRPCCommand;

namespace wallet {
Span<const CRPCCommand> GetContractRPCCommands();
} // namespace wallet

#endif // DIGIWAGE_WALLET_RPC_CONTRACT_H
