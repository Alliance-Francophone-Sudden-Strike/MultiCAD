#pragma once

#include "types.h"
#include "util.h"
#include "PanelScale.h"
#include "Zoom.h"
#include "ZeppelinPanel.h"
#include "GameIni.h"

#include <cstdlib>

namespace Graphics
{
    constexpr size_t kBitsPerPixel8 = 8;
    constexpr size_t kBitsPerPixel16 = 16;
    constexpr size_t kBitsPerPixel24 = 24;
    constexpr size_t kBitsPerPixel32 = 32;

    constexpr size_t kPaletteSize = 256;

    constexpr U32 kMinWidth = 640;
    constexpr U32 kMinHeight = 480;

    constexpr U32 kDefaultWidth = 1920;
    constexpr U32 kDefaultHeight = 1080;

    constexpr U32 kMaxWidth = 3840;
    constexpr U32 kMaxHeight = 2160;

    constexpr DWORD kWindowedStyle = WS_CAPTION | WS_SYSMENU;
}

class Screen
{
public:
    static S32 width_;             // Width in pixels
    static S32 height_;             // Height in pixels
    static S32 widthInBytes_;       // Width in bytes
    static S32 heightInBytes_;      // Height in bytes
    static S32 sizeInPixels_;       // Number of pixels are in the screen
    static S32 sizeInBytes_;        // Number of bytes are in the screen
    static S32 sizeInDoublePixels_; // Number of double pixels are in the screen

    static bool resolutionFromIni_;

    static void UpdateSize(S32 width, S32 height)
    {
        width_ = width;
        height_ = height;

        widthInBytes_ = width * 2;
        heightInBytes_ = height * 2;

        sizeInPixels_ = width * height;
        sizeInBytes_ = sizeInPixels_ * sizeof(Pixel);
        sizeInDoublePixels_ = sizeInPixels_ * sizeof(Pixel) * 2;
    }

    static void UpdateToOrigSize()
    {
        constexpr S32 width = 1024;
        constexpr S32 height = 768;

        width_ = width;
        height_ = height;

        widthInBytes_ = width * 2;
        heightInBytes_ = height * 2;

        sizeInPixels_ = width * height;
        sizeInBytes_ = sizeInPixels_ * sizeof(Pixel);
        sizeInDoublePixels_ = sizeInPixels_ * sizeof(Pixel) * 2;
    }

    static void GetNativeResolution(S32& width, S32& height)
    {
        width = Graphics::kDefaultWidth;
        height = Graphics::kDefaultHeight;

        // Registry, not current: the menu sets a temporary 640x480 fullscreen mode;
        // only the registry mode keeps the real desktop resolution.
        DEVMODEA dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(nullptr, ENUM_REGISTRY_SETTINGS, &dm))
        {
            width = static_cast<S32>(dm.dmPelsWidth);
            height = static_cast<S32>(dm.dmPelsHeight);
        }

        if (width < static_cast<S32>(Graphics::kMinWidth))   width = Graphics::kMinWidth;
        if (width > static_cast<S32>(Graphics::kMaxWidth))   width = Graphics::kMaxWidth;
        if (height < static_cast<S32>(Graphics::kMinHeight)) height = Graphics::kMinHeight;
        if (height > static_cast<S32>(Graphics::kMaxHeight)) height = Graphics::kMaxHeight;

