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
| Wireless adapter | Off or Switch relay; changing it restarts from the in-game save |
| Connection UI | Switch approval, Find game / Host game / Disconnect, connection details |
| Diagnostic export | Recent game events; detailed private capture remains developer-only |
| Android USB / ESP32 route | Replaced by BLE to a modified Switch running LDN Relay |
| FireRed / LeafGreen multiplayer | Host or join with one guest through the relay |
| Emerald / Ruby / Sapphire multiplayer | Not implemented in the Apple relay adapter |
| Local cable multiplayer | Not implemented in this frontend |
| Save states / rewind / fast-forward | Not provided by either mobile frontend |

The Android USB backend and the Apple relay adapter are different game transport
implementations. Building the latest upstream core does not add the ESP32 game's
protocol support to the Apple adapter. Extending supported games needs separate
protocol work and console testing.

The Apple keyboard keeps its previous mapping (Z=A, X=B, A=L, S=R) so existing
users do not have to relearn it. Gamepad east=A and south=B match Android's
physical button arrangement. Buttons absent on a controller remain available
on screen. Multi-touch controls also work with an external keyboard.

## Validation

Geometry, diagonal input, color transforms and frame blending have automated
sanitizer tests. Existing save and relay protocol tests run unchanged. Both
Apple targets and the pinned Switch NRO build in CI. Mac UI checks cover actual
ROM playback and keyboard input, customization and save handling.

Physical iPhone touch/orientation, external gamepads and a complete trade with
these release builds still need device testing. Prior successful trades used
the same game protocol; that is not a substitute for testing the new frontend.
