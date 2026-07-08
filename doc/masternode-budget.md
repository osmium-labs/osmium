Masternode Governance & Budget System
======================================

Maximus supports decentralized budgets ("governance objects") that are paid
directly from the blockchain via superblocks. This document reflects the
current `gobject`-based RPC interface (verified against `src/rpc/governance.cpp`).

> This document replaces an earlier version describing a legacy
> `mngovernance`/`mnfinalbudget` interface that no longer exists in this
> codebase.

Overview
--------

Governance objects go through these stages:

 * **prepare** — create a collateral transaction that funds a proposal (fee: **1 MAXI**, see `GOVERNANCE_PROPOSAL_FEE_TX`)
 * **submit** — propagate the proposal to the network
 * **voting** — masternodes vote `funding`/`valid`/`delete`/`endorsed`, each with an outcome of `yes`/`no`/`abstain`
 * **superblock** — once per cycle, funded proposals are paid out automatically by the network

Superblocks occur every `nSuperblockCycle` blocks (**36000 blocks (~1 month) on mainnet**),
starting at block `nSuperblockStartBlock` (**18000**). These values differ on
testnet/devnet/regtest — query them live with `getgovernanceinfo` rather than
assuming mainnet numbers.

1. Prepare a proposal
----------------------

    gobject prepare <parent-hash> <revision> <time> <data-hex> <use-IS> [outputHash] [outputIndex]

  - `parent-hash` — hash of the parent object; use `"0"` for a top-level proposal
  - `revision` — object revision number (start at `1`)
  - `time` — creation timestamp
  - `data-hex` — the proposal payload, hex-encoded JSON (name, URL, payment amount, start/end block, payout address)
  - `use-IS` — deprecated, ignored (kept for backward compatibility)
  - `outputHash`/`outputIndex` — optional: a specific UTXO to pay the collateral fee from

This creates and broadcasts a **1 MAXI** collateral transaction. Output is the
transaction hash — save it for the next step.

**Warning:** if any field in the proposal data changes after this step, the
collateral transaction becomes invalid and must be recreated.

2. Submit the proposal
-----------------------

    gobject submit <parent-hash> <revision> <time> <data-hex> <fee-txid>

`fee-txid` is the collateral transaction hash from step 1. The collateral
transaction must have enough confirmations before this succeeds. Output is
the proposal's governance-object hash, used by all subsequent commands.

3. Inspect a proposal
-----------------------

    gobject get <governance-hash>

Returns full details: data, collateral hash, vote tallies (`AbsoluteYesCount`,
`YesCount`, `NoCount`, `AbstainCount`) per signal (funding/valid/delete/endorsed),
and local validity status.

    gobject list [signal] [type]

Lists all known governance objects. `signal` filters by
`valid|funding|delete|endorsed|all` (default `valid`); `type` filters by
`proposals|triggers|all` (default `all`).

4. Vote on a proposal
-----------------------

Requires a wallet loaded with masternode voting keys.

    gobject vote-many <governance-hash> <vote> <vote-outcome>

Votes with every masternode voting key present in the local wallet.
  - `vote` — one of `funding|valid|delete|endorsed`
  - `vote-outcome` — one of `yes|no|abstain`

    gobject vote-alias <governance-hash> <vote> <vote-outcome> <protx-hash>

Votes with a single specific masternode's voting key (by its `proTxHash`).

A proposal typically needs its `AbsoluteYesCount` on the `funding` signal to
clear a threshold of roughly 10% of the current masternode count (computed
live — see `fundingthreshold` in `getgovernanceinfo`, below) to be included
in the next superblock.

5. Check governance & superblock parameters
---------------------------------------------

    getgovernanceinfo

Returns live parameters, including:
  - `governanceminquorum` — absolute minimum vote count for a governance action
  - `proposalfee` — current collateral fee required (in MAXI)
  - `superblockcycle` — blocks between superblocks
  - `superblockmaturitywindow` — the superblock trigger creation window
  - `lastsuperblock` / `nextsuperblock` — block heights
  - `fundingthreshold` — absolute yes-vote count currently required to fund a proposal
  - `governancebudget` — total superblock budget available next cycle, in MAXI

    getsuperblockbudget <block-height>

Returns the maximum total superblock payout allowed at a given height.

6. Get paid
------------

If a proposal's funding vote clears the threshold before the superblock
maturity window closes, it is included in the next superblock and paid
automatically to the address specified in its proposal data — no further
action is required.

7. Full command reference
---------------------------

    gobject "command"... ( "passphrase" )
      check              - Validate governance object data (proposal only)
      prepare            - Prepare governance object by signing and creating a collateral tx
      list-prepared      - List governance objects prepared by this wallet
      submit             - Submit governance object to network
      deserialize        - Deserialize governance object from hex string to JSON
      count              - Count governance objects and votes
      get                - Get governance object by hash
      getcurrentvotes    - Get current (tallying) votes for a governance object
      list               - List governance objects (filterable by signal/type)
      diff               - List differences since last diff or list
      vote-alias         - Vote on a governance object by masternode proTxHash
      vote-many          - Vote on a governance object using all wallet-held voting keys

    voteraw <mn-collateral-tx-hash> <mn-collateral-tx-index> <governance-hash> <vote-signal> <vote-outcome> <time> <vote-sig>

  - Relay a governance vote using an externally-provided signature (for offline/cold-key signing setups)

    getgovernanceinfo
    getsuperblockbudget <height>

All commands and their exact parameters are also available live via
`maximus-cli help gobject` and `maximus-cli help <command>` on a running node,
which will always be authoritative over this document.
