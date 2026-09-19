# Digiwage migration on DigiWage v25.1

This branch starts from the unmodified DigiWage `v25.1` tag
(`6c237b719c3e39607668024e0e69e2873318ee6c`). The historical Digiwage
repository at `/root/digiwage` is the consensus reference. The previous port
at `/root/digiwage-digiwage/digiwage` is an implementation reference, not a source of
consensus truth. Do not copy a disabled check merely because it allowed reindex
to advance.

## Compatibility gates

1. Preserve Digiwage's exact header and block serialization by version, including
   Quark hashing for versions below 4, the accumulator checkpoint for version 4,
   and the original 80-byte hash preimage for versions above 4. Keep DigiWage's
   extended header format behind an explicit future activation rule.
2. Load known historical blocks from the existing local block data into fixtures.
   Compare block hash, transaction hashes, Merkle root, block signature, and
   serialized size with the Digiwage implementation at genesis, PoW-to-PoS
   transition (heights 1000/1001), reward boundaries (86400, 259200, 432000,
   604800), accumulator activation (1551955), and RHF (1552000).
3. Port and test Digiwage's PoW target, PoS kernel, stake modifier, timestamp,
   signature, and difficulty rules. Ensure both acceptance and connection invoke
   the applicable checks. An invalid stake or difficulty must be rejected.
4. Port Digiwage's reward and money-supply rules, including the legacy use of
   the previous block height for expected mint. Check the exact boundary blocks.
5. Map each Digiwage script, transaction, and network activation explicitly.
   Keep incompatible DigiWage rules inactive on historical Digiwage blocks; choose
   any future DigiWage-feature activation height as a separate network fork.
6. Reindex from genesis in an isolated data directory. Compare hashes at the
   known checkpoints and the final tip with a Digiwage node. Run `verifychain`
   and invalid-block tests before using the node for balances or staking.

## Known hazards in the previous port

- `ConnectBlock` omits `UpdateHashProof`, which contains PoS proof, timestamp,
  target, and last-PoW checks.
- The body carries Digiwage's legacy signature, while DigiWage's signature verifier
  reads the header signature. The legacy signature needs its own verification.
- The DigiWage `nLastPOWBlock` is 5000 on mainnet; Digiwage's last PoW height is 1000.
- Digiwage validates block N against `GetBlockValue(N - 1)`. Any DigiWage reward
  mapping must account for that shifted height, including the block-1 premine.
- Mainnet does not initialize every newly added PoS difficulty parameter.
- Digiwage's first PoS target is still calculated by Dark Gravity Wave because
  block 1000 is the last PoW block. Later PoS targets use Digiwage's original
  per-block retarget. Applying DigiWage's retarget here produces the wrong target.
- The original v1 modifier initializes block 1 with
  `uint64_t("stakemodifier")`, a string-literal pointer conversion. Its numeric
  value depends on the deployed binary. Recomputing the first 1001 historical
  headers with several seed values shows the modifiers converge at height 11;
  the first PoS block at height 1001 connects with the converged modifier.

These hazards are reasons to build tests and stage consensus changes. Sync
progress alone is not an acceptance criterion.

## Verified first stage

- DigiWage `v25.1` is the starting commit; mainnet genesis and wire/message values
  are sourced from Digiwage. No DigiWage DNS or fixed seeds are used by mainnet.
- Header hash vectors from the local historical block file pass for genesis,
  the PoW/PoS boundary, and height 112750. A complete block at height 1001
  deserializes with its 70-byte body signature and verifies against its
  coinstake public key.
- An isolated `-loadblock` run connected blocks 0 through 1000, including the
  first Dark Gravity Wave adjustment at height 25. The hash at height 1000 is
  `0000005a2f56efc4caddb8c454bb2b72e18ed847d3d5fb3fe1edca31a12c86ad`.
- An isolated chain reopens at heights 30 and 1000. Historical EVM roots are
  internal index state, not part of Digiwage's serialized block header.

## Current validation boundary

The clean implementation validates the legacy block signature, v1 kernel,
64-bit stake modifier, exact PoW/PoS difficulty, timestamps, and reward rules.
Its modifier ordering uses the legacy numeric `uint256` comparison; the modern
bytewise comparison diverges at height 694 and rejects block 1074. Seed probes
confirm that the historical pointer-derived block-1 seed converges by height 11.

Fresh isolated imports reached height 154494 from the existing local block file
and height 235060 from the official v1.2.0 bootstrap. The checkpoint at height
235000 matched `25961563bc9afae2b7ed3bc8dc61da40185bbe86096d687f746c632a885a1e01`.
The official v2.0.1 binary was SHA256 verified against its release checksum and
used to obtain mainnet block files past height 2.2 million. Header-first import
is supported even though legacy headers do not encode the proof type; the full
block body updates the index and performs PoS validation.

A clean import validated every available block through height 2218399, then a
reopened node synchronized the remaining legacy inventory windows through the
live tip at height 3457740. Its tip hash
`3075d637627485595e1afcd0fddf66f2895a21d41bfd7b6dea5ab64fb68b21d3`
matched the public explorer. An earlier proof-hash comparison at height
2219414 also matched the explorer:
`0002f0824ab992dcb03c91fb09f585649a5a9d557b343cd04e37d2088af9fec6`.

The clean node validates the modifier selection change at 1551935, modifier-v2
activation at 1551945, version-4 accumulator transition at 1551955, and RHF at
1552000. RHF activates version 5, 15-second timestamp slots, 600-block stake
depth, the v2 target limit, and the 30-block v2 retarget interval. The deployed
chain stops requiring calculated `nBits` equality at modifier-v2 activation;
kernel target and signature checks remain mandatory. Cold-stake scripts are
recognized and validated, and ordinary wallet spending selects the owner key.

DigiWage CSV, SegWit, QIP9, offline staking, reduced block time, Taproot, and DigiWage
header signatures remain inactive on historical Digiwage mainnet. Native EVM
activation is scheduled for height 4000000. Starting at that height, version-6
headers commit `hashStateRoot`, `hashUTXORoot`, and `prevoutStake`; contract
execution, QIP5-QIP7, and the Shanghai EVM rules activate atomically. The
extended header carries the PoS signature, which covers both roots and the
stake prevout; legacy body signatures remain unchanged below the fork. Contract
transactions are rejected before that boundary. This height remains a network
release decision and must be coordinated with stakers, exchanges, explorers,
and seed operators before deployment.

Use the isolated `forktest` network in `doc/digiwage-forktest.md` to rehearse
this boundary; it activates the same rules at height 30 with worthless coins.
