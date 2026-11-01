# WC3 Orc Overlay

**English** | [Українська](README.uk.md)

Timed build-order hints for Warcraft III: The Frozen Throne (1.26a), playing Orc vs Night Elf against the AI.
The hints show up as lines in the chat area, like chat messages. The timer starts by itself
when the match starts.

It works in **exclusive fullscreen** too. A small DLL draws the lines inside the game frame (Direct3D 8 and 9).

![preview](https://github.com/stratiiv/wc3-orc-overlay/releases/download/v1.0/preview.png)

It also adds:
- **Camera zoom** on `PageUp` / `PageDown`, in steps of 150 between 1000 and 3500 (the default is 1650),
  and **camera tilt** on `Ctrl+PageUp` / `Ctrl+PageDown`. Both are remembered in `%APPDATA%\orc_hints.ini`
  and applied at the start of every match (delete the file to go back to the defaults).
- **Reforged-style grid hotkeys**: every command-card button gets the key of its position (Q W E R / A S D F / Z X C V).

## What's inside

| File | What it does |
|------|--------------|
| `orc_hints.txt` | The hints. Plain text, Ukrainian; edit or translate it, no rebuild needed. |
| `orc.c`, `d3d8hook.c`, `d3d9hook.c`, `orc.h`, `ijl15.def` | Source of `orc_hints.dll`. |
| `test_proxy.c` | Checks that the DLL forwards the game's `ijl15` calls and sets its hooks. |
| `make_grid_keys.py` | Builds a grid `CustomKeys.txt` from your own game files: button positions come from `Units\*Func.txt` in the MPQs, tooltips from the game's `CustomKeysSample.txt` (so they stay in your game's language). Includes a tiny standard-library MPQ reader. |
| `build.cmd` | Builds the DLL. |

A prebuilt `orc_hints.dll` is in [Releases](https://github.com/stratiiv/wc3-orc-overlay/releases).

## Installation

Tested in a test host on Windows 11; made for Warcraft III 1.26a. WFE is not needed.

1. Close the game, then run `powershell -ExecutionPolicy Bypass -File setup\install.ps1`. Add the game folder as an argument if it is not in `C:\Program Files (x86)\Warcraft 3`.
2. Start the game with `war3.exe`. About 10 seconds later the DLL is active.

The game itself loads `ijl15.dll` from its folder, so the script renames the real one to `ijl15_orig.dll` and puts this DLL in its place; every `ijl15` call is forwarded. To undo, delete `ijl15.dll` and rename `ijl15_orig.dll` back. If WFE runs alongside (it hides in the tray), it will still inject its own DLL, so close it.

The script also moves the icons with a wrong letter to `ReplaceableTextures_wrong_letters`, turns off custom hotkeys, sets `maxfps` to 75 and copies the default camera if you have none saved.

Optional, grid hotkeys: install [Python 3](https://www.python.org/downloads/) and run `python make_grid_keys.py "C:\Program Files (x86)\Warcraft 3"` as administrator. Then turn on **Options → Gameplay → Custom Keyboard Shortcuts** in the game.

> Third-party DLLs may be banned on community servers (iCCup and others). This is meant for games against the AI.

## Same setup on another PC

The game and the lettered icon pack are not in this repo: copy the game folder to the other PC, clone this repo (or download it as a ZIP) and run the script above. It downloads the DLL from the release if you have not built it.

## Keys

| Key | Action |
|-----|--------|
| `Page Up` / `Page Down` | zoom the camera out / in |
| `Ctrl+Page Up` / `Ctrl+Page Down` | tilt the camera flatter / steeper |
| `Home` | restart the hint timer from 00:00 |

## Customizing

- `orc_hints.txt`: a line `m:ss text` is shown at that match time.
  Lines starting with `#` are comments. The game reads the file at start.
- In `orc.c` (rebuild after changes): `MSG_MS` (how long a line stays),
  `MAX_MSG`, `CHAT_X`, `CHAT_BOTTOM` (where the lines are), `ZOOM_*` and `TILT_*` (camera steps and limits).

The timer counts real time while you are in a match. If you pause a single-player game with the menu open,
it may run ahead; press `Home` at the start of a match to resync.

## Building the DLL

```
winget install MartinStorsjo.LLVM-MinGW.MSVCRT
build.cmd
```

## How it works

- The DLL creates a throwaway D3D8/D3D9 device to find the addresses of `Present` and `Reset`
  in `d3d8.dll` / `d3d9.dll`. On Windows 10/11 every device has its own vtable, so the DLL hooks
  the function code itself, using Microsoft's hot-patch layout (`mov edi, edi` plus 5 padding bytes).
- Inside `Present`, it draws the chat lines from one texture atlas and restores the device state with a state block.
  Before `Reset` (for example on Alt+Tab), it releases its resources.
- It reads two values from `game.dll` 1.26a to know whether a match is running and whether the chat box is open,
  and writes two: the camera distance and angle, for zoom and tilt. The addresses come from the SLAVA_DROM `Camera.mix` plugin.
  On another game version it checks the module size and the current distance and does nothing.
- It subclasses the game window to catch `PageUp` / `PageDown` / `Home`.
