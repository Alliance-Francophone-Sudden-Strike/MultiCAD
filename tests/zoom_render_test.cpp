// Built with -fno-access-control to exercise the existing hook boundary.
#include "pch.h"
#include "GameDllHooks.h"
#include "ProfileSpecs.h"
#include "renderer.h"
#include "Zoom.h"
#include "UIFilter.h"
#include <array>
#include <cassert>
#include <bit>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>

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

static void __cdecl moveMouseDuringCallback()
{
    *callbackMouseX = 7;
    *callbackMouseY = 9;
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

// A scaled decoration (UIScale): a block of decorColor at logical (4..9, 2..5).
constexpr Pixel decorColor = 0x07e0;
static void __thiscall drawBlock(Hooks::UIRenderElement*, Hooks::UiElementBase* target)
{
    for (int y = 2; y <= 5; ++y)
        for (int x = 4; x <= 9; ++x)
            target->sprites[y * target->stride + x] = decorColor;
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

static void testTrackedRenderer()
{
    auto& zoom = Zoom::GetState();
    zoom.setMode(Zoom::Mode::On);
    Screen::UpdateSize(width, height);
    static ModuleStateShort module{};
    g_moduleState = &module;
    module.surface.main = g_rendererState.surfaces.main;
    module.surface.back = g_rendererState.surfaces.back;
    module.surface.stencil = g_rendererState.surfaces.stencil;
    module.surface.stride = width * sizeof(Pixel);
    module.pitch = pitch * sizeof(Pixel);
    module.shadeColorMask = 0x7bef;
    memset(module.fogSprites, 0, sizeof(module.fogSprites));
    constexpr size_t count = width * (height + 1);
    std::array<Pixel, count> originalMain{}, originalBack{}, expectedMain{}, expectedBack{};
    std::array<Pixel, pitch * height> output{};
    for (size_t i = 0; i < count; ++i)
    {
        originalMain[i] = static_cast<Pixel>(i * 17 + 1);
        originalBack[i] = static_cast<Pixel>(i * 13 + 3);
    }
    const auto check = [&](const char* name, auto draw)
    {
        for (int offset : {0, width * 9, width * 9 + 5})
        {
            for (bool tracked : {false, true})
            {
                std::copy(originalMain.begin(), originalMain.end(), module.surface.main);
                std::copy(originalBack.begin(), originalBack.end(), module.surface.back);
                std::fill_n(module.surface.stencil, count, 0);
                module.windowRect = {0, 0, width - 1, height - 1};
                module.surface.offset = offset * sizeof(Pixel);
                module.surface.y = height - offset / width;
                module.surface.renderer = output.data();
                module.pitch = pitch * sizeof(Pixel);
                if (tracked)
                    zoom.beginWorldIsolation(module.surface.main, module.surface.back, count, width);
                draw();
                if (!tracked)
                {
                    std::copy_n(module.surface.main, count, expectedMain.data());
                    std::copy_n(module.surface.back, count, expectedBack.data());
                }
                else
                {
                    assert(std::equal(expectedMain.begin(), expectedMain.end(), module.surface.main));
                    assert(std::equal(expectedBack.begin(), expectedBack.end(), module.surface.back));
                    zoom.finishWorldIsolation(module.surface.main, module.surface.back);
                    if (!std::equal(originalMain.begin(), originalMain.end(), module.surface.main) ||
                        !std::equal(originalBack.begin(), originalBack.end(), module.surface.back))
                    {
                        printf("Isolation mismatch: %s offset=%d\n", name, offset);
                        assert(false);
                    }
                }
            }
        }
    };
    check("primitives", [&]
    {
        drawMainSurfaceHorLine(-2, 6, width + 4, textColor);
        drawMainSurfaceVertLine(7, -1, height + 2, textColor);
        drawMainSurfaceFilledColorRect(29, 5, 6, 5, textColor);
        drawMainSurfaceShadeColorRect(1, 4, 30, 5, textColor);
        drawMainSurfaceColorPoint(31, 6, textColor);
        drawBackSurfaceColorPoint(31, 6, textColor);
        drawMainSurfaceColorRect(2, 2, 8, 10, textColor);
        drawMainSurfaceColorEllipse(12, 8, 3, textColor, 1000);
    });
    check("outline fallback", [&] { drawMainSurfaceColorOutline(1, 1, 30, 14, textColor); });
    check("scroll guard row", [&] { copyMainBackSurfaces(width, 0); copyMainBackSurfaces(-width, 0); });
    check("copy and aliased destinations", [&]
    {
        copyBackToMainSurfaceRect(0, 0, width, height);
        readMainSurfaceRect(0, 0, width, height, 0, 0, width, module.surface.back);
        copyPixelRectFromTo(0, 0, width, originalMain.data(), 0, 0, width, module.surface.back, width, height);
        convertAllColors(originalMain.data(), module.surface.main, count);
        convertNotMagentaColors(originalBack.data(), module.surface.back, count);
        module.surface.renderer = module.surface.back;
        module.pitch = width * sizeof(Pixel);
        copyMainSurfaceToRenderer(0, 0, width, height);
        copyToRendererSurfaceRect(0, 0, width, height, 0, 0, width, originalMain.data());
        copyMainSurfaceToRendererWithWarFog(0, 0, width - 1, height - 1);
    });
    check("fog", [&] { blendMainSurfaceWithWarFog(0, 0, width - 1, height - 1); });
    check("palette sprites", [&]
    {
        for (int x : {-1, 3, 31})
        {
            drawMainSurfacePaletteSpriteCompact(x, 6, palette, &glyph);
            drawMainSurfacePaletteSprite(x, 6, palette, &glyph);
            drawMainSurfaceVanishingPaletteSprite(x, 6, 8, palette, &glyph);
            drawMainSurfacePaletteSpriteStencil(x, 6, 0x300, palette, &glyph);
            drawMainSurfacePaletteSpriteFrontStencil(x, 6, 0x300, palette, &glyph);
            drawMainSurfacePaletteSpriteBackStencil(x, 6, 0x300, palette, &glyph);
            drawMainSurfaceActualSprite(x, 6, 0x300, palette, &glyph);
            drawMainSurfaceAdjustedSprite(x, 6, 0x300, &glyph);
            drawBackSurfacePalletteSprite(x, 6, palette, &glyph);
            drawBackSurfacePaletteSpriteAndStencil(x, 6, 0x300, palette, &glyph);
            drawBackSurfacePaletteShadedSprite(x, 6, 0x300, palette, &glyph);
        }
    });
    check("ui aliases", [&]
    {
        ImageSpriteUI ui{reinterpret_cast<Addr>(module.surface.back), width, 0, 0, width - 1, height - 1};
        drawUiSprite(31, 6, &glyph, palette, &ui);
        drawVanishingUiSprite(31, 6, 8, palette, &glyph, &ui);
    });
    check("raw sprite", [&]
    {
        const ImageSprite sprite{0, 0, 2, 1, 0xa1, 3, {{0x82, {textColor}}}};
        drawMainSurfaceSprite(31, 6, &sprite);
    });
    zoom.setMode(Zoom::Mode::Off);
    puts("Tracked renderer: clipping, aliases, fog, sprites, wrapping and guard-row restoration OK");
}

static int benchmarkRows;
static bool benchmarkBack;
static void __thiscall prepareScopeCheck(Hooks::UiElementBase* element)
{
    auto* main = g_rendererState.surfaces.main;
    auto* back = g_rendererState.surfaces.back;
    if (element->type == 1)
    {
        assert(main[0] == textColor);
        back[0] = 0x2468;
    }
    else if (element->type == 2)
    {
        assert(main[0] == 1 && back[0] == 2);
        main[0] = 0x1357;
    }
    else
    {
        assert(main[0] == 0x1357 && back[0] == 2);
        main[0] = back[0] = 0x4567;
    }
}

static void benchmarkWorldDraw(S32, S32, const Pixel*, const ImagePaletteSprite*)
{
    const auto clip = g_moduleState->windowRect;
    g_moduleState->windowRect = {0, 0, Screen::width_ - 1, Screen::height_ - 1};
    if (benchmarkRows)
        drawMainSurfaceFilledColorRect(0, 0, Screen::width_, benchmarkRows, textColor);
    if (benchmarkBack)
        copyPixelRectFromTo(0, 0, Screen::width_, g_rendererState.surfaces.main,
                           0, 0, Screen::width_, g_rendererState.surfaces.back, Screen::width_, benchmarkRows);
    g_moduleState->windowRect = clip;
}

static void benchmarkUiDraw(S32, S32, const ImagePaletteSprite*, const void*, const ImageSpriteUI*)
{
    benchmarkWorldDraw(0, 0, nullptr, nullptr);
}

static void testNativeIsolation(uintptr_t game)
{
    Screen::UpdateSize(width, height);
    static ModuleStateShort module{};
    g_moduleState = &module;
    module.surface.main = g_rendererState.surfaces.main;
    module.surface.back = g_rendererState.surfaces.back;
    module.surface.stencil = g_rendererState.surfaces.stencil;
    module.actions.drawMainSurfacePaletteSpriteCompact = drawMainSurfacePaletteSpriteCompact;
    module.actions.blendMainSurfaceWithWarFog_0 = blendMainSurfaceWithWarFog;
    module.actionsPostfix.copyMainSurfaceToRenderer = reinterpret_cast<COPY_MAIN_SURFACE_TO_RENDERER_PTR>(&copyMainSurfaceToRenderer);
    module.actionsPostfix.copyMainSurfaceToRendererWithWarFog = copyMainSurfaceToRendererWithWarFog;
    module.actionsPostfix.readMainSurfaceRect = readMainSurfaceRect;
    module.actionsPostfix.drawUiSprite = drawUiSprite;
    memset(module.fogSprites, 0x80, sizeof(module.fogSprites));
    const auto value = [game](uintptr_t rva) -> uintptr_t& { return *reinterpret_cast<uintptr_t*>(game + rva); };
    value(0x106f6e4) = reinterpret_cast<uintptr_t>(&module.windowRect);
    value(0x106e864) = reinterpret_cast<uintptr_t>(&drawMainSurfacePaletteSpriteStencil);
    value(0x106e868) = reinterpret_cast<uintptr_t>(palette);
    value(0x106e86c) = 0;
    value(0x106e858) = value(0x106e85c) = 0;
    auto* decorArea = reinterpret_cast<int*>(game + 0x103cf10);
    auto* uiArea = reinterpret_cast<int*>(game + 0x103b708);
    const auto dirty = [&]
    {
        for (auto* area : {decorArea, uiArea})
        {
            std::memset(area, 0, 104);
            area[0] = 2;
            area[1] = 1;
        }
        reinterpret_cast<uint8_t*>(decorArea + 2)[0] = 0x10;
        reinterpret_cast<uint8_t*>(decorArea + 2)[1] = 0x10;
        reinterpret_cast<uint8_t*>(uiArea + 2)[0] = 2;
        reinterpret_cast<uint8_t*>(uiArea + 2)[1] = 2;
    };
    Hooks::UIRenderElement decor{};
    decor.vtable = reinterpret_cast<void**>(game + 0xef874);
    decor.rect = {0, 0, 31, 7};
    decor.scale = reinterpret_cast<int*>(&glyph);
    decor.some_ui_param = reinterpret_cast<int>(palette);
    decor.type = 40;
    value(0x103b6ec) = reinterpret_cast<uintptr_t>(&decor);
    std::array<Pixel, width * 8> panelPixels{};
    Hooks::UiElementBase panel{};
    panel.vtable = reinterpret_cast<Hooks::UiElementVtable*>(game + 0xef19c);
    panel.rightX = panel.clipRight = 31;
    panel.bottomY = panel.clipBottom = 7;
    panel.sprites = panelPixels.data();
    panel.dstBuf = panelPixels.data();
    panel.stride = width;
    Hooks::DrawDecorUiElementData data{};
    data.uiRenderElem = &decor;
    data.closedAreaGameDataArray = decorArea;
    data.cadPtr = reinterpret_cast<uintptr_t>(&module.windowRect);
    data.blendMainWithWarFog = reinterpret_cast<decltype(data.blendMainWithWarFog)>(game + 0x982b0);
    data.getFirstDecorUi = reinterpret_cast<decltype(data.getFirstDecorUi)>(game + 0x79b10);
    data.getNextDecorUi = reinterpret_cast<decltype(data.getNextDecorUi)>(game + 0x79b60);
    Hooks::init(game);
    Hooks::configureWorldIsolation(GameVersion::HS_2);
    assert(Hooks::KnownIsolationDecor(&decor));
    assert(Hooks::KnownIsolationUi(&panel));
    auto& zoom = Zoom::GetState();
    zoom.setMode(Zoom::Mode::On);
    std::array<Pixel, width * (height + 1)> mainBefore{}, backBefore{};
    for (size_t i = 0; i < mainBefore.size(); ++i)
    {
        mainBefore[i] = static_cast<Pixel>(i + 1);
        backBefore[i] = static_cast<Pixel>(i + 2);
    }
    std::copy(mainBefore.begin(), mainBefore.end(), module.surface.main);
    std::copy(backBefore.begin(), backBefore.end(), module.surface.back);
    std::fill_n(module.surface.stencil, mainBefore.size(), 0);
    module.surface.stride = width * sizeof(Pixel);
    module.surface.offset = width * 9 * sizeof(Pixel);
    module.surface.y = 7;
    module.windowRect = {0, 0, width - 1, height - 1};
    std::array<Pixel, pitch * height> output{};
    module.surface.renderer = output.data();
    module.pitch = pitch * sizeof(Pixel);
    data.surfaceWidth = width;
    data.surfaceHeight = height;
    value(0x103b6f8) = width;
    value(0x103b6f4) = height;
    dirty();
    Hooks::prepareUiElements(&panel);
    assert(zoom.isolationCopiedBytes() == 0);
    assert(panelPixels[0] == textColor);
    dirty();
    Hooks::drawDecorUiElements(data);
    assert(zoom.isolationCopiedBytes() == width * sizeof(Pixel) * 2);
    assert(output[0] == textColor);
    assert(std::equal(mainBefore.begin(), mainBefore.end(), module.surface.main));
    assert(std::equal(backBefore.begin(), backBefore.end(), module.surface.back));

    value(0x106e850) = value(0x106e900) = 2;
    value(0x106e854) = value(0x106e8fc) = 2;
    value(0x106e858) = 2;
    value(0x106e85c) = 1;
    value(0x106e86c) = reinterpret_cast<uintptr_t>(&glyph);
    data.uiRenderElem = nullptr;
    std::array<Pixel, 64 * 64> eagerSave{};
    std::array<Pixel, pitch * height> eagerOutput{};
    for (bool tracked : {false, true})
    {
        Hooks::configureWorldIsolation(tracked ? GameVersion::HS_2 : GameVersion::UNKNOWN);
        dirty();
        output.fill(0);
        Hooks::drawDecorUiElements(data);
        assert(std::equal(mainBefore.begin(), mainBefore.end(), module.surface.main));
        assert(std::equal(backBefore.begin(), backBefore.end(), module.surface.back));
        auto* save = reinterpret_cast<Pixel*>(game + 0x106c848);
        assert(save[0] == mainBefore[11 * width + 2]);
        assert(output[2 * pitch + 2] == textColor);
        if (tracked)
        {
            assert(output == eagerOutput);
            assert(std::equal(eagerSave.begin(), eagerSave.end(), save));
            assert(zoom.isolationCopiedBytes() < mainBefore.size() * sizeof(Pixel) * 4);
        }
        else
        {
            eagerOutput = output;
            std::copy_n(save, eagerSave.size(), eagerSave.data());
        }
    }
    value(0x106e86c) = 0;
    data.uiRenderElem = &decor;
    std::array<void*, 9> unknownTable{};
    unknownTable[1] = reinterpret_cast<void*>(&drawNative);
    Hooks::UIRenderElement unknown{};
    unknown.vtable = unknownTable.data();
    unknown.type = 60;
    decor.prev = &unknown;
    dirty();
    Hooks::drawDecorUiElements(data);
    assert(std::equal(mainBefore.begin(), mainBefore.end(), module.surface.main));
    assert(std::equal(backBefore.begin(), backBefore.end(), module.surface.back));
    assert(zoom.isolationCopiedBytes() == mainBefore.size() * sizeof(Pixel) * 4);
    decor.prev = nullptr;
    puts("Native Fusion callbacks: lazy UI/decor, cursor background and unknown-callback fallback OK");

    module.actions.drawMainSurfacePaletteSpriteCompact = benchmarkWorldDraw;
    module.actionsPostfix.drawUiSprite = benchmarkUiDraw;
    module.surface.offset = 0;
    module.surface.y = height;
    benchmarkRows = 2;
    Hooks::UiElementVtable scopeTable{};
    scopeTable.fn_A1000 = prepareScopeCheck;
    Hooks::UiElementBase unknownUi{}, field{}, afterField{};
    Hooks::UiEventArea fieldArea{};
    fieldArea.tag = 'FILD';
    unknownUi.vtable = field.vtable = afterField.vtable = &scopeTable;
    unknownUi.type = 1;
    field.type = 2;
    afterField.type = 3;
    field.uiEventArea = &fieldArea;
    panel.prev = &unknownUi;
    unknownUi.prev = &field;
    field.prev = &afterField;
    dirty();
    Hooks::prepareUiElements(&panel);
    assert(module.surface.main[0] == 0x1357 && module.surface.back[0] == 2);
    assert(std::equal(mainBefore.begin() + 1, mainBefore.end(), module.surface.main + 1));
    panel.prev = nullptr;
    puts("UI isolation: contiguous callbacks and FILD commit boundaries OK");

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    for (const auto resolution : {std::array<int, 2>{1920, 1080}, std::array<int, 2>{3840, 2160}})
    {
        const int w = resolution[0], h = resolution[1];
        Screen::UpdateSize(w, h);
        module.surface.stride = w * sizeof(Pixel);
        module.surface.offset = 0;
        module.surface.y = h;
        module.pitch = w * sizeof(Pixel);
        std::vector<Pixel> frame(static_cast<size_t>(w) * h);
        module.surface.renderer = frame.data();
        data.surfaceWidth = w;
        data.surfaceHeight = h;
        value(0x103b6f8) = w;
        value(0x103b6f4) = h;
        zoom.setMode(Zoom::Mode::On);
        for (bool prepare : {true, false})
            for (int rowsToWrite : {0, 100, h, h * 2})
                for (bool tracked : {false, true})
                {
                    Hooks::configureWorldIsolation(tracked ? GameVersion::HS_2 : GameVersion::UNKNOWN);
                    benchmarkRows = std::min(rowsToWrite, h);
                    benchmarkBack = rowsToWrite > h;
                    LARGE_INTEGER start{}, stop{};
                    const auto run = [&]
                    {
                        dirty();
                        if (prepare)
                            Hooks::prepareUiElements(&panel);
                        else
                            Hooks::drawDecorUiElements(data);
                    };
                    run();
                    const size_t bytes = zoom.isolationCopiedBytes();
                    const size_t expected = tracked ? static_cast<size_t>(w) * rowsToWrite * sizeof(Pixel) * 2
                        : static_cast<size_t>(w) * (h + 1) * sizeof(Pixel) * 4;
                    assert(bytes == expected);
                    QueryPerformanceCounter(&start);
                    for (int i = 0; i < 80; ++i)
                        run();
                    QueryPerformanceCounter(&stop);
                    printf("%dx%d %s surface_rows=%d %s bytes=%zu ms=%.4f\n", w, h,
                           prepare ? "prepare" : "decor", rowsToWrite, tracked ? "tracked" : "eager", bytes,
                           1000.0 * (stop.QuadPart - start.QuadPart) / frequency.QuadPart / 80);
                }
    }
    zoom.setMode(Zoom::Mode::Off);
    value(0x103b6ec) = 0;
    Hooks::shutdown();
}

static void testParallelPasses()
{
    static ModuleStateShort module{};
    g_moduleState = &module;
    setPixelColorMasks(0xF800, 0x07E0, 0x001F);
    auto& zoom = Zoom::GetState();
    uint32_t seed = 1;
    const auto random = [&seed] { seed = seed * 1664525 + 1013904223; return static_cast<Pixel>(seed >> 13); };
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);

    for (const auto [w, h] : {std::pair{1920, 1080}, std::pair{2560, 1440}, std::pair{3840, 2160}})
    {
        const int stride = w + 8;
        Screen::UpdateSize(w, h);
        module.windowRect = {0, 0, w - 1, h - 1};
        module.surface.stride = w * sizeof(Pixel);
        module.surface.y = h - 301;
        module.surface.offset = 301 * w * sizeof(Pixel);
        std::generate_n(g_rendererState.surfaces.main, w * (h + 1), random);
        for (size_t row = 0; row < std::size(module.fogSprites); ++row)
            for (size_t column = 0; column < std::size(module.fogSprites[row].unk); ++column)
            {
                const int kind = static_cast<int>(row / 5 + column / 3) % 3;
                module.fogSprites[row].unk[column] = kind == 0 ? 0x80 : kind == 1 ? 0 : static_cast<U8>(random());
            }

        std::vector<Pixel> world(static_cast<size_t>(w) * h), scratch(Zoom::ScaleScratchSize(w) * kMaxRenderThreads);
        std::generate(world.begin(), world.end(), random);
        assert(zoom.ensureBuffers(world.size(), w));

        const auto run = [&](int threads, std::vector<Pixel>& output, const auto& pass)
        {
            StopRenderThreads();
            SetRenderThreads(threads);
            std::fill(output.begin(), output.end(), padding);
            module.surface.renderer = output.data();
            module.pitch = stride * sizeof(Pixel);
            pass();
            LARGE_INTEGER start{}, stop{};
            QueryPerformanceCounter(&start);
            for (int i = 0; i < 20; ++i)
                pass();
            QueryPerformanceCounter(&stop);
            return 1000.0 * (stop.QuadPart - start.QuadPart) / frequency.QuadPart / 20;
        };
        const auto check = [&](const char* name, const auto& pass)
        {
            std::vector<Pixel> serial(static_cast<size_t>(stride) * h), parallel(serial.size());
            const double one = run(1, serial, pass);
            const double many = run(kMaxRenderThreads, parallel, pass);
            assert(serial == parallel);
            printf("%dx%d %s: 1 thread %.3f ms, %d threads %.3f ms\n", w, h, name, one, kMaxRenderThreads, many);
            return serial;
        };

        check("fog copy", [&] { copyMainSurfaceToRendererWithWarFog(0, 0, w - 1, h - 1); });
        check("plain copy", [&] { copyMainSurfaceToRenderer(0, 0, w, h); });

        for (int scale : {5, 8})
        {
            const Zoom::Transform transform = Zoom::MakeTransform({16, 8, w - 32, h - 160}, scale, 7, -5);
            const auto scaled = check(scale == 5 ? "scale 1.25x" : "scale 2x", [&]
            {
                scaleWorldToPresentation(world.data(), w, static_cast<Pixel*>(module.surface.renderer), stride,
                    transform, scratch.data());
            });
            std::vector<Pixel> reference(scaled.size(), padding);
            Zoom::ScaleSharp16(world.data(), w, reference.data(), stride, transform, scratch.data());
            assert(scaled == reference);
        }

        std::vector<Pixel> source(static_cast<size_t>(stride) * h);
        std::generate(source.begin(), source.end(), random);
        const auto presented = check("presentation copies", [&]
        {
            void* renderer = source.data();
            uint32_t pitch = module.pitch;
            zoom.beginPresentation(renderer, pitch, w, h);
            zoom.actualRenderer_ = module.surface.renderer;
            zoom.finishPresentation(renderer, pitch, w, h);
        });
        for (int row = 0; row < h; ++row)
            assert(std::equal(source.begin() + row * stride, source.begin() + row * stride + w,
                presented.begin() + row * stride));

        const size_t surface = static_cast<size_t>(w) * (h + 1);
        std::generate_n(g_rendererState.surfaces.back, surface, random);
        const std::vector<Pixel> backBefore(g_rendererState.surfaces.back, g_rendererState.surfaces.back + surface);
        zoom.setMode(Zoom::Mode::On);
        for (bool fog : {false, true})
        {
            std::vector<Pixel> written[2];
            for (int threads : {1, kMaxRenderThreads})
            {
                StopRenderThreads();
                SetRenderThreads(threads);
                zoom.beginWorldIsolation(g_rendererState.surfaces.main, g_rendererState.surfaces.back, surface, w);
                module.surface.renderer = g_rendererState.surfaces.back;
                module.pitch = w * sizeof(Pixel);
                if (fog)
                    copyMainSurfaceToRendererWithWarFog(0, 0, w - 1, h - 1);
                else
                    copyMainSurfaceToRenderer(0, 0, w, h);
                written[threads > 1].assign(g_rendererState.surfaces.back, g_rendererState.surfaces.back + surface);
                zoom.finishWorldIsolation(g_rendererState.surfaces.main, g_rendererState.surfaces.back);
                assert(std::equal(backBefore.begin(), backBefore.end(), g_rendererState.surfaces.back));
            }
            assert(written[0] == written[1] && written[0] != backBefore);
        }
        zoom.setMode(Zoom::Mode::Off);
    }

    StopRenderThreads();
    SetRenderThreads(1);
    ParallelRows(0, 1024, 1, [](int, int) {});
    SetRenderThreads(kMaxRenderThreads);
    std::mutex idsMutex;
    std::set<DWORD> ids;
    ParallelRows(0, 1024, 1, [&](int, int)
    {
        std::lock_guard lock(idsMutex);
        ids.insert(GetCurrentThreadId());
    });
    assert(ids.size() > 1 || std::thread::hardware_concurrency() < 2);

    DWORD_PTR process = 0, system = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
    if (std::popcount(system) > 1)
    {
        const auto currentMask = [system]
        {
            const DWORD_PTR mask = SetThreadAffinityMask(GetCurrentThread(), system);
            SetThreadAffinityMask(GetCurrentThread(), mask);
            return mask;
        };
        StopRenderThreads();
        assert(SetProcessAffinityMask(GetCurrentProcess(), 1));
        std::set<DWORD_PTR> masks;
        ParallelRows(0, 1024, 1, [&](int, int)
        {
            std::lock_guard lock(idsMutex);
            masks.insert(currentMask());
        });
        assert((masks == std::set<DWORD_PTR>{ 1, system & ~DWORD_PTR{ 1 } }));
        DWORD_PTR widened = 0;
        GetProcessAffinityMask(GetCurrentProcess(), &widened, &system);
        assert(widened == system);
        DWORD_PTR created = 0;
        std::thread([&] { PinNewThread(); created = currentMask(); }).join();
        assert(created == 1);
        StopRenderThreads();
        GetProcessAffinityMask(GetCurrentProcess(), &widened, &system);
        assert(widened == 1);
        SetProcessAffinityMask(GetCurrentProcess(), process);
    }

    StopRenderThreads();
    SetRenderThreads(1);
    zoom.setMode(Zoom::Mode::Off);
    puts("Parallel passes: fog copy, plain copy, scaler and presentation copies match at 1 and N threads");
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    uintptr_t nativeGame = 0;
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
        nativeGame = game;
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

    std::array<Pixel, width * height> world{}, scaledWorld{};
    std::array<Pixel, Zoom::ScaleScratchSize(width)> worldScratch{};
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
        Zoom::ScaleSharp16(world.data(), width, scaledWorld.data(), width, zoom.transform(),
                           worldScratch.data());
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                Pixel expected = scaledWorld[y * width + x];
                if (data.uiRenderElem && ((y == 1 && x < 2) || (y == height - 1 && x == width - 1)))
                    expected = textColor;
                assert(renderer[y * pitch + x] == expected);
            }
            for (int x = width; x < pitch; ++x)
                assert(renderer[y * pitch + x] == padding);
        }
    };

    int cursorRedraw = 0;
    data.cursorRedrawFlag = &cursorRedraw;

    frame(); // Native 1x text must be visible, but absent from the world cache.
    assert(nativeDraws == 2 && blends == 1 && copies == 1);
    // Nothing of ours overwrote the frame here, so leave the game's own cursor
    // redraw schedule alone.
    assert(cursorRedraw == 0);
    for (int scale = 5; scale <= 8; ++scale)
    {
        assert(zoom.addWheelDelta(120));
        frame();
        // Composition replaced every pixel, the ones the cursor sits on
        // included. The game redraws the cursor only when this flag says so, so
        // a frame that leaves it clear is presented with no cursor at all.
        assert(cursorRedraw == 1);
        cursorRedraw = 0;
        frame(); // Paused presentation tick, no world update.
        assert(zoom.presentedScale() == scale);
    }
    cursorRedraw = 0;
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
    assert(cameraHook->detour == reinterpret_cast<uintptr_t>(&Hooks::moveCameraAtZoom_ver<GameVersion::SS_2>));
    static_assert(ValidateZoomTraits<GameVersion::SS_RW_V2_4>());
    const auto rwWorldHook = std::find_if(
        hooks_game_ss_rw_v2_4<GameVersion::SS_RW_V2_4>.begin(),
        hooks_game_ss_rw_v2_4<GameVersion::SS_RW_V2_4>.end(),
        [](const HookSpec& hook) { return hook.targetRva == 0x952D1; });
    assert(rwWorldHook != hooks_game_ss_rw_v2_4<GameVersion::SS_RW_V2_4>.end());
    assert(rwWorldHook->detour == reinterpret_cast<uintptr_t>(
        &Hooks::renderWorldAtZoom_ver<GameVersion::SS_RW_V2_4>));
    assert(rwWorldHook->opcode == 0xE8);
    const auto rwLoopHook = std::find_if(
        hooks_game_ss_rw_v2_4<GameVersion::SS_RW_V2_4>.begin(),
        hooks_game_ss_rw_v2_4<GameVersion::SS_RW_V2_4>.end(),
        [](const HookSpec& hook) { return hook.targetRva == 0x952EE; });
    assert(rwLoopHook != hooks_game_ss_rw_v2_4<GameVersion::SS_RW_V2_4>.end());
    assert(rwLoopHook->overwriteSize == 24);

    static_assert(ValidateZoomTraits<GameVersion::SS_GOLD_HD_1_2_INT>());
    const auto ss1LoopHook = std::find_if(
        hooks_game_ss_gold_hd_v1_2<GameVersion::SS_GOLD_HD_1_2_INT>.begin(),
        hooks_game_ss_gold_hd_v1_2<GameVersion::SS_GOLD_HD_1_2_INT>.end(),
        [](const HookSpec& hook) { return hook.targetRva == 0x6AC80; });
    assert(ss1LoopHook != hooks_game_ss_gold_hd_v1_2<GameVersion::SS_GOLD_HD_1_2_INT>.end());
    assert(ss1LoopHook->overwriteSize == 30);

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

    Hooks::withBattlefieldMouseCoordinates(
        &mouseX, &mouseY, &battlefield, moveMouseDuringCallback);
    assert(mouseX == 7 && mouseY == 9);
    mouseX = 4;
    mouseY = 4;

    Hooks::UiEventArea overlay{};
    overlay.tag = 'OVRL';
    overlay.width = 8;
    overlay.height = 8;
    battlefield.next = &overlay;
    overlay.flags = 0;
    assert(Hooks::areaOwnsPoint(&battlefield, &battlefield, 4, 4, 8));
    overlay.flags = 8;
    assert(!Hooks::areaOwnsPoint(&battlefield, &battlefield, 4, 4, 8));
    battlefield.next = nullptr;

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

    {
        // A scaled decoration under the cursor. The game erases its cursor by stamping its
        // save-under back, and redraws it in place: that save-under must hold the
        // decoration, or the cursor's box shows the world through it (a hole that stays,
        // because unchanged pixels under the cursor are not written again).
        zoom.resetScale();
        UIScale::Allow(true);
        UIScale::Set(width / 2, height / 2, width, height);  // x2: the block is (8..19, 4..11)
        assert(UIScale::Active());
        std::array<void*, 9> blockTable{};
        blockTable[8] = reinterpret_cast<void*>(&drawBlock);
        Hooks::UIRenderElement block{};
        block.vtable = blockTable.data();
        block.type = 60;
        data.uiRenderElem = &block;
        data.uiElement = nullptr;
        data.getFirstDecorUi = next;  // the world is not presented: nothing changed there
        int redraw = 0;
        data.cursorRedrawFlag = &redraw;
        int savedX = 10, savedY = 5, savedWidth = 4, savedHeight = 4;
        std::array<Pixel, 64 * 64> savedPixels;
        constexpr Pixel world = 0x1111, cursorInk = 0xffff;
        savedPixels.fill(world);
        data.cursorSavedX = &savedX;
        data.cursorSavedY = &savedY;
        data.cursorSavedWidth = &savedWidth;
        data.cursorSavedHeight = &savedHeight;
        data.cursorSavedPixels = savedPixels.data();
        const auto scaledFrame = [&]
        {
            redraw = 0;
            module.surface.renderer = renderer.data();
            module.pitch = pitch * sizeof(Pixel);
            Hooks::drawDecorUiElements(data);
        };
        const auto savedAt = [&](int x, int y) { return savedPixels[(y - savedY) * 64 + x - savedX]; };
        const auto eraseAndRedraw = [&]  // the game, when told to (or on its own)
        {
            for (int y = savedY; y < savedY + savedHeight; ++y)
                for (int x = savedX; x < savedX + savedWidth; ++x)
                    renderer[y * pitch + x] = savedAt(x, y);
            renderer[savedY * pitch + savedX] = cursorInk;
        };

        scaledFrame();  // first frame: written everywhere, the save-under follows
        assert(redraw == 1);
        for (int y = savedY; y < savedY + savedHeight; ++y)
            for (int x = savedX; x < savedX + savedWidth; ++x)
                assert(savedAt(x, y) == decorColor);
        eraseAndRedraw();
        scaledFrame();  // settled: nothing to write, the cursor is left alone
        assert(redraw == 0 && renderer[savedY * pitch + savedX] == cursorInk);

        // A save-under taken from the world (the hole): the hook corrects it and asks for a
        // redraw, without writing over the cursor itself.
        savedPixels.fill(world);
        eraseAndRedraw();
        scaledFrame();
        assert(redraw == 1 && renderer[savedY * pitch + savedX] == cursorInk);
        eraseAndRedraw();
        for (int y = savedY; y < savedY + savedHeight; ++y)
            for (int x = savedX; x < savedX + savedWidth; ++x)
                assert(savedAt(x, y) == decorColor &&
                    renderer[y * pitch + x] == (x == savedX && y == savedY ? cursorInk : decorColor));
        scaledFrame();
        assert(redraw == 0);

        // The world presented under the cursor this frame: the block goes back there too.
        data.getFirstDecorUi = first;
        scaledFrame();
        assert(redraw == 1);
        for (int y = savedY; y < savedY + savedHeight; ++y)
            for (int x = savedX; x < savedX + savedWidth; ++x)
                assert(renderer[y * pitch + x] == decorColor && savedAt(x, y) == decorColor);

        UIScale::Allow(false);
        data.uiRenderElem = nullptr;
        data.cursorSavedPixels = nullptr;
        data.cursorRedrawFlag = nullptr;
    }

    testTrackedRenderer();
    testParallelPasses();
    if (nativeGame)
        testNativeIsolation(nativeGame);

    puts("Zoom rendering: pause/chat, 1x-2x, clipping, pitch, circular wrap, source preservation OK");
}
