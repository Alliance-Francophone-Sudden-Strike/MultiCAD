#include "pch.h"
#include "cad.h"
#include "renderer.h"
#include "DllVersionDetector.h"
#include "GameModuleNames.h"
#include "ProfileOverride.h"

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

void* InitializeModule()
{
#pragma comment(linker, "/EXPORT:" "CADraw_Init=" __FUNCDNAME__)

    return IsLongModuleState(ResolveModuleStateVersion())
        ? InitModuleStateLong()
        : InitModuleStateShort();
}
