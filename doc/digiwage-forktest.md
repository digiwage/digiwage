# Digiwage fork rehearsal network

`forktest` is an isolated private network for rehearsing the Digiwage native
EVM fork. It cannot connect to mainnet: it has a different genesis block,
message magic, P2P port, RPC port, and data directory. It has no DNS or fixed
seeds.

Network parameters:

- version-6/native-EVM activation: height 30
- coinbase maturity: 10 blocks
- P2P port: 34608
- RPC port: 34602 (localhost only in the configuration below)
- data subdirectory: `forktest/`

Never copy a mainnet wallet or use a mainnet data directory for this test.

The implementation is on branch `forktest-network`. A small Git bundle can be
copied to the second server and applied to a clean DigiWage v25.1 clone:

```bash
# first server
scp /root/digiwage-forktest-thin.bundle root@SERVER_2_IP:/root/

# second server
git clone --branch v25.1 --recurse-submodules https://github.com/digiwageproject/digiwage.git digiwage-forktest
cd digiwage-forktest
git fetch /root/digiwage-forktest-thin.bundle forktest-network:forktest-network
git switch forktest-network
git submodule update --init --recursive
```

## Build on both servers

Install the normal build dependencies and SQLite, then build with descriptor
wallet support:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config \
  bsdmainutils python3 jq libgmp3-dev libevent-dev libboost-dev libsqlite3-dev
./configure CPPFLAGS='-DPACKAGE_NAME="DigiWage_Core" -DPACKAGE_VERSION="25.1.0" -DPACKAGE_URL="https://digiwage.org/" -DPACKAGE_BUGREPORT="https://github.com/digiwageproject/digiwage/issues"' \
  --without-gui --without-bdb --with-sqlite=yes --disable-bench \
  --disable-fuzz-binary --disable-shared --with-pic --enable-module-recovery
make -j2
```

## Configure server 1

Use a brand-new directory:

```bash
mkdir -p /root/digiwage-forktest
cat > /root/digiwage-forktest/digiwage.conf <<'EOF'
chain=forktest
server=1
listen=1
discover=0
dnsseed=0
upnp=0
natpmp=0
onlynet=ipv4
rpcuser=forktest
rpcpassword=CHANGE_THIS_LONG_RANDOM_PASSWORD
logevents=1
EOF
```

Start it with:

```bash
./src/digiwaged -datadir=/root/digiwage-forktest -daemon
```

Permit TCP port 34608 in the firewall only from server 2's IP. Never expose
RPC port 34602.

## Configure server 2

Use the same configuration in its own new directory and append server 1's IP:

```bash
mkdir -p /root/digiwage-forktest
cp /path/to/digiwage.conf /root/digiwage-forktest/digiwage.conf
echo 'connect=SERVER_1_IP:34608' >> /root/digiwage-forktest/digiwage.conf
./src/digiwaged -datadir=/root/digiwage-forktest -daemon
```

On server 1, optionally pin the reverse connection:

```bash
./src/digiwage-cli -datadir=/root/digiwage-forktest addnode SERVER_2_IP:34608 add
```

Confirm both nodes report `"chain": "forktest"` and the peer connection:

```bash
./src/digiwage-cli -datadir=/root/digiwage-forktest getblockchaininfo
./src/digiwage-cli -datadir=/root/digiwage-forktest getpeerinfo
```

## Create worthless test coins and cross the fork

On server 1:

```bash
CLI='./src/digiwage-cli -datadir=/root/digiwage-forktest'
$CLI createwallet forktest
ADDR=$($CLI -rpcwallet=forktest getnewaddress)
$CLI -rpcwallet=forktest generatetoaddress 11 "$ADDR"
$CLI -rpcwallet=forktest getbalances
```

At height 11, block-1 mining rewards are mature. Build a contract transaction;
the wallet records it locally, but the node must reject it from the mempool:

```bash
PRE=$($CLI -rpcwallet=forktest createcontract 6001600c60003960016000f300 1000000)
PRE_TXID=$(echo "$PRE" | jq -r .txid)
PRE_RAW=$($CLI -rpcwallet=forktest gettransaction "$PRE_TXID" | jq -r .hex)
$CLI testmempoolaccept '["'"$PRE_RAW"'"]'
$CLI -rpcwallet=forktest abandontransaction "$PRE_TXID"
```

The result must say `"allowed": false` and
`"reject-reason": "premature-contract"`.

Mine to height 29, submit the same small contract, then mine activation block
30 containing it:

```bash
$CLI -rpcwallet=forktest generatetoaddress 18 "$ADDR"
$CLI getblockcount
DEPLOY=$($CLI -rpcwallet=forktest createcontract 6001600c60003960016000f300 1000000)
CONTRACT_TXID=$(echo "$DEPLOY" | jq -r .txid)
CONTRACT_ADDRESS=$(echo "$DEPLOY" | jq -r .address)
$CLI getrawmempool
$CLI -rpcwallet=forktest generatetoaddress 1 "$ADDR"
$CLI getblockcount
H29=$($CLI getblockhash 29)
H30=$($CLI getblockhash 30)
$CLI getblock "$H29" 1
$CLI getblock "$H30" 1
```

Height 29 must have version 5. Height 30 must have version 6 and contain the
contract transaction. Check execution using the values saved above:

```bash
$CLI gettransactionreceipt "$CONTRACT_TXID"
$CLI getaccountinfo "$CONTRACT_ADDRESS"
$CLI callcontract "$CONTRACT_ADDRESS" ''
```

Create a wallet/address on server 2, send it coins from server 1, mine one
block, and confirm the balance on server 2:

```bash
# server 2
CLI='./src/digiwage-cli -datadir=/root/digiwage-forktest'
$CLI createwallet forktest2
$CLI -rpcwallet=forktest2 getnewaddress

# server 1: replace ADDRESS_FROM_SERVER_2
$CLI -rpcwallet=forktest sendtoaddress ADDRESS_FROM_SERVER_2 100
$CLI -rpcwallet=forktest generatetoaddress 1 "$ADDR"
```

## Restart and reindex checks

On both servers, record the same height, best-block hash, state root, and UTXO
root. Stop both nodes cleanly, restart them, and compare again:

```bash
$CLI getblockchaininfo
TIP=$($CLI getbestblockhash)
$CLI getblock "$TIP" 1
$CLI stop
./src/digiwaged -datadir=/root/digiwage-forktest -daemon
```

Finally reindex server 2 and compare its tip again:

```bash
$CLI stop
./src/digiwaged -datadir=/root/digiwage-forktest -reindex -daemon
```

Delete only `/root/digiwage-forktest` when the rehearsal is finished.
