// Built with -fno-access-control to exercise the existing hook boundary.
#include "pch.h"
#include "GameDllHooks.h"
#include "ProfileSpecs.h"
#include "renderer.h"
#include "Zoom.h"
#include "UIFilter.h"
#include <array>
#include <cassert>
#include <cstdio>

using Hooks = GameDllHooks;
constexpr int width = 32, height = 16, pitch = width + 4;
constexpr Pixel textColor = 0x7fff, padding = 0xabcd;
static int nativeDraws, uiDraws, blends, copies;
static Pixel palette[256];
static ImagePaletteSprite glyph{ 0, 0, 2, 1, 0xa1, 2, {{ 0x82, {1} }} };
static int* callbackMouseX;
static int* callbackMouseY;
static int callbackX;
static int callbackY;

static Hooks::UiEventArea* nestedAreas;
static int nestedX, nestedY, cursorX, cursorY;

static void __cdecl captureMouse()
{
    callbackX = *callbackMouseX;
    callbackY = *callbackMouseY;
}

static void __cdecl captureCursorType(int x, int y, int*)
{
    cursorX = x;
    cursorY = y;
}

// The game reaches these overrides from inside one another. Whichever one runs
// nested must leave the already-mapped coordinate alone instead of cropping and
// scaling it a second time.
static void __cdecl captureNestedMouse()
{
    Hooks::withBattlefieldMouseCoordinates(
        callbackMouseX, callbackMouseY, nestedAreas, captureMouse);
    nestedX = callbackX;
    nestedY = callbackY;

    int cursorType = 0;
    Hooks::calculateCursorTypeAtZoom(
        *callbackMouseX, *callbackMouseY, &cursorType, nestedAreas, captureCursorType);
}

static void __thiscall drawNative(Hooks::UIRenderElement* ui)
{
    ++nativeDraws;
    drawMainSurfacePaletteSpriteCompact(ui->x, ui->y, palette, &glyph);
    g_rendererState.surfaces.back[0] = textColor;
}

static void __thiscall drawIntoUi(Hooks::UIRenderElement* ui, Hooks::UiElementBase* target)
{
    ++uiDraws;
    assert(target->leftX == 0 && target->topY == 0);
    assert(target->clipRight == width - 1 && target->clipBottom == height - 1);
    drawUiSprite(ui->x, ui->y, &glyph, palette,
        reinterpret_cast<ImageSpriteUI*>(&target->sprites));
}

static void __stdcall blend() { ++blends; }
static int __thiscall first(int*, Hooks::GameData2* region)
{
    region->cellMask = 0; // Already-fogged native region: plain copy.
    region->alignX = region->alignY = 0;
    region->allowX = width - 1;
    region->allowY = height - 1;
    return 1;
}
static int __thiscall next(int*, Hooks::GameData2*) { return 0; }
static void __cdecl copyNative(int x, int y, int w, int h)
{
    ++copies;
    copyMainSurfaceToRenderer(x, y, w, h);
}

