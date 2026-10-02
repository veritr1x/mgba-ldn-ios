# Android and Apple frontends

The Apple player uses the same mGBA core and ports the Android frontend's layout
and display options to UIKit. It is not a pixel-identical Android app: menus,
file pickers, settings and window behavior follow the Apple platform.

| Feature | Apple 0.7.0 |
| --- | --- |
| GBA playback and sound | Supported, same mGBA core |
| Portrait / landscape touch layout | Ported, including multi-touch and diagonal D-pad input |
| Compact game menu | Games, saves, pause, reset, multiplayer, display settings, diagnostic export and About |
| ROM import / reopen | One or several `.gba` files; last game reopens |
| Raw save import / export | Per-ROM storage, backup before replacement, named exports |
| Keyboard and gamepad | Keyboard plus extended Apple gamepads; no remapping UI |
| Button appearance | Show/hide, opacity, shared or per-button presets/custom hex colors |
| Backgrounds | Portrait and left/right colors or pictures; mirrored left picture option |
| Color modes | Original, muted, vivid, black and white, DMG; saturation setting |
| Effects | Frame blending, pixel grid, round pixels, RGB subpixels, horizontal/vertical scanlines |
| Counters | FPS and emulated frame count |
| Wireless adapter | Automatic, wireless, cable wrapper or Off; changing it restarts from the in-game save |
| Connection UI | Switch approval, Find game / Host game / Disconnect, connection details |
| Diagnostic export | Recent game events; detailed private capture remains developer-only |
| Android USB / ESP32 route | Replaced by BLE to a modified Switch running LDN Relay |
| FireRed / LeafGreen multiplayer | Host or join with one guest through the relay |
| Emerald multiplayer | Upstream RFU driver through Switch relay; join-only, experimental |
| Ruby / Sapphire multiplayer | Upstream cable-to-wireless translator through Switch relay; join-only, experimental |
| Local cable multiplayer | Not implemented in this frontend |
| Save states / rewind / fast-forward | Not provided by either mobile frontend |

The Apple adapter connects the upstream RFU driver and Ruby/Sapphire cable
translator to LDN Relay. Automatic mode selects the driver from the ROM header.
Emerald and Ruby/Sapphire join a FireRed/LeafGreen host on the stock console;
complete trades with these new Apple paths have not been verified on hardware.
Android's USB/ESP32 transport itself is not implemented on Apple.

The Apple keyboard keeps its previous mapping (Z=A, X=B, A=L, S=R) so existing
users do not have to relearn it. Gamepad east=A and south=B match Android's
physical button arrangement. Buttons absent on a controller remain available
on screen. Multi-touch controls also work with an external keyboard.

## Validation

Geometry, diagonal input, color transforms and frame blending have automated
sanitizer tests. Cable translator tests cover Ruby/Sapphire player exchange,
host filtering, RFU connection, initial data transfer and backend cleanup.
Existing save and relay protocol tests also run. Both
Apple targets and the pinned Switch NRO build in CI. Mac UI checks cover actual
ROM playback and keyboard input, customization and save handling.

Physical iPhone touch/orientation, external gamepads and a complete trade with
these release builds still need device testing. Prior successful trades used
the same game protocol; that is not a substitute for testing the new frontend.
