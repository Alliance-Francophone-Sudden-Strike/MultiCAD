# `ZeppelinPanelBehaviour` — temp / toggle

## Context

The zeppelin capture panel is currently a **timed peek**: `Alt`+`z` sets
`g_zeppelinRequested = true` and stamps `g_zeppelinRequestTick`; each frame
`ZeppelinPanel::HoldOpacity(tick - requestTick)` decays it to 0 after 5s
(`kHoldMs`), with a 250ms fade tail. Nothing ever clears `g_zeppelinRequested`
at runtime — the panel vanishes purely because opacity hits zero. Re-pressing
restarts the five seconds.

On a zeppelin-capture map a player often wants the board up _continuously_
while contesting groups, and re-tapping `Alt`+`z` every five seconds is noise.
This adds `[Game] ZeppelinPanelBehaviour` with two values:

- `temp` (default, current behaviour) — five-second hold, then fade.
- `toggle` — `Alt`+`z` opens, `Alt`+`z` closes. No timer.

Default must stay `temp` so existing ini files are unaffected.

## Approach

Only the **opacity source** changes. Visibility (`g_zeppelinVisible`), geometry,
drawing, dirty-rect handling and the key-swallow path all stay exactly as they
are. `g_zeppelinRequested` gains a second write site (the toggle-off) and
`g_zeppelinRequestTick` is reused as the fade-change tick.

