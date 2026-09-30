# mGBA LDN for iOS and Mac Catalyst

Experimental UIKit frontend for GBA emulation and FireRed/LeafGreen local
wireless through [LDN Relay](https://github.com/veritr1x/ldn-relay).
This is a personal, AI-assisted port based on Gr3nSkyDragon/mgba_LDN commit
`f6d6cd9161745e8b0a85a8803774011f1b1087ec`; it is not endorsed by mGBA or Nintendo.

**Status, 1 October 2026: the Mac build runs FireRed, loads a save, joins a
stock Switch 2 session through the relay, authenticates Pia packets, completes
the Pia handshake, and receives the game's RFU acceptance and game frames.
A complete trade has not succeeded.** The latest completed hardware attempt
disconnected shortly after game acceptance. BLE traffic pacing is under test.
The iOS app builds and signs, but physical iPhone/iPad gameplay is unverified.

## Runtime

```text
UIKit app on iPhone / iPad / Apple silicon Mac
  GBA core → emulated Wireless Adapter → Pia/game adapter
  → CoreBluetooth → modified original Switch running LDN Relay
  → native LDN / UDP → stock Switch 2 hosting FireRed or LeafGreen
```

The phone uses public CoreBluetooth APIs. It does not join LDN with its own
Wi-Fi radio and does not need a USB adapter. The modified Switch handles the
native radio connection. This route does not import `prod.keys`: native Switch
services perform LDN authentication, and session metadata supplies the Pia key
material. The upstream Windows/Android instructions describe different routes.

Implemented: ROM import, 240×160 video, audio output, touch and keyboard input,
raw save import/export, autosave, native BLE relay discovery/join/leave, AES-GCM
with the protocol's 8-byte tag, Pia handshake, reliable transport, RFU and the
upstream FRLG game-frame/trade-shim adapter. Reliable messages are batched,
queued under BLE backpressure, and retried with a conservative timeout floor.

Limitations: joining FRLG only; no hosting, ROM browser, save states, controller
configuration or Ruby/Sapphire cable wrapper. Loading another GBA game does
not imply multiplayer compatibility. The generic relay can transport other
protocols, but those games need their own companion adapter. Keep the app in
the foreground on iOS; leaving it or pausing ends multiplayer. The Mac keeps
running when another app gains focus. A failed game session requires leaving
and rejoining in the relay tab. No trade, post-trade save, audio-quality or
sustained multiplayer performance claim is made from a build or handshake.

## Build

Requires a full Xcode installation with iPhoneOS and Mac SDKs, CMake, Ninja and
Python 3. Tested with Xcode 27. Build scripts target arm64 iOS 17+ and Apple
silicon Mac Catalyst (macOS 14+); Intel and simulator builds are not provided.
From the repository root:

```sh
# iOS core plus an ad-hoc signed app (not installable on an ordinary iPhone):
python3 src/platform/ios-ldn/build.py --platform ios

# Same UIKit player for the Mac:
python3 src/platform/ios-ldn/build.py --platform mac

# Installable development build using your own unexpired wildcard profile
# and matching certificate already present in the local keychain:
python3 src/platform/ios-ldn/build.py --platform ios --skip-core \
  --profile /path/to/development.mobileprovision

# Synthetic crypto and protocol checks, with AddressSanitizer/UBSan:
sh src/platform/ios-ldn/test.sh
```

Outputs: `build/player-ios/mGBA LDN.app` and
`build/player-mac/mGBA LDN.app`. `--skip-core` reuses the core build; omit it
after changing core sources or build configuration. iOS installation requires
your device to be covered by the development profile and Developer Mode enabled.
The scripts contain no signing identity, device identifier or provisioning profile.
No ROMs, saves or keys are bundled. `build-core.sh` can also build only the
static GBA/RFU core; that alone is not an application.

## Try it

1. Open the player and use **Load ROM** to import an uncompressed `.gba` file.
   Use **Import save** for a compatible raw `.sav` with Wireless Club access.
2. Keep other LDN Relay companion apps closed so only this app advertises.
3. On the modified Switch, open LDN Relay v0.1.1 through Album (Applet Mode).
   In the player's **Switch relay** tab, leave the app open, then press A on
   the modified Switch to connect Bluetooth.
4. On the stock Switch 2, host FRLG in the Pokémon Center's Direct Corner
   (right counter upstairs), waiting as leader. Scan in the player and tap
   the discovered FireRed/LeafGreen session. The preset uses protocol 3 and
   UDP port 12345.
5. Return to **Play**. In the emulated game choose Direct Corner → Join Group
   and select the host. This remains a development test; joining the LDN
   network or showing “Pia handshake connected” is not a completed trade.

Keyboard: arrows, Z=A, X=B, Return=Start, right Shift=Select, A=L, S=R.
Touch controls are also available. Use copies of saves for testing.

On iOS data lives in the app's Documents directory. On Mac it lives in
`~/Library/Application Support/mGBA LDN/`. Imported ROMs and saves are grouped
by ROM SHA-256, with saves written atomically and backed up on load. Logs are
`game.log` and `relay.log`; `--trace-rfu` enables a much larger RFU trace.
Logs and saves are local development data and are not included in this source.

## Validation boundaries

The protocol test checks a NIST AES-GCM vector, truncated 8-byte tags and
rejection of tampering; metadata bounds; encrypted handshake replies; waiting
for the host's game acceptance; duplicate suppression; retained messages under
BLE backpressure; ACK/data batching and message flags; retry pacing; timeout
and backend reinitialization. It uses a synthetic host, not a ROM or console.

Physical Mac tests separately confirmed ROM/save playback, BLE connection,
native LDN join, live authenticated Pia handshake and game acceptance. The
first attempt overflowed the outgoing command queue. Backpressure removed
that failure in the second attempt, which exchanged game frames before the
stock console reported an error. A third attempt with client-side batching also failed, with 281 additional
incoming UDP drops reported by relay v0.1.0 across the test interval. The
updated companion negotiates event batching with relay v0.1.1 to reduce that
transport overhead; its physical test is pending. A successful full trade and
save reload remain required.

See [THIRD_PARTY.md](THIRD_PARTY.md) for provenance and licensing. The relay's
MIT licence does not relicense mGBA-derived code.
