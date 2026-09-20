# Panel scale settings: `ZeppelinPanelScale`, `GroupPanelScale`, `ZoomIndicatorScale`

## Context

The three overlay panels MultiCAD draws — the control-group panel (top-right), the
zeppelin capture panel (bottom-right) and the zoom indicator (left/right edge) — are
rasterised at fixed pixel sizes by hand-rolled RGB565 blitters. The group panel is
247x31 px, the zeppelin panel 53 px wide, the indicator 12 px dots. At 1920x1080 and
above these are small; at 4K they are barely readable. There is no DPI or UI-scale
concept anywhere in the codebase (a `UIScale` feature existed briefly and was removed
in `f7c5136` as unstable).

This adds one `.ini` setting per panel so each can be enlarged independently:

```ini
[Game]
GroupPanelScale=1.5
ZeppelinPanelScale=2
ZoomIndicatorScale=1.25
```

Floating-point factor, default `1`, allowed range `1`–`3` in `0.25` steps. Values
outside the range clamp to the nearest bound; values between steps round to the
nearest step. Rendering must stay crisp (no resampling blur, no half-pixel seams) and
the group panel's mouse hitbox must keep matching its painted size exactly.

**Decisions taken** (asked and confirmed):

- The 12 px screen-edge margin does **not** scale. Panels grow inward from their
  corner so panels at different scales stay aligned on the same edge.
- A scale too large for the current resolution keeps today's behaviour: `Fits()`
  returns false and the panel is simply not drawn (and no hitbox is registered).
  This is the existing small-screen path, not new code. Documented in the README.

## Design

