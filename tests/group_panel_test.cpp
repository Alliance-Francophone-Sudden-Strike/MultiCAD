#include "GroupPanel.h"
#include "GroupPanelTraits.h"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using namespace GroupPanel;

    // --- slot -> key mapping -------------------------------------------------
    // The panel reads 1..9 then 0, but the game's key table is ordered '0'
    // first and stores index+1. Group '1' is therefore stored as 2 and group
    // '0' as 1. This is the mapping that was verified against live memory.
    static_assert(KeyIndexForSlot(0) == 1);
    static_assert(KeyIndexForSlot(8) == 9);
    static_assert(KeyIndexForSlot(9) == 0);

    static_assert(StoredValueForSlot(0) == 2);    // key '1'
    static_assert(StoredValueForSlot(4) == 6);    // key '5'
    static_assert(StoredValueForSlot(8) == 10);   // key '9'
    static_assert(StoredValueForSlot(9) == 1);    // key '0'

    static_assert(LabelForSlot(0) == '1');
    static_assert(LabelForSlot(4) == '5');
    static_assert(LabelForSlot(5) == '6');
    static_assert(LabelForSlot(9) == '0');

    static_assert(VirtualKeyForSlot(0) == 0x31);
    static_assert(VirtualKeyForSlot(8) == 0x39);
    static_assert(VirtualKeyForSlot(9) == 0x30);

    // every slot maps to a distinct stored value covering exactly 1..10
    {
        std::array<bool, kCount + 1> seen{};
        for (int slot = 0; slot < kCount; ++slot)
        {
            const int value = StoredValueForSlot(slot);
            assert(value >= 1 && value <= kCount);
            assert(!seen[value]);
            seen[value] = true;
        }
        for (int value = 1; value <= kCount; ++value)
            assert(seen[value]);
    }

    // stored value -> slot is the exact inverse, and rejects junk
    static_assert(SlotForStoredValue(2) == 0);    // key '1'
    static_assert(SlotForStoredValue(10) == 8);   // key '9'
    static_assert(SlotForStoredValue(1) == 9);    // key '0'
    static_assert(SlotForStoredValue(0) == -1);   // no group
    static_assert(SlotForStoredValue(11) == -1);  // out of range
    static_assert(SlotForStoredValue(-1) == -1);
    for (int slot = 0; slot < kCount; ++slot)
        assert(SlotForStoredValue(StoredValueForSlot(slot)) == slot);

    // --- geometry ------------------------------------------------------------
    constexpr int screenWidth = 1024;
    constexpr int screenHeight = 768;
    static_assert(kRows * kColumns == kCount);
    assert(Fits(screenWidth, screenHeight));
    assert(!Fits(10, 10));

    for (int slot = 0; slot < kCount; ++slot)
    {
        const Zoom::Rect cell = CellRect(slot, screenWidth);
        assert(cell.width == kCell && cell.height == kCell);
        assert(cell.x >= 0 && cell.y >= 0);
        assert(cell.x + cell.width <= screenWidth - kMargin);   // stays inside the margin
        assert(cell.y >= kMargin);
    }

    // top-right anchored: the last column ends exactly one margin from the edge
    {
        const Zoom::Rect topRight = CellRect(kColumns - 1, screenWidth);
        assert(topRight.x + topRight.width == screenWidth - kMargin);
        const Zoom::Rect topLeft = CellRect(0, screenWidth);
        assert(topLeft.x == screenWidth - kMargin - Width());
        // all ten cells share one row
        assert(CellRect(kCount - 1, screenWidth).y == topLeft.y);
        // panel follows the right edge when the resolution changes
        assert(CellRect(0, 1920).x > CellRect(0, 1024).x);
    }

    // no two cells overlap
    for (int a = 0; a < kCount; ++a)
        for (int b = a + 1; b < kCount; ++b)
        {
            const Zoom::Rect ra = CellRect(a, screenWidth);
            const Zoom::Rect rb = CellRect(b, screenWidth);
            const bool disjoint =
                ra.x + ra.width <= rb.x || rb.x + rb.width <= ra.x ||
                ra.y + ra.height <= rb.y || rb.y + rb.height <= ra.y;
            assert(disjoint);
        }

    // --- hit testing ---------------------------------------------------------
    for (int slot = 0; slot < kCount; ++slot)
    {
        const Zoom::Rect cell = CellRect(slot, screenWidth);
        assert(HitTest(cell.x, cell.y, screenWidth) == slot);                       // top-left corner
        assert(HitTest(cell.x + cell.width / 2, cell.y + cell.height / 2, screenWidth) == slot);
        assert(HitTest(cell.x + cell.width - 1, cell.y + cell.height - 1, screenWidth) == slot);
        assert(HitTest(cell.x + cell.width, cell.y, screenWidth) != slot);          // just past the right edge
        assert(HitTest(cell.x, cell.y + cell.height, screenWidth) != slot);         // just past the bottom
    }
    assert(HitTest(0, 0, screenWidth) == -1);                     // top-left of the screen
    assert(HitTest(screenWidth - 1, 0, screenWidth) == -1);       // inside the margin
    assert(HitTest(screenWidth / 2, screenHeight / 2, screenWidth) == -1);
    // the gap between columns must not register as a hit
    {
        const Zoom::Rect first = CellRect(0, screenWidth);
        assert(HitTest(first.x + kCell + kGap / 2, first.y, screenWidth) == -1);
    }

    // --- 16-bit rendering ---------------------------------------------------
    {
        constexpr int width = 360;
        constexpr int height = 80;
        constexpr int pitch = width + 4;
        constexpr uint16_t untouched = 0xABCD;
        std::array<uint16_t, pitch * height> pixels{};
        pixels.fill(untouched);
        std::array<bool, kCount> active{};

        Draw16(pixels.data(), pitch, width, height, active);
        for (uint16_t pixel : pixels)
            assert(pixel == untouched); // no groups: no panel

        active[0] = true;
        active[9] = true;

        Draw16(pixels.data(), pitch, width, height, active);

        const Zoom::Rect one = CellRect(0, width);
        const Zoom::Rect two = CellRect(1, width);
        const Zoom::Rect zero = CellRect(9, width);
        assert(pixels[one.y * pitch + one.x] == kActiveBorder);
        assert(pixels[(one.y + 1) * pitch + one.x + 1] == kActiveFill);
        assert(pixels[(one.y + 6) * pitch + one.x + 12] == kActiveText); // top of "1"
        assert(pixels[two.y * pitch + two.x] == kInactiveBorder);
        assert(pixels[(two.y + 1) * pitch + two.x + 1] == kInactiveFill);
        assert(pixels[(two.y + 6) * pitch + two.x + 9] == kInactiveText); // top of "2"
        assert(pixels[zero.y * pitch + zero.x] == kActiveBorder);
        assert(pixels[0] == untouched);
        assert(pixels[(height - 1) * pitch + width] == untouched); // row padding

        std::array<uint16_t, 16> tooSmall{};
        tooSmall.fill(untouched);
        Draw16(tooSmall.data(), 4, 4, 4, active);
        for (uint16_t pixel : tooSmall)
            assert(pixel == untouched);
    }

    // --- signature matching --------------------------------------------------
    {
        const std::array<uint8_t, 6> code{ 0x8a, 0x51, 0x1a, 0xb8, 0x01, 0x00 };
        assert(Matches(code.data(), "8a511ab80100"));
        assert(Matches(code.data(), "8a511a??0100"));      // wildcard over one byte
        assert(Matches(code.data(), "8a51"));              // prefix only
        assert(!Matches(code.data(), "8a511ab80101"));     // one byte differs
        assert(!Matches(code.data(), "8a511ab801000"));    // odd length -> malformed
        assert(!Matches(code.data(), "zz511ab80100"));     // bad hex -> fail closed
        assert(!Matches(code.data(), ""));
        assert(!Matches(nullptr, "8a51"));
        assert(SignatureLength("8a511a??") == 4);
    }

    // --- the SS_2 binding ----------------------------------------------------
    // The signature patterns are hand-transcribed from disassembly, so a typo
    // would silently disable the panel rather than fail loudly. Check each one
    // is well formed and that the matcher round-trips it.
    {
        assert(TryGetGroupPanelAddresses(GameVersion::SS_2) != nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::HS_2) != nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_V1_0) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_RW_V2_3) == nullptr);

        const GroupPanelAddresses& bound = *TryGetGroupPanelAddresses(GameVersion::SS_2);
        assert(TryGetGroupPanelAddresses(GameVersion::HS_2) == &bound);
        assert(bound.unitGroupOffset == 0x44);
        assert(bound.unitNextOffset == 0xA);

        for (const GroupPanelSignature& signature : bound.signatures)
        {
            assert(signature.rva != 0);
            assert(!signature.pattern.empty());
            assert(signature.pattern.size() % 2 == 0);   // whole bytes only

            // Build the bytes this pattern describes, then match them back.
            std::array<uint8_t, 128> bytes{};
            const int length = SignatureLength(signature.pattern);
            assert(length > 0 && length <= static_cast<int>(bytes.size()));

            int fixed = 0;
            for (int i = 0; i < length; ++i)
            {
                const char hi = signature.pattern[static_cast<size_t>(i) * 2];
                const char lo = signature.pattern[static_cast<size_t>(i) * 2 + 1];
                if (hi == '?' && lo == '?')
                {
                    bytes[static_cast<size_t>(i)] = 0xAB;   // wildcard: anything goes
                    continue;
                }
                const auto digit = [](char c)
                {
                    assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
                    return c <= '9' ? c - '0' : c - 'a' + 10;
                };
                bytes[static_cast<size_t>(i)] = static_cast<uint8_t>(digit(hi) << 4 | digit(lo));
                ++fixed;
            }

            assert(fixed > 0);                                   // all-wildcard proves nothing
            assert(Matches(bytes.data(), signature.pattern));     // round-trips

            // a single changed fixed byte must reject
            for (int i = 0; i < length; ++i)
            {
                if (signature.pattern[static_cast<size_t>(i) * 2] == '?')
                    continue;
                const uint8_t original = bytes[static_cast<size_t>(i)];
                bytes[static_cast<size_t>(i)] = static_cast<uint8_t>(original ^ 0xFF);
                assert(!Matches(bytes.data(), signature.pattern));
                bytes[static_cast<size_t>(i)] = original;
                break;
            }
        }
    }

    return 0;
}