The fade follows [GroupPanel.h:367-373](src/core/GroupPanel.h#L367-L373)
(`Fade::opacityAt`): a symmetric 250ms `Zoom::kIndicatorFadeMs` ramp, with the
mid-fade reversal trick from [GroupPanel.h:357-362](src/core/GroupPanel.h#L357-L362)
so a fast double-press does not pop.

`PersistentGroupPanel` ([ScreenConfig.h:233-242](src/core/ScreenConfig.h#L233-L242)
→ `configureGroupPanel(..., persistent)`) is the existing template for a
panel-behaviour setting; `Zoom::ParseIndicatorShape`
([Zoom.h:64-69](src/core/Zoom.h#L64-L69)) is the template for a string-enum ini
value. Reuse both shapes rather than inventing new ones.

## Changes

### 1. [src/core/ZeppelinPanel.h](src/core/ZeppelinPanel.h) — enum, parser, fade

Add `#include <string_view>`. Next to `HoldOpacity` (line 54):

```cpp
enum class Behaviour
{
    Temp,
    Toggle
};

// Caller lowercases, exactly like Zoom::ParseIndicatorShape.
constexpr Behaviour ParseBehaviour(std::string_view value)
{
    return value == "toggle" ? Behaviour::Toggle : Behaviour::Temp;
}

// Toggle mode has no hold timer: the panel stays until the player closes it,
// and only ramps opacity across that change. Same ramp as GroupPanel::Fade.
constexpr int ToggleOpacity(bool shown, uint32_t elapsed)
{
    const int ramp = elapsed >= kFadeMs
        ? 16 : static_cast<int>(elapsed * 16 / kFadeMs);
    return shown ? ramp : 16 - ramp;
}
```

Startup is correct by construction: `requested == false`, `requestTick == 0`, so
`elapsed` saturates → `16 - 16 == 0`.

### 2. [src/core/ScreenConfig.h](src/core/ScreenConfig.h) — ini getter

Add `#include "ZeppelinPanel.h"`. After `GetZeppelinPanel()` (line 244), mirror
`GetZoomIndicatorShape()` ([ScreenConfig.h:155-167](src/core/ScreenConfig.h#L155-L167))
verbatim — including its lowercasing loop:

```cpp
static ZeppelinPanel::Behaviour GetZeppelinPanelBehaviour()
{
    const std::string iniPath = GetIniPath();
    if (iniPath.empty())
        return ZeppelinPanel::Behaviour::Temp;

    char buffer[8]{};
    GetPrivateProfileStringA("Game", "ZeppelinPanelBehaviour", "temp",
                             buffer, sizeof(buffer), iniPath.c_str());
    for (char& c : buffer)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return ZeppelinPanel::ParseBehaviour(buffer);
}
```

`buffer[8]` holds `"toggle"` with room to spare.

### 3. [src/patcher/GameDllHooks.h](src/patcher/GameDllHooks.h) — signature

Add `#include "ZeppelinPanel.h"` and default the new parameter at line 1296, so
the `shutdown()` call at line 1301 keeps compiling unchanged (same pattern as
`configureGroupPanel`'s defaults on line 1294):

```cpp
static void configureZeppelinPanel(
    GameVersion version,
    ZeppelinPanel::Behaviour behaviour = ZeppelinPanel::Behaviour::Temp);
```

### 4. [src/patcher/GameDllHooks.cpp](src/patcher/GameDllHooks.cpp) — the actual behaviour

**State** — beside the other zeppelin globals at line 73-79:

```cpp
ZeppelinPanel::Behaviour g_zeppelinBehaviour = ZeppelinPanel::Behaviour::Temp;
```

**Press handler** — new free function in the anonymous namespace, right after
`zeppelinPanelAltShow` ([GameDllHooks.cpp:173-176](src/patcher/GameDllHooks.cpp#L173-L176)):

```cpp
void zeppelinPanelPress()
{
    const uint32_t now = GetTickCount();

    if (g_zeppelinBehaviour != ZeppelinPanel::Behaviour::Toggle)
    {
        g_zeppelinRequested = true;
        g_zeppelinRequestTick = now;
        return;
    }

    // Reversing mid-fade resumes from the current opacity instead of
    // snapping, the way GroupPanel::Fade rebases changeTick_.
    const int current = ZeppelinPanel::ToggleOpacity(
        g_zeppelinRequested, now - g_zeppelinRequestTick);
    g_zeppelinRequested = !g_zeppelinRequested;
    const int ramp = g_zeppelinRequested ? current : 16 - current;
    g_zeppelinRequestTick = now - ramp * ZeppelinPanel::kFadeMs / 16;
}
```

**Keydown** — replace the body at
[GameDllHooks.cpp:4982-4987](src/patcher/GameDllHooks.cpp#L4982-L4987):

```cpp
if (zeppelinPanelAltShow(a3))
{
    // lParam bit 30 is the previous key state: skip auto-repeat, which
    // would otherwise flip the toggle many times per second.
    if ((a4 & 0x40000000) == 0)
        zeppelinPanelPress();
    break;
}
```

`a4` is the real `lParam` — `dispatchWndMessage_ver(a1, a2, a3, a4)`
([GameDllHooks.h:1570](src/patcher/GameDllHooks.h#L1570)) is a standard WndProc,
and the mouse path already reads `(unsigned short)a4` / `HIWORD(a4)` as the
cursor position. The guard is harmless in `temp` mode (re-stamping the tick on
repeat is a no-op) so it applies to both.

**Per-frame opacity** — replace
[GameDllHooks.cpp:1947-1948](src/patcher/GameDllHooks.cpp#L1947-L1948):

```cpp
g_zeppelinOpacity = g_zeppelinBehaviour == ZeppelinPanel::Behaviour::Toggle
    ? ZeppelinPanel::ToggleOpacity(g_zeppelinRequested, tick - g_zeppelinRequestTick)
    : (g_zeppelinRequested
        ? ZeppelinPanel::HoldOpacity(tick - g_zeppelinRequestTick) : 0);
```

**Reset** — in `configureZeppelinPanel`
([GameDllHooks.cpp:388-399](src/patcher/GameDllHooks.cpp#L388-L399)) take the
parameter and add `g_zeppelinBehaviour = behaviour;` alongside the existing
`g_zeppelinRequested = false;`.

### 5. [src/patcher/PatchInstallers.cpp:107-108](src/patcher/PatchInstallers.cpp#L107-L108)

```cpp
GameDllHooks::configureZeppelinPanel(
    Screen::GetZeppelinPanel() ? version : GameVersion::UNKNOWN,
    Screen::GetZeppelinPanelBehaviour());
```

### 6. [tests/zeppelin_panel_test.cpp](tests/zeppelin_panel_test.cpp) — checks

Beside the `HoldOpacity` asserts at lines 144-149:

```cpp
static_assert(ToggleOpacity(true, 0) == 0);
static_assert(ToggleOpacity(true, kFadeMs / 2) == 8);
static_assert(ToggleOpacity(true, kFadeMs) == 16);
static_assert(ToggleOpacity(false, 0) == 16);
static_assert(ToggleOpacity(false, kFadeMs / 2) == 8);
static_assert(ToggleOpacity(false, kFadeMs) == 0);
static_assert(ToggleOpacity(false, 0u - 1u) == 0);

static_assert(ParseBehaviour("toggle") == Behaviour::Toggle);
static_assert(ParseBehaviour("temp") == Behaviour::Temp);
static_assert(ParseBehaviour("") == Behaviour::Temp);
static_assert(ParseBehaviour("nonsense") == Behaviour::Temp);
```

Existing `HoldOpacity` asserts stay untouched — `temp` mode is unchanged.

### 7. [README.md:179](README.md#L179)

Rewrite the Alt+z sentence and add the ini key after the existing
`ZeppelinPanel=on` block:

> Show it with **`Alt` + `z`**. By default it stays up for five seconds, then
> fades out, and pressing the shortcut again restarts those five seconds. Set
> `ZeppelinPanelBehaviour=toggle` to have the shortcut open and close the panel
> instead, leaving it on screen until you press it again. Either `Alt` key
> works, including `AltGr`. Defaults to `temp`.

## Verification

**Locally on macOS (works today, no mingw/wine needed).** The host test is a
single translation unit and system `clang++` already builds it — verified before
writing this plan:

```sh
clang++ -std=c++20 -Wall -Wextra -pedantic \
  -Isrc/core -Isrc/patcher tests/zeppelin_panel_test.cpp -o /tmp/zp_test && /tmp/zp_test
```

That covers `ParseBehaviour` and `ToggleOpacity` — the whole of the new core
logic. It passes on the current tree, so a failure is a real regression.

**On a Windows/mingw box.** `cd mingw && make test` (runs `zeppelin-panel-test`
plus the Wine LoadLibrary smoke test). Also worth a `make` to confirm
`ScreenConfig.h`, `GameDllHooks.h`/`.cpp` and `PatchInstallers.cpp` still
compile with the new parameter.

**In game (Hidden Stroke 2, a map with zeppelin groups):**

1. No `ZeppelinPanelBehaviour` in the ini → `Alt`+`z` still shows for ~5s then
   fades. Unchanged.
2. `ZeppelinPanelBehaviour=toggle` → `Alt`+`z` fades the panel in and it stays;
   `Alt`+`z` again fades it out.
3. **Hold** `Alt`+`z` for a second in toggle mode — the panel must not flicker
   (auto-repeat guard).
4. Double-tap `Alt`+`z` quickly — opacity reverses smoothly, no pop.
5. Toggle on, then pan the camera / zoom — no smearing or stale panel pixels
   (the dirty-rect path at
   [GameDllHooks.cpp:1988-1999](src/patcher/GameDllHooks.cpp#L1988-L1999) is
   driven by `zeppelinVisibleRows()`, which is unchanged).
6. `ZeppelinPanelBehaviour=TOGGLE` / `Toggle` → same as `toggle` (lowercasing).
   A typo such as `ZeppelinPanelBehaviour=sticky` falls back to `temp`.
7. Toggle on, then capture every remaining group so the panel empties: it
   disappears (no rows) and reappears if a group is later contested again. See
   note below.

## Known edges, left as-is

- `zeppelinPanelAltShow()` requires `g_zeppelinVisible`, which is _content_
  availability, not user intent. With zero rows on screen, `Alt`+`z` is neither
  handled nor swallowed, so a bare `Z` reaches the game and the toggle cannot be
  flipped. Pre-existing in `temp` mode; out of scope.
- The toggle state lives for the session — `configureZeppelinPanel` is only
  called at patch install, so it is not reset between missions. That is the
  intended "until the player closes it" reading.
- `fix-zep-plan.md` (player-index vs team-index in `ZeppelinReader::scan()`)
  touches [ZeppelinReader.h:83-135](src/patcher/ZeppelinReader.h#L83-L135), not
  the show/hide path. No collision.
