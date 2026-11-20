#include "pch.h"
#include "cad.h"
#include "renderer.h"
#include "DllVersionDetector.h"
#include "GameModuleNames.h"
#include "ProfileOverride.h"
#include "UIScale.h"

static ModuleStateLong  g_moduleStateLong;
static ModuleStateShort g_moduleStateShort;

ModuleStateBase* g_moduleState{ nullptr };

inline void InitActionsPrefix(RendererActionsPrefix& a)
{
    a.initValues                                = initValues;
    a.initDxInstance                            = initDxInstance;
    a.restoreDxInstance                         = restoreDxInstance;
    a.initWindowDxSurface                       = initWindowDxSurface;
    a.setPixelColorMasks                        = setPixelColorMasks;
    a.releaseDxSurface                          = releaseDxSurface;
    a.lockDxSurface                             = lockDxSurface;
    a.unlockDxSurface                           = unlockDxSurface;
    a.copyMainBackSurfaces                      = copyMainBackSurfaces;
    a.convertNotMagentaColors                   = convertNotMagentaColors;
    a.convertAllColors                          = convertAllColors;
    a.getTextLength                             = getTextLength;
    a.drawBackSurfaceText                       = drawBackSurfaceText;
    a.drawMainSurfaceText                       = drawMainSurfaceText;
    a.callDrawBackSurfacePaletteRhomb           = callDrawBackSurfacePaletteRhomb;
    a.callDrawBackSurfaceMaskRhomb              = callDrawBackSurfaceMaskRhomb;
    a.drawBackSurfaceRhombsPaletteSprite        = drawBackSurfaceRhombsPaletteSprite;
    a.drawBackSurfaceRhombsPaletteSprite2       = drawBackSurfaceRhombsPaletteSprite2;
    a.drawBackSurfaceRhombsPaletteShadedSprite  = drawBackSurfaceRhombsPaletteShadedSprite;
    a.drawBackSurfacePaletteShadedSprite        = drawBackSurfacePaletteShadedSprite;
    a.drawBackSurfacePaletteSpriteAndStencil    = drawBackSurfacePaletteSpriteAndStencil;
    a.drawBackSurfacePalletteSprite             = drawBackSurfacePalletteSprite;
    a.drawBackSurfaceShadowSprite               = drawBackSurfaceShadowSprite;
    a.copyBackToMainSurfaceRect                 = copyBackToMainSurfaceRect;
    a.drawBackSurfaceColorPoint                 = drawBackSurfaceColorPoint;
    a.callShadeMainSurfaceRhomb                 = callShadeMainSurfaceRhomb;
    a.callCleanMainSurfaceRhomb                 = callCleanMainSurfaceRhomb;
    a.drawMainSurfacePaletteSpriteCompact       = drawMainSurfacePaletteSpriteCompact;
    a.drawMainSurfaceSprite                     = drawMainSurfaceSprite;
    a.drawMainSurfacePaletteSprite              = drawMainSurfacePaletteSprite;
    a.drawMainSurfacePaletteSpriteStencil       = drawMainSurfacePaletteSpriteStencil;
    a.drawMainSurfacePaletteSpriteFrontStencil  = drawMainSurfacePaletteSpriteFrontStencil;
    a.drawMainSurfacePaletteSpriteBackStencil   = drawMainSurfacePaletteSpriteBackStencil;
    a.drawMainSurfaceAnimationSpriteStencil     = drawMainSurfaceAnimationSpriteStencil;
    a.blendMainSurfaceWithWarFog_0              = blendMainSurfaceWithWarFog;
}

inline void InitActionsAnimationSprite(RendererActionsAnimationSprite& a)
{
    a.drawMainSurfaceAnimationSprite = drawMainSurfaceAnimationSprite;
}

