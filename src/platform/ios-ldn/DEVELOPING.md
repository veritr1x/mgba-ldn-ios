# Working on the Apple app

The source directory keeps its original `ios-ldn` name; both Apple platforms
use it. The app uses the upstream mGBA emulator, RFU implementation and Pia
code. Keep upstream changes and their original authors when merging
`Gr3nSkyDragon/mgba_LDN`'s `mgba-ldn` branch.

- `main.m`: game menu, emulator loop, audio, input and file pickers.
- `PlayerView.m`: touch surface, display settings and background imports.
- `PlayerMath.h`: portable layout, hit testing, color transforms and frame blending.
- `GameProfile.h`: ROM-based automatic wireless/cable selection and join-only games.
- `GameFiles.m`: backed-up save replacement and named export.
- `RelayController.m`: room discovery, host/join UI and relay commands.
- `relay-backend.c`, `native-host.inc`, `pia-host.c`: FRLG/Pia/RFU adaptation.
- `relay/`: shared transport from [LDN Relay](https://github.com/veritr1x/ldn-relay).
- `build.py`, `tools/package.py`: Apple bundles and release archives.

The generic Switch relay transports messages. Game-specific timing and
compatibility belong in the companion adapter. Adding another game means
implementing its protocol and testing against that game's real console peer;
changing a communication ID alone is not enough.

## Checks

Build the core once, then run:

```sh
python3 src/platform/ios-ldn/tools/test_lab.py
python3 src/platform/ios-ldn/tools/test_notification.py
python3 src/platform/ios-ldn/tools/test_game_files.py
python3 src/platform/ios-ldn/tools/test_player.py
python3 -m unittest discover -s src/platform/ios-ldn/tools -p 'test_*checks.py'
python3 src/platform/ios-ldn/tools/privacy_check.py
```

The C and save-file tests use AddressSanitizer and UndefinedBehaviorSanitizer.
Protocol tests cover encryption, reliable windows, host/join, backpressure and
the post-save trade phases. They do not replace a complete console trade and
save reload. Avoid changing transport pacing as part of unrelated UI work.

## Diagnostics

`build.py --diagnostics` exposes protocol controls, benchmarks and local logs.
`--capture-trade` additionally enables packet captures, including save contents.
Keep those private. Normal releases disable both. `--usb` and `--lab-host`
are development build variants; the default download uses Switch-approved BLE.
The [host lab notes](HOST-LAB.md) describe the experimental emulator-pair tools.

On Mac, launch a disposable copy without touching your normal saves:

```sh
open -n 'build/player-mac-v0.7.0/mGBA LDN.app' --args \
  --data-dir /tmp/mgba-test-data --rom /path/to/game.gba --capture-screen
```

The data directory is isolated and does not change the normal last-game
preference. `--capture-screen` writes local screenshots for rendering checks.
Never commit ROMs, saves, captures, provisioning profiles or device logs.

Release tags must match `VERSION`. Download the CI-built archives and verify
`SHA256SUMS.txt`. For a release, test fresh install, ROM import, save import,
export/reload, relay approval, joining/hosting and a complete trade. Track
hardware results separately from automated protocol tests.

## Matching Switch build

`.github/workflows/apple.yml` pins `SWITCH_RELAY_REF` to a commit in
`veritr1x/ldn-relay`. Update that pin deliberately when adopting a new relay.
CI tests it, builds its NRO with the relay's pinned devkitPro image and runs
its binary/privacy validation. `tools/package_switch.py` preserves the NRO,
source record and licenses in separate release assets. Tag publishing waits
for both jobs and generates checksums for the IPA, both ZIPs and NRO.

## Additional Pokémon games

Emerald uses the same upstream RFU driver as FireRed/LeafGreen. Ruby/Sapphire
use `GBASIORFUWrapper` and the upstream `rfu-wrapper-air.c` translator.
`GBASIORFUWrapperAttachAirBackend` injects the Apple relay backend instead of
creating an ESP32 serial backend. It takes ownership on success or failure;
destroying the wrapper deinitializes and frees the injected backend.

`wrapper-test.c` exercises the real translator with a controlled RFU peer. It
checks Ruby/Sapphire LinkPlayer exchange, room filtering, connection, initial NI
data and failure cleanup. It does not emulate a full game or prove trade completion.
The existing encrypted relay tests cover the shared transport separately.
Rebuild the Mac core without `--skip-core` after changing the upstream translator.
See [ANDROID_PARITY.md](ANDROID_PARITY.md) for remaining platform differences.
