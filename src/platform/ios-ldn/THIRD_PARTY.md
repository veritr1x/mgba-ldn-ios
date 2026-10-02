# Source provenance

- mGBA / Gr3nSkyDragon/mgba_LDN: based on
  `9e73f2d6e2bc2b0c0cc584afa9f7fc410b92697f`. Existing source and the new
  mGBA-derived frontend, Apple crypto adapter, RFU/Pia backend and tests retain
  MPL-2.0. See the repository's `LICENSE` and upstream file notices.
  `relay-backend.c` adapts the game-frame logic from `rfu-broadcast.c` and uses
  the upstream portable Pia, reliable-channel and trade-shim implementations.
- veritr1x/ldn-relay: companion controller and `relay/` protocol/codec files
  adapted from `9713b4366c3416259bdf052ff02ef756d62eb657` (v0.1.0), with
  negotiated event batching from `4c0b420` (v0.1.1), under MIT. The current shared transport follows the 0.5.0 approval-mode
  relay, including streaming, notification recovery and batched messages.
  `apple_signing.py` also comes from LDN Relay, under the same MIT licence.
  The copyright notice and permission text are in `relay/LICENSE`.
- zstd: uses the existing vendored `src/third-party/zstd` source and licence.
- Apple system frameworks are linked from the installed SDK and not vendored.

ROMs, test saves, development profiles, device identifiers and private hardware
logs are not part of this port's source distribution.