inline void InitActionsPostfix(RendererActionsPostfix& p)
{
    p.drawMainSurfaceShadowSprite           = drawMainSurfaceShadowSprite;
    p.drawMainSurfaceActualSprite           = drawMainSurfaceActualSprite;
    p.drawMainSurfaceAdjustedSprite         = drawMainSurfaceAdjustedSprite;
    p.drawMainSurfaceVanishingPaletteSprite = drawMainSurfaceVanishingPaletteSprite;
    p.drawMainSurfaceColorPoint             = drawMainSurfaceColorPoint;
    p.drawMainSurfaceFilledColorRect        = drawMainSurfaceFilledColorRect;
    p.drawMainSurfaceColorRect              = drawMainSurfaceColorRect;
    p.drawMainSurfaceHorLine                = drawMainSurfaceHorLine;
    p.drawMainSurfaceVertLine               = drawMainSurfaceVertLine;
    p.drawMainSurfaceShadeColorRect         = drawMainSurfaceShadeColorRect;
    p.drawMainSurfaceColorOutline           = drawMainSurfaceColorOutline;
    p.drawMainSurfaceColorEllipse           = drawMainSurfaceColorEllipse;
    p.copyMainSurfaceToRendererWithWarFog   = copyMainSurfaceToRendererWithWarFog;
    p.copyMainSurfaceToRenderer             = copyMainSurfaceToRenderer;
    p.readMainSurfaceRect                   = readMainSurfaceRect;
    p.maskStencilSurfaceRect                = maskStencilSurfaceRect;
    p.resetStencilSurface                   = resetStencilSurface;
    p.copyToRendererSurfaceRect             = copyToRendererSurfaceRect;
    p.copyPixelRectFromTo                   = copyPixelRectFromTo;
    p.drawUiSprite                          = drawUiSprite;
    p.drawVanishingUiSprite                 = drawVanishingUiSprite;
    p.markUiWithButtonType                  = markUiWithButtonType;
    p.releaseDxInstance                     = releaseDxInstance;
    p.blendMainSurfaceWithWarFog_1          = blendMainSurfaceWithWarFog;
}

template<typename ModuleState>
void InitModuleState(ModuleState& s)
{
    s.pad.bits.isLong = std::is_same_v<ModuleState, ModuleStateLong> ? 1 : 0;

    s.surface.main = g_rendererState.surfaces.main;
    s.surface.back = g_rendererState.surfaces.back;
    s.surface.stencil = g_rendererState.surfaces.stencil;

    initValues();

    InitActionsPrefix(s.actions);

    if constexpr (requires { s.actionsPostfix; })
    {
        InitActionsPostfix(s.actionsPostfix);
    }

    if constexpr (requires { s.actionsPostfix.drawMainSurfaceAnimationSprite; })
    {
        InitActionsAnimationSprite(s.actionsPostfix);
    }
}

void* InitModuleStateLong()
{
    g_moduleState = &g_moduleStateLong;

    InitModuleState(g_moduleStateLong);

    return &g_moduleStateLong.windowRect;
}

void* InitModuleStateShort()
{
    g_moduleState = &g_moduleStateShort;

    InitModuleState(g_moduleStateShort);

    return &g_moduleStateShort.windowRect;
}

// Sudden Strike Gold (en/de/fr) and Gold HD 1.2 use the long module state.
// Everything else - including Gold ru, which was a debug build - uses the short one.
constexpr bool IsLongModuleState(const GameVersion version)
{
    switch (version)
    {
    case GameVersion::SS_GOLD_EN:
    case GameVersion::SS_GOLD_DE:
    case GameVersion::SS_GOLD_FR:
    case GameVersion::SS_GOLD_HD_1_2_RU:
    case GameVersion::SS_GOLD_HD_1_2_INT:
        return true;
    default:
        return false;
    }
}

// The module state picks the layout of the function table the game calls, so getting it
// wrong is fatal - and it has to be decided before any dll is loaded. Strongest signal
// first: an explicit ini override, then the game dll (whose hash tells the Gold builds
// apart from everything else), then the menu dll, then short - right for every version
// but Gold. Silent when nothing matches: short is the common case, and the game dll is
// warned about properly in InstallGamePatches once it actually loads.
static GameVersion ResolveModuleStateVersion()
{
    if (const GameVersion forced = ProfileOverride::GetProfileOverride(DllType::Game); forced != GameVersion::UNKNOWN)
        return forced;

    if (const GameVersion forced = ProfileOverride::GetProfileOverride(DllType::Menu); forced != GameVersion::UNKNOWN)
        return forced;

    DllVersionDetector& detector = DllVersionDetector::GetInstance();

    // Each probe walks the whole game tree, so stop at the first name that matches. The
    // name the ini gives goes first: it is the file the game itself loads, where the parts
    // below are a guess that finds nothing on an install using its own module names.
    const std::wstring configuredGame = GameModules::GetConfiguredName(DllType::Game);

    bool res = !configuredGame.empty() && detector.DetectFileDllVersion(DllType::Game, configuredGame);
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Game, ToDllName(DllType::Game));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Game, ToDllName(DllType::GameGulfWar));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Game, ToDllName(DllType::GameBlackGold));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Game, ToDllName(DllType::GameEurope2015));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Game, ToDllName(DllType::GameBlackSea));

    if (const GameVersion gameDllVersion = detector.GetGameVersion(DllType::Game); gameDllVersion != GameVersion::UNKNOWN)
        return gameDllVersion;

    const std::wstring configuredMenu = GameModules::GetConfiguredName(DllType::Menu);

    res = !configuredMenu.empty() && detector.DetectFileDllVersion(DllType::Menu, configuredMenu);
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Menu, ToDllName(DllType::Menu));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Menu, ToDllName(DllType::MenuGulfWar));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Menu, ToDllName(DllType::MenuBlackGold));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Menu, ToDllName(DllType::MenuEurope2015));
    if (!res)
        res = detector.DetectFileDllVersion(DllType::Menu, ToDllName(DllType::MenuBlackSea));

    return detector.GetGameVersion(DllType::Menu);
}

