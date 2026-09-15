#include "GroupPanel.h"
#include "GroupPanelTraits.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>

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
        Slots active{};
        Slots house{};
        Slots wheel{};

        Draw16(pixels.data(), pitch, width, height, active, house, wheel);
        for (uint16_t pixel : pixels)
            assert(pixel == untouched); // no groups: no panel

        active[0] = true;
        active[9] = true;

        Draw16(pixels.data(), pitch, width, height, active, house, wheel);

        const Zoom::Rect one = CellRect(0, width);
        const Zoom::Rect two = CellRect(1, width);
        const Zoom::Rect zero = CellRect(9, width);
        assert(pixels[one.y * pitch + one.x] == kActiveBorder);
        assert(pixels[(one.y + 1) * pitch + one.x + 1] == kActiveFill);
        assert(pixels[(one.y + glyphY) * pitch + one.x + glyphX + glyphScale] == kActiveText); // top of "1"
        assert(pixels[two.y * pitch + two.x] == kInactiveBorder);
        assert(pixels[(two.y + 1) * pitch + two.x + 1] == kInactiveFill);
        assert(pixels[(two.y + glyphY) * pitch + two.x + glyphX] == kInactiveText); // top of "2"
        assert(pixels[zero.y * pitch + zero.x] == kActiveBorder);
        assert(pixels[0] == untouched);
        assert(pixels[(height - 1) * pitch + width] == untouched); // row padding

        // --- container badges ------------------------------------------------
        const int badgeY = one.y + kIconY;
        const int roofX = one.x + kHouseX + 2;
        const int rimX = one.x + kWheelX + 2;
        assert(pixels[badgeY * pitch + roofX] == kActiveFill);
        assert(pixels[badgeY * pitch + rimX] == kActiveFill);

        house[0] = true;
        wheel[0] = true;
        Draw16(pixels.data(), pitch, width, height, active, house, wheel);

        assert(pixels[badgeY * pitch + roofX] == kContainedIcon);
        assert(pixels[badgeY * pitch + rimX] == kContainedIcon);
        assert(pixels[one.y * pitch + one.x] == kActiveBorder);
        assert(pixels[(one.y + glyphY) * pitch + one.x + glyphX + glyphScale] == kActiveText);
        assert(pixels[(zero.y + kIconY) * pitch + zero.x + kHouseX + 2] == kActiveFill);
        assert(pixels[(zero.y + kIconY) * pitch + zero.x + kWheelX + 2] == kActiveFill);

        house[0] = false;
        wheel[0] = false;

        std::array<uint16_t, 16> tooSmall{};
        tooSmall.fill(untouched);
        Draw16(tooSmall.data(), 4, 4, 4, active, house, wheel);
        for (uint16_t pixel : tooSmall)
            assert(pixel == untouched);
    }

    // --- fade in/out, same ramp as the zoom indicator -------------------------
    {
        constexpr int width = 360;
        constexpr int height = 80;
        constexpr int pitch = width + 4;
        constexpr uint16_t untouched = 0xABCD;
        std::array<uint16_t, pitch * height> pixels{};
        pixels.fill(untouched);
        Slots active{};
        Slots house{};
        Slots wheel{};
        active[0] = true;
        house[0] = true;

        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 0);
        for (uint16_t pixel : pixels)
            assert(pixel == untouched); // zero opacity: nothing drawn

        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 8);
        const Zoom::Rect one = CellRect(0, width);
        const uint16_t halfBlended = pixels[one.y * pitch + one.x];
        assert(halfBlended != untouched && halfBlended != kActiveBorder); // blended, not snapped
        const uint16_t halfRoof = pixels[(one.y + kIconY) * pitch + one.x + kHouseX + 2];
        assert(halfRoof != untouched && halfRoof != kContainedIcon);

        Fade fade;
        Slots none{};
        constexpr uint32_t fadeMs = Zoom::kIndicatorFadeMs;
        constexpr uint32_t base = 2000;

        assert(fade.update(none, none, none, 1000) == 0); // never needed: stays hidden

        assert(fade.update(active, house, none, base) == 0); // just became needed: fades in from 0
        const int fadingIn = fade.update(active, house, none, base + fadeMs / 2);
        assert(fadingIn > 0 && fadingIn < 16);
        assert(fade.update(active, house, none, base + fadeMs) == 16); // fully faded in
        assert(fade.slots()[0]);

        assert(fade.update(none, none, none, base + fadeMs) == 16); // just stopped being needed: still full
        const int fadingOut = fade.update(none, none, none, base + fadeMs + fadeMs / 2);
        assert(fadingOut > 0 && fadingOut < 16);
        assert(fade.update(none, none, none, base + 2 * fadeMs) == 0); // fully faded out
        assert(fade.slots()[0]); // keeps the last active pattern while fading
        assert(fade.houses()[0]);
        assert(!fade.wheels()[0]);
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
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_EN) ==
               TryGetGroupPanelAddresses(GameVersion::SS_GOLD_HD_1_2_INT));
        assert(TryGetGroupPanelAddresses(GameVersion::SS_RW_V2_3) ==
               TryGetGroupPanelAddresses(GameVersion::SS_RW_V2_4));
        assert(TryGetGroupPanelAddresses(GameVersion::SS_RW_V2_4)->unitAliveVtableOffset == 0x38);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_HD_1_2_INT)->unitAliveVtableOffset == 0x74);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_HD_1_2_INT)->unitGroupVtableOffset == 0xC);

        const GroupPanelAddresses& bound = *TryGetGroupPanelAddresses(GameVersion::SS_2);
        assert(TryGetGroupPanelAddresses(GameVersion::HS_2) == &bound);
        assert(bound.unitAliveVtableOffset == 0x34);
        assert(bound.unitGroupVtableOffset == 0x1C);
        assert(bound.unitNextOffset == 0xA);

        // The reader mirrors fnGroupSelect's two virtual calls, so its
        // signature has to pin the offsets the reader uses: call [edx+0x34]
        // then call [eax+0x1c]. Changing one without the other would silently
        // call the wrong slot on every unit in the game.
        const std::string_view select = bound.signatures[0].pattern;
        assert(select.find("ff5234") != std::string_view::npos);
        assert(select.find("ff501c") != std::string_view::npos);

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

    // --- the group byte offset must be pinned by a signature -----------------
    {
        const GameVersion families[]
        {
            GameVersion::SS_2,
            GameVersion::SS_RW_V2_4,
            GameVersion::SS_GOLD_HD_1_2_INT,
        };

        for (GameVersion version : families)
        {
            const GroupPanelAddresses* addresses = TryGetGroupPanelAddresses(version);
            assert(addresses != nullptr);
            assert(addresses->unitGroupOffset < 0x80);   // has to encode as a disp8

            char writes[8]{};
            std::snprintf(writes, sizeof(writes), "884e%02x",
                          static_cast<unsigned>(addresses->unitGroupOffset));

            bool pinned = false;
            for (const GroupPanelSignature& signature : addresses->signatures)
                if (signature.pattern.find(writes) != std::string_view::npos)
                    pinned = true;
            assert(pinned);
        }
    }

    // --- unit counts ---------------------------------------------------------
    static_assert(CountDigits(1) == 1);
    static_assert(CountDigits(9) == 1);
    static_assert(CountDigits(10) == 2);
    static_assert(CountDigits(99) == 2);
    static_assert(CountDigits(100) == 3);
    static_assert(CountDigits(999) == 3);
    static_assert(CountDigits(1000) == 4);
    static_assert(CountDigits(kCountMax) == kCountMaxDigits);
    static_assert(CountWidth(1) == kCountDigitWidth * kCountScale);
    static_assert(CountWidth(2) == 2 * kCountDigitWidth * kCountScale + kCountDigitGap);
    static_assert(CountLeft(9) > CountLeft(99));
    static_assert(CountLeft(99) > CountLeft(999));
    static_assert(CountLeft(999) > CountLeft(1000));
    static_assert(kCountTop > kCell);
    static_assert(Height() == kCell + CountStripHeight());

    for (int value : { 1, 9, 10, 99, 100, 999, 1000, kCountMax })
    {
        const int digits = CountDigits(value);
        assert(CountLeft(value) - kCountOutline >= 0);
        assert(CountLeft(value) + CountWidth(digits) + kCountOutline <= kCell);
        assert(CountWidth(digits) <= kCell - 2 * kCountOutline);
    }

    {
        constexpr int width = 360;
        constexpr int height = 80;
        constexpr int pitch = width + 4;
        constexpr uint16_t untouched = 0xABCD;
        std::array<uint16_t, pitch * height> pixels{};
        Slots active{};
        Slots house{};
        Slots wheel{};
        Counts counts{};

        active[0] = true;
        counts[0] = 3;

        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, nullptr);

        const Zoom::Rect one = CellRect(0, width);
        assert(pixels[(one.y + glyphY) * pitch + one.x + glyphX + glyphScale] == kActiveText);
        for (int y = 0; y < CountStripHeight(); ++y)
            for (int x = 0; x < kCell; ++x)
                assert(pixels[(one.y + kCell + y) * pitch + one.x + x] == untouched);

        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);

        assert(pixels[(one.y + glyphY) * pitch + one.x + glyphX + glyphScale] == kActiveText);
        const int topRow = one.y + kCountTop;
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + one.x + CountLeft(3) + x] == kCountText);

        for (int x = -kCountOutline; x < kCountDigitWidth * kCountScale + kCountOutline; ++x)
            assert(pixels[(topRow - kCountOutline) * pitch + one.x + CountLeft(3) + x] == kCountPlate);

        assert(pixels[(one.y + kCell) * pitch + one.x + kCell / 2] == untouched);

        counts[0] = 12;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        const int secondDigitX =
            one.x + CountLeft(12) + kCountDigitWidth * kCountScale + kCountDigitGap;
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + secondDigitX + x] == kCountText);
        assert(CountLeft(12) + CountWidth(2) + kCountOutline <= kCell);

        counts[0] = 1000;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        const int fourthDigitX =
            one.x + CountLeft(1000) + 3 * (kCountDigitWidth * kCountScale + kCountDigitGap);
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + fourthDigitX + x] == kCountText);

        counts[0] = 50000;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + one.x + CountLeft(kCountMax) + x] == kCountText);

        counts[1] = 7;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        const Zoom::Rect two = CellRect(1, width);
        assert(!active[1]);
        for (int y = 0; y < CountStripHeight(); ++y)
            for (int x = 0; x < kCell; ++x)
                assert(pixels[(two.y + kCell + y) * pitch + two.x + x] == untouched);

        Slots all{};
        Counts big{};
        for (int slot = 0; slot < kCount; ++slot)
        {
            all[slot] = true;
            big[slot] = 99;
        }
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, all, house, wheel, 16, &big);
        for (int slot = 0; slot + 1 < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, width);
            for (int gap = 0; gap < kGap; ++gap)
                for (int y = 0; y < kCell + CountStripHeight(); ++y)
                    assert(pixels[(cell.y + y) * pitch + cell.x + kCell + gap] == untouched);
        }

        const Zoom::Rect last = CellRect(kCount - 1, width);
        for (int y = 0; y < kCell + CountStripHeight(); ++y)
            assert(pixels[(last.y + y) * pitch + last.x + kCell] == untouched);
        for (int x = 0; x < kCell; ++x)
            assert(pixels[(one.y + Height()) * pitch + one.x + x] == untouched);
    }

    // --- the fade carries counts through the fade-out ------------------------
    {
        Fade fade;
        Slots active{};
        Slots none{};
        Counts counts{};
        active[2] = true;
        counts[2] = 4;

        fade.update(active, none, none, 1000, &counts);
        assert(fade.counts()[2] == 4);

        Counts empty{};
        fade.update(none, none, none, 1000, &empty);
        assert(fade.counts()[2] == 4);

        fade.update(active, none, none, 1000);
        assert(fade.counts()[2] == 4);
    }

    // --- assign is reachable through the runtime lookup ----------------------
    {
        const GameVersion bound[]
        {
            GameVersion::SS_2,
            GameVersion::SS_RW_V2_4,
            GameVersion::SS_GOLD_HD_1_2_INT,
        };

        for (GameVersion version : bound)
        {
            const GroupPanelAddresses* addresses = TryGetGroupPanelAddresses(version);
            assert(addresses != nullptr);
            assert(addresses->fnGroupAssign != 0);
            assert(addresses->fnGroupAssign != addresses->fnGroupSelect);

            bool pinned = false;
            for (const GroupPanelSignature& signature : addresses->signatures)
                if (signature.rva == addresses->fnGroupAssign)
                    pinned = true;
            assert(pinned);
        }
    }

    // --- profiles the panel is deliberately not bound against ---------------
    {
        assert(TryGetGroupPanelAddresses(GameVersion::SS_BLACK_GOLD) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_DE) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_FR) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_V1_0) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_V1_2) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_RU) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_HD_V1_1_RU) == nullptr);
    }

    return 0;
}