**Scale is an integer count of quarter steps, never a float past the ini boundary.**
`1.0x == 4`, `3.0x == 12`, so all nine allowed values are exactly representable and
every derived dimension is integer arithmetic. This mirrors the quarter-unit idiom
already used for world zoom (`Zoom::kMinScale = 4 // quarter units: 4 == 1x`,
[Zoom.h:164](src/core/Zoom.h#L164)).

**Two rounding rules, deliberately different:**

- `Size(v, q) = (v * q + 2) / 4` — round half-up, for pixel sizes and offsets.
- `Repeat(v, q) = max(1, v * q / 4)` — floor, for the _integer replication factor_
  of the 3x5 bitmap glyphs and 5x5 icons. Flooring keeps glyph replication integral
  (so glyphs stay pixel-sharp, never resampled) and guarantees a glyph never
  outgrows the cell it is centred in.

Both reduce to identity at `q == 4`, so **the 1x render is byte-identical to today**
and every existing test keeps passing unchanged.

**Scale the atoms, derive everything else.** Every existing `constexpr` geometry
function gains a `quarters` parameter and is rebuilt from scaled atoms rather than
from a scaled result. Because `Width()`, `Height()`, `CellRect()`, `HitTest()` and
`Fits()` all compose from the same scaled `Cell(q)`/`Gap(q)`, positions tile with no
accumulated drift and **the hitbox follows the visual by construction**.

**Ambient scale, set once at install.** Each panel namespace gets a private
function-local static holding its quarters, plus a zero-arg overload of every
geometry function that reads it. The scale is read from the ini exactly once, at
patch-install time, and never changes at runtime — the same lifecycle as every other
setting. This is what makes the diff small and the hitbox safe: the four places that
hit-test the group panel ([GameDllHooks.cpp:83](src/patcher/GameDllHooks.cpp#L83)
`GroupPanelRect`, [:101](src/patcher/GameDllHooks.cpp#L101) `groupPanelVisible`,
[:125](src/patcher/GameDllHooks.cpp#L125) `HitTest`,
[:4584](src/patcher/GameDllHooks.cpp#L4584) `syncGroupPanelArea`, which registers the
game's own `UiEventArea`) all already route through those functions and need **no
edits at all**. There is no site left to forget.

The pure `(…, int quarters)` overloads stay `constexpr`, so the layout invariants
stay compile-time checked and the tests stay `static_assert`-based.

The zoom indicator is the exception: it has a single call site and all its state
already lives in `Zoom::State` alongside `indicatorAnchor_`/`indicatorShape_`, so it
takes an explicit parameter instead of an ambient static — matching the existing
indicator-setting idiom.

## Files

### 1. `src/core/PanelScale.h` (new, ~35 lines)

The only new file. Header-only, so no Makefile change is needed. Included by
`GroupPanel.h`, `ZeppelinPanel.h`, `ZoomIndicator.h` and `ScreenConfig.h`.

```cpp
namespace PanelScale
{
    constexpr int kStep = 4;          // quarters per 1.0x
    constexpr int kMinQuarters = 4;   // 1.00x
    constexpr int kMaxQuarters = 12;  // 3.00x

    // Ini factor -> quarter steps: clamped to [1,3], snapped to the nearest 0.25.
    // The `!(factor > 1.f)` test also rejects NaN into the minimum; std::clamp
    // would propagate it. Same reasoning as the removed UIScale::Set (85a82ac).
    constexpr int Quarters(float factor)
    {
        if (!(factor > 1.f))   return kMinQuarters;
        if (factor >= 3.f)     return kMaxQuarters;
        return static_cast<int>(factor * kStep + 0.5f);
    }

    // Pixel size or offset. Round half-up so scaled edges land on whole pixels.
    constexpr int Size(int value, int quarters)
    {
        return (value * quarters + kStep / 2) / kStep;
    }

    // Integer replication factor for a bitmap glyph or icon. Floored, so the
    // blitter stays a whole-pixel nearest-neighbour copy (crisp, never resampled)
    // and a glyph never outgrows the box it is centred in.
    constexpr int Repeat(int value, int quarters)
    {
        const int scaled = value * quarters / kStep;
        return scaled < 1 ? 1 : scaled;
    }
}
```

### 2. `src/core/ScreenConfig.h` — three getters

Follow the existing getter shape exactly
([GetZeppelinPanel, :244-253](src/core/ScreenConfig.h#L244-L253)), returning already-
snapped quarters so no unvalidated float escapes the ini layer:

```cpp
static int GetGroupPanelScale()
{
    const std::string iniPath = GetIniPath();
    if (iniPath.empty())
        return PanelScale::kMinQuarters;

    char buffer[16]{};
    GetPrivateProfileStringA("Game", "GroupPanelScale", "1", buffer, sizeof(buffer), iniPath.c_str());
    for (char& c : buffer)
        if (c == ',')
            c = '.';                      // locale-typed "1,5"; atof only reads '.'
    return PanelScale::Quarters(static_cast<float>(std::atof(buffer)));
}
```

`std::atof` (not `std::stof`) — no exceptions, junk yields `0` which clamps to 1x.
The comma normalisation is recovered from the removed `GetUIScale`. Repeat verbatim
for `GetZeppelinPanelScale` / `GetZoomIndicatorScale`.

### 3. `src/core/GroupPanel.h` — scale-parameterised geometry

Keep every existing `constexpr int kCell = 22;`-style constant as the **1x base**, so
the tests and the zeppelin panel that reference them keep compiling. Add beside them:

```cpp
inline int& ScaleQuarters() { static int quarters = PanelScale::kMinQuarters; return quarters; }
inline void SetScale(int quarters) { ScaleQuarters() = std::clamp(quarters, PanelScale::kMinQuarters, PanelScale::kMaxQuarters); }

constexpr int Cell(int q)       { return PanelScale::Size(kCell, q); }
constexpr int Gap(int q)        { return PanelScale::Size(kGap, q); }
constexpr int GlyphScale(int q) { return PanelScale::Repeat(glyphScale, q); }
constexpr int GlyphX(int q)     { return (Cell(q) - kCountDigitWidth * GlyphScale(q)) / 2; }
constexpr int GlyphY(int q)     { return (Cell(q) - kCountDigitHeight * GlyphScale(q)) / 2; }

// Icon box derives from the replication factor, not from Size(), so the drawn
// icon fills its box exactly instead of floating inside a slightly larger one.
constexpr int IconRepeat(int q) { return PanelScale::Repeat(1, q); }
constexpr int IconBox(int q)    { return kIconSize * IconRepeat(q); }
constexpr int IconInset(int q)  { return PanelScale::Size(kIconInset, q); }
constexpr int WheelX(int q)     { return IconInset(q); }          // == TransportX
constexpr int HouseX(int q)     { return Cell(q) - IconInset(q) - IconBox(q); }   // == GunX
constexpr int IconY(int q)      { return IconInset(q); }
constexpr int WheelY(int q)     { return Cell(q) - IconInset(q) - IconBox(q); }   // == GunY

constexpr int CountScale(int q)     { return PanelScale::Repeat(kCountScale, q); }
constexpr int CountDigitGap(int q)  { return PanelScale::Size(kCountDigitGap, q); }
constexpr int CountGapY(int q)      { return PanelScale::Size(kCountGapY, q); }
constexpr int CountOutline(int q)   { return PanelScale::Size(kCountOutline, q); }
constexpr int CountTop(int q)       { return Cell(q) + CountGapY(q) + CountOutline(q); }
constexpr int CountWidth(int digits, int q);
constexpr int CountLeft(int value, int q);
constexpr int CountStripHeight(int q);

constexpr int  Width(int q);
constexpr int  Height(int q);
constexpr Rect CellRect(int slot, int screenWidth, int q);
constexpr int  HitTest(int x, int y, int screenWidth, int q);
constexpr bool Fits(int screenWidth, int screenHeight, int q);
```

Then one ambient overload per public entry point — these are what the patcher already
calls, so **`GameDllHooks.cpp` needs no changes for the group panel**:

```cpp
inline int  Width()  { return Width(ScaleQuarters()); }
inline int  Height() { return Height(ScaleQuarters()); }
inline Rect CellRect(int slot, int screenWidth) { return CellRect(slot, screenWidth, ScaleQuarters()); }
inline int  HitTest(int x, int y, int screenWidth) { return HitTest(x, y, screenWidth, ScaleQuarters()); }
inline bool Fits(int screenWidth, int screenHeight) { return Fits(screenWidth, screenHeight, ScaleQuarters()); }
```

`Draw16` keeps its signature. Read the scale **once** at the top
(`const int q = ScaleQuarters();`) so a frame is internally consistent, then replace
each `kCell` / `glyphX` / `kCountTop` / `kHouseX` / … with its `(q)` form and pass the
per-icon replication factor to `blit` in place of the hardcoded `1` at
[:312](src/core/GroupPanel.h#L312), [:320](src/core/GroupPanel.h#L320),
[:322](src/core/GroupPanel.h#L322). While there, replace the magic `blit(kDigits[…], 3, 5, …)`
literals at [:284](src/core/GroupPanel.h#L284) with `kCountDigitWidth, kCountDigitHeight`.

**Replace the `static_assert` block at [:162-176](src/core/GroupPanel.h#L162-L176)**
with the same invariants expressed over every allowed scale:

```cpp
constexpr bool LayoutFits(int q)
{
    return WheelX(q) + IconBox(q) <= GlyphX(q) &&
           HouseX(q) >= GlyphX(q) + kCountDigitWidth * GlyphScale(q) &&
           HouseX(q) + IconBox(q) <= Cell(q) - IconInset(q) &&
           WheelY(q) >= IconY(q) + IconBox(q) &&
           GlyphX(q) >= 0 && GlyphY(q) >= 0 &&
           CountLeft(kCountMax, q) - CountOutline(q) >= 0 &&
           CountLeft(kCountMax, q) + CountWidth(kCountMaxDigits, q) + CountOutline(q) <= Cell(q) &&
           CountTop(q) + kCountDigitHeight * CountScale(q) + CountOutline(q)
               == Cell(q) + CountStripHeight(q);
}

constexpr bool LayoutFitsEveryScale()
{
    for (int q = PanelScale::kMinQuarters; q <= PanelScale::kMaxQuarters; ++q)
        if (!LayoutFits(q))
            return false;
    return true;
}

static_assert(LayoutFitsEveryScale());
```

All nine steps were verified by hand to satisfy every one of these; the
`static_assert` is what stops a future constant tweak from breaking one silently.

A naive uniform scale does **not** hold here: deriving the count plate from a
rounded-up replication factor overflows the cell at 1.5x. That is the reason `Size`
and `Repeat` round in opposite directions.

### 4. `src/core/ZeppelinPanel.h` — same treatment

`Pad(q)`, `Swatch(q)`, `TimeScale(q)` (`Repeat`), `DigitGap(q)`, `RowHeight(q)`,
`RowGap(q)`, `TimeWidth(q)`, `CountWidth(q)`, `TextWidth(q)`, `GlyphWidth(glyph, q)`,
`GlyphsWidth(glyphs, count, q)`, `Width(q)`, `Height(rows, q)`,
`PanelRect(w, h, rows, q)`, `RowRect(index, w, h, rows, q)` — plus ambient overloads
matching today's signatures, so `ZeppelinPanelRect()` at
[GameDllHooks.cpp:164](src/patcher/GameDllHooks.cpp#L164) is untouched.

`Fits` is the one that needs both scales: it checks the zeppelin panel clears the
group panel, whose height depends on the _group panel's_ own setting.

```cpp
constexpr bool Fits(int screenWidth, int screenHeight, int rows, int q, int groupQuarters)
{
    return GroupPanel::Fits(screenWidth, screenHeight, groupQuarters) &&
           screenWidth >= Width(q) + 2 * kMargin &&
           Bottom(screenHeight) - Height(rows, q) >=
               GroupPanel::kMargin + GroupPanel::Height(groupQuarters) + RowGap(q);
}

inline bool Fits(int screenWidth, int screenHeight, int rows)   // ambient, both panels
{
    return Fits(screenWidth, screenHeight, rows, ScaleQuarters(), GroupPanel::ScaleQuarters());
}
```

Replace the `static_assert` block at [:188-192](src/core/ZeppelinPanel.h#L188-L192)
with the same `LayoutFitsEveryScale()` pattern (`Swatch(q) < RowHeight(q)`,
`kDigitHeight * TimeScale(q) <= RowHeight(q)`, `WidestTimeWidth(q) == TimeWidth(q)`,
`WidestCountWidth(q) == CountWidth(q)`).

### 5. `src/core/ZoomIndicator.h` + `src/core/Zoom.h` — indicator

Add a trailing `int quarters = PanelScale::kMinQuarters` to `DrawIndicatorSquares16`,
`DrawIndicatorBars16` and `DrawIndicator16`. The defaulted parameter keeps the
existing calls in `tests/zoom_test.cpp` compiling untouched. Inside each, the
function-local `constexpr int size/gap/thickness/shortLen/longLen` become
`const int … = PanelScale::Size(…, quarters)` — `margin` stays 12 per the decision
above. `dots` is unchanged. The existing bounds guard
([:34](src/core/ZoomIndicator.h#L34), [:75](src/core/ZoomIndicator.h#L75)) already
refuses to draw when the scaled indicator would not fit.

In `Zoom::State`, beside `indicatorShape_` ([Zoom.h:789](src/core/Zoom.h#L789)):

```cpp
int  indicatorScaleQuarters() const { return indicatorScale_; }
void setIndicatorScale(int quarters) { indicatorScale_ = std::clamp(quarters, PanelScale::kMinQuarters, PanelScale::kMaxQuarters); }
…
int indicatorScale_{ PanelScale::kMinQuarters };
```

Pass it at the one call site,
[GameDllHooks.cpp:1838-1846](src/patcher/GameDllHooks.cpp#L1838-L1846).

### 6. `src/patcher/GameDllHooks.h` / `.cpp` — config funnel

Add a defaulted `int scaleQuarters = PanelScale::kMinQuarters` to
`configureGroupPanel` ([GameDllHooks.h:1294](src/patcher/GameDllHooks.h#L1294)) and
`configureZeppelinPanel` ([:1296](src/patcher/GameDllHooks.h#L1296)); each body calls
the matching `SetScale`. Because `shutdown()` already calls both with defaults, the
scale resets to 1x on unload for free — no change to `UninstallGamePatches`.

### 7. `src/patcher/PatchInstallers.cpp` — wiring

Three arguments at the existing config block
([:97-108](src/patcher/PatchInstallers.cpp#L97-L108)):

```cpp
Zoom::GetState().setIndicatorScale(Screen::GetZoomIndicatorScale());
…
GameDllHooks::configureGroupPanel(…, Screen::GetGroupPanelScale());
GameDllHooks::configureZeppelinPanel(…, Screen::GetZeppelinPanelScale());
```

### 8. Tests

No new Makefile targets — the new assertions go in existing test files that already
pull in the changed headers.

`tests/group_panel_test.cpp`:

- `PanelScale::Quarters` boundary table: `0 -> 4`, `-1 -> 4`, `1.0 -> 4`, `1.1 -> 4`,
  `1.125 -> 5` (exact midpoint rounds up), `1.2 -> 5`, `2.0 -> 8`, `2.99 -> 12`,
  `3.0 -> 12`, `99 -> 12`, and a runtime `assert` for `quiet_NaN() -> 4`.
- `static_assert(Width(4) == 247 && Height(4) == 31)` — pins the 1x baseline.
- Loop `q = 4..12`: `Width(q)`/`Height(q)` strictly increase; the last column ends
  exactly at `screenWidth - kMargin`; no two `CellRect`s overlap; `HitTest` returns
  the right slot at each cell's four corners and `-1` one pixel past each edge and in
  the inter-column gap.
- **The hitbox check the request asks for**, at 2x: `SetScale(8)`, `Draw16` into a
  buffer, then assert the painted extent (first and last touched pixel on each axis)
  equals `CellRect(0,…).x` / `Width()` / `Height()` as `GroupPanelRect()` computes
  them. Reset to `SetScale(4)` afterwards so later sections are unaffected.

`tests/zeppelin_panel_test.cpp`: the same shape — 1x baseline pinned, then `q = 4..12`
for `Width(q)`, `Height(rows,q)`, non-overlapping `RowRect`s, glyphs right-aligned
inside `TextWidth(q)`, swatch inside the row. The local helpers `TextArea`/`SwatchArea`
([:19-28](tests/zeppelin_panel_test.cpp#L19-L28)) switch from `kTextWidth`/`kPad`/
`kSwatch` to the ambient `TextWidth()`/`Pad()`/`Swatch()` so they follow the scale.

`tests/zoom_test.cpp`: draw the indicator at `kMaxQuarters` into a bounds-checked
buffer and assert nothing is written outside it, for both shapes and both anchors.

### 9. `README.md`

New subsection under `### Control-Group Panel` and `### Zeppelin Capture Panel`, and
alongside the `ZoomIndicator` keys at [:121-140](README.md#L121-L140). Follow the
existing wording idiom for a bounded numeric value ([:109](README.md#L109)). State:
range `1`–`3`, steps of `0.25`, default `1`, out-of-range clamps, in-between rounds to
the nearest step, and that a panel too large for the current resolution is not drawn.

## Verification

**Everything except the DLL build runs locally on macOS** — all four host-compiled
test targets were confirmed to build and pass with Apple clang, no mingw needed:

```bash
cd /Users/jean/dev/github/Alliance-Francophone-Sudden-Strike/MultiCAD
for t in group_panel zeppelin_panel zoom world_isolation; do
  c++ -std=c++20 -Wall -Wextra -pedantic -Isrc/core -Isrc/patcher \
      tests/${t}_test.cpp -o /tmp/$t && /tmp/$t && echo "$t PASS"
done
```

This covers the whole of the new logic: the ini value -> quarters snapping, every
scaled geometry function, the layout invariants at all nine steps (via the
`static_assert`s, which fail the _compile_), the rendering, and the
hitbox-matches-visual assertion. The `Zoom`/`GroupPanel`/`ZeppelinPanel` headers are
deliberately Windows-free for exactly this reason
([GroupPanel.h:3-5](src/core/GroupPanel.h#L3-L5)).

**Not reproducible locally**, needs the mingw box:

- `cd mingw && make test` — the full suite plus the Wine `LoadLibrary` smoke test and
  `analyze-dll-test`.
- `make zoom-render-test` — exercises the real composition hook under Wine. It
  references `GroupPanel::CellRect` and `ZeppelinPanel::PanelRect`
  ([zoom_render_test.cpp:233-234](tests/zoom_render_test.cpp#L233-L234)) at 1x only,
  so it should pass unchanged; worth confirming rather than assuming.
- The MSVC build via `MultiCAD.sln`. Note `src/MultiCAD.vcxproj` does not list the
  panel headers under `ClInclude` at all (they compile transitively), so the new
  `PanelScale.h` needs no project edit either — but adding it to the `ClInclude` list
  is a one-line courtesy for IDE users.

**In-game**, once built: set `GroupPanelScale=2`, `ZeppelinPanelScale=1.5`,
`ZoomIndicatorScale=3` in `sudtest.ini` and check each panel scales independently,
that glyph and icon edges stay hard (no blur or seams), that left-clicking a
control-group cell still selects the group and right-clicking still assigns it with
the enlarged cells, and that the cursor changes shape over the enlarged panel but not
just outside it. Then confirm `GroupPanelScale=0.1` and `=7` both behave as `1` and
`3`, and that `1.3` behaves as `1.25`.