int main(int argc, char** argv)
{
    Screen::UpdateSize(width, height);
    static ModuleStateShort module{};
    g_moduleState = &module;
    module.windowRect = {0, 0, width - 1, height - 1};
    module.surface.stride = width * sizeof(Pixel);
    // Deliberately wrap in the middle of the image, including a partial fog block.
    module.surface.y = 7;
    module.surface.offset = (height - module.surface.y) * width * sizeof(Pixel);
    module.actionsPostfix.copyMainSurfaceToRenderer =
        reinterpret_cast<COPY_MAIN_SURFACE_TO_RENDERER_PTR>(&copyNative);
    module.actionsPostfix.drawUiSprite = drawUiSprite;
    memset(module.fogSprites, 0x80, sizeof(module.fogSprites));
    palette[1] = textColor;
    GetUIFilter().setEnabled(true);

    std::array<Pixel, pitch * height> renderer;
    renderer.fill(padding);
    std::array<void*, 9> vtable{};
    vtable[1] = reinterpret_cast<void*>(&drawNative);
    vtable[8] = reinterpret_cast<void*>(&drawIntoUi);
    // Optional unpacked Fusion image: exercise its real sprite-to-UI callback
    // without running game initialization or resolving its external imports.
    if (argc > 1)
    {
        const auto game = reinterpret_cast<uintptr_t>(
            LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES));
        assert(game);
        *reinterpret_cast<void**>(game + 0x106f6e4) = &module.windowRect;
        vtable[8] = reinterpret_cast<void*>(game + 0xa0490);
    }
    Hooks::UIRenderElement chat{}, pause{};
    chat.vtable = pause.vtable = vtable.data();
    chat.scale = pause.scale = reinterpret_cast<int*>(&glyph);
    chat.some_ui_param = pause.some_ui_param = reinterpret_cast<int>(palette);
    chat.type = 60;
    chat.x = 0; chat.y = 1;
    pause.type = 40;
    pause.x = width - 1; pause.y = height - 1; // Sprite clipped at both screen edges.
    pause.prev = &chat;
    std::array<int, 2 + kRowStrideDwordSize * (height / 8)> coverage{};
    coverage[0] = width / 16; coverage[1] = height / 8;
    Hooks::DrawDecorUiElementData data{};
    data.uiRenderElem = &pause;
    data.closedAreaGameDataArray = coverage.data();
    data.cadPtr = reinterpret_cast<uintptr_t>(&module.windowRect);
    data.surfaceWidth = width; data.surfaceHeight = height;
    data.blendMainWithWarFog = blend;
    data.getFirstDecorUi = first; data.getNextDecorUi = next;

    std::array<Pixel, width * height> world{};
    const auto updateWorld = [&](int tick)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const Pixel color = static_cast<Pixel>(1 + tick * 1024 + y * width + x);
                world[y * width + x] = color;
                const int index = ((height - module.surface.y + y) % height) * width + x;
                g_rendererState.surfaces.main[index] = color;
                g_rendererState.surfaces.back[index] = color;
            }
    };
    updateWorld(0);
    auto& zoom = Zoom::GetState();
    zoom.setMode(Zoom::Mode::On);
    zoom.setBattlefield({0, 0, width, height});
    const auto frame = [&]
    {
        const auto mainBefore = g_rendererState.surfaces.main[0];
        std::array<Pixel, width * (height + 1)> main{}, back{};
        std::copy_n(g_rendererState.surfaces.main, main.size(), main.data());
        std::copy_n(g_rendererState.surfaces.back, back.size(), back.data());
        module.surface.renderer = renderer.data();
        module.pitch = pitch * sizeof(Pixel);
        Hooks::drawDecorUiElements(data);
        zoom.finishPresentation(module.surface.renderer, module.pitch, width, height);
        assert(module.surface.renderer == renderer.data());
        assert(module.pitch == pitch * sizeof(Pixel));
        assert(module.surface.y == 7);
        assert(g_rendererState.surfaces.main[0] == mainBefore);
        assert(std::equal(main.begin(), main.end(), g_rendererState.surfaces.main));
        assert(std::equal(back.begin(), back.end(), g_rendererState.surfaces.back));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                Pixel expected = world[zoom.transform().sourceY(y) * width + zoom.transform().sourceX(x)];
                if (data.uiRenderElem && ((y == 1 && x < 2) || (y == height - 1 && x == width - 1)))
                    expected = textColor;
                assert(renderer[y * pitch + x] == expected);
            }
            for (int x = width; x < pitch; ++x)
                assert(renderer[y * pitch + x] == padding);
        }
    };

    frame(); // Native 1x text must be visible, but absent from the world cache.
    assert(nativeDraws == 2 && blends == 1 && copies == 1);
    for (int scale = 5; scale <= 8; ++scale)
    {
        assert(zoom.addWheelDelta(120));
        frame();
        frame(); // Paused presentation tick, no world update.
        assert(zoom.presentedScale() == scale);
    }
    assert(nativeDraws == 2 && blends == 1 && copies == 1);
    if (argc == 1)
        assert(uiDraws == 16); // Pause and chat redraw at physical coordinates.

    const auto worldHook = std::find_if(
        hooks_game_ss_2_v2_2<GameVersion::SS_2>.begin(),
        hooks_game_ss_2_v2_2<GameVersion::SS_2>.end(),
        [](const HookSpec& hook) { return hook.targetRva == 0x980D1; });
    assert(worldHook != hooks_game_ss_2_v2_2<GameVersion::SS_2>.end());
    assert(worldHook->detour == reinterpret_cast<uintptr_t>(
        &Hooks::renderWorldAtZoom_ver<GameVersion::SS_2>));
    const auto cameraHook = std::find_if(
        hooks_game_ss_2_v2_2<GameVersion::SS_2>.begin(),
        hooks_game_ss_2_v2_2<GameVersion::SS_2>.end(),
        [](const HookSpec& hook) { return hook.targetRva == 0x97E8A; });
    assert(cameraHook != hooks_game_ss_2_v2_2<GameVersion::SS_2>.end());
    assert(cameraHook->detour == reinterpret_cast<uintptr_t>(&Hooks::moveCameraAtZoom));
    Hooks::UiEventArea battlefield{};
    battlefield.tag = 'FILD';
    battlefield.width = width;
    battlefield.height = height;
    int mouseX = 4, mouseY = 4;
    callbackMouseX = &mouseX;
    callbackMouseY = &mouseY;
    Hooks::withBattlefieldMouseCoordinates(
        &mouseX, &mouseY, &battlefield, captureMouse);
    assert(callbackX == 10 && callbackY == 6); // 2x world coordinate seen by unit rendering.
    assert(mouseX == 4 && mouseY == 4); // UI and cursor keep physical coordinates.

    int cursorType = 0;
    Hooks::calculateCursorTypeAtZoom(
        mouseX, mouseY, &cursorType, &battlefield, captureCursorType);
    assert(cursorX == 10 && cursorY == 6); // Outermost override still maps.

    nestedAreas = &battlefield;
    Hooks::withBattlefieldMouseCoordinates(
        &mouseX, &mouseY, &battlefield, captureNestedMouse);
    assert(nestedX == 10 && nestedY == 6); // Nested override must not map again.
    assert(cursorX == 10 && cursorY == 6);
    assert(mouseX == 4 && mouseY == 4); // Every override restored its own view.

    updateWorld(1); // Resume/camera redraw must survive decoration composition.
    frame();
    data.uiRenderElem = nullptr;
    frame(); // Text disappears without leaving scaled ghost pixels.
    zoom.resetScale();
    frame(); // Returning to 1x also clears the last zoomed presentation.
    frame(); // Native path resumes with the same complete world.
    assert(blends == 2 && copies == 2);
    // A panel pixel drawn after the cursor save-under was captured must survive
    // the cursor erase. The save-under is a frame behind; the sprites are not.
    assert(zoom.addWheelDelta(120));
    Hooks::UiEventArea panelArea{};
    panelArea.tag = 'PANL';
    Hooks::UiElementBase panel{};
    panel.uiEventArea = &panelArea;
    panel.rightX = panel.bottomY = 7;
    std::array<Pixel, 8 * 8> sprites;
    sprites.fill(textColor); // The selection border the game just drew.
    panel.sprites = sprites.data();
    panel.stride = 8;
    int savedX = 2, savedY = 2, savedWidth = 4, savedHeight = 4;
    std::array<Pixel, 64 * 64> savedPixels;
    savedPixels.fill(padding); // Stale: captured before the border was drawn.
    data.uiElement = &panel;
    data.cursorSavedX = &savedX;
    data.cursorSavedY = &savedY;
    data.cursorSavedWidth = &savedWidth;
    data.cursorSavedHeight = &savedHeight;
    data.cursorSavedPixels = savedPixels.data();
    module.surface.renderer = renderer.data();
    module.pitch = pitch * sizeof(Pixel);
    Hooks::drawDecorUiElements(data);
    zoom.finishPresentation(module.surface.renderer, module.pitch, width, height);
    for (int y = savedY; y < savedY + savedHeight; ++y)
        for (int x = savedX; x < savedX + savedWidth; ++x)
            assert(renderer[y * pitch + x] == textColor);

    // The game draws its cursor straight onto the presented frame and erases it at
    // a moment the hook never sees. Composing a panel out of what is on screen
    // reads whatever is left of that cursor into a panel that repaints only where
    // it is marked dirty - and reads it back again every frame after that, which is
    // what pinned a block of cursor over the panel for good.
    constexpr Pixel ghost = 0x4321;
    for (int y = 0; y < savedY; ++y)
        for (int x = savedX; x < savedX + savedWidth; ++x)
            renderer[y * pitch + x] = ghost;
    module.surface.renderer = renderer.data();
    module.pitch = pitch * sizeof(Pixel);
    Hooks::drawDecorUiElements(data);
    zoom.finishPresentation(module.surface.renderer, module.pitch, width, height);
    for (int y = 0; y <= panel.bottomY; ++y)
        for (int x = 0; x <= panel.rightX; ++x)
            assert(renderer[y * pitch + x] == textColor);

    constexpr Pixel iconColor = 0x2468;
    Hooks::UiElementBase icon{};
    icon.type = 320;
    icon.leftX = icon.topY = 2;
    icon.rightX = icon.bottomY = 5;
    std::array<Pixel, 4 * 4> iconSprites;
    iconSprites.fill(iconColor);
    icon.sprites = iconSprites.data();
    icon.stride = 4;
    icon.prev = &panel;
    panel.next = &icon;
    data.uiElement = &icon;
    module.surface.renderer = renderer.data();
    module.pitch = pitch * sizeof(Pixel);
    Hooks::drawDecorUiElements(data);
    zoom.finishPresentation(module.surface.renderer, module.pitch, width, height);
    for (int y = 0; y <= panel.bottomY; ++y)
        for (int x = 0; x <= panel.rightX; ++x)
        {
            const bool inIcon = x >= icon.leftX && x <= icon.rightX &&
                y >= icon.topY && y <= icon.bottomY;
            assert(renderer[y * pitch + x] == (inIcon ? iconColor : textColor));
        }
    panel.next = nullptr;
    data.uiElement = &panel;

    if (argc == 1)
    {
        // The game erases its cursor by stamping the save-under back over the
        // presented frame, after composition and at a moment the hook never sees.
        // Under an element that owns no sprites - the in-game menu's click targets,
        // drawn by the decoration rather than by itself - holding its pixels back for
        // an incremental redraw that can never come keeps that stamp on screen for good.
        pause.x = 4; pause.y = 5;
        pause.prev = &chat;
        chat.x = 20; chat.y = 2; chat.prev = nullptr;
        data.uiRenderElem = &pause;
        const auto decorFrame = [&]
        {
            module.surface.renderer = renderer.data();
            module.pitch = pitch * sizeof(Pixel);
            Hooks::drawDecorUiElements(data);
            zoom.finishPresentation(module.surface.renderer, module.pitch, width, height);
        };

        Hooks::UiElementBase targets{};
        targets.leftX = 8; targets.topY = 8;
        targets.rightX = 15; targets.bottomY = 13;
        targets.sprites = nullptr; // Drawn by the decoration, not by itself.
        data.uiElement = &targets;
        savedX = 0; savedY = 0; savedWidth = 4; savedHeight = 4;
        decorFrame();
        decorFrame(); // Settled: same state in, same pixels out.
        const std::array<Pixel, pitch * height> reference = renderer;

        constexpr Pixel stale = 0x1234;
        for (int y = targets.topY; y <= targets.bottomY; ++y)
            for (int x = targets.leftX; x <= targets.rightX; ++x)
                renderer[y * pitch + x] = stale;

        decorFrame();
        assert(renderer == reference);

        // The strategic map covers the screen and repaints only where the game marks
        // it dirty, so nothing beneath it may compose over it: not the world from a
        // rect reopened for the decoration, and not the zoomed presentation, which
        // holds the panels' own rects back from the previous frame. Both leave the
        // bottom-left panel's rect showing the screen as it was before the map opened.
        // It paints through dstBuf, so it owns no sprites to recognise it by.
        Hooks::UiEventArea mapArea{};
        mapArea.tag = 'TMAP';
        Hooks::UiElementBase map{};
        map.uiEventArea = &mapArea;
        map.rightX = map.clipRight = width - 1;
        map.bottomY = map.clipBottom = height - 1;
        map.stride = width;
        assert(!map.sprites);
        data.uiElement = &map;
        data.uiRenderElem = nullptr;

        assert(zoom.addWheelDelta(120));
        const int held = zoom.presentedScale();
        coverage.fill(0);
        coverage[0] = width / 16; coverage[1] = height / 8;
        decorFrame();
        for (int row = 0; row < height / 8; ++row)
            for (int column = 0; column < width / 16; ++column)
                assert((reinterpret_cast<const uint8_t*>(coverage.data() + 2)
                    [column + kRowStrideByteSize * row] & 0x10) == 0);
        assert(zoom.presentedScale() == held); // Nothing presented over the map.

        data.uiElement = nullptr;
        decorFrame(); // The map closes and the world composes again.
        assert(zoom.presentedScale() == zoom.scale());
    }

    puts("Zoom rendering: pause/chat, 1x-2x, clipping, pitch, circular wrap, source preservation OK");
}
