#pragma once

// Control-group panel: pure geometry, slot mapping and signature matching.
// Deliberately free of Windows and game dependencies so the host compiler can
// test it (see tests/group_panel_test.cpp). Live state stays in the patcher.

#include "Zoom.h"           // Rect, kIndicatorFadeMs
#include "ZoomIndicator.h"  // Blend565

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace GroupPanel
{
    // Ten groups drawn on one row: "1".."9","0".
    constexpr int kCount = 10;
    constexpr int kColumns = kCount;
    constexpr int kRows = 1;

    using Slots = std::array<bool, kCount>;

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

    // 19% smaller than the original 27px cell / 4px gap.
    constexpr int kCell = 22;
    constexpr int kGap = 3;
    constexpr int kMargin = 12;

    constexpr int glyphScale = 2;
    constexpr int glyphX = (kCell - 3 * glyphScale) / 2;
    constexpr int glyphY = (kCell - 5 * glyphScale) / 2;

    constexpr int Width() { return kColumns * kCell + (kColumns - 1) * kGap; }
    constexpr int Height() { return kRows * kCell + (kRows - 1) * kGap; }

    // Anchored to the top-right corner, in screen coordinates.
    constexpr Zoom::Rect CellRect(int slot, int screenWidth)
    {
        const int column = slot % kColumns;
        const int row = slot / kColumns;
        const int left = screenWidth - kMargin - Width();
        return { left + column * (kCell + kGap),
                 kMargin + row * (kCell + kGap),
                 kCell, kCell };
    }

    // Slot under the point, or -1 when the point misses the panel.
    constexpr int HitTest(int x, int y, int screenWidth)
    {
        for (int slot = 0; slot < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, screenWidth);
            if (x >= cell.x && x < cell.x + cell.width &&
                y >= cell.y && y < cell.y + cell.height)
                return slot;
        }
        return -1;
    }

    constexpr bool Fits(int screenWidth, int screenHeight)
    {
        return screenWidth >= Width() + 2 * kMargin &&
               screenHeight >= Height() + 2 * kMargin;
    }

    constexpr uint16_t kActiveBorder = 0xFFFF;
    constexpr uint16_t kActiveFill = 0x2104;
    constexpr uint16_t kActiveText = 0xFFFF;
    constexpr uint16_t kInactiveBorder = 0x4208;
    constexpr uint16_t kInactiveFill = 0x1082;
    constexpr uint16_t kInactiveText = 0x8410;

    constexpr int kIconSize = 5;
    constexpr int kIconInset = 2;
    constexpr int kWheelX = kIconInset;
    constexpr int kHouseX = kCell - kIconInset - kIconSize;
    constexpr int kIconY = kIconInset;
    constexpr uint16_t kContainedIcon = 0xFFE0;

    static_assert(kWheelX >= 2 && kWheelX + kIconSize <= glyphX);
    static_assert(kHouseX >= glyphX + 3 * glyphScale && kHouseX + kIconSize <= kCell - 2);
    static_assert(kIconY >= 2 && kIconY + kIconSize <= kCell - 2);

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
        int opacity = 16)
    {
        if (!destination || pitch < width || !Fits(width, height) || opacity <= 0 ||
            std::none_of(active.begin(), active.end(), [](bool value) { return value; }))
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

        for (int slot = 0; slot < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, width);
            const uint16_t border = active[slot] ? kActiveBorder : kInactiveBorder;
            const uint16_t fill = active[slot] ? kActiveFill : kInactiveFill;
            const uint16_t text = active[slot] ? kActiveText : kInactiveText;

            for (int y = 0; y < kCell; ++y)
                for (int x = 0; x < kCell; ++x)
                    plot(cell.x + x, cell.y + y,
                         x == 0 || y == 0 || x == kCell - 1 || y == kCell - 1 ? border : fill);

            blit(kDigits[LabelForSlot(slot) - '0'], 3, 5,
                 cell.x + glyphX, cell.y + glyphY, glyphScale, text);

            if (house[slot])
                blit(kHouse, kIconSize, kIconSize, cell.x + kHouseX, cell.y + kIconY, 1, kContainedIcon);
            if (wheel[slot])
                blit(kWheel, kIconSize, kIconSize, cell.x + kWheelX, cell.y + kIconY, 1, kContainedIcon);
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
        int update(const Slots& active, const Slots& house, const Slots& wheel, uint32_t tick)
        {
            const bool needed = std::any_of(active.begin(), active.end(), [](bool value) { return value; });
            if (needed)
            {
                lastActive_ = active;
                lastHouse_ = house;
                lastWheel_ = wheel;
            }

            if (needed != wasNeeded_)
            {
                changeTick_ = tick;
                wasNeeded_ = needed;
            }

            const uint32_t elapsed = tick - changeTick_;
            const int ramp = elapsed >= Zoom::kIndicatorFadeMs
                ? 16 : static_cast<int>(elapsed * 16 / Zoom::kIndicatorFadeMs);
            return needed ? ramp : 16 - ramp;
        }

        const Slots& slots() const { return lastActive_; }
        const Slots& houses() const { return lastHouse_; }
        const Slots& wheels() const { return lastWheel_; }

    private:
        Slots lastActive_{};
        Slots lastHouse_{};
        Slots lastWheel_{};
        bool wasNeeded_ = false;
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
