#include "GroupPanel.h"
#include "GroupPanelTraits.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

namespace
{
    // Group predicates exactly as they sit in the SS2 v2.2 Game_Dll, read from
    // an unpacked dump. Absolute addresses are those of the dump.

    // vehicle: rva 0x48ef0, container at +0x6d
    constexpr std::array<uint8_t, 47> kSs2Vehicle
    {
        0x8b, 0x44, 0x24, 0x04, 0x33, 0xd2, 0x8a, 0x51, 0x44, 0x3b, 0xd0, 0x74, 0x1a, 0x8b, 0x54, 0x24,
        0x08, 0x85, 0xd2, 0x74, 0x0d, 0x50, 0x83, 0xc1, 0x6d, 0xe8, 0x42, 0x8a, 0xfb, 0xff, 0x85, 0xc0,
        0x75, 0x05, 0x33, 0xc0, 0xc2, 0x08, 0x00, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc2, 0x08, 0x00,
    };

    // gun: rva 0x28b60, two seats 0x17 apart, seat 0's group byte at +0xfa
    constexpr std::array<uint8_t, 69> kSs2Gun
    {
        0x33, 0xc0, 0x56, 0x8a, 0x41, 0x44, 0x8b, 0x74, 0x24, 0x08, 0x3b, 0xc6, 0x75, 0x09, 0xb8, 0x01,
        0x00, 0x00, 0x00, 0x5e, 0xc2, 0x08, 0x00, 0x8b, 0x44, 0x24, 0x0c, 0x85, 0xc0, 0x74, 0x20, 0x33,
        0xd2, 0x8d, 0x81, 0xfa, 0x00, 0x00, 0x00, 0x8b, 0x48, 0xf2, 0x85, 0xc9, 0x74, 0x08, 0x33, 0xc9,
        0x8a, 0x08, 0x3b, 0xce, 0x74, 0xd8, 0x42, 0x83, 0xc0, 0x17, 0x83, 0xfa, 0x02, 0x7c, 0xe8, 0x33,
        0xc0, 0x5e, 0xc2, 0x08, 0x00,
    };

    // building: rva 0x3da20, type dword at +0x26e, seats 0x15 apart from +0xaa
    constexpr std::array<uint8_t, 105> kSs2Building
    {
        0x56, 0x33, 0xc0, 0x8a, 0x41, 0x44, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x3b, 0xc7, 0x75, 0x0a, 0x5f,
        0xb8, 0x01, 0x00, 0x00, 0x00, 0x5e, 0xc2, 0x08, 0x00, 0x8b, 0x81, 0x6e, 0x02, 0x00, 0x00, 0x33,
        0xf6, 0x8d, 0x14, 0x80, 0x8d, 0x04, 0x50, 0x8b, 0x04, 0x45, 0x2c, 0x49, 0x89, 0x10, 0x8d, 0x14,
        0xc0, 0x8d, 0x14, 0x50, 0x8d, 0x14, 0x52, 0x8d, 0x04, 0xd0, 0x8b, 0x14, 0x85, 0xad, 0xa3, 0x89,
        0x10, 0x85, 0xd2, 0x7e, 0x1d, 0x8d, 0x81, 0xaa, 0x00, 0x00, 0x00, 0x8b, 0x48, 0xf2, 0x85, 0xc9,
        0x74, 0x08, 0x33, 0xc9, 0x8a, 0x08, 0x3b, 0xcf, 0x74, 0xb5, 0x46, 0x83, 0xc0, 0x15, 0x3b, 0xf2,
        0x7c, 0xe9, 0x5f, 0x33, 0xc0, 0x5e, 0xc2, 0x08, 0x00,
    };

    // The Gold gun predicate: the SS2 bytes with `push esi` moved after the
    // group-byte load, which is the only difference between the two patterns.
    constexpr std::array<uint8_t, 69> GoldGun()
    {
        std::array<uint8_t, 69> code = kSs2Gun;
        code[2] = 0x8a;
        code[3] = 0x41;
        code[4] = 0x44;
        code[5] = 0x56;
        return code;
    }
    constexpr std::array<uint8_t, 69> kGoldGun = GoldGun();

