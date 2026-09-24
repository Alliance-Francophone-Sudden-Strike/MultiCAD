#pragma once

// Control-group panel: pure geometry, slot mapping, signature matching, and
// the decoding of container predicates the unit count relies on. Deliberately
// free of Windows and game dependencies so the host compiler can test it (see
// tests/group_panel_test.cpp). Live state stays in the patcher.

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

    constexpr int Nibble(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // Byte `index` of a pattern, or -1 where the pattern has "??" there.
    constexpr int PatternByte(std::string_view pattern, int index)
    {
        const auto at = static_cast<size_t>(index) * 2;
        if (index < 0 || at + 1 >= pattern.size() || pattern[at] == '?')
            return -1;
        return Nibble(pattern[at]) << 4 | Nibble(pattern[at + 1]);
    }

    constexpr bool Matches(const uint8_t* memory, std::string_view pattern)
    {
        if (!memory || pattern.empty() || pattern.size() % 2 != 0)
            return false;

        for (size_t i = 0; i + 1 < pattern.size(); i += 2)
        {
            if (pattern[i] == '?' && pattern[i + 1] == '?')
                continue;

            const int hi = Nibble(pattern[i]);
            const int lo = Nibble(pattern[i + 1]);
            if (hi < 0 || lo < 0)
                return false;               // malformed pattern: fail closed
            if (memory[i / 2] != static_cast<uint8_t>(hi << 4 | lo))
                return false;
        }
        return true;
    }

    // --- counting units hidden inside containers ------------------------------
    // A unit that boards a vehicle, mans a gun or garrisons a building leaves
    // the game's top-level unit list and lives on as an occupant record of the
    // container. Every such record starts the same way: a dword that is
    // non-zero while the seat is taken, then the occupant's own group byte
    // 0xE bytes later. Record sizes differ (0x15 in a vehicle's lists and a
    // building, 0x17 between a gun's seats), so seat walks take their stride
    // from the game code they mirror.
    constexpr size_t kOccupantGroupOffset = 0xE;
    constexpr size_t kVehicleRecordSize = 0x15;

    // Panel slot of the occupant a record holds, or -1. A vehicle's lists are
    // packed, so every record below their count is live; guns and buildings
    // keep fixed seats, where an empty one may hold a stale group byte and
    // only the occupancy dword tells it apart.
    constexpr int OccupantSlot(const uint8_t* record, bool checkSeat)
    {
        if (checkSeat && !(record[0] | record[1] | record[2] | record[3]))
            return -1;
        return SlotForStoredValue(record[kOccupantGroupOffset]);
    }

    // Whether a member entry of the unit list counts once itself, on top of
    // any occupants counted from its records. An entry whose own group byte
    // names the slot is a member itself: a loose unit, or a container that
    // was itself assigned the group. An entry that answers the slot only
    // through its predicate is a container standing in for units the list no
    // longer holds, so it counts nothing once those were counted from its
    // records. A container nothing decodes still counts as one, which
    // undercounts rather than hides the group.
    constexpr bool CountsItself(bool ownByte, bool occupantsCounted)
    {
        return ownByte || !occupantsCounted;
    }

    // Seats inlined into a container: seat i's group byte sits at
    // this + groupOffset + i * stride, its occupancy dword 0xE bytes earlier.
    struct Seats
    {
        int32_t groupOffset{ 0 };
        uint8_t stride{ 0 };
    };

    // Offset of seat 0's record from the container, and the bytes a walk of
    // `count` seats reads from there.
    constexpr int32_t SeatRecords(const Seats& seats)
    {
        return seats.groupOffset - static_cast<int32_t>(kOccupantGroupOffset);
    }

    constexpr size_t SeatBytes(const Seats& seats, int count)
    {
        return count <= 0 ? 0
            : static_cast<size_t>(count - 1) * seats.stride + kOccupantGroupOffset + 1;
    }

    constexpr int32_t ReadInt32(const uint8_t* bytes)
    {
        return static_cast<int32_t>(
            static_cast<uint32_t>(bytes[0]) |
            static_cast<uint32_t>(bytes[1]) << 8 |
            static_cast<uint32_t>(bytes[2]) << 16 |
            static_cast<uint32_t>(bytes[3]) << 24);
    }

    // Group predicate of the common unit class, reached through the unit's
    // vtable. With a modifier it searches a passenger list through a call on
    // `this + container`; that container holds crew at +4/+0xc and
    // passengers at +0x10/+0x18.
    //   0d: 8b 54 24 08    mov  edx, [esp+8]      ; modifiers
    //   15: 50             push eax
    //   16: 83 c1 ??       add  ecx, container    <- 24
    //   19: e8 ????????    call passengerSearch
    inline constexpr std::string_view kVehiclePredicate =
        "8b44240433d28a51??3bd0741a8b54240885d2740d5083c1??e8????????"
        "85c0750533c0c20800b801000000c20800";
    constexpr int kVehicleContainerAt = 24;

    // Offset of the vehicle container inside the unit, or 0 when `code` is
    // not the common-unit predicate.
    constexpr uint8_t VehicleContainerOffset(const uint8_t* code)
    {
        return Matches(code, kVehiclePredicate) ? code[kVehicleContainerAt] : 0;
    }

    // Group predicate of a gun: a fixed array of crew seats inlined into the
    // gun, searched only while a modifier is held.
    //   21: 8d 81 ????????  lea  eax, [ecx+disp32]  ; &seat[0].group  <- 35..38
    //   27: 8b 48 f2        mov  ecx, [eax-0xe]     ; seat taken?
    //   30: 8a 08           mov  cl, [eax]          ; seat group byte
    //   37: 83 c0 ??        add  eax, stride                          <- 57
    //   3a: 83 fa 02        cmp  edx, seats                           <- 60
    // The Gold build only swaps the first two instructions, so both builds
    // share every index above.
    inline constexpr std::string_view kGunPredicateSs2 =
        "33c0568a41??8b7424083bc67509b8010000005ec208008b44240c85c0742033d2"
        "8d81????????8b48f285c9740833c98a083bce74d84283c0??83fa027ce833c05ec20800";
    inline constexpr std::string_view kGunPredicateGold =
        "33c08a41??568b7424083bc67509b8010000005ec208008b44240c85c0742033d2"
        "8d81????????8b48f285c9740833c98a083bce74d84283c0??83fa027ce833c05ec20800";
    constexpr int kGunSeatGroupAt = 35;
    constexpr int kGunSeatStrideAt = 57;
    constexpr int kGunSeatCountAt = 60;

    struct GunCrew
    {
        Seats seats;
        uint8_t count{ 0 };     // 0 when the predicate is not a gun's
    };

    constexpr GunCrew DecodeGunCrew(const uint8_t* code)
    {
        if (!Matches(code, kGunPredicateSs2) && !Matches(code, kGunPredicateGold))
            return {};
        return { { ReadInt32(code + kGunSeatGroupAt), code[kGunSeatStrideAt] },
                 code[kGunSeatCountAt] };
    }

    // Group predicate of a building. Unlike the others it searches its seats
    // with no modifier held, which is why a garrisoned squad answers the bare
    // number key and carries the house badge. Its capacity is not stored in
    // the building but looked up by building type through two game tables:
    //   19: 8b 81 ????????     mov  eax, [ecx+disp32]       ; type    <- 27..30
    //   21: 8d 14 80           lea  edx, [eax+eax*4]
    //   24: 8d 04 50           lea  eax, [eax+edx*2]
    //   27: 8b 04 45 ????????  mov  eax, [eax*2+table]      ; x22     <- 42..45
    //   2e: 8d 14 c0           lea  edx, [eax+eax*8]
    //   31: 8d 14 50           lea  edx, [eax+edx*2]
    //   34: 8d 14 52           lea  edx, [edx+edx*2]
    //   37: 8d 04 d0           lea  eax, [eax+edx*8]
    //   3a: 8b 14 85 ????????  mov  edx, [eax*4+table]      ; x1828   <- 61..64
    //   45: 8d 81 ????????     lea  eax, [ecx+disp32]       ; &seat[0].group <- 71..74
    //   4b: 8b 48 f2           mov  ecx, [eax-0xe]          ; seat taken?
    //   5b: 83 c0 ??           add  eax, stride                       <- 93
    //   5e: 3b f2              cmp  esi, edx                ; capacity
    // Both tables are absolute addresses, read back from the live code so
    // they follow the loader and MultiCAD's own relocation of game data.
    inline constexpr std::string_view kBuildingPredicate =
        "5633c08a41??578b7c240c3bc7750a5fb8010000005ec20800"
        "8b81????????33f68d14808d04508b0445????????"
        "8d14c08d14508d14528d04d08b1485????????85d27e1d"
        "8d81????????8b48f285c9740833c98a083bcf74b54683c0??3bf27ce9"
        "5f33c05ec20800";
    constexpr int kBuildingTypeAt = 27;
    constexpr int kBuildingTypeTableAt = 42;
    constexpr int kBuildingCapacityTableAt = 61;
    constexpr int kBuildingSeatGroupAt = 71;
    constexpr int kBuildingSeatStrideAt = 93;
    constexpr uint32_t kBuildingTypeRow = 22;          // lea chain at 0x21..0x27
    constexpr uint32_t kBuildingCapacityRow = 1828;    // lea chain at 0x2e..0x3a

    struct BuildingSeats
    {
        Seats seats;
        int32_t typeOffset{ 0 };        // this-relative dword: building type
        uint32_t typeTable{ 0 };        // 0 when the predicate is not a building's
        uint32_t capacityTable{ 0 };
    };

    constexpr BuildingSeats DecodeBuildingSeats(const uint8_t* code)
    {
        if (!Matches(code, kBuildingPredicate))
            return {};
        return { { ReadInt32(code + kBuildingSeatGroupAt), code[kBuildingSeatStrideAt] },
                 ReadInt32(code + kBuildingTypeAt),
                 static_cast<uint32_t>(ReadInt32(code + kBuildingTypeTableAt)),
                 static_cast<uint32_t>(ReadInt32(code + kBuildingCapacityTableAt)) };
    }

    // Addresses the building predicate reads its capacity from, computed in
    // the same wrapping 32-bit arithmetic the game uses.
    constexpr uint32_t BuildingTypeRow(const BuildingSeats& building, int32_t type)
    {
        return building.typeTable + static_cast<uint32_t>(type) * kBuildingTypeRow;
    }

    constexpr uint32_t BuildingCapacityRow(const BuildingSeats& building, int32_t row)
    {
        return building.capacityTable + static_cast<uint32_t>(row) * kBuildingCapacityRow;
    }

    // The decoders read operands at fixed indices, so pin each index to the
    // instruction it belongs to: an edit that shifts a pattern fails here
    // rather than in a mission.
    constexpr bool IsWildcard(std::string_view pattern, int first, int count)
    {
        for (int i = first; i < first + count; ++i)
            if (PatternByte(pattern, i) != -1 || static_cast<size_t>(i) * 2 + 1 >= pattern.size())
                return false;
        return true;
    }

    constexpr bool HasBytes(std::string_view pattern, int at, std::string_view bytes)
    {
        return pattern.substr(static_cast<size_t>(at) * 2, bytes.size()) == bytes;
    }

    // `lea eax, [ecx+disp32]` onto seat 0's group byte, then the occupancy
    // test at [eax-0xe] that OccupantSlot mirrors.
    constexpr bool SeatWalkPinned(std::string_view pattern, int groupAt)
    {
        return HasBytes(pattern, groupAt - 2, "8d81") && IsWildcard(pattern, groupAt, 4) &&
               HasBytes(pattern, groupAt + 4, "8b48") &&
               PatternByte(pattern, groupAt + 6) ==
                   static_cast<uint8_t>(-static_cast<int>(kOccupantGroupOffset));
    }

    static_assert(HasBytes(kVehiclePredicate, kVehicleContainerAt - 2, "83c1") &&
                  IsWildcard(kVehiclePredicate, kVehicleContainerAt, 1));

    constexpr bool GunPatternPinned(std::string_view pattern)
    {
        return SeatWalkPinned(pattern, kGunSeatGroupAt) &&
               HasBytes(pattern, kGunSeatStrideAt - 2, "83c0") &&
               IsWildcard(pattern, kGunSeatStrideAt, 1) &&
               HasBytes(pattern, kGunSeatCountAt - 2, "83fa") &&
               PatternByte(pattern, kGunSeatCountAt) > 0;
    }

    static_assert(GunPatternPinned(kGunPredicateSs2));
    static_assert(GunPatternPinned(kGunPredicateGold));
    static_assert(SignatureLength(kGunPredicateSs2) == SignatureLength(kGunPredicateGold));

    static_assert(HasBytes(kBuildingPredicate, kBuildingTypeAt - 2, "8b81") &&
                  IsWildcard(kBuildingPredicate, kBuildingTypeAt, 4));
    // type * 5, then type + 5 * type * 2 = type * 11, then scaled by 2
    static_assert(HasBytes(kBuildingPredicate, kBuildingTypeTableAt - 9, "8d14808d04508b0445") &&
                  IsWildcard(kBuildingPredicate, kBuildingTypeTableAt, 4) &&
                  kBuildingTypeRow == 11 * 2);
    // row * 9, * 19, * 57, then row + 57 * row * 8 = row * 457, scaled by 4
    static_assert(HasBytes(kBuildingPredicate, kBuildingCapacityTableAt - 15,
                           "8d14c08d14508d14528d04d08b1485") &&
                  IsWildcard(kBuildingPredicate, kBuildingCapacityTableAt, 4) &&
                  kBuildingCapacityRow == (1 + (1 + 9 * 2) * 3 * 8) * 4);
    static_assert(SeatWalkPinned(kBuildingPredicate, kBuildingSeatGroupAt) &&
                  HasBytes(kBuildingPredicate, kBuildingSeatStrideAt - 2, "83c0") &&
                  IsWildcard(kBuildingPredicate, kBuildingSeatStrideAt, 1) &&
                  HasBytes(kBuildingPredicate, kBuildingSeatStrideAt + 1, "3bf2"));
}
