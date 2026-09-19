#pragma once

#include "GroupPanel.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ZeppelinPanel
{
    using Zoom::Rect;

    constexpr int kMaxRows = 32;

    struct Row
    {
        uint16_t color;
        int secondsLeft;
        int held;
        int total;
        bool started;
    };

    using Rows = std::array<Row, kMaxRows>;

    constexpr int kMargin = GroupPanel::kMargin;
    constexpr int kPad = 7;
    constexpr int kSwatch = 12;
    constexpr int kTimeScale = 2;
    constexpr int kDigitWidth = GroupPanel::kCountDigitWidth;
    constexpr int kDigitHeight = GroupPanel::kCountDigitHeight;
    constexpr int kDigitGap = GroupPanel::kCountDigitGap;
    constexpr int kColonWidth = 1;
    constexpr int kSlashWidth = 3;
    constexpr int kMaxGlyphs = 5;
    constexpr int kMaxSeconds = 99 * 60 + 59;
    constexpr int kMaxZeppelins = 32;
    constexpr int kTimeWidth = 4 * kDigitWidth * kTimeScale + kColonWidth * kTimeScale + 4 * kDigitGap;
    constexpr int kCountWidth = 4 * kDigitWidth * kTimeScale + kSlashWidth * kTimeScale + 4 * kDigitGap;
    constexpr int kTextWidth = kTimeWidth > kCountWidth ? kTimeWidth : kCountWidth;
    constexpr int kRowHeight = 16;
    constexpr int kRowGap = 2;

    constexpr int kGlyphColon = 10;
    constexpr int kGlyphSlash = 11;

    constexpr uint16_t kTimeText = 0xFFFF;
    constexpr uint16_t kCountText = 0xBDF7;

    constexpr uint32_t kHoldMs = 5000;
    constexpr uint32_t kFadeMs = Zoom::kIndicatorFadeMs;
    constexpr uint32_t kAlternateMs = 2000;

    constexpr int HoldOpacity(uint32_t elapsed)
    {
        if (elapsed >= kHoldMs)
            return 0;
        if (elapsed + kFadeMs <= kHoldMs)
            return 16;
        return static_cast<int>((kHoldMs - elapsed) * 16 / kFadeMs);
    }

    constexpr bool FrozenShowsTime(uint32_t tick)
    {
        return (tick / kAlternateMs) % 2 != 0;
    }

    constexpr int Width()
    {
        return kTextWidth + kPad + kSwatch;
    }

    constexpr int Height(int rows)
    {
        return rows <= 0 ? 0 : rows * kRowHeight + (rows - 1) * kRowGap;
    }

    constexpr int Bottom(int screenHeight)
    {
        return screenHeight - kMargin;
    }

    constexpr Rect PanelRect(int screenWidth, int screenHeight, int rows)
    {
        const int height = Height(rows);
        return { screenWidth - kMargin - Width(), Bottom(screenHeight) - height, Width(), height };
    }

    constexpr Rect RowRect(int index, int screenWidth, int screenHeight, int rows)
    {
        const Rect panel = PanelRect(screenWidth, screenHeight, rows);
        return { panel.x, panel.y + index * (kRowHeight + kRowGap), Width(), kRowHeight };
    }

    constexpr bool Fits(int screenWidth, int screenHeight, int rows)
    {
        return GroupPanel::Fits(screenWidth, screenHeight) &&
               screenWidth >= Width() + 2 * kMargin &&
               Bottom(screenHeight) - Height(rows) >=
                   GroupPanel::kMargin + GroupPanel::Height() + kRowGap;
    }

    constexpr int SecondsLeft(int progress, int target, int ticksPerSecond)
    {
        if (ticksPerSecond <= 0 || target <= progress)
            return 0;
        return (target - progress + ticksPerSecond - 1) / ticksPerSecond;
    }

    constexpr int GlyphWidth(int glyph)
    {
        return (glyph == kGlyphColon ? kColonWidth
                : glyph == kGlyphSlash ? kSlashWidth
                : kDigitWidth) * kTimeScale;
    }

    constexpr int TimeGlyphs(int seconds, int* out)
    {
        seconds = seconds < 0 ? 0 : (seconds > kMaxSeconds ? kMaxSeconds : seconds);

        const int minutes = seconds / 60;
        const int rest = seconds % 60;

        int count = 0;
        if (minutes >= 10)
            out[count++] = minutes / 10;
        out[count++] = minutes % 10;
        out[count++] = kGlyphColon;
        out[count++] = rest / 10;
        out[count++] = rest % 10;
        return count;
    }

    constexpr int CountGlyphs(int held, int total, int* out)
    {
        held = std::clamp(held, 0, kMaxZeppelins);
        total = std::clamp(total, 0, kMaxZeppelins);

        int count = 0;
        if (held >= 10)
            out[count++] = held / 10;
        out[count++] = held % 10;
        out[count++] = kGlyphSlash;
        if (total >= 10)
            out[count++] = total / 10;
        out[count++] = total % 10;
        return count;
    }

    constexpr int GlyphsWidth(const int* glyphs, int count)
    {
        int width = 0;
        for (int i = 0; i < count; ++i)
            width += GlyphWidth(glyphs[i]) + (i > 0 ? kDigitGap : 0);
        return width;
    }

    constexpr int WidestTimeWidth()
    {
        int glyphs[kMaxGlyphs]{};
        return GlyphsWidth(glyphs, TimeGlyphs(kMaxSeconds, glyphs));
    }

    constexpr int WidestCountWidth()
    {
        int glyphs[kMaxGlyphs]{};
        return GlyphsWidth(glyphs, CountGlyphs(kMaxZeppelins, kMaxZeppelins, glyphs));
    }

    inline constexpr uint8_t kColon[kDigitHeight]
    {
        0b0,
        0b1,
        0b0,
        0b1,
        0b0,
    };

    inline constexpr uint8_t kSlash[kDigitHeight]
    {
        0b001,
        0b001,
        0b010,
        0b100,
        0b100,
    };

    static_assert(kSwatch < kRowHeight);
    static_assert(kDigitHeight * kTimeScale <= kRowHeight);
    static_assert(kTextWidth + kPad + kSwatch == Width());
    static_assert(WidestTimeWidth() == kTimeWidth);
    static_assert(WidestCountWidth() == kCountWidth);

    inline void Draw16(
        uint16_t* destination,
        int pitch,
        int width,
        int height,
        const Rows& rows,
        int rowCount,
        const Rect* clip = nullptr,
        int opacity = 16,
        uint32_t tick = 0)
    {
        rowCount = std::clamp(rowCount, 0, kMaxRows);
        opacity = std::clamp(opacity, 0, 16);

        if (!destination || pitch < width || rowCount <= 0 || opacity <= 0 ||
            !Fits(width, height, rowCount))
            return;

        const auto plot = [&](int x, int y, uint16_t color)
        {
            uint16_t& pixel = destination[y * pitch + x];
            pixel = opacity == 16 ? color : Zoom::Blend565(pixel, color, opacity);
        };

        const auto fillRect = [&](int left, int top, int cols, int rowsCount, uint16_t color)
        {
            for (int y = 0; y < rowsCount; ++y)
                for (int x = 0; x < cols; ++x)
                    plot(left + x, top + y, color);
        };

        const auto blit = [&](const auto* bits, int cols, int rowsCount,
                              int left, int top, int scale, uint16_t color)
        {
            for (int y = 0; y < rowsCount; ++y)
                for (int x = 0; x < cols; ++x)
                    if (bits[y] & (1 << (cols - 1 - x)))
                        for (int dy = 0; dy < scale; ++dy)
                            for (int dx = 0; dx < scale; ++dx)
                                plot(left + x * scale + dx, top + y * scale + dy, color);
        };

        const auto hidden = [&](const Rect& area)
        {
            return clip && (clip->x >= area.x + area.width ||
                            clip->x + clip->width <= area.x ||
                            clip->y >= area.y + area.height ||
                            clip->y + clip->height <= area.y);
        };

        for (int index = 0; index < rowCount; ++index)
        {
            const Rect row = RowRect(index, width, height, rowCount);
            if (hidden(row))
                continue;

            const Row& entry = rows[static_cast<size_t>(index)];

            fillRect(row.x + kTextWidth + kPad, row.y + (kRowHeight - kSwatch) / 2,
                     kSwatch, kSwatch, entry.color);

            if (entry.held <= 0 || entry.total <= 0)
                continue;

            const bool running = entry.held >= entry.total;
            const bool showTime =
                running || (entry.started && FrozenShowsTime(tick));

            int glyphs[kMaxGlyphs]{};
            const int count = showTime
                ? TimeGlyphs(entry.secondsLeft, glyphs)
                : CountGlyphs(entry.held, entry.total, glyphs);

            const uint16_t ink = running ? kTimeText : kCountText;
            int x = row.x + kTextWidth - GlyphsWidth(glyphs, count);
            const int textY = row.y + (kRowHeight - kDigitHeight * kTimeScale) / 2;

            for (int i = 0; i < count; ++i)
            {
                const int glyph = glyphs[i];
                if (glyph == kGlyphColon)
                    blit(kColon, kColonWidth, kDigitHeight, x, textY, kTimeScale, ink);
                else if (glyph == kGlyphSlash)
                    blit(kSlash, kSlashWidth, kDigitHeight, x, textY, kTimeScale, ink);
                else
                    blit(GroupPanel::kDigits[glyph], kDigitWidth, kDigitHeight,
                         x, textY, kTimeScale, ink);

                x += GlyphWidth(glyph) + kDigitGap;
            }
        }
    }
}
