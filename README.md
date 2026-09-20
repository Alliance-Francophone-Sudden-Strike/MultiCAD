# MultiCAD

**MultiCAD** is a universal graphics DLL replacement for **Sudden Strike**, **Sudden Strike Forever**, and related games.
It supports **any custom screen resolution** from 640x480 up to 3840x2160 (4K) — note that higher resolutions may be a bit laggy — and includes various bug fixes across different game versions.

> [!NOTE]
> This fork main goal is to produce the same multi resolution `.dll` as the parent project but without the menu version check. It should allow community to use the custom resolution `.dll` next to a custom menu `.dll` without conflicts.
>
> This fork also allow the user to force a profile override for the game and menu modules, allowing potential custom builds of `game*.dll` and `menu*.dll`. This feature targets advanced users who need to run modified or repacked versions of the game and menu DLLs. **Use with caution.**
>
> Missing zeppelins in strategic map (opened with the `M` key) bug is fixed in this build. Crash on strategic map opening for ultra wide resolutions should also be addressed.
>
> An enormous thanks to @IvanishinV for all the work poured into the original MultiCAD project. It was a long wait of more than 20 years.

> [!WARNING]
> This fork also provides 3 new features: the ability to zoom and the control-group panel.
> **Verified in game:** Sudden Strike 2, Hidden Stroke 2, Sudden Strike: Resource War 2.4, Europe 2015, Sudden Strike Gold HD v1.2 (de, en, fr, ru).
> **Supported but not yet played through:** Sudden Strike: Resource War 2.3, Black Sea, Black Gold, Sudden Strike Gold (en).
> **Not supported:** Sudden Strike 1.0 and 1.2, Sudden Strike Gold de/fr/ru, Sudden Strike HD v1.1. They run with resolution support only.
> A 3rd new feature is the ability to display information about zeppelins capture timing in multiplayer games but only for SS2/HS2.

## Supported Games

### Original Games

| Game                            | Status | Versions / Languages | Fixes       |
| ------------------------------- | ------ | -------------------- | ----------- |
| **Sudden Strike**               | ✔      | 1.0: de, ru, 1.2: en | 2 bug fixes |
| **Sudden Strike Forever**       | ✔      | en, de, fr, ru, ch   | 7 bug fixes |
| **Sudden Strike Gold**          | ✔      | en, de, fr, ru       | 7 bug fixes |
| **Sudden Strike 2**             | ✔      | 2.2                  | 6 bug fixes |
| **Sudden Strike: Resource War** | ✔      | 2.3, 2.4             | 6 bug fixes |

### Red Ice Team Games

| Game                   | Status | Fixes       |
| ---------------------- | ------ | ----------- |
| **Black Gold**         | ✔      | 6 bug fixes |
| **Black Sea**          | ✔      | 6 bug fixes |
| **Cold War Conflicts** | ✔      | 6 bug fixes |
| **Europe 2015**        | ✔      | 6 bug fixes |
| **Gulf War**           | ✔      | 6 bug fixes |

### Mods

| Game                             | Status | Versions / Languages | Fixes       |
| -------------------------------- | ------ | -------------------- | ----------- |
| **Sudden Strike HD v1.1**        | ✔      | en, ru               | 2 bug fixes |
| **Sudden Strike Gold HD v1.2**   | ✔      | en, de, fr, ru       | 7 bug fixes |
| **APRM**                         | ✔      | 3.0, 3.1, 4.0        | 7 bug fixes |
| **AXPRM**                        | ✔      | 2.0                  | 7 bug fixes |
| **TWO**                          | ✔      | en                   | 7 bug fixes |
| **Eastern Front Mod**            | ✔      |                      | 6 bug fixes |
| **Hidden Stroke 2 APRM**         | ✔      |                      | 6 bug fixes |
| **Hidden Stroke 2 Fusion**       | ✔      |                      | 6 bug fixes |
| **Hidden Stroke 2 Resource War** | ✔      |                      | 6 bug fixes |
| **Hidden Stroke 3**              | ✔      |                      | 6 bug fixes |
| **Hidden Stroke 4**              | ✔      |                      | 6 bug fixes |
| **Liberation Mod**               | ✔      | 2.75, 5.1, 5.3       | 6 bug fixes |
| **LRM**                          | ✔      | 5.1                  | 6 bug fixes |
| **MWM 3**                        | ✔      |                      | 6 bug fixes |
| **Neddus Stroke**                | ✔      |                      | 6 bug fixes |
| **PWM**                          | ✔      | 2.0, 3.0             | 6 bug fixes |
| **RCM**                          | ✔      | 2.7                  | 6 bug fixes |
| **RWM 6.x**                      | ✔      | 6.5, 6.6, 6.71, 6.8  | 6 bug fixes |
| **RWM 8.x**                      | ✔      | 8.0, 8.5             | 6 bug fixes |
| **RWG Truth of War**             | ✔      | en, de, fr, ru       | 6 bug fixes |
| **Vietnam Project**              | ✔      | 1.0, 1.1, 1.2        | 6 bug fixes |
| **Warzone 2**                    | ✔      |                      | 6 bug fixes |
| **World at War**                 | ✔      | 0.5                  | 6 bug fixes |

