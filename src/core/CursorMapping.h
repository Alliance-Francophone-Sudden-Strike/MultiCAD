#pragma once

#include "cad.h"
#include "ImportHooks.h"

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

        return TRUE;
    }

    inline BOOL WINAPI ClipCursorHook(const RECT* rect)
    {
        if (g_originalClipCursor == nullptr)
            return FALSE;

        // A null rect releases the cursor - the same in either mode.
        if (rect == nullptr || !NeedsTranslation())
            return g_originalClipCursor(rect);

        POINT origin{ 0, 0 };
        ClientToScreen(g_moduleState->hwnd, &origin);

        RECT onScreen = *rect;
        OffsetRect(&onScreen, origin.x, origin.y);

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