    // Writes one occupant record, as the game lays it out.
    template<size_t N>
    void PutRecord(std::array<uint8_t, N>& object, size_t at, bool occupied, int slot)
    {
        object[at] = occupied ? 0x5a : 0;       // any non-zero occupancy dword
        object[at + GroupPanel::kOccupantGroupOffset] =
            slot < 0 ? 0 : GroupPanel::StoredValueForSlot(slot);
    }

    template<size_t N>
    void PutSeat(std::array<uint8_t, N>& object, const GroupPanel::Seats& seats, int index,
                 bool occupied, int slot)
    {
        PutRecord(object, static_cast<size_t>(GroupPanel::SeatRecords(seats)) +
                          static_cast<size_t>(index) * seats.stride, occupied, slot);
    }

    // The same tally GroupPanelReader::refresh keeps for one list entry.
    void CountSeats(const uint8_t* object, const GroupPanel::Seats& seats, int count,
                    GroupPanel::Counts& counts)
    {
        const uint8_t* records = object + GroupPanel::SeatRecords(seats);
        for (int i = 0; i < count; ++i)
        {
            const int slot = GroupPanel::OccupantSlot(records + static_cast<size_t>(i) * seats.stride, true);
            if (slot >= 0)
                ++counts[static_cast<size_t>(slot)];
        }
    }

    void CountList(const uint8_t* records, int count, GroupPanel::Counts& counts)
    {
        for (int i = 0; i < count; ++i)
        {
            const int slot = GroupPanel::OccupantSlot(
                records + static_cast<size_t>(i) * GroupPanel::kVehicleRecordSize, false);
            if (slot >= 0)
                ++counts[static_cast<size_t>(slot)];
        }
    }

    void CountEntry(int ownSlot, const GroupPanel::Slots& predicate, bool occupantsCounted,
                    GroupPanel::Counts& counts)
    {
        for (int slot = 0; slot < GroupPanel::kCount; ++slot)
        {
            const bool own = slot == ownSlot;
            if ((own || predicate[static_cast<size_t>(slot)]) &&
                GroupPanel::CountsItself(own, occupantsCounted))
                ++counts[static_cast<size_t>(slot)];
        }
    }

