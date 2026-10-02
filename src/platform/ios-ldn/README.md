# mGBA LDN for iOS and macOS

A GBA player with FireRed / LeafGreen multiplayer through
[LDN Relay](https://github.com/veritr1x/ldn-relay). A modified Switch provides
the wireless connection to the stock Switch or Switch 2.

## Install

Download the latest files from [Releases](https://github.com/veritr1x/mgba-ldn-ios-macos/releases).

- **iPhone / iPad (iOS 17+):** import the `.ipa` into
  [AltStore Classic](https://faq.altstore.io/altstore-classic/your-altstore)
  or another IPA sideloading tool. The tool signs it with your Apple account;
  follow its instructions for Developer Mode and refreshing the app.
  The download has no developer profile and cannot install just by tapping it.
  JIT is not required.
- **Mac (macOS 14+, Apple silicon):** unzip the macOS download, drag
  **mGBA LDN.app** to Applications, and open it. This build is ad-hoc signed,
  not notarized. If macOS blocks it, use Apple's
  [Open Anyway instructions](https://support.apple.com/en-us/102445)
  after checking that you downloaded the release from this repository.

Allow Bluetooth when asked. ROM playback and saves work without a relay.
No ROMs, saves, console keys or signing credentials are bundled.

## Games and saves

Choose **Open game → Import .gba file** for an uncompressed GBA ROM.
Imported games appear in the same menu, and the last game reopens at launch.

Save using the game's own menu. The app periodically writes that save to disk,
including when pausing or leaving the app. **Export save** writes a named `.sav`
copy to Files or a folder you choose. **Import save** replaces the current game's
save after confirmation and restarts it. Use a raw save for that ROM, not a
save state; the app cannot verify that the chosen save belongs to the game.

Each ROM has its own folder and `game.sav`. Existing saves are backed up when
loaded or replaced. On iOS, look in **Files → On My iPhone/iPad → mGBA LDN → Games**;
on Mac, look in `~/Library/Application Support/mGBA LDN/Games/`.
Folders use ROM hashes, so ROM revisions have separate saves. Export important
saves before reinstalling, changing signing accounts, or deleting the app.
Keep the same bundle ID and signing account when updating to retain app data.

Touch controls are on screen. Keyboard: arrows, **Z** = A, **X** = B,
**Return** = Start, **right Shift** = Select, **A** = L, **S** = R.

## Trade with a Switch

Use [LDN Relay 0.5.0 or later](https://github.com/veritr1x/ldn-relay) on a modified
Switch, plus FireRed or LeafGreen on the stock console. Have compatible saves
with access to Direct Corner and Pokémon to trade.

1. Load and resume the ROM. Close other relay companion apps.
2. Open **Multiplayer**. On the modified Switch, launch LDN Relay through
   Album and press **A** to find this app, then **A** again to approve it.
   Approval is needed each time; no key import is required.
3. Choose a host:
   - **Stock console hosts:** choose Direct Corner → Become Leader there,
     tap **Find game** in this app, and select the room. Return to **Play**
     and choose Direct Corner → Join Group.
   - **This app hosts:** tap **Host game**, return to **Play**, and choose
     Direct Corner → Become Leader. Choose Join Group on the stock console.
4. Complete the trade and save in both games. Use **Disconnect game** before
   changing games or switching between hosting and joining.

Keep the iOS app visible and unlocked. Pausing or leaving it ends multiplayer;
resume and reconnect before trying again. The Mac continues running when it
loses focus. **Show connection details** provides errors for troubleshooting.
If a room does not appear, make sure the other game is waiting as leader.

Multiple completed iPhone ↔ Switch 2 trades have been reported with the 0.5.0
transport. Release 0.6.0 keeps that game protocol and adds the interface and
file-handling changes. A fresh release-build trade still needs hardware testing.
Only one guest is supported. Other GBA games can run, but this Apple multiplayer
adapter currently supports FireRed / LeafGreen only. There are no save states,
controller configuration or cable-link emulation in this frontend.

## Build

Requires full Xcode, CMake, Ninja and Python 3. From the repository root:

```sh
python3 src/platform/ios-ldn/build.py --platform ios
python3 src/platform/ios-ldn/build.py --platform mac --skip-core
python3 src/platform/ios-ldn/tools/package.py
```

Apps are under `build/player-{ios,mac}-v0.6.0/`; distributable IPA, macOS ZIP,
notices and SHA-256 checksums are under `dist/`. Both apps target arm64.
CI runs tests and builds both downloads; pushing a matching `v0.6.0` tag
publishes them as a GitHub prerelease. No Apple signing secrets are needed.

For a locally installable iOS development build, supply your own valid profile
and matching certificate in Keychain:

```sh
python3 src/platform/ios-ldn/build.py --platform ios --skip-core \
  --profile /path/to/development.mobileprovision --bundle-id your.bundle.id
```

Omit `--skip-core` after core changes. Do not package a profile-signed build
for public downloads; rebuild without `--profile` first.

[Developer guide](DEVELOPING.md) · [Source and licences](THIRD_PARTY.md)