> 💡 Note: `Audio Mixer Zero-Volume Fix` restores the game volume in the audio mixer to full if it was set to zero. Applies to **all versions**.

## Installation

1. Download the latest precompiled `cadMulti_mt.dll` file from the [Releases](../../releases/latest) page.
2. Place `cadMulti_mt.dll` into the folder containing the original `cad*.dll` files (typically the game directory).
3. Open the game's ini in the game folder — `sudtest.ini` for Sudden Strike, or `gulfwar.ini`, `blackgold.ini`, `blacksea.ini`, `euro2015.ini` for the Confrontation titles — and set **at least one** `SSDraw` entry to `cadMulti_mt.dll`. Example:
   > ```ini
   > [Game]
   > SSDraw1=cad640.dll
   > SSDraw2=cad1024.dll
   > SSDraw3=cadMulti_mt.dll
   > ```
4. Launch the game.

## Configuration

### Resolution Setup

The game launches at your desktop resolution by default — no configuration needed.

To set a specific resolution, add a `Resolution` line **anywhere after** the `[Game]` header in the game's ini:

> ```ini
> [Game]
> Resolution=1600x900
> SSDraw1=cad640.dll
> SSDraw2=cad1024.dll
> SSDraw3=cadMulti_mt.dll
> ```
>
> Restart the game to apply the change.

> 💡 Note: `Resolution` must be between 640x480 and 3840x2160, with a height divisible by 8 — a renderer requirement. Out-of-range values are ignored with a message.
> Heights are rounded down to a multiple of 8 and widths to a multiple of 16 — the renderer and the fog-of-war blitter work in blocks of that size — so `1366x768` runs as `1360x768`. A mode your display then refuses brings up a picker listing the modes it does report, and saves your choice back to the ini. With no `Resolution` line set, the game uses your desktop resolution, rounded the same way.

Battlefield zoom is now possible but disabled by default for all game DLLs.

To enable it, simply set `Zoom=on` in the game's ini:

> ```ini
> [Game]
> Zoom=on
> ```

In addition, you can configure the zoom indicator and its persistence:

> ```ini
> [Game]
> Zoom=on
> ZoomIndicator=right
> ZoomIndicatorShape=bars
> PersistentZoomIndicator=on
> InvertZoom=on
> ZoomOnCursor=on
> ```

Use `Zoom=off` to disable zoom or `ZoomIndicator=hidden` to hide its indicator. `ZoomIndicator` can be set to `left`, `right`, or `hidden`.

Other available settings settings:

- `ZoomIndicatorShape=bars` draws the indicator as growing bars instead of squares; it can be set to `squares` or `bars` and defaults to `squares`.
- `PersistentZoomIndicator=on` keeps the indicator visible while zoomed in; it defaults to `off`.
- `InvertZoom=on` reverses the mouse wheel direction for zooming; it defaults to `off`.
- `ZoomOnCursor=on` zooms towards the cursor instead of the screen centre; it defaults to `off`.
- `ZoomIndicatorScale=1.5` draws the indicator larger. It accepts `1` to `3` in steps of `0.25` and defaults to `1`. Values outside that range are clamped to the nearest bound and values between steps round to the nearest step, so `1.3` behaves as `1.25`. An indicator too large for the current resolution is not drawn.

### Control-Group Panel

MultiCAD can also show a row of ten indicator cells for your control groups, labelled `1`–`9` and `0`, in the screen's top-right corner. Groups that currently hold units light up; empty groups stay dim.

A cell shows a grey house in its top-right corner for members inside a building, a grey wheel in its bottom-left for vehicle drivers, and a grey-outlined dim-green square in its top-left for transported units.

The cells are clickable:

- **Left-click** a lit cell to select that group — exactly what pressing its number-row key does.
- **Right-click** a cell to assign the current selection to that group, as `Ctrl` + the number key does.

To enable it, set `GroupPanel=on` in the game's ini:

> ```ini
> [Game]
> GroupPanel=on
> ```

Use `GroupPanel=off` to disable it; it defaults to `off`.

Each lit cell also shows how many units the group holds, centred just below the cell. To hide the counts and keep the plain indicators, set:

> ```ini
> [Game]
> GroupPanel=on
> GroupPanelCount=off
> PersistentGroupPanel=on
> ```

`GroupPanelCount` defaults to `on`, so the counts appear unless you turn them off.

By default, the panel is only drawn while at least one group holds units. To keep it on screen at all times, even with no groups, set `PersistentGroupPanel=on` in the game's ini.

On a high-resolution display the cells can be enlarged with `GroupPanelScale`:

> ```ini
> [Game]
> GroupPanel=on
> GroupPanelScale=2
> ```

