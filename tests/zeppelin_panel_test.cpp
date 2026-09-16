#include "ZeppelinPanel.h"
#include "ZeppelinTraits.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kWidth = 360;
    constexpr int kHeight = 260;
    constexpr int kPitch = kWidth + 4;
    constexpr uint16_t kUntouched = 0xABCD;
    constexpr int kTicks = 25;

    Zoom::Rect TextArea(const Zoom::Rect& row)
    {
        return { row.x, row.y, ZeppelinPanel::kTextWidth, row.height };
    }

    Zoom::Rect SwatchArea(const Zoom::Rect& row)
    {
        return { row.x + ZeppelinPanel::kTextWidth + ZeppelinPanel::kPad, row.y,
                 ZeppelinPanel::kSwatch, row.height };
    }

    bool Centred(const std::vector<uint16_t>& pixels, const Zoom::Rect& area, uint16_t color)
    {
        int lo = area.x + area.width, hi = area.x - 1;
        for (int y = area.y; y < area.y + area.height; ++y)
            for (int x = area.x; x < area.x + area.width; ++x)
                if (pixels[static_cast<size_t>(y) * kPitch + x] == color)
                {
                    lo = lo < x ? lo : x;
                    hi = hi > x ? hi : x;
                }
        if (hi < lo)
            return false;
        const int before = lo - area.x;
        const int after = area.x + area.width - 1 - hi;
        return before - after <= 1 && after - before <= 1;
    }

    bool AreaHas(const std::vector<uint16_t>& pixels, const Zoom::Rect& area, uint16_t color)
    {
        for (int y = area.y; y < area.y + area.height; ++y)
            for (int x = area.x; x < area.x + area.width; ++x)
                if (pixels[static_cast<size_t>(y) * kPitch + x] == color)
                    return true;
        return false;
    }

    void ExpectGlyphs(int seconds, const std::vector<int>& expected)
    {
        int glyphs[ZeppelinPanel::kMaxGlyphs]{};
        const int count = ZeppelinPanel::TimeGlyphs(seconds, glyphs);
        assert(count == static_cast<int>(expected.size()));
        for (int i = 0; i < count; ++i)
            assert(glyphs[i] == expected[static_cast<size_t>(i)]);
        assert(ZeppelinPanel::GlyphsWidth(glyphs, count) <= ZeppelinPanel::kTextWidth);
    }

    void ExpectCount(int held, int total, const std::vector<int>& expected)
    {
        int glyphs[ZeppelinPanel::kMaxGlyphs]{};
        const int count = ZeppelinPanel::CountGlyphs(held, total, glyphs);
        assert(count == static_cast<int>(expected.size()));
        for (int i = 0; i < count; ++i)
            assert(glyphs[i] == expected[static_cast<size_t>(i)]);
        assert(ZeppelinPanel::GlyphsWidth(glyphs, count) <= ZeppelinPanel::kTextWidth);
    }

    std::vector<uint8_t> Materialise(std::string_view pattern, uint8_t filler = 0xAA)
    {
        std::vector<uint8_t> bytes(GroupPanel::SignatureLength(pattern), filler);
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            const std::string_view token = pattern.substr(i * 2, 2);
            if (token == "??")
                continue;
            bytes[i] = static_cast<uint8_t>(std::stoul(std::string(token), nullptr, 16));
        }
        return bytes;
    }
}