// HS2Engine's [Game] UIScale: the logical screen the UI is laid out on (UIScale.h).
// Honoured for the HS_2 profile only; for any other game dll the UI stays unscaled.
// Nonzero when the map is on: otherwise the engine keeps its real-screen layout.
int SetUiScaleExport(float /*scale*/, int logicalWidth, int logicalHeight)
{
#pragma comment(linker, "/EXPORT:" "SetUiScale=" __FUNCDNAME__)

    UIScale::Set(logicalWidth, logicalHeight, Screen::width_, Screen::height_);
    return UIScale::Active();
}

// HS2Engine's zoom out: nonzero when the game draws a view larger than the screen; then
// `width` x `height` is the screen the player sees, which the engine lays the UI out on.
int GetPresentedSizeExport(int* width, int* height)
{
#pragma comment(linker, "/EXPORT:" "GetPresentedSize=" __FUNCDNAME__)

    if (!Screen::ViewMode())
        return 0;
    if (width)
        *width = Screen::presentWidth_;
    if (height)
        *height = Screen::presentHeight_;
    return 1;
}

// HS2Engine's zoom out: the view the game must draw now (it follows the zoom), at the
// origin of the frame. Zero when the game draws its stock view.
int GetViewSizeExport(int* width, int* height)
{
#pragma comment(linker, "/EXPORT:" "GetViewSize=" __FUNCDNAME__)

    int w = 0, h = 0;
    if (!Zoom::GetState().requestedView(w, h))
        return 0;
    if (width)
        *width = w;
    if (height)
        *height = h;
    return Zoom::GetState().native() ? 2 : 1;  // 2: native, the game draws its UI at the screen's size
}

// With GetViewSize: where that view goes, from the origin of the game's view now (world pixels).
int GetViewOriginExport(int* x, int* y)
{
#pragma comment(linker, "/EXPORT:" "GetViewOrigin=" __FUNCDNAME__)

    int ox = 0, oy = 0;
    if (!Zoom::GetState().requestedOrigin(ox, oy))
        return 0;
    if (x)
        *x = ox;
    if (y)
        *y = oy;
    return 1;
}

// HS2Engine draws the zoom indicator as a UI element. The first call stops MultiCAD's own
// indicator. `size` is the caller's sizeof(ZoomIndicatorInfo); fields may only be added.
// Zero for any other game dll (no HS2Engine zoom): MultiCAD keeps its indicator.
struct ZoomIndicatorInfo
{
    int size;
    int count;     // the levels (notches), 0.5x .. 3x at 1080p
    int home;      // the 1x level's index
    int position;  // the shown scale on the levels, 256 for each level, 0 = the lowest
    int visible;   // [Game] ZoomIndicator / PersistentZoomIndicator, 1.5 s after a zoom change
    int right;     // [Game] ZoomIndicator=right
};

int GetZoomIndicatorExport(ZoomIndicatorInfo* info)
{
#pragma comment(linker, "/EXPORT:" "GetZoomIndicator=" __FUNCDNAME__)

    Zoom::State& zoom = Zoom::GetState();
    if (!info || info->size < static_cast<int>(sizeof(ZoomIndicatorInfo)) || !zoom.hs2Zoom() ||
        zoom.mode() == Zoom::Mode::Off)
        return 0;
    zoom.setEngineIndicator(true);
    info->count = zoom.levelCount();
    info->home = zoom.homeLevel();
    info->position = zoom.levelPosition256();
    info->visible = zoom.indicatorVisible(GetTickCount()) ? 1 : 0;
    info->right = zoom.indicatorAnchor() == Zoom::IndicatorAnchor::Right ? 1 : 0;
    return 1;
}

void* InitializeModule()
{
#pragma comment(linker, "/EXPORT:" "CADraw_Init=" __FUNCDNAME__)

    return IsLongModuleState(ResolveModuleStateVersion())
        ? InitModuleStateLong()
        : InitModuleStateShort();
}
