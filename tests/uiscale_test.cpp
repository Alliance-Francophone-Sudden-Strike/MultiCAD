#include "UIScale.h"

#include <cassert>
#include <algorithm>
#include <vector>

namespace
{
    constexpr int kScreenWidth = 1920;
    constexpr int kScreenHeight = 1080;

    UIScale::Rect apply(int x, int y, int width, int height)
    {
        return UIScale::Apply(x, y, width, height, kScreenWidth, kScreenHeight);
    }

    bool identical(const UIScale::Rect& rect, int x, int y, int width, int height)
    {
        return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
    }
}

int main()
{
    assert(!UIScale::Active());
    assert(identical(apply(0, 943, 352, 137), 0, 943, 352, 137));

    UIScale::Set(0.2f);
    assert(UIScale::Factor() == UIScale::kMinFactor);
    UIScale::Set(9.0f);
    assert(UIScale::Factor() == UIScale::kMaxFactor);

    UIScale::Set(2.0f);
    assert(UIScale::Active());

    const UIScale::Rect bottomLeft = apply(0, kScreenHeight - 137, 352, 137);
    assert(identical(bottomLeft, 0, kScreenHeight - 274, 704, 274));

    const UIScale::Rect topLeft = apply(0, 0, 352, 137);
    assert(identical(topLeft, 0, 0, 704, 274));

    const UIScale::Rect topRight = apply(kScreenWidth - 352, 0, 352, 137);
    assert(topRight.x + topRight.width == kScreenWidth);

    const UIScale::Rect bottomRight = apply(kScreenWidth - 352, kScreenHeight - 137, 352, 137);
    assert(bottomRight.x + bottomRight.width == kScreenWidth);
    assert(bottomRight.y + bottomRight.height == kScreenHeight);

    const UIScale::Rect centred = apply(760, 390, 400, 300);
    assert(centred.x + centred.width / 2 == 960 && centred.y + centred.height / 2 == 540);

    UIScale::Rect previous{};
    for (int slot = 0; slot < 6; ++slot)
    {
        const UIScale::Rect icon = apply(slot * 32, 4, 32, 32);
        assert(icon.width == 64 && icon.height == 64);
        if (slot > 0)
            assert(icon.x == previous.x + previous.width);
        previous = icon;
    }

    const UIScale::Rect statsPanel = apply(0, 0, 200, 60);
    const UIScale::Rect besideStats = apply(210, 5, 32, 32);
    assert(besideStats.x - (statsPanel.x + statsPanel.width) == (210 - 200) * 2);

    assert(identical(apply(0, 0, kScreenWidth, kScreenHeight), 0, 0, kScreenWidth, kScreenHeight));
    assert(identical(apply(0, 0, kScreenWidth, 64), 0, 0, kScreenWidth, 64));
    assert(identical(apply(0, 0, 64, kScreenHeight), 0, 0, 64, kScreenHeight));
    assert(identical(apply(-4, 0, 352, 137), -4, 0, 352, 137));
    assert(identical(apply(0, 0, 352, 0), 0, 0, 352, 0));

    UIScale::Set(3.0f);
    const UIScale::Rect clamped = apply(0, 0, 1000, 500);
    assert(clamped.width == kScreenWidth && clamped.height == 960);
    assert(clamped.x == 0 && clamped.y == 0);

    UIScale::Set(2.0f);
    const UIScale::Rect panel = apply(0, kScreenHeight - 137, 352, 137);
    for (int physical = panel.x; physical < panel.x + panel.width; ++physical)
    {
        const int local = (physical - panel.x) * 352 / panel.width;
        assert(local >= 0 && local < 352);
    }
    assert((panel.x - panel.x) * 352 / panel.width == 0);
    assert((panel.x + panel.width - 1 - panel.x) * 352 / panel.width == 351);

    UIScale::Set(1.5f);
    const UIScale::Rect panelRect = apply(0, kScreenHeight - 137, 352, 137);
    const int nativeWidth = 352;

    int covered = 0;
    for (int tile = 0; tile * 16 < nativeWidth; ++tile)
    {
        const int sourceLeft = tile * 16;
        const int sourceRight = std::min(sourceLeft + 15, nativeWidth - 1);

        const int left = UIScale::Project(sourceLeft, nativeWidth, panelRect.width);
        const int blitWidth = UIScale::Project(sourceRight + 1, nativeWidth, panelRect.width) - left;

        assert(left == covered);
        assert(blitWidth > 0);
        covered += blitWidth;

        for (int x = 0; x < blitWidth; ++x)
        {
            const int sampled = UIScale::Project(left + x, panelRect.width, nativeWidth);
            assert(sampled >= sourceLeft && sampled <= sourceRight);
        }
    }
    assert(covered == panelRect.width);

    UIScale::Set(1.25f);
    {
        const int nativeTop = kScreenHeight - 137;
        const UIScale::Rect hud = apply(0, nativeTop, 352, 137);
        const UIScale::Rect inset = UIScale::TileInset(hud);

        assert(hud.y == kScreenHeight - 171);
        assert(inset.y > hud.y && (inset.y & 7) == 0);
        assert(inset.y + inset.height <= hud.y + hud.height);
        assert(inset.x + inset.width <= hud.x + hud.width);

        std::vector<bool> painted(kScreenHeight, false);
        const auto paintRows = [&painted](const UIScale::Rect& area)
        {
            for (int y = area.y; y < area.y + area.height; ++y)
            {
                assert(!painted[y]);
                painted[y] = true;
            }
        };

        paintRows({ hud.x, hud.y, hud.width, inset.y - hud.y });
        paintRows({ hud.x, inset.y, hud.width, inset.height });
        paintRows({ hud.x, inset.y + inset.height, hud.width,
            hud.y + hud.height - inset.y - inset.height });

        for (int y = hud.y; y < hud.y + hud.height; ++y)
            assert(painted[y]);

        for (int tile = nativeTop >> 3; tile <= (nativeTop + 136) >> 3; ++tile)
        {
            const UIScale::Rect blit = UIScale::ProjectRegion(
                { 0, 0, 352, 137 }, hud, { 0, tile * 8 - nativeTop, 352, 8 });

            assert(blit.y >= hud.y);
            assert(blit.y + blit.height <= hud.y + hud.height);
            assert(blit.x >= hud.x && blit.x + blit.width <= hud.x + hud.width);
        }

        std::vector<bool> rows(kScreenHeight, false);
        for (int tile = nativeTop >> 3; tile <= (nativeTop + 136) >> 3; ++tile)
        {
            const UIScale::Rect blit = UIScale::ProjectRegion(
                { 0, 0, 352, 137 }, hud, { 0, tile * 8 - nativeTop, 352, 8 });
            for (int y = blit.y; y < blit.y + blit.height; ++y)
            {
                assert(!rows[y]);
                rows[y] = true;
            }
        }
        for (int y = hud.y; y < hud.y + hud.height; ++y)
            assert(rows[y]);
    }

    // Every factor in range, not just the ones that happen to divide evenly:
    // neighbours must keep sharing an edge, and the tile blits must cover the
    // whole scaled rect. A gap in either is a black seam on screen.
    for (int step = 100; step <= 300; ++step)
    {
        UIScale::Set(step / 100.0f);

        UIScale::Rect left{};
        for (int slot = 0; slot < 10; ++slot)
        {
            const UIScale::Rect icon = apply(slot * 32, 4, 32, 32);
            if (slot > 0)
                assert(icon.x == left.x + left.width);
            left = icon;
        }

        // Stacked against the bottom edge: they may overlap once scaled, but a
        // gap between them would show through as a black strip.
        UIScale::Rect above{};
        for (int row = 0; row < 4; ++row)
        {
            const UIScale::Rect band = apply(0, kScreenHeight - 137 + row * 32, 352, 32);
            if (row > 0)
                assert(band.y <= above.y + above.height);
            above = band;
        }

        // A panel held off the screen edge by a bar below it keeps its bottom
        // edge: anything it natively covered stays covered at every factor.
        for (const int bar : { 0, 23, 64, 107 })
        {
            const UIScale::Rect held = apply(0, kScreenHeight - bar - 137, 352, 137);
            assert(held.y + held.height == kScreenHeight - bar);
            assert(held.y <= kScreenHeight - bar - 137);
        }

        const int nativeTop = kScreenHeight - 137;
        const UIScale::Rect hud = apply(0, nativeTop, 352, 137);
        assert(hud.width >= 352 && hud.height >= 137);
        assert(hud.x >= 0 && hud.y >= 0);
        assert(hud.x + hud.width <= kScreenWidth && hud.y + hud.height <= kScreenHeight);

        std::vector<bool> rows(kScreenHeight, false);
        for (int tile = nativeTop >> 3; tile <= (nativeTop + 136) >> 3; ++tile)
        {
            const int top = std::max(tile * 8, nativeTop);
            const int bottom = std::min(tile * 8 + 7, kScreenHeight - 1);
            const UIScale::Rect blit = UIScale::ProjectRegion(
                { 0, 0, 352, 137 }, hud,
                { 0, top - nativeTop, 352, bottom - top + 1 });
            for (int y = blit.y; y < blit.y + blit.height; ++y)
            {
                assert(!rows[y]);
                rows[y] = true;
            }
        }
        for (int y = hud.y; y < hud.y + hud.height; ++y)
            assert(rows[y]);
    }

    UIScale::Set(UIScale::kMinFactor);
    assert(identical(apply(0, 943, 352, 137), 0, 943, 352, 137));
}
