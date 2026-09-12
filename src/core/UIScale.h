#pragma once

#include <algorithm>

namespace UIScale
{
    constexpr float kMinFactor = 1.0f;
    constexpr float kMaxFactor = 3.0f;

    struct Rect
    {
        int x{};
        int y{};
        int width{};
        int height{};
    };

    inline float& Factor()
    {
        static float factor = kMinFactor;
        return factor;
    }

    inline void Set(float factor)
    {
        Factor() = factor >= kMinFactor ? std::min(factor, kMaxFactor) : kMinFactor;
    }

    inline bool Active()
    {
        return Factor() > kMinFactor;
    }

    constexpr int kTileWidth = 16;
    constexpr int kTileHeight = 8;

    inline Rect TileInset(const Rect& rect)
    {
        const int left = (rect.x + kTileWidth - 1) & ~(kTileWidth - 1);
        const int top = (rect.y + kTileHeight - 1) & ~(kTileHeight - 1);
        const int right = (rect.x + rect.width) & ~(kTileWidth - 1);
        const int bottom = (rect.y + rect.height) & ~(kTileHeight - 1);

        return { left, top, std::max(0, right - left), std::max(0, bottom - top) };
    }

    inline int Project(int position, int fromExtent, int toExtent)
    {
        return position * toExtent / fromExtent;
    }

    inline Rect ProjectRegion(const Rect& native, const Rect& scaled, const Rect& source)
    {
        const int left = std::max(
            scaled.x, scaled.x + Project(source.x, native.width, scaled.width));
        const int top = std::max(
            scaled.y, scaled.y + Project(source.y, native.height, scaled.height));
        const int right = std::min(
            scaled.x + scaled.width,
            scaled.x + Project(source.x + source.width, native.width, scaled.width));
        const int bottom = std::min(
            scaled.y + scaled.height,
            scaled.y + Project(source.y + source.height, native.height, scaled.height));

        return { left, top, right - left, bottom - top };
    }

    inline int Anchor(int position, int extent, int scaledExtent, int screenExtent, float factor)
    {
        const int middle = screenExtent / 2;

        int scaled;
        if (position + extent <= middle)
            scaled = static_cast<int>(position * factor);
        else if (position >= middle)
            scaled = screenExtent - static_cast<int>((screenExtent - position - extent) * factor) - scaledExtent;
        else
            scaled = position + extent / 2 - scaledExtent / 2;

        return std::clamp(scaled, 0, screenExtent - scaledExtent);
    }

    inline Rect Apply(int x, int y, int width, int height, int screenWidth, int screenHeight)
    {
        const Rect native{ x, y, width, height };

        if (!Active() || width <= 0 || height <= 0 || x < 0 || y < 0 ||
            x + width > screenWidth || y + height > screenHeight ||
            width >= screenWidth || height >= screenHeight)
        {
            return native;
        }

        const float factor = std::min({
            Factor(),
            static_cast<float>(screenWidth) / static_cast<float>(width),
            static_cast<float>(screenHeight) / static_cast<float>(height) });

        const int scaledWidth = std::min(static_cast<int>(width * factor), screenWidth);
        const int scaledHeight = std::min(static_cast<int>(height * factor), screenHeight);

        return {
            Anchor(x, width, scaledWidth, screenWidth, factor),
            Anchor(y, height, scaledHeight, screenHeight, factor),
            scaledWidth,
            scaledHeight
        };
    }
}
