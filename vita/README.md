# JA2 Stracciatella for PS Vita

A port of [JA2 Stracciatella](https://github.com/ja2-stracciatella/ja2-stracciatella) to the PlayStation Vita
(HENkaku/Ensō, VitaShell). Branch `vita` carries the port; `master` tracks upstream.

You need your own copy of the original *Jagged Alliance 2* game data (e.g. the GOG release). No game data is included.

## Features

- Full game, 960×544, with the bundled mods (e.g. Simplified Chinese localization)
- Gamepad controls: stick cursor, tile-snapping D-pad cursor in tactical, right-stick map scroll,
  L/R to cycle mercs, Select to hide the bottom panel, △ for a 3×3 context action menu
- Front touch as mouse; rear touchpad as a relative or absolute pointer
- Touch shortcut buttons beside the tactical panel
- "Vita settings" page in the in-game options (confirm button, rear touchpad, cursor speeds)

| Button | Action |
|---|---|
| Left stick | Cursor |
| Right stick | Scroll the map |
| ✕ / ○ | Left / right click (swappable in Vita settings) |
| D-pad | Tactical: step the cursor one tile; menus: arrow keys |
| △ | Tactical: context action menu; elsewhere: Enter |
| □ | Space (next merc) |
| L / R | Previous / next merc, centred |
| Select | Hide / show the bottom panel |
| Start | Esc |

Porting notes (in Chinese) are in [NOTES.md](NOTES.md); [HOTKEYS.md](HOTKEYS.md) lists the game's keyboard hotkeys.

## Build

Requires podman. Everything runs in a VitaSDK + Rust nightly container.

```sh
podman build --network=host -t stracciatella-vita-sdk vita
vita/build.sh            # -> build-vita/ja2-stracciatella.vpk
```

## Install

1. Install `ja2-stracciatella.vpk` with VitaShell (title ID `JAST00001`).
2. `vita/stage-data.sh [path-to-JA2-install]` assembles `vita-data/ja2-stracciatella/`; copy it to
   `ux0:data/ja2-stracciatella/` (USB or FTP; `vita/push.sh` uploads it over VitaShell FTP, `VITA=ip:port`).
3. Optional: an empty file `ux0:data/ja2-stracciatella/overclock` raises the CPU to 444 MHz.

## License

Same as upstream: see [SFI Source Code license agreement.txt](<../SFI Source Code license agreement.txt>)
(non-commercial use only).
