# Mac host ↔ iPhone laboratory

**Development-only:** the lab central still uses the older paired transport;
it is not compatible with the default 0.5.0+ approval-mode iPhone build.
Update and verify its handshake before attempting an emulator-pair test.
Use the normal Switch relay route for the downloadable app.

The original v0.3.4 lab added a two-player Pia/RFU host on Mac and carries encrypted
Pia datagrams through the existing BLE stream. No Switch is needed for this lab.
A real-ROM trade has **not yet been verified** with this mode.

## What it exercises

- Net connection, Session join/update/finalization and RTT messages.
- AES-GCM authenticated Pia datagrams and the existing reliable stream.
- RFU connection requests and acceptance by the emulated host's adapter.
- Host/child `T` slot carriers, child `K` acknowledgements, and the games' own RFU payloads.
- Real CoreBluetooth central on Mac and peripheral on iPhone, six-frame window,
  five-millisecond ACK coalescing, bounded queues and bidirectional batching.

This is an emulator host, not a recreation of Nintendo's entire Switch wrapper.
Retail FireRed ROMs run on both sides. The Switch-specific post-trade shim and
child tag rewriting are disabled only for this explicit lab mode. A successful
lab trade therefore validates this pair and transport, not the extra Switch
standby round or Switch Bluetooth throughput. It is not an exact recording of
Nintendo's trade timing; use the recorder below for that comparison.

One guest, virtual IPv4 addresses 169.254.1.1 and .2, FRLG Pia 6.39 protocol
versions. No LDN radio, other games, host migration, or multi-guest support.
The Mac sends an explicit `MGL1` lab descriptor through a separate relay opcode;
native Switch metadata continues through the existing join implementation.

## Builds

From the repository root, after building the core normally:

```sh
python3 src/platform/ios-ldn/build.py --platform mac --lab-host --skip-core
python3 src/platform/ios-ldn/build.py --platform ios --skip-core --profile /path/to/development.mobileprovision
```

Outputs:

- `build/player-mac-host-lab-v0.6.0/mGBA LDN.app`
- `build/player-ios-v0.6.0/mGBA LDN.app`

The Mac host uses bundle ID `dev.local.mgba-ldn.hostlab` and saves under
`~/Library/Application Support/mGBA LDN Host Lab/`. ROMs/saves are not bundled or published.

## Live test

1. Close other LDN BLE benchmark/relay apps, including the standalone iPhone
   relay app. Install and open the **mGBA LDN game app** on the iPhone.
2. Load FireRed and an eligible save on each device. Use independent copies.
3. Launch the Mac host app with `--capture-trade` (optionally `--trace-rfu`).
   `--lab-no-radio` is an offline boot check only; omit it for a BLE test.
4. Keep the iPhone game foreground and unlocked. Use built-in audio for the
   initial test so Bluetooth audio is not another variable.
5. Choose **Become Leader** in the Mac's Direct Corner and **Join Group** on
   iPhone. The lab carries the Mac ROM's actual RFU advertisement to the phone.
6. Complete a trade, check the received Pokémon on both games, and save both.
7. Leave the relay session to finish each capture. Archive both captures and
   relay logs with the observed result. If Bluetooth disconnects or the game
   resets mid-session, restart the lab session; automatic live recovery is not
   yet provided.

The iPhone application must acknowledge lab setup before the Mac starts Pia.
An old standalone relay app may advertise the same BLE service but does not
understand this lab protocol; the host reports the missing acknowledgement.

## Local checks

```sh
python3 src/platform/ios-ldn/tools/test_lab.py
python3 src/platform/ios-ldn/tools/test_trade_capture.py
```

The first runs host/client encrypted integration and existing Switch-join
regressions with assertions enabled and address/undefined-behavior sanitizers.
It verifies byte-exact bidirectional slots and backpressure recovery. It also
checks Net/Session bytes against independent public synthetic fixtures generated
by [pokeldn's protocol builders](https://github.com/Decryptu/pokeldn/blob/9973b57c5b965f56d3c1f093c1bd02574d0a7bc1/pokeldn/ldn/pia_connect.py).
These use adapter callbacks, not two running ROMs: passing is protocol-plumbing
proof, not trade success. Fixtures contain no ROM, save or captured game data.

## Trade recorder (USB, BLE, or lab)

Launch a player with `--capture-trade`. This creates a unique
`trade-capture-<UUID>.jsonl` in its documents directory. It records:

- Every Pia receive and attempted send, full payload, endpoint and queue result.
- Monotonic callback timestamps, emulated frame numbers and input events.
- Session metadata, ROM hash, last persisted save and game status events.

`--trace-rfu` adds a matching unique `.rfu.log`. These are private local captures.
The saved `.sav` snapshot is **not** an emulator RAM savestate. Application
callback timing is **not** a radio timestamp. End-of-session is not an assertion
that a trade succeeded; record the visible game outcome separately.

```sh
python3 src/platform/ios-ldn/tools/validate_trade_capture.py /path/to/trade-capture-UUID.jsonl --output /path/to/new-timeline.json
```

The validator rejects truncation, malformed JSON, missing sequence numbers,
nonmonotonic timestamps, invalid payloads and sessions without traffic both ways.
Buffered logger overflow writes a non-JSON marker, so it cannot silently pass.
Output creation refuses to overwrite an existing timeline.

A validated timeline can feed a future timed transport replay. Captured encrypted
packets cannot simply be injected into a new live Nintendo session: session keys,
nonces and reliable state differ. A live stateful host, as implemented here, is a
different test from playback. A complete successful USB trade capture is still
needed to compare this lab's sequence and traffic distribution with the Switch.
