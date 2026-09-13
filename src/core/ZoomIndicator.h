#pragma once

#include "Zoom.h"

#include <algorithm>
#include <cstdint>

namespace Zoom
{
    inline uint16_t Blend565(uint16_t from, uint16_t to, int t)
    {
        const int fr = (from >> 11) & 0x1F, fg = (from >> 5) & 0x3F, fb = from & 0x1F;
        const int tr = (to >> 11) & 0x1F, tg = (to >> 5) & 0x3F, tb = to & 0x1F;
        return static_cast<uint16_t>(
            (((fr * (16 - t) + tr * t + 8) / 16) << 11) |
            (((fg * (16 - t) + tg * t + 8) / 16) << 5) |
            ((fb * (16 - t) + tb * t + 8) / 16));
    }

    inline float IndicatorLevelFraction(float scale, int dot)
    {
        return std::clamp(scale - (kMaxScale - dot) + 1.f, 0.f, 1.f);
    }

    inline void DrawIndicatorSquares16(
        uint16_t* destination, int pitch, int width, int height, float scale, bool right,
        int opacity = 16)
    {
        constexpr int dots = kMaxScale - kMinScale + 1;
        constexpr int size = 12;
        constexpr int gap = 8;
        constexpr int margin = 12;
        constexpr uint16_t outline = 0x8410;
        constexpr uint16_t fill = 0xC618;
        constexpr int totalHeight = dots * size + (dots - 1) * gap;
        const int left = right ? width - margin - size : margin;
        const int top = (height - totalHeight) / 2;

        if (!destination || left < 0 || top < 0 || left + size > width || top + totalHeight > height)
            return;

        scale = std::clamp(scale, static_cast<float>(kMinScale), static_cast<float>(kMaxScale));
        opacity = std::clamp(opacity, 0, 16);

        for (int dot = 0; dot < dots; ++dot)
        {
            const int levelT = static_cast<int>(IndicatorLevelFraction(scale, dot) * 16.f + 0.5f);
            const uint16_t borderColor = Blend565(outline, fill, levelT);
            const int y = top + dot * (size + gap);

            for (int dy = 0; dy < size; ++dy)
                for (int dx = 0; dx < size; ++dx)
                {
                    const bool border = dx == 0 || dx == size - 1 || dy == 0 || dy == size - 1;
                    uint16_t& pixel = destination[(y + dy) * pitch + left + dx];
                    const uint16_t color = border ? borderColor : Blend565(pixel, fill, levelT);
                    pixel = opacity == 16 ? color : Blend565(pixel, color, opacity);
                }
        }
    }

    inline void DrawIndicatorBars16(
        uint16_t* destination, int pitch, int width, int height, float scale, bool right,
        int opacity = 16)
    {
        constexpr int dots = kMaxScale - kMinScale + 1;
        constexpr int thickness = 2;
        constexpr int gap = 14;
        constexpr int shortLen = 14;
        constexpr int longLen = 28;
        constexpr int margin = 12;
        constexpr uint16_t outline = 0x8410;
        constexpr uint16_t fill = 0xC618;
        constexpr int totalHeight = dots * thickness + (dots - 1) * gap;
        const int anchorX = right ? width - margin : margin;
        const int top = (height - totalHeight) / 2;
        const int leftEdge = right ? anchorX - longLen : anchorX;
        const int rightEdge = right ? anchorX : anchorX + longLen;

        if (!destination || top < 0 || top + totalHeight > height || leftEdge < 0 || rightEdge > width)
            return;

        scale = std::clamp(scale, static_cast<float>(kMinScale), static_cast<float>(kMaxScale));
        opacity = std::clamp(opacity, 0, 16);

        for (int dot = 0; dot < dots; ++dot)
        {
            const float fraction = IndicatorLevelFraction(scale, dot);
            const int len = shortLen + static_cast<int>((longLen - shortLen) * fraction + 0.5f);
            const uint16_t color = Blend565(outline, fill, static_cast<int>(fraction * 16.f + 0.5f));
            const int y = top + dot * (thickness + gap);
            const int x0 = right ? anchorX - len : anchorX;

            for (int dy = 0; dy < thickness; ++dy)
                for (int dx = 0; dx < len; ++dx)
                {
                    uint16_t& pixel = destination[(y + dy) * pitch + x0 + dx];
                    pixel = opacity == 16 ? color : Blend565(pixel, color, opacity);
                }
        }
    }

    inline void DrawIndicator16(
        IndicatorShape shape, uint16_t* destination, int pitch, int width, int height, float scale,
        bool right, int opacity = 16)
    {
        if (shape == IndicatorShape::Bars)
            DrawIndicatorBars16(destination, pitch, width, height, scale, right, opacity);
        else
            DrawIndicatorSquares16(destination, pitch, width, height, scale, right, opacity);
    }
}