`GroupPanelScale` accepts `1` to `3` in steps of `0.25` and defaults to `1`. Values outside that range are clamped to the nearest bound and values between steps round to the nearest step, so `1.3` behaves as `1.25`. The cells, glyphs, badges and counts all grow together, the panel stays anchored to the top-right corner, and the clickable area keeps matching what is drawn. A panel too large for the current resolution is not drawn at all.

### Zeppelin Capture Panel

On multiplayer maps built around capturing zeppelins, MultiCAD can list the zeppelin groups you have not captured yet in the bottom-right corner of the screen: one colour swatch per group, with its state to the left of it: `2/3` while you hold only some of the group's zeppelins, then the capture countdown once you hold them all. If a started capture is interrupted, the countdown freezes and the row alternates every two seconds between the held count and that frozen time, both greyed out — including once you hold none of the group; if an enemy capture resets the group, the count comes back on its own. A group drops off the list as soon as you own it.

Show it with **`Alt` + `z`**. By default it stays up for five seconds, then fades out, and pressing the shortcut again restarts those five seconds. Either `Alt` key works, including `AltGr`.

To enable it, set `ZeppelinPanel=on` in the game's ini:

> ```ini
> [Game]
> ZeppelinPanel=on
> ```

Set `ZeppelinPanelBehaviour=toggle` to have the shortcut open and close the panel instead, leaving it on screen until you press it again:

> ```ini
> [Game]
> ZeppelinPanel=on
> ZeppelinPanelBehaviour=toggle
> ```

`ZeppelinPanelBehaviour` defaults to `temp`, the five-second hold described above.

`ZeppelinPanel` defaults to `off`. The panel only appears on maps that actually define zeppelin groups, so it stays out of the way in single-player and on ordinary multiplayer maps.

The panel has its own scale, independent of the control-group panel's:

> ```ini
> [Game]
> ZeppelinPanel=on
> ZeppelinPanelScale=1.5
> ```

`ZeppelinPanelScale` accepts `1` to `3` in steps of `0.25` and defaults to `1`, with the same clamping and rounding as `GroupPanelScale`. The panel stays anchored to the bottom-right corner, and one too large for the current resolution is not drawn.

> [!NOTE]
> This panel is currently enabled for **Hidden Stroke 2** only. On every other version it stays inactive, even with `ZeppelinPanel=on`.

### Replacement Menu or Game Modules

The game binds its two modules by name, from the ini it boots with:

> ```ini
> [StartUp]
> Module1=Menu_Dll.dll
> Module2=Game_Dll.dll
> ```

MultiCAD reads the same two keys, so a replacement menu under any name, for example `Module1=Anything.dll`, is still recognised as the menu module, on disk and when it loads.
No configuration is needed; the usual `menu*.dll` / `game*.dll` names remain the fallback when the keys are absent.

### Forcing a Profile

MultiCAD identifies your game by hashing the code section of its `game*.dll` and `menu*.dll`. A dll it doesn't recognise like a custom or repacked build is simply left unpatched: the menu is skipped silently, and the game falls back to 1024x768 with a message naming the hash it computed.

If you know which version a custom dll is based on, you can force the matching profile from the `[Game]` section of the game ini:

> ```ini
> [Game]
> GameProfile=SS_2
> MenuProfile=SS_2
> ```

`GameProfile` covers the game dll, `MenuProfile` the menu dll; set either or both.
Accepted values (case-insensitive):

`SS_V1_0`, `SS_V1_2`, `SS_GOLD_RU`, `SS_GOLD_EN`, `SS_GOLD_DE`, `SS_GOLD_FR`, `SS_2`,
`SS_RW_V2_3`, `SS_RW_V2_4`, `SS_BLACK_GOLD`, `SS_EUROPE_2015`, `SS_BLACK_SEA`,
`SS_HD_V1_1_RU`, `SS_HD_V1_1_EN`, `SS_GOLD_HD_1_2_RU`, `SS_GOLD_HD_1_2_INT`, `HS_2`.

Anything else is ignored and normal detection applies.

> ⚠️ **At your own risk.** Forcing a profile writes jumps at fixed addresses into a dll whose contents haven't been verified. If the profile doesn't match the dll, the game will crash as soon as a patched function runs. Only use this when you know the build your dll came from, and remove the line if the game stops starting.

### UI Toggle Hotkey

You can temporarily disable or enable the in-game UI overlay by pressing:

**Alt + Y**

This can be useful when taking screenshots or when the UI interferes with gameplay.

## Compilation

To build the project from source:

### Requirements

- Windows 10 or 11
- Visual Studio 2022 or later
- Visual C++ build tools

### Steps

1. Clone or download this repository.
2. Open `MultiCAD.sln` in **Visual Studio 2022**.
3. Choose the `Release`, `Release_MT` or `Debug` configuration.
4. Build the solution.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

## Contact

For author information and ways to get in touch, see the [Contact Information](CONTACT.md) file.