    GroupPanel::Slots Only(int slot)
    {
        GroupPanel::Slots slots{};
        slots[static_cast<size_t>(slot)] = true;
        return slots;
    }
}

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

    static_assert(AltOnly(true, false, false, false));
    static_assert(AltOnly(true, true, false, true));
    static_assert(!AltOnly(true, true, false, false));
    static_assert(!AltOnly(true, false, true, false));
    static_assert(!AltOnly(false, false, false, true));

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

    static_assert(PanelScale::Quarters(0.f) == PanelScale::kMinQuarters);
    static_assert(PanelScale::Quarters(-1.f) == PanelScale::kMinQuarters);
    static_assert(PanelScale::Quarters(1.f) == 4);
    static_assert(PanelScale::Quarters(1.1f) == 4);
    static_assert(PanelScale::Quarters(1.125f) == 5);
    static_assert(PanelScale::Quarters(1.2f) == 5);
    static_assert(PanelScale::Quarters(2.f) == 8);
    static_assert(PanelScale::Quarters(2.99f) == 12);
    static_assert(PanelScale::Quarters(3.f) == PanelScale::kMaxQuarters);
    static_assert(PanelScale::Quarters(99.f) == PanelScale::kMaxQuarters);
    assert(PanelScale::Quarters(std::numeric_limits<float>::quiet_NaN()) == PanelScale::kMinQuarters);

    static_assert(Width(PanelScale::kMinQuarters) == 247);
    static_assert(Height(PanelScale::kMinQuarters) == 31);

    for (int q = PanelScale::kMinQuarters; q <= PanelScale::kMaxQuarters; ++q)
    {
        assert(Fits(screenWidth, screenHeight, q));
        if (q > PanelScale::kMinQuarters)
        {
            assert(Width(q) > Width(q - 1));
            assert(Height(q) > Height(q - 1));
        }

        const Zoom::Rect first = CellRect(0, screenWidth, q);
        const Zoom::Rect last = CellRect(kCount - 1, screenWidth, q);
        assert(first.x == screenWidth - kMargin - Width(q));
        assert(last.x + last.width == screenWidth - kMargin);
        assert(first.width == Cell(q) && first.height == Cell(q));

        for (int a = 0; a < kCount; ++a)
            for (int b = a + 1; b < kCount; ++b)
            {
                const Zoom::Rect ra = CellRect(a, screenWidth, q);
                const Zoom::Rect rb = CellRect(b, screenWidth, q);
                assert(ra.x + ra.width <= rb.x || rb.x + rb.width <= ra.x);
            }

        for (int slot = 0; slot < kCount; ++slot)
        {
            const Zoom::Rect cell = CellRect(slot, screenWidth, q);
            assert(HitTest(cell.x, cell.y, screenWidth, q) == slot);
            assert(HitTest(cell.x + cell.width - 1, cell.y, screenWidth, q) == slot);
            assert(HitTest(cell.x, cell.y + cell.height - 1, screenWidth, q) == slot);
            assert(HitTest(cell.x + cell.width - 1, cell.y + cell.height - 1, screenWidth, q) == slot);
            assert(HitTest(cell.x - 1, cell.y, screenWidth, q) != slot);
            assert(HitTest(cell.x + cell.width, cell.y, screenWidth, q) != slot);
            assert(HitTest(cell.x, cell.y - 1, screenWidth, q) == -1);
            assert(HitTest(cell.x, cell.y + cell.height, screenWidth, q) == -1);
            if (slot + 1 < kCount)
                assert(HitTest(cell.x + cell.width + Gap(q) / 2, cell.y, screenWidth, q) == -1);
        }
    }

    {
        SetScale(8);
        assert(ScaleQuarters() == 8);

        constexpr int width = 640;
        constexpr int height = 140;
        constexpr int pitch = width + 4;
        constexpr uint16_t untouched = 0xABCD;
        static std::array<uint16_t, pitch * height> pixels{};
        pixels.fill(untouched);

        Slots active{};
        Slots house{};
        Slots wheel{};
        Slots transport{};
        Slots gun{};
        Counts counts{};
        for (int slot = 0; slot < kCount; ++slot)
        {
            active[slot] = true;
            house[slot] = true;
            wheel[slot] = true;
            transport[slot] = true;
            gun[slot] = true;
            counts[slot] = 9999;
        }

        assert(Fits(width, height));
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts, &transport,
               false, nullptr, &gun);

        int minX = width, maxX = -1, minY = height, maxY = -1;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (pixels[y * pitch + x] != untouched)
                {
                    minX = minX < x ? minX : x;
                    maxX = maxX > x ? maxX : x;
                    minY = minY < y ? minY : y;
                    maxY = maxY > y ? maxY : y;
                }

        const Zoom::Rect first = CellRect(0, width);
        assert(minX == first.x);
        assert(maxX == first.x + Width() - 1);
        assert(minY == first.y);
        assert(maxY == first.y + Height() - 1);
        assert(HitTest(minX, minY, width) == 0);
        assert(HitTest(maxX, minY, width) == kCount - 1);
        assert(HitTest(minX - 1, minY, width) == -1);

        SetScale(PanelScale::kMinQuarters);
        assert(Width() == 247 && Height() == 31);
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
        Slots transport{};
        Slots gun{};

        Draw16(pixels.data(), pitch, width, height, active, house, wheel);
        for (uint16_t pixel : pixels)
            assert(pixel == untouched); // no groups: no panel

        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, nullptr, nullptr, true);
        const Zoom::Rect emptyCell = CellRect(0, width);
        assert(pixels[emptyCell.y * pitch + emptyCell.x] == kInactiveBorder); // persistent: dim panel with no groups
        pixels.fill(untouched);

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
        const int wheelY = one.y + kWheelY;
        const int gunX = one.x + kGunX + 3;
        const int gunY = one.y + kGunY + 1;
        assert(pixels[badgeY * pitch + roofX] == kActiveFill);
        assert(pixels[badgeY * pitch + rimX] == kActiveFill);
        assert(pixels[wheelY * pitch + rimX] == kActiveFill);
        assert(pixels[gunY * pitch + gunX] == kActiveFill);

        house[0] = true;
        wheel[0] = true;
        transport[0] = true;
        gun[0] = true;
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, nullptr, &transport,
               false, nullptr, &gun);

        assert(pixels[badgeY * pitch + roofX] == kContainedIcon);
        assert(pixels[badgeY * pitch + rimX] == kContainedIcon); // transport border
        assert(pixels[(badgeY + 2) * pitch + rimX] == kTransportFill);
        assert(pixels[wheelY * pitch + rimX] == kContainedIcon);
        assert(pixels[gunY * pitch + gunX] == kContainedIcon);
        assert(pixels[one.y * pitch + one.x] == kActiveBorder);
        assert(pixels[(one.y + glyphY) * pitch + one.x + glyphX + glyphScale] == kActiveText);
        assert(pixels[(zero.y + kIconY) * pitch + zero.x + kHouseX + 2] == kActiveFill);
        assert(pixels[(zero.y + kWheelY) * pitch + zero.x + kWheelX + 2] == kActiveFill);
        assert(pixels[(zero.y + kGunY + 1) * pitch + zero.x + kGunX + 3] == kActiveFill);

        {
            const int x[]{ kHouseX, kTransportX, kWheelX, kGunX };
            const int y[]{ kIconY, kIconY, kWheelY, kGunY };
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                    assert(x[a] + kIconSize <= x[b] || x[b] + kIconSize <= x[a] ||
                           y[a] + kIconSize <= y[b] || y[b] + kIconSize <= y[a]);
        }

        house[0] = false;
        wheel[0] = false;
        transport[0] = false;
        gun[0] = false;

        std::array<uint16_t, 16> tooSmall{};
        tooSmall.fill(untouched);
        Draw16(tooSmall.data(), 4, 4, 4, active, house, wheel);
        for (uint16_t pixel : tooSmall)
            assert(pixel == untouched);

        // --- clipped repaint -------------------------------------------------
        // Repairs only touch the cells the damaged region overlaps, and paint
        // those exactly as an unclipped pass would.
        pixels.fill(untouched);
        const Rect damaged{ two.x, two.y, kCell, kCell };
        Draw16(pixels.data(), pitch, width, height, active, house, wheel,
               16, nullptr, &transport, false, &damaged);
        assert(pixels[two.y * pitch + two.x] == kInactiveBorder);
        assert(pixels[one.y * pitch + one.x] == untouched);
        assert(pixels[zero.y * pitch + zero.x] == untouched);

        std::array<uint16_t, pitch * height> whole{};
        whole.fill(untouched);
        Draw16(whole.data(), pitch, width, height, active, house, wheel,
               16, nullptr, &transport);
        for (int y = two.y; y < two.y + kCell + CountStripHeight(); ++y)
            for (int x = two.x; x < two.x + kCell; ++x)
                assert(pixels[y * pitch + x] == whole[y * pitch + x]);

        const Rect miss{ 0, 0, one.x - 1, height };
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel,
               16, nullptr, &transport, false, &miss);
        for (uint16_t pixel : pixels)
            assert(pixel == untouched); // damage left of the panel: nothing repainted
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
        Slots transport{};
        active[0] = true;
        house[0] = true;
        transport[0] = true;

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

        Slots gun{};
        gun[0] = true;

        assert(fade.update(active, house, none, base, nullptr, &transport, &gun) == 0); // fades in from 0
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
        assert(fade.transports()[0]);
        assert(fade.guns()[0]);

        Fade persistentFade;
        persistentFade.setPersistent(true);
        assert(persistentFade.persistent());
        assert(persistentFade.update(none, none, none, base) == 0); // still ramps in from 0
        assert(persistentFade.update(none, none, none, base + fadeMs) == 16); // stays lit with no groups
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
            GameVersion::HS_2,
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
    static_assert(CountWidth(1, PanelScale::kMinQuarters) == kCountDigitWidth * kCountScale);
    static_assert(CountWidth(2, PanelScale::kMinQuarters) ==
                  2 * kCountDigitWidth * kCountScale + kCountDigitGap);
    static_assert(CountLeft(9, PanelScale::kMinQuarters) > CountLeft(99, PanelScale::kMinQuarters));
    static_assert(CountLeft(99, PanelScale::kMinQuarters) > CountLeft(999, PanelScale::kMinQuarters));
    static_assert(CountLeft(999, PanelScale::kMinQuarters) > CountLeft(1000, PanelScale::kMinQuarters));
    static_assert(kCountTop > kCell);
    static_assert(Height(PanelScale::kMinQuarters) ==
                  kCell + CountStripHeight(PanelScale::kMinQuarters));

    for (int value : { 1, 9, 10, 99, 100, 999, 1000, kCountMax })
    {
        const int digits = CountDigits(value);
        assert(CountLeft(value, PanelScale::kMinQuarters) - kCountOutline >= 0);
        assert(CountLeft(value, PanelScale::kMinQuarters) +
                   CountWidth(digits, PanelScale::kMinQuarters) + kCountOutline <= kCell);
        assert(CountWidth(digits, PanelScale::kMinQuarters) <= kCell - 2 * kCountOutline);
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
            assert(pixels[topRow * pitch + one.x + CountLeft(3, PanelScale::kMinQuarters) + x] == kCountText);

        for (int x = -kCountOutline; x < kCountDigitWidth * kCountScale + kCountOutline; ++x)
            assert(pixels[(topRow - kCountOutline) * pitch + one.x + CountLeft(3, PanelScale::kMinQuarters) + x] == kCountPlate);

        assert(pixels[(one.y + kCell) * pitch + one.x + kCell / 2] == untouched);

        counts[0] = 12;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        const int secondDigitX =
            one.x + CountLeft(12, PanelScale::kMinQuarters) + kCountDigitWidth * kCountScale + kCountDigitGap;
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + secondDigitX + x] == kCountText);
        assert(CountLeft(12, PanelScale::kMinQuarters) +
               CountWidth(2, PanelScale::kMinQuarters) + kCountOutline <= kCell);

        counts[0] = 1000;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        const int fourthDigitX =
            one.x + CountLeft(1000, PanelScale::kMinQuarters) +
            3 * (kCountDigitWidth * kCountScale + kCountDigitGap);
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + fourthDigitX + x] == kCountText);

        counts[0] = 50000;
        pixels.fill(untouched);
        Draw16(pixels.data(), pitch, width, height, active, house, wheel, 16, &counts);
        for (int x = 0; x < kCountDigitWidth * kCountScale; ++x)
            assert(pixels[topRow * pitch + one.x + CountLeft(kCountMax, PanelScale::kMinQuarters) + x] == kCountText);

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

    // --- a blip in the reader must not flash the panel ------------------------
    {
        Fade fade;
        Slots active{};
        Slots none{};
        active[0] = true;
        constexpr uint32_t fadeMs = Zoom::kIndicatorFadeMs;
        constexpr uint32_t t0 = 5000;

        fade.update(active, none, none, t0 - fadeMs);
        assert(fade.update(active, none, none, t0) == 16);

        assert(fade.update(none, none, none, t0) == 16);
        const int dipped = fade.update(none, none, none, t0 + fadeMs / 2);
        assert(dipped > 0 && dipped < 16);

        const int resumed = fade.update(active, none, none, t0 + fadeMs / 2);
        assert(resumed >= dipped - 1 && resumed <= dipped + 1);

        const int climbing = fade.update(active, none, none, t0 + fadeMs / 2 + fadeMs / 8);
        assert(climbing >= resumed);
        assert(fade.update(active, none, none, t0 + 3 * fadeMs) == 16);

        const uint32_t t1 = t0 + 3 * fadeMs;
        assert(fade.update(none, none, none, t1) == 16);
        const int falling = fade.update(none, none, none, t1 + fadeMs / 4);
        assert(falling > 0 && falling < 16);
        const int back = fade.update(active, none, none, t1 + fadeMs / 4);
        assert(back >= falling - 1 && back <= falling + 1);
    }

    // --- predicate decoders, against the game's own bytes ---------------------
    {
        static_assert(VehicleContainerOffset(kSs2Vehicle.data()) == 0x6d);
        static_assert(VehicleContainerOffset(kSs2Gun.data()) == 0);
        static_assert(VehicleContainerOffset(kSs2Building.data()) == 0);

        constexpr GunCrew gun = DecodeGunCrew(kSs2Gun.data());
        static_assert(gun.count == 2 && gun.seats.groupOffset == 0xfa && gun.seats.stride == 0x17);
        constexpr GunCrew gold = DecodeGunCrew(kGoldGun.data());
        static_assert(gold.count == 2 && gold.seats.groupOffset == 0xfa && gold.seats.stride == 0x17);
        static_assert(DecodeGunCrew(kSs2Vehicle.data()).count == 0);
        static_assert(DecodeGunCrew(kSs2Building.data()).count == 0);

        constexpr BuildingSeats building = DecodeBuildingSeats(kSs2Building.data());
        static_assert(building.typeOffset == 0x26e);
        static_assert(building.typeTable == 0x1089492c);
        static_assert(building.capacityTable == 0x1089a3ad);
        static_assert(building.seats.groupOffset == 0xaa && building.seats.stride == 0x15);
        static_assert(DecodeBuildingSeats(kSs2Vehicle.data()).typeTable == 0);
        static_assert(DecodeBuildingSeats(kSs2Gun.data()).typeTable == 0);

        // Seat 0's record starts at the occupancy dword the predicates test.
        static_assert(SeatRecords(gun.seats) == 0xec);
        static_assert(SeatRecords(building.seats) == 0x9c);
        static_assert(SeatBytes(gun.seats, 0) == 0);
        static_assert(SeatBytes(gun.seats, 2) == 0x17 + 0xf);
        static_assert(SeatBytes(building.seats, 6) == 5 * 0x15 + 0xf);

        // capacity = capacityTable[typeTable[type * 22] * 1828], wrapping as
        // the game's 32-bit arithmetic does
        static_assert(BuildingTypeRow(building, 0) == 0x1089492c);
        static_assert(BuildingTypeRow(building, 3) == 0x1089492c + 3 * 22);
        static_assert(BuildingCapacityRow(building, 2) == 0x1089a3ad + 2 * 1828);
        static_assert(BuildingTypeRow(building, -1) == 0x1089492c - 22);

        // a single wrong byte outside the wildcards is rejected
        std::array<uint8_t, 105> broken = kSs2Building;
        broken[40] = 0x0c;              // scale 1 instead of 2 on the type row
        assert(DecodeBuildingSeats(broken.data()).typeTable == 0);
        std::array<uint8_t, 69> brokenGun = kSs2Gun;
        brokenGun[60] = 0x03;           // three seats is not the gun this reads
        assert(DecodeGunCrew(brokenGun.data()).count == 0);
    }

    // --- occupant records ------------------------------------------------------
    {
        std::array<uint8_t, 0x20> record{};
        PutRecord(record, 0, true, 0);
        assert(OccupantSlot(record.data(), true) == 0);
        assert(OccupantSlot(record.data(), false) == 0);

        // an empty seat keeps its stale group byte: only a seat walk sees
        // through it, a packed list never holds one
        PutRecord(record, 0, false, 0);
        assert(OccupantSlot(record.data(), true) == -1);
        assert(OccupantSlot(record.data(), false) == 0);

        // any byte of the occupancy dword marks the seat taken
        record[3] = 1;
        assert(OccupantSlot(record.data(), true) == 0);

        PutRecord(record, 0, true, -1);
        assert(OccupantSlot(record.data(), true) == -1);
        record[kOccupantGroupOffset] = kCount + 1;
        assert(OccupantSlot(record.data(), true) == -1);
    }

    // --- the counting rule -------------------------------------------------------
    // One rule for every container: occupants always count, from their
    // records. The container itself counts only when its own group byte names
    // the slot - it was assigned the group, like any unit. Answering the slot
    // through its predicate alone adds nothing once its occupants were read.
    static_assert(CountsItself(true, false));      // loose unit
    static_assert(CountsItself(true, true));       // assigned container
    static_assert(!CountsItself(false, true));     // container standing in for occupants
    static_assert(CountsItself(false, false));     // container nothing decodes

    {
        constexpr int one = 0;      // slot of key '1'
        constexpr int two = 1;      // slot of key '2'
        constexpr GunCrew gunCrew = DecodeGunCrew(kSs2Gun.data());
        constexpr BuildingSeats house = DecodeBuildingSeats(kSs2Building.data());
        constexpr int houseCapacity = 6;
        const Slots none{};

        const auto loose = [&](Counts& counts, int count, int slot)
        {
            for (int i = 0; i < count; ++i)
                CountEntry(slot, none, false, counts);
        };

        // Fills a house with `inside` men of `slot`; the rest of its seats are
        // empty but keep the stale group byte of whoever left them.
        const auto garrison = [&](Counts& counts, int inside, int slot)
        {
            std::array<uint8_t, 0x200> building{};
            for (int seat = 0; seat < houseCapacity; ++seat)
                PutSeat(building, house.seats, seat, seat < inside, slot);
            CountSeats(building.data(), house.seats, houseCapacity, counts);
            CountEntry(-1, Only(slot), true, counts);
        };

        // 3 riflemen of group 1: on open ground, inside a house, and out again
        {
            Counts counts{};
            loose(counts, 3, one);
            assert(counts[one] == 3);
        }
        {
            Counts counts{};
            garrison(counts, 3, one);
            assert(counts[one] == 3);
        }

        // split 2 inside / 1 outside
        {
            Counts counts{};
            garrison(counts, 2, one);
            loose(counts, 1, one);
            assert(counts[one] == 3);
        }

        // two houses, 2 men of the same group in each
        {
            Counts counts{};
            garrison(counts, 2, one);
            garrison(counts, 2, one);
            assert(counts[one] == 4);
        }

        // a house mixing two groups counts each man in his own group
        {
            Counts counts{};
            std::array<uint8_t, 0x200> building{};
            PutSeat(building, house.seats, 0, true, one);
            PutSeat(building, house.seats, 1, true, two);
            PutSeat(building, house.seats, 2, true, two);
            CountSeats(building.data(), house.seats, houseCapacity, counts);
            Slots both = Only(one);
            both[two] = true;
            CountEntry(-1, both, true, counts);
            assert(counts[one] == 1 && counts[two] == 2);
        }

        // 2 men of group 2 on a mortar; then only one left, the other seat
        // still holding his group byte
        {
            Counts counts{};
            std::array<uint8_t, 0x200> mortar{};
            PutSeat(mortar, gunCrew.seats, 0, true, two);
            PutSeat(mortar, gunCrew.seats, 1, true, two);
            CountSeats(mortar.data(), gunCrew.seats, gunCrew.count, counts);
            CountEntry(-1, Only(two), true, counts);
            assert(counts[two] == 2);

            Counts after{};
            PutSeat(mortar, gunCrew.seats, 1, false, two);
            CountSeats(mortar.data(), gunCrew.seats, gunCrew.count, after);
            CountEntry(-1, Only(two), true, after);
            assert(after[two] == 1);
        }

        // two mortars of the same group, 2 servants each
        {
            Counts counts{};
            for (int gun = 0; gun < 2; ++gun)
            {
                std::array<uint8_t, 0x200> mortar{};
                PutSeat(mortar, gunCrew.seats, 0, true, two);
                PutSeat(mortar, gunCrew.seats, 1, true, two);
                CountSeats(mortar.data(), gunCrew.seats, gunCrew.count, counts);
                CountEntry(-1, Only(two), true, counts);
            }
            assert(counts[two] == 4);
        }

        // a truck with 2 crew and 1 passenger of group 1: unchanged
        {
            Counts counts{};
            std::array<uint8_t, 3 * kVehicleRecordSize> records{};
            for (size_t i = 0; i < 3; ++i)
                PutRecord(records, i * kVehicleRecordSize, false, one);   // packed: no occupancy test
            CountList(records.data(), 2, counts);
            CountList(records.data() + 2 * kVehicleRecordSize, 1, counts);
            CountEntry(-1, Only(one), true, counts);
            assert(counts[one] == 3);
        }

        // an empty vehicle assigned group 1 still reads 1, and an assigned
        // vehicle counts once on top of any crew of the same group
        {
            Counts counts{};
            CountEntry(one, none, true, counts);
            assert(counts[one] == 1);

            std::array<uint8_t, 2 * kVehicleRecordSize> crew{};
            PutRecord(crew, 0, false, one);
            PutRecord(crew, kVehicleRecordSize, false, two);
            CountList(crew.data(), 2, counts);
            assert(counts[one] == 2 && counts[two] == 1);
        }

        // a container kind nothing decodes still lights and counts once
        {
            Counts counts{};
            CountEntry(-1, Only(one), false, counts);
            assert(counts[one] == 1);
        }
    }

    // --- assign is reachable through the runtime lookup ----------------------
    {
        const GameVersion bound[]
        {
            GameVersion::SS_2,
            GameVersion::HS_2,
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

    // --- incompatible or untested profiles stay disabled --------------------
    {
        assert(TryGetGroupPanelAddresses(GameVersion::SS_RW_V2_3) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_RW_V2_4) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_EUROPE_2015) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_BLACK_SEA) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_HD_1_2_INT) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_HD_1_2_RU) == nullptr);
        assert(TryGetGroupPanelAddresses(GameVersion::SS_GOLD_EN) == nullptr);
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
