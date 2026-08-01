# Known Issues / Future Work

Running list of deferred items, unresolved edge cases, and larger
follow-up projects identified during development. Not urgent, not
blocking any release, but worth not losing track of.

## Deferred fixes

- Connection-burst pacing in ThreadOpenMasternodeConnections (net.cpp):
  can open 30-50+ masternode connections in a tight burst when a DKG
  session initializes. Contributed to OOM crashes on memory-constrained
  nodes. Proposed fix: increase post-connect sleep from 100ms to 250ms.
  Never implemented or tested.

- MIN_MASTERNODE_PROTO_VERSION gap (70230 vs Dash's 70240): Dash's
  70240 corresponds to a backported request-tracking overhaul (Bitcoin
  PR #19988, merged into Dash 2026-07-24), which explicitly covers
  quorum messages. Not in this codebase. Worth evaluating as a
  backport candidate.

- DKG message-handling consolidation: Dash consolidates DKG message
  handling into one templated function (3 MarkBadMember call sites).
  This codebase still has the older duplicated structure (9 call
  sites). Not a known bug, but worth reducing eventually.

## Build system / tooling

- CMake migration: Bitcoin Core fully migrated to CMake. Dash has NOT
  followed - still Autotools-based. No external pressure to migrate.
  Revisit only if build fragility becomes a frequent cost, or before
  a Qt6 upgrade.

- Qt6 upgrade: currently on Qt 5.15.19. Bitcoin's Qt6 migration
  depended on their CMake migration first - don't attempt Qt6 before
  CMake here either.

- native_clang pinned at 10.0.1, used for macOS cross-compilation.
  Compliant with the project's stated Clang 8+ minimum, just old.
  Target Clang 16-17 if upgrading deliberately.

## Research / long-term

- Post-quantum cryptography: no mature PQC migration path exists yet
  in the Bitcoin/Dash ecosystem. Not urgent, but a real long-term risk
  to ECDSA/BLS signatures. Revisit if/when Bitcoin or Dash publish a
  concrete plan.
