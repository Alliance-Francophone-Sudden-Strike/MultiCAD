// Built with -fno-access-control to exercise the existing hook boundary.
#include "pch.h"
#include "GameDllHooks.h"
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
    zoom.setMode(Zoom::Mode::Steps);
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
    updateWorld(1); // Resume/camera redraw must survive decoration composition.
    frame();
    data.uiRenderElem = nullptr;
    frame(); // Text disappears without leaving scaled ghost pixels.
    zoom.resetScale();
    frame(); // Returning to 1x also clears the last zoomed presentation.
    frame(); // Native path resumes with the same complete world.
    assert(blends == 2 && copies == 2);
    puts("Zoom rendering: pause/chat, 1x-2x, clipping, pitch, circular wrap, source preservation OK");
}
