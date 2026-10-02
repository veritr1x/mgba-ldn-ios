# Wonder Card gifts

Open **Wonder Card gifts** in the game menu. No ROM is needed. Choose a card,
open **Relay connection**, connect your modified Switch running LDN Relay, then
return and tap **Start gift**. The receiving Switch stays unmodified.

On the receiving FireRed or LeafGreen game, unlock Mystery Gift using
**LINK TOGETHER WITH ALL** on the Poké Mart questionnaire and save. Then choose
**MYSTERY GIFT → WONDER CARDS → FRIEND → GBLINK**. Keep the app visible and both
consoles connected until saving finishes. Check the card on the console, then
visit the Pokémon Center deliveryman. A protocol acknowledgement alone cannot
prove that the console saved or redeemed the gift.

The catalogue contains all **76 cards** in the pinned GB-Link web catalogue:
9 event distributions, 7 Project Wonder cards and 60 GB-Link Team cards. Some
cards contain several possible gifts. Their original descriptions and game
version checks are retained; many custom cards require English Switch FireRed
or LeafGreen revision 10. These are community distributions, not an official
Nintendo service. You can also import an international `.wc3` file. Japanese
`.wc3` files are not supported by the upstream importer.

The default iPhone and Mac apps use BLE. The Mac USB build uses the existing
LDN Relay USB helper. This feature does not add direct iPhone USB support.
The game is paused while this screen is open. Only one gift session runs at a
time. Stop before choosing another card; after disconnecting, start again.
Imported files are held for this screen's lifetime, not added to your saves.

**Experimental:** local protocol and build tests only. Gift delivery, saving and
redemption on an actual Switch have not been verified for this adapter.

## Local tests

After building the app, run `python3 src/platform/ios-ldn/tools/test_gifts.py`
from the repository root (Node.js 18+ is also required). This verifies every
payload, imported cards, CRCs and error/duplicate/replacement handling. It then
runs a complete exchange with a simulated RFU recipient through JavaScriptCore
and the native encrypted Pia backend under AddressSanitizer and UBSan. CI runs
the same tests. They do not simulate the Switch radio, storage or redemption.

## Credits and source

Card catalogue, Mystery Gift exchange and RFU leader implementation:
[GB-Link / GB-Link-Switch-LDN](https://github.com/GB-Link/GB-Link-Switch-LDN),
commit `c1a3a97f3ab5c60e87307089d6b7db10e3fd5993`. The original gift work derives
from GB-Link's gblink-wondercards, Project Wonder by Goppier, and the original
event authors. Upstream also credits RAF for Pocket Casino and Decryptu for
Switch Mystery Gift timing and the speed-up event. Original per-card credits
remain in the descriptions. These are credits for reused work, not a claim
that those authors developed or endorse this Apple port.

`vendor/gblink/` contains unchanged pinned files and their SHA-256 manifest.
It also includes the custom card assembly/build sources and upstream credits.
`tools/build_gifts.py` verifies the hashes and bundles selected modules offline
for JavaScriptCore. `apple.js`, `GiftEngine.m` and `GiftController.m` adapt that
code to the Apple UI and existing Pia/LDN backend. No ESP32 firmware is loaded.

The gift modules and card sources are GPL-3.0 as stated in upstream's README;
the RFU leader is covered by its AGPL-3.0 project licence. Both licence texts
are retained under `vendor/gblink/` and included in app notices. The Apple
integration files are AGPL-3.0-or-later. Combined Apple builds containing this
feature are distributed under AGPL-3.0, with the existing MPL-2.0, MIT and other
file-level notices retained. The generic LDN Relay remains a separate MIT
project. Corresponding source, including the vendored scripts and card build
sources, is in this repository; retain it when distributing modified builds.
