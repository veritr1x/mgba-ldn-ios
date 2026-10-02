# Source provenance

- mGBA / Gr3nSkyDragon/mgba_LDN: based on
  `9e73f2d6e2bc2b0c0cc584afa9f7fc410b92697f`. Existing source and the new
  mGBA-derived frontend, Apple crypto adapter, RFU/Pia backend and tests retain
  MPL-2.0. See the repository's `LICENSE` and upstream file notices.
  `PlayerView.m` and `PlayerMath.h` adapt Android `GameView.java` and
  `MainActivity.java` layout, controls, palette and display settings.
  `relay-backend.c` adapts the game-frame logic from `rfu-broadcast.c` and uses
  the upstream portable Pia, reliable-channel and trade-shim implementations.
  Emerald uses the upstream RFU driver. Ruby/Sapphire use the upstream
  `rfu-wrapper.c` and `rfu-wrapper-air.c` cable translator with an injected Apple
  relay backend; the translation code and its original notices remain intact.
- veritr1x/ldn-relay: companion controller and `relay/` protocol/codec files
  adapted from `9713b4366c3416259bdf052ff02ef756d62eb657` (v0.1.0), with
  negotiated event batching from `4c0b420` (v0.1.1), under MIT. The current shared transport follows the 0.5.0 approval-mode
  relay, including streaming, notification recovery and batched messages.
  `apple_signing.py` also comes from LDN Relay, under the same MIT licence.
  The copyright notice and permission text are in `relay/LICENSE`.
- GB-Link / GB-Link-Switch-LDN: all 76 Wonder Cards, gift exchange and RFU leader
  from `c1a3a97f3ab5c60e87307089d6b7db10e3fd5993`. The gift modules/cards are
  GPL-3.0 and the RFU leader is AGPL-3.0. Original files, hashes, card-building
  sources, credits and licences are in `gifts/vendor/gblink/`. See
  [Wonder Card credits](gifts/README.md) for Project Wonder (Goppier), RAF,
  Decryptu and the original contributors. GB-Link is credited for reused code
  and content, not as an endorser of this port. Apple binaries containing this
  feature are distributed under AGPL-3.0, retaining all file-level notices.
- zstd: uses the existing vendored `src/third-party/zstd` source and licence.
- Apple system frameworks are linked from the installed SDK and not vendored.

ROMs, test saves, development profiles, device identifiers and private hardware
logs are not part of this port's source distribution.
