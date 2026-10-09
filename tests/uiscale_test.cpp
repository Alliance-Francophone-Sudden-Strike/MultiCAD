// UIScale: the map keeps edges and neighbours, and Unmap inverts Map.
#include "UIScale.h"

#include <cstdio>
#include <initializer_list>

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_failures; } } while (0)

int main()
{
    UIScale::Set(1280, 720, 1920, 1080);
    CHECK(!UIScale::Active());  // not allowed: the stock game and the other profiles
    UIScale::Allow(true);
    CHECK(!UIScale::Active());  // no SetUiScale yet
    UIScale::Set(1280, 720, 1920, 1080);
    CHECK(UIScale::Active());

    // The bottom-right logical pixel ends on the screen edge.
    const UIScale::Rect corner = UIScale::MapRect(1279, 719, 1279, 719, 1920, 1080);
    CHECK(corner.x + corner.width == 1920 && corner.y + corner.height == 1080);

    // Neighbours touch, whatever the fraction (scale 1.5 and 1.7).
    for (const int lw : { 1280, 1129 })
    {
        UIScale::Set(lw, 720, 1920, 1080);
        for (int edge = 1; edge < lw; edge += 7)
        {
            const UIScale::Rect a = UIScale::MapRect(0, 0, edge - 1, 0, 1920, 1080);
            const UIScale::Rect b = UIScale::MapRect(edge, 0, lw - 1, 0, 1920, 1080);
            CHECK(a.x + a.width == b.x);
        }
        // Every screen pixel of a mapped logical pixel unmaps to it.
        for (int p = 0; p < lw; p += 5)
        {
            const UIScale::Rect r = UIScale::MapRect(p, 0, p, 0, 1920, 1080);
            for (int x = r.x; x < r.x + r.width; ++x)
                CHECK(UIScale::UnmapX(x, 1920) == p);
        }
    }

    // Scale 1 (logical = real) turns the map off.
    UIScale::Set(1920, 1080, 1920, 1080);
    CHECK(!UIScale::Active());

    std::printf(g_failures ? "%d failures\n" : "uiscale ok\n", g_failures);
    return g_failures != 0;
}
