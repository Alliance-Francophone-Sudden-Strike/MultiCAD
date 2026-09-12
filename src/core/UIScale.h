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

    // Round to nearest: a plain cast truncates, and a float factor is never
    // exact (100 * 1.27f is 126.99999), which loses a pixel off every edge.
    inline int Scaled(int value, float factor)
    {
        return static_cast<int>(value * factor + 0.5f);
    }

    // Projects both edges of the element through the same mapping and derives
    // the extent from them. Scaling position and extent separately makes
    // neighbours stop sharing an edge - int(64 * 1.27) is one past
    // int(32 * 1.27) * 2 - and the unpainted seam shows as a black hairline.
    inline void Anchor(
        int position, int extent, int screenExtent, float factor,
        int& outPosition, int& outExtent)
    {
        const int middle = screenExtent / 2;

        if (position + extent <= middle)
        {
            outPosition = Scaled(position, factor);
            outExtent = Scaled(position + extent, factor) - outPosition;
        }
        else if (position >= middle)
        {
            // Pin the far edge where it natively is and grow inward only. Scaling
            // the gap to the screen edge instead lifts the panel off whatever sits
            // below it by gap * (factor - 1) - a black strip under the bottom-left
            // HUD, widening with the factor. Neighbours pinned to the same edge
            // then overlap rather than separate, and an overlap paints, a gap does not.
            const int gap = screenExtent - position - extent;
            outExtent = Scaled(extent, factor);
            outPosition = screenExtent - gap - outExtent;
        }
        else
        {
            // Straddles the middle: no edge to grow from, so keep it centred.
            outExtent = Scaled(extent, factor);
            outPosition = position + extent / 2 - outExtent / 2;
        }

        outExtent = std::clamp(outExtent, 1, screenExtent);
        outPosition = std::clamp(outPosition, 0, screenExtent - outExtent);
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

        Rect scaled{};
        Anchor(x, width, screenWidth, factor, scaled.x, scaled.width);
        Anchor(y, height, screenHeight, factor, scaled.y, scaled.height);

        return scaled;
    }
}
