# WC3 Orc Overlay

**English** | [Українська](README.uk.md)

Timed build-order hints for Warcraft III: The Frozen Throne (1.26a), playing Orc vs Night Elf against the AI.
The hints show up as lines in the chat area, like chat messages. The timer starts by itself
when the match starts. Once a minute you also get a random "Did you know?" tip: control groups, micro,
how to beat the AI's chimaera and mountain giant push at about 15:00.

It works in **exclusive fullscreen** too. A small DLL draws the lines inside the game frame (Direct3D 8 and 9).

![preview](https://github.com/stratiiv/wc3-orc-overlay/releases/download/v1.0/preview.png)

It also adds:
- **Camera zoom** on `PageUp` / `PageDown`, in steps of 150 between 1000 and 3500 (the default is 1650).
- **Reforged-style grid hotkeys**: every command-card button gets the key of its position (Q W E R / A S D F / Z X C V).

## What's inside

| File | What it does |
|------|--------------|
| `orc_hints.txt` | The hints. Plain text, Ukrainian; edit or translate it, no rebuild needed. |
| `orc.c`, `d3d8hook.c`, `d3d9hook.c`, `orc.h` | Source of `orc_hints.dll`. |
| `make_grid_keys.py` | Builds a grid `CustomKeys.txt` from your own game files: button positions come from `Units\*Func.txt` in the MPQs, tooltips from the game's `CustomKeysSample.txt` (so they stay in your game's language). Includes a tiny standard-library MPQ reader. |
| `build.cmd` | Builds the DLL. |

A prebuilt `orc_hints.dll` is in [Releases](https://github.com/stratiiv/wc3-orc-overlay/releases).

## Installation

Tested on Warcraft III 1.26a, Windows 11.

1. Install **WFE (Warcraft Feature Extender)** from [its author's GitHub](https://github.com/UnryzeC/WFE-Release/releases)
   and unpack it into the game folder. WFE is what loads our DLL into the game.
2. Put `orc_hints.dll` and `orc_hints.txt` into the `Libraries` subfolder (WFE creates it).
3. Run `WFEApp.exe` as administrator and set it up once:
   - **Launcher → Game Path**: path to `war3.exe` (or `Frozen Throne.exe`);
   - enable **Auto-inject** and **Load Additional Libraries**,
     and set **Additional Libraries Path** to `Libraries\`;
   - clear the **Camera Distance** / **Camera Height** hotkeys if they are on PageUp / PageDown;
   - save the profile.
4. Optional, grid hotkeys: install [Python 3](https://www.python.org/downloads/) and run
   `python make_grid_keys.py "C:\Program Files (x86)\Warcraft 3"` (as administrator, because it writes
   `CustomKeys.txt` into the game folder). Then in the game turn on **Options → Gameplay → Custom Keyboard Shortcuts**.
5. Start the game with WFE's **Launch** button and start a match.

> Third-party DLLs may be banned on community servers (iCCup and others). This is meant for games against the AI.

## Keys

| Key | Action |
|-----|--------|
| `Page Up` / `Page Down` | zoom the camera out / in |
| `Home` | restart the hint timer from 00:00 |

## Customizing

- `orc_hints.txt`: a line `m:ss text` is shown at that match time, a line `? text` is a random tip.
  Lines starting with `#` are comments. The game reads the file at start.
- In `orc.c` (rebuild after changes): `TIP_SEC` (how often tips come), `MSG_MS` (how long a line stays),
  `MAX_MSG`, `CHAT_X`, `CHAT_BOTTOM` (where the lines are), `ZOOM_STEP`, `ZOOM_MIN`, `ZOOM_MAX`.

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
  If another tool such as WFE has already hooked the function, the DLL chains in front of it.
- Inside `Present`, it draws the chat lines from one texture atlas and restores the device state with a state block.
  Before `Reset` (for example on Alt+Tab), it releases its resources.
- It reads two values from `game.dll` 1.26a to know whether a match is running and whether the chat box is open,
  and writes one: the camera distance, for the zoom. The addresses come from the SLAVA_DROM `Camera.mix` plugin.
  On another game version it checks the module size and the current distance and does nothing.
- It subclasses the game window to catch `PageUp` / `PageDown` / `Home`.
