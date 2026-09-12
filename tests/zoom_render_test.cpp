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

    // A decoration repaints by dirty region at its native coordinates, so the rect
    // reopened for the next frame has to cover those and not just the scaled area it
    // was presented in. Miss them and the buffer holds a fragment of the menu, which
    // anchors as if it were the whole of it.
    if (argc == 1)
    {
        UIScale::Set(2.0f);
        pause.x = 4; pause.y = 5; // Scales to row 10: a different tile row from the source.
        pause.prev = nullptr;
        data.uiRenderElem = &pause;
        data.uiElement = nullptr;
        const auto decorFrame = [&]
        {
            module.surface.renderer = renderer.data();
            module.pitch = pitch * sizeof(Pixel);
            Hooks::drawDecorUiElements(data);
            zoom.finishPresentation(module.surface.renderer, module.pitch, width, height);
        };
        const auto reopenedTileRow0 = [&]
        {
            return reinterpret_cast<const uint8_t*>(coverage.data() + 2)[0] & 0x10;
        };
        const auto clearCoverage = [&]
        {
            coverage.fill(0);
            coverage[0] = width / 16; coverage[1] = height / 8;
        };
        decorFrame();
        clearCoverage();
        decorFrame();
        assert(reopenedTileRow0());

        // Only the cursor's rect is dirty on most frames, so the decoration repaints
        // somewhere else entirely. The rect reopened for the next frame still has to
        // cover everywhere it has been painting, or the menu is only ever redrawn in
        // patches and the cursor scrubs the rest of it away.
        pause.y = 12; // Scales to row 8: this frame alone would reopen tile row 1 only.
        decorFrame();
        clearCoverage();
        decorFrame();
        assert(reopenedTileRow0());

        // Same reason the rect may not shrink: every frame has to place what it does
        // repaint through the same mapping. Anchoring a partial repaint on its own
        // picks a different rule from the whole decoration - here rows 8..9 instead of
        // 14..15 - which is the top border decoration landing inside the menu.
        assert(renderer[14 * pitch + 8] == textColor);
        assert(renderer[15 * pitch + 8] == textColor);

        // The cursor's own rect has to be reopened too. It sits in tile column 1,
        // which the decoration never paints into.
        savedX = 20; savedY = 2; savedWidth = 4; savedHeight = 4;
        decorFrame();
        clearCoverage();
        decorFrame();
        assert(reinterpret_cast<const uint8_t*>(coverage.data() + 2)[1] & 0x10);

        // The game erases its cursor by stamping the save-under back over the presented
        // frame, after composition and at a moment the hook never sees. Composing the
        // next frame has to clear whatever it stamped, or a block of the previous
        // layout stays under the cursor - which is what shows when a modal opens on
        // top of the menu and the decoration is laid out afresh.
        chat.x = 20; chat.y = 2; chat.prev = nullptr;
        pause.prev = &chat; // A second modal on top: more decoration, new layout.
        savedX = 8; savedY = 8; savedWidth = 8; savedHeight = 6;
        decorFrame();
        decorFrame(); // Settled: same state in, same pixels out.
        std::array<Pixel, pitch * height> reference = renderer;

        constexpr Pixel stale = 0x1234;
        for (int y = savedY; y < savedY + savedHeight; ++y)
            for (int x = savedX; x < savedX + savedWidth; ++x)
                renderer[y * pitch + x] = stale;

        decorFrame();
        assert(renderer == reference);

        // The same stamp, but left where the cursor has since moved away from, under
        // an element that owns no sprites - the menu's click targets. Holding its
        // pixels back for an incremental redraw that can never come keeps that stamp
        // on screen for good.
        Hooks::UiElementBase targets{};
        targets.leftX = 8; targets.topY = 8;
        targets.rightX = 15; targets.bottomY = 13;
        targets.sprites = nullptr; // Drawn by the decoration, not by itself.
        data.uiElement = &targets;
        savedX = 0; savedY = 0; savedWidth = 4; savedHeight = 4;
        decorFrame();
        decorFrame();
        reference = renderer;

        const UIScale::Rect covered = Hooks::scaledUiRect(&targets);
        for (int y = covered.y; y < covered.y + covered.height; ++y)
            for (int x = covered.x; x < covered.x + covered.width; ++x)
                renderer[y * pitch + x] = stale;

        decorFrame();
        assert(renderer == reference);
        data.uiElement = nullptr;

        // Opening a modal on top of the menu and closing it again has to leave the
        // menu exactly as it was. The extent is measured from whatever is on screen,
        // so it has to be given up with them - otherwise the menu stays mapped through
        // the rect the pair occupied and the modal's edges stay behind as seams.
        data.uiRenderElem = nullptr;
        decorFrame();
        pause.prev = nullptr;
        data.uiRenderElem = &pause;
        decorFrame();
        decorFrame();
        reference = renderer;

        pause.prev = &chat; // The modal opens on top,
        decorFrame();
        decorFrame();
        pause.prev = nullptr; // and closes again.
        decorFrame();
        decorFrame();
        assert(renderer == reference);
        UIScale::Set(UIScale::kMinFactor);
    }

    puts("Zoom rendering: pause/chat, 1x-2x, clipping, pitch, circular wrap, source preservation OK");
}
