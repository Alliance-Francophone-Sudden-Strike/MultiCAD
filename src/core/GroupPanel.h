#pragma once

// Control-group panel: pure geometry, slot mapping and signature matching.
// Deliberately free of Windows and game dependencies so the host compiler can
// test it (see tests/group_panel_test.cpp). Live state stays in the patcher.

#include "PanelScale.h"
#include "Zoom.h"           // Rect, kIndicatorFadeMs
#include "ZoomIndicator.h"  // Blend565

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace GroupPanel
{
    using Zoom::Rect;

    // Ten groups drawn on one row: "1".."9","0".
    constexpr int kCount = 10;
    constexpr int kColumns = kCount;
    constexpr int kRows = 1;

    using Slots = std::array<bool, kCount>;
    using Counts = std::array<uint16_t, kCount>;

    // The game's own key table is ordered '0','1',..,'9', and a unit's group
    // byte holds that table index + 1. The panel reads 1..9,0, so the last
    // slot is key '0' -> table index 0 -> stored value 1, while slot 0 is key
    // '1' -> index 1 -> stored value 2. Getting this backwards silently shows
    // every group off by one, so it is derived in one place only.
    constexpr int KeyIndexForSlot(int slot)
    {
        return slot == kCount - 1 ? 0 : slot + 1;
    }

    // Value the game writes into the unit's group byte for this slot.
    constexpr uint8_t StoredValueForSlot(int slot)
    {
        return static_cast<uint8_t>(KeyIndexForSlot(slot) + 1);
    }

    // Inverse of StoredValueForSlot: the panel slot a unit's group byte names,
    // or -1 when the byte holds no group (0) or something out of range.
    constexpr int SlotForStoredValue(int stored)
    {
        if (stored < 1 || stored > kCount)
            return -1;
        const int keyIndex = stored - 1;
        return keyIndex == 0 ? kCount - 1 : keyIndex - 1;
    }

    constexpr char LabelForSlot(int slot)
    {
        return static_cast<char>('0' + KeyIndexForSlot(slot));
    }

    // Virtual-key code of the number-row key this slot mirrors.
    constexpr int VirtualKeyForSlot(int slot)
    {
        return 0x30 + KeyIndexForSlot(slot);
    }

    constexpr bool AltOnly(bool alt, bool ctrl, bool shift, bool rightAlt)
    {
        return alt && !shift && (!ctrl || rightAlt);
    }

    // 19% smaller than the original 27px cell / 4px gap.
    constexpr int kCell = 22;
    constexpr int kGap = 3;
    constexpr int kMargin = 12;

    constexpr int kCountScale = 1;
    constexpr int kCountDigitWidth = 3;
    constexpr int kCountDigitHeight = 5;
    constexpr int kCountDigitGap = 1;
    constexpr int kCountMaxDigits = 4;
    constexpr int kCountMax = 9999;
    constexpr int kCountGapY = 2;
    constexpr int kCountOutline = 1;
    constexpr int kCountTop = kCell + kCountGapY + kCountOutline;
    constexpr uint16_t kCountText = 0xFFFF;
    constexpr uint16_t kCountPlate = 0x1082;

    constexpr int CountWidth(int digits)
    {
        return digits * kCountDigitWidth * kCountScale + (digits - 1) * kCountDigitGap;
    }

    constexpr int CountDigits(int value)
    {
        return value >= 1000 ? 4 : value >= 100 ? 3 : value >= 10 ? 2 : 1;
    }

    constexpr int glyphScale = 2;
    constexpr int glyphX = (kCell - kCountDigitWidth * glyphScale) / 2;
    constexpr int glyphY = (kCell - kCountDigitHeight * glyphScale) / 2;

    inline int& ScaleQuarters()
    {
        static int quarters = PanelScale::kMinQuarters;
        return quarters;
    }

    inline void SetScale(int quarters)
    {
        ScaleQuarters() = std::clamp(quarters, PanelScale::kMinQuarters, PanelScale::kMaxQuarters);
    }

    constexpr int Cell(int q)           { return PanelScale::Size(kCell, q); }
    constexpr int Gap(int q)            { return PanelScale::Size(kGap, q); }
    constexpr int GlyphScale(int q)     { return PanelScale::Repeat(glyphScale, q); }
    constexpr int GlyphX(int q)         { return (Cell(q) - kCountDigitWidth * GlyphScale(q)) / 2; }
    constexpr int GlyphY(int q)         { return (Cell(q) - kCountDigitHeight * GlyphScale(q)) / 2; }

    constexpr int CountScale(int q)     { return PanelScale::Repeat(kCountScale, q); }
    constexpr int CountDigitGap(int q)  { return PanelScale::Size(kCountDigitGap, q); }
    constexpr int CountGapY(int q)      { return PanelScale::Size(kCountGapY, q); }
    constexpr int CountOutline(int q)   { return PanelScale::Size(kCountOutline, q); }
    constexpr int CountTop(int q)       { return Cell(q) + CountGapY(q) + CountOutline(q); }

    constexpr int CountWidth(int digits, int q)
    {
        return digits * kCountDigitWidth * CountScale(q) + (digits - 1) * CountDigitGap(q);
    }

    constexpr int CountLeft(int value, int q)
    {
        return (Cell(q) - CountWidth(CountDigits(value), q)) / 2;
    }

    constexpr int CountStripHeight(int q)
    {
        return CountGapY(q) + kCountDigitHeight * CountScale(q) + 2 * CountOutline(q);
    }

    constexpr int Width(int q)  { return kColumns * Cell(q) + (kColumns - 1) * Gap(q); }
    constexpr int Height(int q) { return kRows * Cell(q) + (kRows - 1) * Gap(q) + CountStripHeight(q); }

    constexpr Zoom::Rect CellRect(int slot, int screenWidth, int q)
    {
        const int column = slot % kColumns;
        const int row = slot / kColumns;
        const int left = screenWidth - kMargin - Width(q);
        return { left + column * (Cell(q) + Gap(q)),
                 kMargin + row * (Cell(q) + Gap(q)),
                 Cell(q), Cell(q) };
    }

    constexpr int HitTest(int x, int y, int screenWidth, int q)
    {
        for (int slot = 0; slot < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, screenWidth, q);
            if (x >= cell.x && x < cell.x + cell.width &&
                y >= cell.y && y < cell.y + cell.height)
                return slot;
        }
        return -1;
    }

    constexpr bool Fits(int screenWidth, int screenHeight, int q)
    {
        return screenWidth >= Width(q) + 2 * kMargin &&
               screenHeight >= Height(q) + 2 * kMargin;
    }

    inline int  CountStripHeight() { return CountStripHeight(ScaleQuarters()); }
    inline int  Width()            { return Width(ScaleQuarters()); }
    inline int  Height()           { return Height(ScaleQuarters()); }
    inline Zoom::Rect CellRect(int slot, int screenWidth) { return CellRect(slot, screenWidth, ScaleQuarters()); }
    inline int  HitTest(int x, int y, int screenWidth)    { return HitTest(x, y, screenWidth, ScaleQuarters()); }
    inline bool Fits(int screenWidth, int screenHeight)   { return Fits(screenWidth, screenHeight, ScaleQuarters()); }

    constexpr uint16_t kActiveBorder = 0xFFFF;
    constexpr uint16_t kActiveFill = 0x2104;
    constexpr uint16_t kActiveText = 0xFFFF;
    constexpr uint16_t kInactiveBorder = 0x4208;
    constexpr uint16_t kInactiveFill = 0x1082;
    constexpr uint16_t kInactiveText = 0x8410;

    constexpr int kIconSize = 5;
    constexpr int kIconInset = 2;
    constexpr int kWheelX = kIconInset;
    constexpr int kTransportX = kIconInset;
    constexpr int kHouseX = kCell - kIconInset - kIconSize;
    constexpr int kGunX = kCell - kIconInset - kIconSize;
    constexpr int kIconY = kIconInset;
    constexpr int kWheelY = kCell - kIconInset - kIconSize;
    constexpr int kGunY = kCell - kIconInset - kIconSize;
    constexpr uint16_t kContainedIcon = 0xC618;
    constexpr uint16_t kTransportFill = 0x0300;

    constexpr int IconRepeat(int q) { return PanelScale::Repeat(1, q); }
    constexpr int IconBox(int q)    { return kIconSize * IconRepeat(q); }
    constexpr int IconInset(int q)  { return PanelScale::Size(kIconInset, q); }
    constexpr int WheelX(int q)     { return IconInset(q); }
    constexpr int TransportX(int q) { return IconInset(q); }
    constexpr int HouseX(int q)     { return Cell(q) - IconInset(q) - IconBox(q); }
    constexpr int GunX(int q)       { return HouseX(q); }
    constexpr int IconY(int q)      { return IconInset(q); }
    constexpr int WheelY(int q)     { return Cell(q) - IconInset(q) - IconBox(q); }
    constexpr int GunY(int q)       { return WheelY(q); }

    constexpr bool LayoutFits(int q)
    {
        return WheelX(q) + IconBox(q) <= GlyphX(q) &&
               HouseX(q) >= GlyphX(q) + kCountDigitWidth * GlyphScale(q) &&
               HouseX(q) + IconBox(q) <= Cell(q) - IconInset(q) &&
               WheelY(q) >= IconY(q) + IconBox(q) &&
               IconY(q) + IconBox(q) <= Cell(q) - IconInset(q) &&
               GlyphX(q) >= 0 && GlyphY(q) >= 0 &&
               CountLeft(kCountMax, q) - CountOutline(q) >= 0 &&
               CountLeft(kCountMax, q) + CountWidth(kCountMaxDigits, q) + CountOutline(q) <= Cell(q) &&
               CountTop(q) - CountOutline(q) > Cell(q) - 1 &&
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

    static_assert(CountDigits(kCountMax) == kCountMaxDigits);
    static_assert(kCountGapY >= 1);
    static_assert(LayoutFitsEveryScale());

    inline constexpr uint8_t kHouse[kIconSize]
    {
        0b00100,
        0b01110,
        0b11111,
        0b10101,
        0b10101,
    };

    inline constexpr uint8_t kWheel[kIconSize]
    {
        0b01110,
        0b10001,
        0b10101,
        0b10001,
        0b01110,
    };

    inline constexpr uint8_t kGun[kIconSize]
    {
        0b00001,
        0b00010,
        0b00100,
        0b01110,
        0b10001,
    };

    // Tiny 3x5 digits, enlarged to 9x15 inside each cell. Keeping the glyphs
    // here avoids depending on game font assets whose lifetime varies by UI.
    inline constexpr uint8_t kDigits[10][5]
    {
        { 0b111, 0b101, 0b101, 0b101, 0b111 },
        { 0b010, 0b110, 0b010, 0b010, 0b111 },
        { 0b111, 0b001, 0b111, 0b100, 0b111 },
        { 0b111, 0b001, 0b111, 0b001, 0b111 },
        { 0b101, 0b101, 0b111, 0b001, 0b001 },
        { 0b111, 0b100, 0b111, 0b001, 0b111 },
        { 0b111, 0b100, 0b111, 0b101, 0b111 },
        { 0b111, 0b001, 0b010, 0b010, 0b010 },
        { 0b111, 0b101, 0b111, 0b101, 0b111 },
        { 0b111, 0b101, 0b111, 0b001, 0b111 },
    };

    inline void Draw16(
        uint16_t* destination,
        int pitch,
        int width,
        int height,
        const Slots& active,
        const Slots& house,
        const Slots& wheel,
        int opacity = 16,
        const Counts* counts = nullptr,
        const Slots* transport = nullptr,
        bool persistent = false,
        const Rect* clip = nullptr,
        const Slots* gun = nullptr)
    {
        const int q = ScaleQuarters();

        if (!destination || pitch < width || !Fits(width, height, q) || opacity <= 0 ||
            (!persistent && std::none_of(active.begin(), active.end(), [](bool value) { return value; })))
            return;

        opacity = std::clamp(opacity, 0, 16);

        const auto plot = [&](int x, int y, uint16_t color)
        {
            uint16_t& pixel = destination[y * pitch + x];
            pixel = opacity == 16 ? color : Zoom::Blend565(pixel, color, opacity);
        };

        const auto blit = [&](const uint8_t* bits, int cols, int rows,
                              int left, int top, int scale, uint16_t color)
        {
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x)
                    if (bits[y] & (1 << (cols - 1 - x)))
                        for (int dy = 0; dy < scale; ++dy)
                            for (int dx = 0; dx < scale; ++dx)
                                plot(left + x * scale + dx, top + y * scale + dy, color);
        };

        const auto fillRect = [&](int left, int top, int cols, int rows, uint16_t color)
        {
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x)
                    plot(left + x, top + y, color);
        };

        const int cellSize = Cell(q);
        const int iconBox = IconBox(q);
        const int iconRepeat = IconRepeat(q);

        for (int slot = 0; slot < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, width, q);
            if (clip && (clip->x >= cell.x + cellSize ||
                         clip->x + clip->width <= cell.x ||
                         clip->y >= cell.y + cellSize + CountStripHeight(q) ||
                         clip->y + clip->height <= cell.y))
                continue;

            const uint16_t border = active[slot] ? kActiveBorder : kInactiveBorder;
            const uint16_t fill = active[slot] ? kActiveFill : kInactiveFill;
            const uint16_t text = active[slot] ? kActiveText : kInactiveText;

            for (int y = 0; y < cellSize; ++y)
                for (int x = 0; x < cellSize; ++x)
                    plot(cell.x + x, cell.y + y,
                         x == 0 || y == 0 || x == cellSize - 1 || y == cellSize - 1 ? border : fill);

            blit(kDigits[LabelForSlot(slot) - '0'], kCountDigitWidth, kCountDigitHeight,
                 cell.x + GlyphX(q), cell.y + GlyphY(q), GlyphScale(q), text);

            if (counts && active[slot] && (*counts)[slot] > 0)
            {
                const int value = std::min<int>((*counts)[slot], kCountMax);
                const int digits = CountDigits(value);
                int x = cell.x + CountLeft(value, q);

                fillRect(x - CountOutline(q),
                         cell.y + CountTop(q) - CountOutline(q),
                         CountWidth(digits, q) + 2 * CountOutline(q),
                         kCountDigitHeight * CountScale(q) + 2 * CountOutline(q),
                         kCountPlate);

                for (int digit = digits - 1; digit >= 0; --digit)
                {
                    int place = 1;
                    for (int i = 0; i < digit; ++i)
                        place *= 10;

                    blit(kDigits[value / place % 10], kCountDigitWidth, kCountDigitHeight,
                         x, cell.y + CountTop(q), CountScale(q), kCountText);
                    x += kCountDigitWidth * CountScale(q) + CountDigitGap(q);
                }
            }

            if (house[slot])
                blit(kHouse, kIconSize, kIconSize, cell.x + HouseX(q), cell.y + IconY(q),
                     iconRepeat, kContainedIcon);
            if (transport && (*transport)[slot])
            {
                fillRect(cell.x + TransportX(q), cell.y + IconY(q), iconBox, iconBox, kContainedIcon);
                fillRect(cell.x + TransportX(q) + iconRepeat, cell.y + IconY(q) + iconRepeat,
                         iconBox - 2 * iconRepeat, iconBox - 2 * iconRepeat, kTransportFill);
            }
            if (wheel[slot])
                blit(kWheel, kIconSize, kIconSize, cell.x + WheelX(q), cell.y + WheelY(q),
                     iconRepeat, kContainedIcon);
            if (gun && (*gun)[slot])
                blit(kGun, kIconSize, kIconSize, cell.x + GunX(q), cell.y + GunY(q),
                     iconRepeat, kContainedIcon);
        }
    }

    // Fades the panel in and out around changes in whether it's needed (any
    // group active), using the same linear ramp/blend as the zoom indicator
    // (Zoom::kIndicatorFadeMs, Zoom::Blend565). Unlike the indicator, there is
    // no hold timer: the panel is needed for as long as a group stays active,
    // and only ramps opacity across that need changing.
    class Fade
    {
    public:
        // Call once per frame with the live active-slot pattern. Returns the
        // opacity (0-16) to draw at. While fading out, active is already all
        // false, so slots() keeps returning the last pattern that had
        // anything active instead of blanking before the fade finishes.
        int update(const Slots& active, const Slots& house, const Slots& wheel, uint32_t tick,
                   const Counts* counts = nullptr, const Slots* transport = nullptr,
                   const Slots* gun = nullptr)
        {
            const bool needed = persistent_ ||
                std::any_of(active.begin(), active.end(), [](bool value) { return value; });
            if (needed)
            {
                lastActive_ = active;
                lastHouse_ = house;
                lastWheel_ = wheel;
                if (counts)
                    lastCounts_ = *counts;
                if (transport)
                    lastTransport_ = *transport;
                if (gun)
                    lastGun_ = *gun;
            }

            if (needed != wasNeeded_)
            {
                const int current = opacityAt(tick);
                const int ramp = needed ? current : 16 - current;
                changeTick_ = tick - ramp * Zoom::kIndicatorFadeMs / 16;
                wasNeeded_ = needed;
            }

            return opacityAt(tick);
        }

        int opacityAt(uint32_t tick) const
        {
            const uint32_t elapsed = tick - changeTick_;
            const int ramp = elapsed >= Zoom::kIndicatorFadeMs
                ? 16 : static_cast<int>(elapsed * 16 / Zoom::kIndicatorFadeMs);
            return wasNeeded_ ? ramp : 16 - ramp;
        }

        const Slots& slots() const { return lastActive_; }
        const Slots& houses() const { return lastHouse_; }
        const Slots& wheels() const { return lastWheel_; }
        const Slots& transports() const { return lastTransport_; }
        const Slots& guns() const { return lastGun_; }
        const Counts& counts() const { return lastCounts_; }
        void setPersistent(bool persistent) { persistent_ = persistent; }
        bool persistent() const { return persistent_; }

    private:
        Slots lastActive_{};
        Slots lastHouse_{};
        Slots lastWheel_{};
        Slots lastTransport_{};
        Slots lastGun_{};
        Counts lastCounts_{};
        bool wasNeeded_ = false;
        bool persistent_ = false;
        uint32_t changeTick_ = 0;
    };

    // Hex byte pattern with "??" wildcards, e.g. "8a511ab8??????84". Used to
    // reject a Game_Dll that is not the build these offsets were proved
    // against. Wildcards cover bytes holding a baked-in absolute address,
    // which moves with ASLR on every run.
    constexpr int SignatureLength(std::string_view pattern)
    {
        return static_cast<int>(pattern.size() / 2);
    }

    constexpr bool Matches(const uint8_t* memory, std::string_view pattern)
    {
        if (!memory || pattern.empty() || pattern.size() % 2 != 0)
            return false;

        const auto nibble = [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        for (size_t i = 0; i + 1 < pattern.size(); i += 2)
        {
            if (pattern[i] == '?' && pattern[i + 1] == '?')
                continue;

            const int hi = nibble(pattern[i]);
            const int lo = nibble(pattern[i + 1]);
            if (hi < 0 || lo < 0)
                return false;               // malformed pattern: fail closed
            if (memory[i / 2] != static_cast<uint8_t>(hi << 4 | lo))
                return false;
        }
        return true;
    }
}