int main()
{
    using namespace ZeppelinPanel;

    static_assert(Width() == kTextWidth + kPad + kSwatch);
    static_assert(Height(0) == 0);
    static_assert(Height(3) == 3 * kRowHeight + 2 * kRowGap);
    static_assert(WidestTimeWidth() == kTimeWidth);
    static_assert(WidestCountWidth() == kCountWidth);

    static_assert(SecondsLeft(0, 3750, kTicks) == 150);
    static_assert(SecondsLeft(3750, 3750, kTicks) == 0);
    static_assert(SecondsLeft(3749, 3750, kTicks) == 1);
    static_assert(SecondsLeft(3726, 3750, kTicks) == 1);
    static_assert(SecondsLeft(1875, 3750, kTicks) == 75);
    static_assert(SecondsLeft(1875, 3750, 0) == 0);

    ExpectGlyphs(0, { 0, kGlyphColon, 0, 0 });
    ExpectGlyphs(9, { 0, kGlyphColon, 0, 9 });
    ExpectGlyphs(59, { 0, kGlyphColon, 5, 9 });
    ExpectGlyphs(60, { 1, kGlyphColon, 0, 0 });
    ExpectGlyphs(150, { 2, kGlyphColon, 3, 0 });
    ExpectGlyphs(599, { 9, kGlyphColon, 5, 9 });
    ExpectGlyphs(600, { 1, 0, kGlyphColon, 0, 0 });
    ExpectGlyphs(kMaxSeconds, { 9, 9, kGlyphColon, 5, 9 });
    ExpectGlyphs(kMaxSeconds + 10000, { 9, 9, kGlyphColon, 5, 9 });
    ExpectGlyphs(-5, { 0, kGlyphColon, 0, 0 });

    ExpectCount(1, 3, { 1, kGlyphSlash, 3 });
    ExpectCount(0, 9, { 0, kGlyphSlash, 9 });
    ExpectCount(9, 12, { 9, kGlyphSlash, 1, 2 });
    ExpectCount(12, 12, { 1, 2, kGlyphSlash, 1, 2 });
    ExpectCount(kMaxZeppelins + 5, kMaxZeppelins + 5, { 3, 2, kGlyphSlash, 3, 2 });
    ExpectCount(-1, 4, { 0, kGlyphSlash, 4 });

    {
        for (int rows : { 1, 3, 5 })
        {
            const Zoom::Rect panel = PanelRect(kWidth, kHeight, rows);
            assert(panel.x + panel.width == kWidth - kMargin);
            assert(panel.y + panel.height == kHeight - kMargin);
            assert(panel.y >= GroupPanel::kMargin + GroupPanel::Height());
        }

        const Zoom::Rect first = RowRect(0, kWidth, kHeight, 3);
        const Zoom::Rect second = RowRect(1, kWidth, kHeight, 3);
        const Zoom::Rect panel = PanelRect(kWidth, kHeight, 3);
        assert(first.x == panel.x && first.width == panel.width);
        assert(first.y == panel.y);
        assert(second.y == first.y + kRowHeight + kRowGap);
        assert(RowRect(2, kWidth, kHeight, 3).y + kRowHeight == panel.y + panel.height);
        assert(SwatchArea(first).x + kSwatch == first.x + first.width);
    }

    assert(!Fits(200, kHeight, 3));
    assert(!Fits(kWidth, 60, 3));
    assert(Fits(kWidth, kHeight, 3));

    Rows rows{};
    rows[0] = { 0xF800, 150, 0, 3 };
    rows[1] = { 0x07E0, 75, 3, 3 };
    rows[2] = { 0x001F, 150, 1, 3 };

    std::vector<uint16_t> open(static_cast<size_t>(kPitch) * kHeight, kUntouched);
    Draw16(open.data(), kPitch, kWidth, kHeight, rows, 3);
    {
        const Zoom::Rect zero = RowRect(0, kWidth, kHeight, 3);
        const Zoom::Rect one = RowRect(1, kWidth, kHeight, 3);

        const Zoom::Rect swatch = SwatchArea(zero);
        const size_t middle = static_cast<size_t>(zero.y + kRowHeight / 2) * kPitch;
        assert(open[middle + swatch.x] == rows[0].color);
        assert(open[middle + swatch.x + kSwatch - 1] == rows[0].color);
        assert(open[middle + swatch.x - 1] == kUntouched);
        assert(open[static_cast<size_t>(one.y + kRowHeight / 2) * kPitch +
                    SwatchArea(one).x + kSwatch / 2] == rows[1].color);

        const Zoom::Rect idle = TextArea(zero);
        for (int y = idle.y; y < idle.y + idle.height; ++y)
            for (int x = idle.x; x < idle.x + idle.width; ++x)
                assert(open[static_cast<size_t>(y) * kPitch + x] == kUntouched);

        assert(AreaHas(open, TextArea(one), kTimeText));
        assert(!AreaHas(open, TextArea(one), kCountText));
        assert(!Centred(open, TextArea(one), kTimeText));

        const Zoom::Rect two = RowRect(2, kWidth, kHeight, 3);
        assert(AreaHas(open, TextArea(two), kCountText));
        assert(!AreaHas(open, TextArea(two), kTimeText));

        Rows timed = rows;
        timed[2].held = timed[2].total;
        std::vector<uint16_t> running(static_cast<size_t>(kPitch) * kHeight, kUntouched);
        Draw16(running.data(), kPitch, kWidth, kHeight, timed, 3);
        assert(AreaHas(running, TextArea(two), kTimeText));
        assert(!AreaHas(running, TextArea(two), kCountText));

        const Zoom::Rect panel = PanelRect(kWidth, kHeight, 3);
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
                if (y < panel.y || y >= panel.y + panel.height ||
                    x < panel.x || x >= panel.x + panel.width)
                    assert(open[static_cast<size_t>(y) * kPitch + x] == kUntouched);

        int painted = 0;
        for (int x = panel.x; x < panel.x + panel.width; ++x)
            if (open[static_cast<size_t>(zero.y) * kPitch + x] != kUntouched)
                ++painted;
        assert(painted == 0);
    }

    {
        std::vector<uint16_t> clipped(static_cast<size_t>(kPitch) * kHeight, kUntouched);
        const Zoom::Rect all = PanelRect(kWidth, kHeight, 3);
        Draw16(clipped.data(), kPitch, kWidth, kHeight, rows, 3, &all);
        assert(clipped == open);

        std::vector<uint16_t> partial(static_cast<size_t>(kPitch) * kHeight, kUntouched);
        const Zoom::Rect band = RowRect(1, kWidth, kHeight, 3);
        Draw16(partial.data(), kPitch, kWidth, kHeight, rows, 3, &band);
        for (int y = band.y; y < band.y + band.height; ++y)
            for (int x = band.x; x < band.x + band.width; ++x)
            {
                const size_t at = static_cast<size_t>(y) * kPitch + x;
                assert(partial[at] == open[at]);
            }
        assert(!AreaHas(partial, TextArea(RowRect(2, kWidth, kHeight, 3)), kTimeText));
        assert(!AreaHas(partial, SwatchArea(RowRect(2, kWidth, kHeight, 3)), rows[2].color));
    }

    {
        std::vector<uint16_t> clamped(static_cast<size_t>(kPitch) * kHeight, kUntouched);
        Draw16(clamped.data(), kPitch, kWidth, kHeight, rows, kMaxRows + 99);
        for (uint16_t pixel : clamped)
            assert(pixel == kUntouched);

        std::vector<uint16_t> none(static_cast<size_t>(kPitch) * kHeight, kUntouched);
        Draw16(none.data(), kPitch, kWidth, kHeight, rows, 0);
        for (uint16_t pixel : none)
            assert(pixel == kUntouched);
    }

    {
        const ZeppelinAddresses* hs2 = TryGetZeppelinAddresses(GameVersion::HS_2);
        assert(hs2 != nullptr);

        assert(TryGetZeppelinAddresses(GameVersion::SS_2) == nullptr);
        assert(TryGetZeppelinAddresses(GameVersion::SS_RW_V2_4) == nullptr);
        assert(TryGetZeppelinAddresses(GameVersion::SS_RW_V2_3) == nullptr);
        assert(TryGetZeppelinAddresses(GameVersion::SS_EUROPE_2015) == nullptr);
        assert(TryGetZeppelinAddresses(GameVersion::SS_BLACK_SEA) == nullptr);
        assert(TryGetZeppelinAddresses(GameVersion::SS_GOLD_EN) == nullptr);
        assert(TryGetZeppelinAddresses(GameVersion::UNKNOWN) == nullptr);

        assert(hs2->records + hs2->recordStride * 12 <= hs2->objectSize);
        assert(hs2->captureSeconds + sizeof(int) <= hs2->objectSize);
        assert(hs2->colorRoot + sizeof(uint32_t) <= hs2->objectSize);
        assert(hs2->recordOwner + sizeof(uint32_t) <= hs2->recordStride);
        assert(hs2->recordCaptor < hs2->recordProgress);
        assert(hs2->recordProgress + sizeof(int) * 4 <= hs2->recordStride);

        int pinned = 0;
        for (const auto& signature : hs2->signatures)
        {
            if (signature.rva == 0 || signature.pattern.empty())
                continue;
            ++pinned;

            const std::vector<uint8_t> bytes = Materialise(signature.pattern);
            assert(!bytes.empty());
            assert(GroupPanel::Matches(bytes.data(), signature.pattern));

            for (size_t i = 0; i < bytes.size(); ++i)
            {
                if (signature.pattern.substr(i * 2, 2) == "??")
                    continue;
                std::vector<uint8_t> flipped = bytes;
                flipped[i] = static_cast<uint8_t>(flipped[i] ^ 0xFF);
                assert(!GroupPanel::Matches(flipped.data(), signature.pattern));
            }
        }
        assert(pinned == 4);

        assert(hs2->signatures[0].pattern.find("2c150000") != std::string_view::npos);
        assert(hs2->signatures[1].pattern.find("24150000") != std::string_view::npos);
        assert(hs2->signatures[3].pattern.find("4c010000") != std::string_view::npos);
        assert(hs2->signatures[3].pattern.find("44010000") != std::string_view::npos);
    }

    return 0;
}
