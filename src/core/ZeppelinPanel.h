#pragma once

#include "GroupPanel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

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

    enum class Behaviour
    {
        Temp,
        Toggle
    };

    constexpr Behaviour ParseBehaviour(std::string_view value)
    {
        return value == "toggle" ? Behaviour::Toggle : Behaviour::Temp;
    }

    constexpr int ToggleOpacity(bool shown, uint32_t elapsed)
    {
        const int ramp = elapsed >= kFadeMs
            ? 16 : static_cast<int>(elapsed * 16 / kFadeMs);
        return shown ? ramp : 16 - ramp;
    }

    constexpr bool FrozenShowsTime(uint32_t tick)
    {
        return (tick / kAlternateMs) % 2 != 0;
    }

    inline int& ScaleQuarters()
    {
        static int quarters = PanelScale::kMinQuarters;
        return quarters;
    }

    inline void SetScale(int quarters)
    {
        ScaleQuarters() = std::clamp(quarters, PanelScale::kMinQuarters, PanelScale::kMaxQuarters);
    }

    constexpr int Pad(int q)       { return PanelScale::Size(kPad, q); }
    constexpr int Swatch(int q)    { return PanelScale::Size(kSwatch, q); }
    constexpr int TimeScale(int q) { return PanelScale::Repeat(kTimeScale, q); }
    constexpr int DigitGap(int q)  { return PanelScale::Size(kDigitGap, q); }
    constexpr int RowHeight(int q) { return PanelScale::Size(kRowHeight, q); }
    constexpr int RowGap(int q)    { return PanelScale::Size(kRowGap, q); }

    constexpr int TimeWidth(int q)
    {
        return 4 * kDigitWidth * TimeScale(q) + kColonWidth * TimeScale(q) + 4 * DigitGap(q);
    }

    constexpr int CountWidth(int q)
    {
        return 4 * kDigitWidth * TimeScale(q) + kSlashWidth * TimeScale(q) + 4 * DigitGap(q);
    }

    constexpr int TextWidth(int q)
    {
        return TimeWidth(q) > CountWidth(q) ? TimeWidth(q) : CountWidth(q);
    }

    constexpr int Width(int q)
    {
        return TextWidth(q) + Pad(q) + Swatch(q);
    }

    constexpr int Height(int rows, int q)
    {
        return rows <= 0 ? 0 : rows * RowHeight(q) + (rows - 1) * RowGap(q);
    }

    constexpr int Bottom(int screenHeight)
    {
        return screenHeight - kMargin;
    }

    constexpr Rect PanelRect(int screenWidth, int screenHeight, int rows, int q)
    {
        const int height = Height(rows, q);
        return { screenWidth - kMargin - Width(q), Bottom(screenHeight) - height, Width(q), height };
    }

    constexpr Rect RowRect(int index, int screenWidth, int screenHeight, int rows, int q)
    {
        const Rect panel = PanelRect(screenWidth, screenHeight, rows, q);
        return { panel.x, panel.y + index * (RowHeight(q) + RowGap(q)), Width(q), RowHeight(q) };
    }

    constexpr bool Fits(int screenWidth, int screenHeight, int rows, int q, int groupQuarters)
    {
        return GroupPanel::Fits(screenWidth, screenHeight, groupQuarters) &&
               screenWidth >= Width(q) + 2 * kMargin &&
               Bottom(screenHeight) - Height(rows, q) >=
                   GroupPanel::kMargin + GroupPanel::Height(groupQuarters) + RowGap(q);
    }

    constexpr int SecondsLeft(int progress, int target, int ticksPerSecond)
    {
        if (ticksPerSecond <= 0 || target <= progress)
            return 0;
        return (target - progress + ticksPerSecond - 1) / ticksPerSecond;
    }

    constexpr int GlyphWidth(int glyph, int q)
    {
        return (glyph == kGlyphColon ? kColonWidth
                : glyph == kGlyphSlash ? kSlashWidth
                : kDigitWidth) * TimeScale(q);
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

    constexpr int GlyphsWidth(const int* glyphs, int count, int q)
    {
        int width = 0;
        for (int i = 0; i < count; ++i)
            width += GlyphWidth(glyphs[i], q) + (i > 0 ? DigitGap(q) : 0);
        return width;
    }

    constexpr int WidestTimeWidth(int q)
    {
        int glyphs[kMaxGlyphs]{};
        return GlyphsWidth(glyphs, TimeGlyphs(kMaxSeconds, glyphs), q);
    }

    constexpr int WidestCountWidth(int q)
    {
        int glyphs[kMaxGlyphs]{};
        return GlyphsWidth(glyphs, CountGlyphs(kMaxZeppelins, kMaxZeppelins, glyphs), q);
    }

    inline int Pad()       { return Pad(ScaleQuarters()); }
    inline int Swatch()    { return Swatch(ScaleQuarters()); }
    inline int TimeScale() { return TimeScale(ScaleQuarters()); }
    inline int RowHeight() { return RowHeight(ScaleQuarters()); }
    inline int RowGap()    { return RowGap(ScaleQuarters()); }
    inline int TextWidth() { return TextWidth(ScaleQuarters()); }
    inline int Width()     { return Width(ScaleQuarters()); }
    inline int Height(int rows) { return Height(rows, ScaleQuarters()); }
    inline int GlyphsWidth(const int* glyphs, int count) { return GlyphsWidth(glyphs, count, ScaleQuarters()); }

    inline Rect PanelRect(int screenWidth, int screenHeight, int rows)
    {
        return PanelRect(screenWidth, screenHeight, rows, ScaleQuarters());
    }

    inline Rect RowRect(int index, int screenWidth, int screenHeight, int rows)
    {
        return RowRect(index, screenWidth, screenHeight, rows, ScaleQuarters());
    }

    inline bool Fits(int screenWidth, int screenHeight, int rows)
    {
        return Fits(screenWidth, screenHeight, rows, ScaleQuarters(), GroupPanel::ScaleQuarters());
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

    constexpr bool LayoutFits(int q)
    {
        return Swatch(q) < RowHeight(q) &&
               kDigitHeight * TimeScale(q) <= RowHeight(q) &&
               TextWidth(q) + Pad(q) + Swatch(q) == Width(q) &&
               WidestTimeWidth(q) == TimeWidth(q) &&
               WidestCountWidth(q) == CountWidth(q);
    }

    constexpr bool LayoutFitsEveryScale()
    {
        for (int q = PanelScale::kMinQuarters; q <= PanelScale::kMaxQuarters; ++q)
            if (!LayoutFits(q))
                return false;
        return true;
    }

    static_assert(LayoutFitsEveryScale());

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

        const int q = ScaleQuarters();

        if (!destination || pitch < width || rowCount <= 0 || opacity <= 0 ||
            !Fits(width, height, rowCount, q, GroupPanel::ScaleQuarters()))
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

        const int textWidth = TextWidth(q);
        const int swatch = Swatch(q);
        const int rowHeight = RowHeight(q);
        const int timeScale = TimeScale(q);

        for (int index = 0; index < rowCount; ++index)
        {
            const Rect row = RowRect(index, width, height, rowCount, q);
            if (hidden(row))
                continue;

            const Row& entry = rows[static_cast<size_t>(index)];

            fillRect(row.x + textWidth + Pad(q), row.y + (rowHeight - swatch) / 2,
                     swatch, swatch, entry.color);

            if (entry.total <= 0 || (entry.held <= 0 && !entry.started))
                continue;

            const bool running = entry.held >= entry.total;
            const bool showTime =
                running || (entry.started && FrozenShowsTime(tick));

            int glyphs[kMaxGlyphs]{};
            const int count = showTime
                ? TimeGlyphs(entry.secondsLeft, glyphs)
                : CountGlyphs(entry.held, entry.total, glyphs);

            const uint16_t ink = running ? kTimeText : kCountText;
            int x = row.x + textWidth - GlyphsWidth(glyphs, count, q);
            const int textY = row.y + (rowHeight - kDigitHeight * timeScale) / 2;

            for (int i = 0; i < count; ++i)
            {
                const int glyph = glyphs[i];
                if (glyph == kGlyphColon)
                    blit(kColon, kColonWidth, kDigitHeight, x, textY, timeScale, ink);
                else if (glyph == kGlyphSlash)
                    blit(kSlash, kSlashWidth, kDigitHeight, x, textY, timeScale, ink);
                else
                    blit(GroupPanel::kDigits[glyph], kDigitWidth, kDigitHeight,
                         x, textY, timeScale, ink);

                x += GlyphWidth(glyph, q) + DigitGap(q);
            }
        }
    }
}