        // Renderer requires a height divisible by 8.
        height &= ~7;
        if (height < static_cast<S32>(Graphics::kMinHeight))
            height = Graphics::kMinHeight;
    }

    // Set from the renderer. Drops the cache: the target depends on it.
    static void SetWindowed(bool windowed)
    {
        if (windowed_ == windowed)
            return;

        windowed_ = windowed;
        targetResolved_ = false;
    }

    // Native desktop resolution, overridden by an explicit ini value. Cached.
    static void ResolveTargetResolution(S32& width, S32& height)
    {
        if (!targetResolved_)
        {
            GetNativeResolution(targetWidth_, targetHeight_);
            resolutionFromIni_ = ApplyIniResolution(targetWidth_, targetHeight_);

            if (windowed_)
                ClampToWindowedArea(targetWidth_, targetHeight_);

            targetWidth_ &= ~15;
            targetHeight_ &= ~7;
            targetResolved_ = true;
        }

        width = targetWidth_;
        height = targetHeight_;
    }

    // Call when the game dll loads; the menu runs at its own fixed size.
    static void ApplyGameResolution()
    {
        S32 width, height;
        ResolveTargetResolution(width, height);
        UpdateSize(width, height);
    }

    static void SaveResolutionToIni(S32 width, S32 height)
    {
        GameIni::Write("Game", "Resolution", std::to_string(width) + "x" + std::to_string(height));
    }

    static Zoom::Mode GetZoom()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return Zoom::Mode::Off;

        char buffer[16]{};
        GetPrivateProfileStringA("Game", "Zoom", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer);
    }

    static Zoom::IndicatorAnchor GetZoomIndicator()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return Zoom::IndicatorAnchor::Left;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "ZoomIndicator", "left", buffer, sizeof(buffer), iniPath.c_str());
        for (char& c : buffer)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        return Zoom::ParseIndicatorAnchor(buffer);
    }

    static Zoom::IndicatorShape GetZoomIndicatorShape()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return Zoom::IndicatorShape::Squares;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "ZoomIndicatorShape", "squares", buffer, sizeof(buffer), iniPath.c_str());
        for (char& c : buffer)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        return Zoom::ParseIndicatorShape(buffer);
    }

    static bool GetPersistentZoomIndicator()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "PersistentZoomIndicator", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetInvertZoom()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "InvertZoom", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetZoomOnCursor()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "ZoomOnCursor", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetGroupPanel()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "GroupPanel", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetGroupPanelDebug()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "GroupPanelDebug", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetGroupPanelCount()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return true;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "GroupPanelCount", "on", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetPersistentGroupPanel()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "PersistentGroupPanel", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static bool GetZeppelinPanel()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return false;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "ZeppelinPanel", "off", buffer, sizeof(buffer), iniPath.c_str());
        return Zoom::ParseMode(buffer) == Zoom::Mode::On;
    }

    static ZeppelinPanel::Behaviour GetZeppelinPanelBehaviour()
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return ZeppelinPanel::Behaviour::Temp;

        char buffer[8]{};
        GetPrivateProfileStringA("Game", "ZeppelinPanelBehaviour", "temp", buffer, sizeof(buffer), iniPath.c_str());
        for (char& c : buffer)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        return ZeppelinPanel::ParseBehaviour(buffer);
    }

    static int GetPanelScale(const char* key)
    {
        const std::string iniPath = GameIni::Path();
        if (iniPath.empty())
            return PanelScale::kMinQuarters;

        char buffer[16]{};
        GetPrivateProfileStringA("Game", key, "1", buffer, sizeof(buffer), iniPath.c_str());
        for (char& c : buffer)
            if (c == ',')
                c = '.';
        return PanelScale::Quarters(static_cast<float>(std::atof(buffer)));
    }

    static int GetGroupPanelScale() { return GetPanelScale("GroupPanelScale"); }
    static int GetZeppelinPanelScale() { return GetPanelScale("ZeppelinPanelScale"); }
    static int GetZoomIndicatorScale() { return GetPanelScale("ZoomIndicatorScale"); }

private:

    static bool targetResolved_;
    static bool windowed_;
    static S32 targetWidth_;
    static S32 targetHeight_;

    // Work area minus the frame: a window must fit the screen showing it.
    static void ClampToWindowedArea(S32& width, S32& height)
    {
        RECT work{};
        if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0))
            return;

        RECT frame{};   // zero rect in, frame thickness out
        AdjustWindowRect(&frame, Graphics::kWindowedStyle, FALSE);

        const S32 maxWidth = (work.right - work.left) - (frame.right - frame.left);
        const S32 maxHeight = (work.bottom - work.top) - (frame.bottom - frame.top);

        if (maxWidth <= 0 || maxHeight <= 0)
            return;

        if (width > maxWidth)   width = maxWidth;
        if (height > maxHeight) height = maxHeight;

        // Renderer requires a height divisible by 8.
        height &= ~7;

        if (width < static_cast<S32>(Graphics::kMinWidth))   width = Graphics::kMinWidth;
        if (height < static_cast<S32>(Graphics::kMinHeight)) height = Graphics::kMinHeight;
    }

    // Reads "[Game] Resolution=WIDTHxHEIGHT" from the ini. True if a valid value was found.
    static bool ApplyIniResolution(S32& outWidth, S32& outHeight)
    {
        const std::string value = GameIni::Read("Game", "Resolution");

        const size_t xPos = value.find('x');
        if (xPos == std::string::npos)
            return false;

        S32 width = std::atol(value.c_str());
        S32 height = std::atol(value.c_str() + xPos + 1);

        if (width < static_cast<S32>(Graphics::kMinWidth) || height < static_cast<S32>(Graphics::kMinHeight))
        {
            ShowErrorAsync("The resolution specified in sudtest.ini is too small and will be ignored. Minimum supported is 640x480.");
            return false;
        }

        if (width > static_cast<S32>(Graphics::kMaxWidth) || height > static_cast<S32>(Graphics::kMaxHeight))
        {
            ShowErrorAsync("The resolution specified in sudtest.ini is too large and will be ignored. Maximum supported is 3840x2160.");
            return false;
        }

        outWidth = width;
        outHeight = height;
        return true;
    }
};
