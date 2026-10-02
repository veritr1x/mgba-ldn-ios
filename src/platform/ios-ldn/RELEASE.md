GBA playback and Pokémon trading through [LDN Relay](https://github.com/veritr1x/ldn-relay).

- **iOS.ipa:** iPhone/iPad, iOS 17+. Install with AltStore Classic or another IPA sideloading tool that signs it with your Apple account. This is not a directly installable App Store or TestFlight build.
- **macOS-arm64.zip:** Apple silicon Mac, macOS 14+. Unzip and move the app to Applications. The app is ad-hoc signed and not notarized.

Choose **☰ → Open game** to import your own `.gba` ROM. **Import save** and **Export save** use raw `.sav` files. No games or saves are bundled. Back up saves before changing signing accounts or reinstalling.

The player now follows the Android frontend: full-screen gameplay, portrait and landscape touch controls, a compact menu, customizable button colors and backgrounds, display effects, frame blending and gamepad input. **☰ → Switch multiplayer** keeps the host/join flow with approval on the modified Switch. Save imports keep a backup; exports use the game name.

- **Switch .nro:** LDN Relay 0.5.0, built in CI from pinned source. Copy it to `/switch/ldn-relay/` on the modified Switch SD card.
- **Switch ZIP:** the same NRO plus installation instructions, licenses and build provenance. Apple and relay version numbers are independent.

FireRed / LeafGreen supports hosting or joining with one guest. Emerald now uses the upstream wireless driver, and Ruby/Sapphire use its cable-to-wireless translator. These new paths are experimental and join a stock FireRed/LeafGreen host; complete console trades have not yet been verified. Automatic adapter selection chooses the driver for your ROM. Android's USB/ESP32 transport itself is not included. [Feature comparison](https://github.com/veritr1x/mgba-ldn-ios-macos/blob/ios-relay/src/platform/ios-ldn/ANDROID_PARITY.md).

Diagnostics and private trade capture are off by default. Includes upstream `9e73f2d`.

[Setup and controls](https://github.com/veritr1x/mgba-ldn-ios-macos/blob/ios-relay/src/platform/ios-ldn/README.md).

Multiple trades were reported working on the preceding 0.5.0 build. These downloads are a preview: automated tests and builds do not substitute for a fresh release-build console trade or a sideloading check on another Apple account.

Wonder Card gifts: all 76 cards from GB-Link’s pinned catalogue, plus `.wc3` import. Use the game menu to open the gift sender; no ROM is needed. This adapter has local protocol coverage only; physical-console gift delivery, saving and redemption are not yet verified. GB-Link, Project Wonder and original card authors are credited in the app and bundled notices. Combined Apple builds include GPL/AGPL code and corresponding source references.
