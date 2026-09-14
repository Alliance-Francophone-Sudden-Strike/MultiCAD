#pragma once

// Control-group panel: pure geometry, slot mapping and signature matching.
// Deliberately free of Windows and game dependencies so the host compiler can
// test it (see tests/group_panel_test.cpp). Live state stays in the patcher.

#include "Zoom.h"   // Rect

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

    constexpr int kCell = 27;
    constexpr int kGap = 4;
    constexpr int kMargin = 12;

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
        const std::array<bool, kCount>& active)
    {
        if (!destination || pitch < width || !Fits(width, height) ||
            std::none_of(active.begin(), active.end(), [](bool value) { return value; }))
            return;

        constexpr int glyphScale = 3;
        constexpr int glyphX = (kCell - 3 * glyphScale) / 2;
        constexpr int glyphY = (kCell - 5 * glyphScale) / 2;

        for (int slot = 0; slot < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, width);
            const uint16_t border = active[slot] ? kActiveBorder : kInactiveBorder;
            const uint16_t fill = active[slot] ? kActiveFill : kInactiveFill;
            const uint16_t text = active[slot] ? kActiveText : kInactiveText;

            for (int y = 0; y < kCell; ++y)
                for (int x = 0; x < kCell; ++x)
                    destination[(cell.y + y) * pitch + cell.x + x] =
                        x == 0 || y == 0 || x == kCell - 1 || y == kCell - 1 ? border : fill;

            const int digit = LabelForSlot(slot) - '0';
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 3; ++x)
                    if (kDigits[digit][y] & (1 << (2 - x)))
                        for (int dy = 0; dy < glyphScale; ++dy)
                            for (int dx = 0; dx < glyphScale; ++dx)
                                destination[(cell.y + glyphY + y * glyphScale + dy) * pitch +
                                            cell.x + glyphX + x * glyphScale + dx] = text;
        }
    }

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
