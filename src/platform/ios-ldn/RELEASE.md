GBA playback and FireRed / LeafGreen trading through [LDN Relay](https://github.com/veritr1x/ldn-relay).

- **iOS.ipa:** iPhone/iPad, iOS 17+. Install with AltStore Classic or another IPA sideloading tool that signs it with your Apple account. This is not a directly installable App Store or TestFlight build.
- **macOS-arm64.zip:** Apple silicon Mac, macOS 14+. Unzip and move the app to Applications. The app is ad-hoc signed and not notarized.

Choose **Open game** to import your own `.gba` ROM. **Import save** and **Export save** use raw `.sav` files. No games or saves are bundled. Back up saves before changing signing accounts or reinstalling.

The interface now offers host/join without protocol settings or benchmark buttons. Save imports keep a backup, and exports use the game name. Diagnostics and trade capture are off by default. Includes upstream `9e73f2d` (Emerald ESP32 connectivity fix; Apple multiplayer remains FireRed/LeafGreen).

[Setup and controls](https://github.com/veritr1x/mgba-ldn-ios-macos/blob/ios-relay/src/platform/ios-ldn/README.md).

Multiple trades were reported working on the preceding 0.5.0 build. These downloads are a preview: automated tests and builds do not substitute for a fresh release-build console trade or a sideloading check on another Apple account.
