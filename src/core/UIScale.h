#pragma once

// In-game UI scale (HS2Engine, [Game] UIScale). The engine lays the UI out on a
// logical screen of lw x lh pixels and gives its size to SetUiScale. One linear
// map per axis puts the logical screen on the real one, so an element on an edge
// of the logical screen stays on that edge, and elements that touch still touch.
// HS_2 profile only: Allow() is called after the patches of a recognised HS_2 game dll.

namespace UIScale
{
    struct Rect
    {
        int x{};
        int y{};
        int width{};
        int height{};
    };

    struct State
    {
        bool allowed = false;
        int logicalWidth = 0;
        int logicalHeight = 0;
    };

    inline State& GetState()
    {
        static State state;
        return state;
    }

    inline bool Active() { const State& s = GetState(); return s.allowed && s.logicalWidth > 0 && s.logicalHeight > 0; }

    inline void Allow(bool allowed) { GetState() = State{ allowed, 0, 0 }; }

    // Ignored unless allowed. A logical size that is not smaller than the real
    // screen (scale 1) turns the map off.
    inline void Set(int logicalWidth, int logicalHeight, int width, int height)
    {
        State& s = GetState();
        const bool valid = s.allowed && logicalWidth > 0 && logicalHeight > 0 &&
            logicalWidth <= width && logicalHeight <= height &&
            (logicalWidth < width || logicalHeight < height);
        s.logicalWidth = valid ? logicalWidth : 0;
        s.logicalHeight = valid ? logicalHeight : 0;
    }

    // Screen position of the left (top) edge of logical pixel `p`.
    inline int MapX(int p, int width) { return static_cast<int>(static_cast<long long>(p) * width / GetState().logicalWidth); }
    inline int MapY(int p, int height) { return static_cast<int>(static_cast<long long>(p) * height / GetState().logicalHeight); }

    // The logical pixel whose mapped span holds screen pixel `p` (inverse of MapX/MapY).
    inline int UnmapX(int p, int width) { return static_cast<int>(((p + 1LL) * GetState().logicalWidth - 1) / width); }
    inline int UnmapY(int p, int height) { return static_cast<int>(((p + 1LL) * GetState().logicalHeight - 1) / height); }

    // Logical pixels [left, right] x [top, bottom] on the real screen; both edges go through the map.
    inline Rect MapRect(int left, int top, int right, int bottom, int width, int height)
    {
        const int x0 = MapX(left, width);
        const int y0 = MapY(top, height);
        const int x1 = MapX(right + 1, width);
        const int y1 = MapY(bottom + 1, height);
        return { x0, y0, x1 > x0 ? x1 - x0 : 1, y1 > y0 ? y1 - y0 : 1 };
    }
}
