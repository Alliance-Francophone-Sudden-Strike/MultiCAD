#pragma once

#include "cad.h"
#include "ImportHooks.h"
#include "Zoom.h"

#include <cstdlib>

bool ViewCentreCrop(int* x, int* y);  // renderer.h

// The game treats GetCursorPos and ClipCursor as client coordinates, which only
// holds fullscreen, where the client area is the screen. In a window both are
// out by the client origin. Redirecting the imports covers every version.
namespace CursorMapping
{
    inline BOOL(WINAPI* g_originalGetCursorPos)(LPPOINT) = nullptr;
    inline BOOL(WINAPI* g_originalClipCursor)(const RECT*) = nullptr;

    inline bool NeedsTranslation()
    {
        return g_moduleState != nullptr
            && !g_moduleState->isFullScreen
            && g_moduleState->hwnd != nullptr;
    }

    inline BOOL WINAPI GetCursorPosHook(LPPOINT point)
    {
        if (g_originalGetCursorPos == nullptr)
            return FALSE;

        if (!g_originalGetCursorPos(point))
            return FALSE;

        if (point != nullptr && NeedsTranslation())
            ScreenToClient(g_moduleState->hwnd, point);

        // View mode (HS2Engine zoom out): the game works in the view, the player points on the presented frame.
        int cropX, cropY;
        if (point != nullptr && ViewCentreCrop(&cropX, &cropY))
        {
            point->x += cropX;
            point->y += cropY;
        }
        else if (point != nullptr && Screen::ViewMode() && !Zoom::GetState().native())
        {
            point->x = point->x * Screen::width_ / Screen::presentWidth_;
            point->y = point->y * Screen::height_ / Screen::presentHeight_;
        }

        return TRUE;
    }

    inline BOOL WINAPI ClipCursorHook(const RECT* rect)
    {
        if (g_originalClipCursor == nullptr)
            return FALSE;

        // A null rect releases the cursor - the same in either mode.
        if (rect == nullptr || (!NeedsTranslation() && !Screen::ViewMode()))
            return g_originalClipCursor(rect);

        RECT onScreen = *rect;
        // The box selection clips to the game's view, which is presented over the whole
        // frame; a dialog keeps the box it asks for. Native mode: the screen's coordinates.
        int cropX, cropY;
        const bool crop = ViewCentreCrop(&cropX, &cropY);  // the frame's centre shown 1:1
        const bool scaled = Screen::ViewMode() && !crop;  // the engine maps its clips to the frame in both modes
        if (crop)
            OffsetRect(&onScreen, -cropX, -cropY);
        if (scaled)
        {
            const Zoom::Rect field = Zoom::GetState().battlefield();
            if (rect->left == field.x && rect->top == field.y &&
                std::abs(rect->right - (field.x + field.width)) <= 1 &&
                std::abs(rect->bottom - (field.y + field.height)) <= 1)
                onScreen = { 0, 0, Screen::width_, Screen::height_ };
        }
        if (scaled)
        {
            onScreen.left = onScreen.left * Screen::presentWidth_ / Screen::width_;
            onScreen.right = onScreen.right * Screen::presentWidth_ / Screen::width_;
            onScreen.top = onScreen.top * Screen::presentHeight_ / Screen::height_;
            onScreen.bottom = onScreen.bottom * Screen::presentHeight_ / Screen::height_;
        }

        if (NeedsTranslation())
        {
            POINT origin{ 0, 0 };
            ClientToScreen(g_moduleState->hwnd, &origin);
            OffsetRect(&onScreen, origin.x, origin.y);
        }

        return g_originalClipCursor(&onScreen);
    }

    inline void Install(const uintptr_t moduleBase)
    {
        // Capture once: a table already patched would chain the hook to itself.
        if (void* const previous = ImportHooks::Redirect(moduleBase, "user32.dll", "GetCursorPos", &GetCursorPosHook))
        {
            if (g_originalGetCursorPos == nullptr)
                g_originalGetCursorPos = reinterpret_cast<BOOL(WINAPI*)(LPPOINT)>(previous);
        }

        if (void* const previous = ImportHooks::Redirect(moduleBase, "user32.dll", "ClipCursor", &ClipCursorHook))
        {
            if (g_originalClipCursor == nullptr)
                g_originalClipCursor = reinterpret_cast<BOOL(WINAPI*)(const RECT*)>(previous);
        }
    }
}
