# WC3 Orc Overlay

**English** | [Українська](README.uk.md)

Timed build-order hints for Warcraft III: The Frozen Throne, playing Orc vs Night Elf.
A strip at the top of the screen shows the game time, what to do right now and the next step.
Press Alt+number to see how to counter a Night Elf unit.

It works in **exclusive fullscreen** too. A small DLL draws the hints inside the game frame
(Direct3D 8 and Direct3D 9). It never reads or writes game memory.

![preview](https://github.com/stratiiv/wc3-orc-overlay/releases/download/v1.0/preview.png)

The hint texts are in Ukrainian. You can translate them in `orc_overlay.pyw` (see [Customizing](#customizing)).

It also adds **Reforged-style grid hotkeys**. Every command-card button gets the key of its position
(Q W E R / A S D F / Z X C V), and the letter is drawn in the corner of each cell:

![grid letters](https://github.com/stratiiv/wc3-orc-overlay/releases/download/v1.0/grid.png)

## What's inside

| File | What it does |
|------|--------------|
| `orc_overlay.pyw` | The brain: timer, hotkeys, sound, hint schedule. Python standard library only. Without the DLL it shows a translucent strip over the game by itself (for Windowed / Borderless mode). |
| `orc.c`, `d3d8hook.c`, `d3d9hook.c`, `orc.h` | Source of `orc_hints.dll`. The DLL reads the text from the Python overlay through shared memory and draws it in the game. If the overlay isn't running yet, the DLL starts it. |
| `make_grid_keys.py` | Builds a grid `CustomKeys.txt` from your own game files: button positions come from `Units\*Func.txt` in the MPQs, tooltips from the game's `CustomKeysSample.txt` (so they stay in your game's language). Includes a tiny standard-library MPQ reader. |
| `build.cmd` | Builds the DLL. |

A prebuilt `orc_hints.dll` is in [Releases](https://github.com/stratiiv/wc3-orc-overlay/releases).

## Installation

Tested on Warcraft III 1.26a, Windows 11.

1. Install **Python 3** from [python.org](https://www.python.org/downloads/). During setup, tick
   "Add python.exe to PATH" and keep tcl/tk and the py launcher enabled.
2. Install **WFE (Warcraft Feature Extender)** from [its author's GitHub](https://github.com/UnryzeC/WFE-Release/releases)
   and unpack it into the game folder. WFE is what loads our DLL into the game.
3. Put these files into the game folder:
   - `orc_overlay.pyw` next to `war3.exe`;
   - `orc_hints.dll` into the `Libraries` subfolder (WFE creates it).
4. Run `WFEApp.exe` as administrator and set it up once:
   - **Launcher → Game Path**: path to `war3.exe` (or `Frozen Throne.exe`);
   - enable **Auto-inject** and **Load Additional Libraries**,
     and set **Additional Libraries Path** to `Libraries\`;
   - save the profile.
5. Grid hotkeys: run `python make_grid_keys.py "C:\Program Files (x86)\Warcraft 3"` (as administrator,
   because it writes `CustomKeys.txt` into the game folder). Then in the game turn on
   **Options → Gameplay → Custom Keyboard Shortcuts**.
6. Start the game with WFE's **Launch** button. The overlay starts by itself and the strip appears in the game.
   The grid letters show up once you press `Home` to start the timer, and hide again after `End`.

> Third-party DLLs may be banned on community servers (iCCup and others). This overlay is meant
> for games against the AI.

## Hotkeys (they work while the game is focused)

| Key | Action |
|-----|--------|
| `Home` | start / pause the timer (press it when the match starts) |
| `Page Up` / `Page Down` | +10 s / −10 s |
| `End` | reset the timer |
| `Ctrl+End` | close the overlay |
| `Alt+1…8` | show the counter to a Night Elf unit for 8 s: archers, huntresses, dryads, bears, mountain giants, chimaeras, hippogryphs / faerie dragons, Demon Hunter |

When a new hint appears, a short sound plays and the frame flashes yellow for 1.5 s.

## Customizing

- Hint schedule and counters: the `SCHEDULE`, `MACRO` and `COUNTERS` lists at the top of `orc_overlay.pyw`.
- `SPEED` in `orc_overlay.pyw`: adjust it if the timer drifts from the in-game clock.
- `TOP_PERCENT`, `BASE_W`, `BASE_H`, `BG_ALPHA` in `orc.c`: position, size and opacity of the
  in-game strip. Rebuild the DLL after changing them.
- `GRID_LETTERS` and `GRID` in `orc_overlay.pyw`: turn the grid letters on or off, and the cell geometry
  in game UI units (0.8 × 0.6 screen). The defaults were measured from the 1.26 console texture. If you
  use WFE's widescreen UI and the letters drift, adjust `GRID`; no rebuild is needed.
- `LETTER_SCALE` and `LETTER_RGB` in `orc.c`: letter size relative to a cell, and its color.

The DLL can't see which buttons a unit actually has, so it draws all 12 letters, empty cells included.

## Building the DLL

```
winget install MartinStorsjo.LLVM-MinGW.MSVCRT
build.cmd
```

## How it works

- Python writes 4 lines of text and flags (flash, counter mode) into the shared memory
  `Local\OrcOverlay`. A seqlock keeps the DLL from reading a half-written line.
- The DLL creates a throwaway D3D8/D3D9 device to find the addresses of `Present` and `Reset`
  in `d3d8.dll` / `d3d9.dll`. On Windows 10/11 every device has its own vtable, so the DLL hooks
  the function code itself, using Microsoft's hot-patch layout (`mov edi, edi` plus 5 padding bytes).
  If another tool such as WFE has already hooked the function, the DLL chains in front of it.
- Inside `Present`, the DLL draws from one texture atlas (the strip plus 12 outlined letters) and restores
  the device state with a state block.
  Before `Reset` (for example on Alt+Tab), it releases its resources.
- While the DLL is drawing in the game, the Python overlay hides its own window so the strip doesn't show twice.
