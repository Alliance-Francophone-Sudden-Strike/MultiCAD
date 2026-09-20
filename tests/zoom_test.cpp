#include "Zoom.h"
#include "ZoomIndicator.h"

#include <array>
#include <cassert>
#include <vector>

int main()
{
    using namespace Zoom;

    static_assert(ParseMode("off") == Mode::Off);
    static_assert(ParseMode(" On ") == Mode::On);
    static_assert(ParseMode("steps") == Mode::Off);
    static_assert(ParseMode("smooth") == Mode::Off);
    static_assert(ParseMode("unknown") == Mode::Off);
    static_assert(ParseIndicatorAnchor("right") == IndicatorAnchor::Right);
    static_assert(ParseIndicatorAnchor("hidden") == IndicatorAnchor::Hidden);
    static_assert(ParseIndicatorAnchor("unknown") == IndicatorAnchor::Left);
    static_assert(ViewportExtent(100, kMinScale) == 100);
    static_assert(ViewportExtent(100, kMaxScale) == 50);

    const Transform one = MakeTransform({ 10, 20, 8, 4 });
    assert(one.source.x == 10 && one.source.y == 20);
    assert(one.sourceX(10) == 10 && one.sourceX(17) == 17);

    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        const Transform level = MakeTransform({ 10, 20, 100, 80 }, scale);
        assert(level.sourceX(10) == level.source.x);
        assert(level.sourceX(109) == level.source.x + level.source.width - 1);
        assert(level.sourceY(20) == level.source.y);
        assert(level.sourceY(99) == level.source.y + level.source.height - 1);
    }

    const Transform two = MakeTransform({ 0, 0, 8, 4 }, 8);
    assert(two.source.x == 2 && two.source.y == 1);
    assert(two.sourceX(0) == 2 && two.sourceX(7) == 5);
    assert(two.sourceY(0) == 1 && two.sourceY(3) == 2);

    const Transform corner = MakeTransform({ 10, 20, 8, 4 }, 8, -2, 1);
    assert(corner.source.x == 10 && corner.source.y == 22);

    std::array<uint16_t, 24> source{}; // 4 rows, pitch 6
    std::array<uint16_t, 40> destination{}; // 4 rows, pitch 10
    std::array<uint16_t, ScaleScratchSize(8)> scratch{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            source[y * 6 + x] = static_cast<uint16_t>(y * 10 + x);
    ScaleSharp16(source.data(), 6, destination.data(), 10, two, scratch.data());
    assert(destination[0] == 12 && destination[7] == 15);
    assert(destination[30] == 22 && destination[37] == 25);
    assert(destination[8] == 0 && destination[38] == 0); // padded pitch untouched

    // The divide-free walk is the whole point of the Sampler: it replaces the
    // multiply-and-divide Transform::sourceX did per pixel. It must track that
    // computation exactly, or the rendered image drifts from the mapping the
    // mouse hit-testing uses.
    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        const Transform level = MakeTransform({ 10, 20, 100, 80 }, scale);
        const int left = level.destination.x;

        Sampler walk = level.samplerX(left);
        for (int x = left; x < left + level.destination.width; ++x, walk.advance())
        {
            const Sampler direct = level.samplerX(x);
            assert(walk.index == direct.index && walk.remainder == direct.remainder);
            assert(walk.index == level.sourceX(x));
        }
    }

    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        const Transform level = MakeTransform({ 10, 20, 100, 80 }, scale);
        const int top = level.destination.y;

        Sampler walk = level.samplerY(top);
        for (int y = top; y < top + level.destination.height; ++y, walk.advance())
        {
            const Sampler direct = level.samplerY(y);
            assert(walk.index == direct.index && walk.remainder == direct.remainder);
            assert(walk.index == level.sourceY(y));
        }
    }

    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        const Transform level = MakeTransform({ 10, 20, 100, 80 }, scale);
        const int lastColumn = level.source.x + level.source.width - 1;
        const int lastRow = level.source.y + level.source.height - 1;

        Sampler x = level.samplerX(level.destination.x);
        for (int px = 0; px < level.destination.width; ++px, x.advance())
        {
            const int t = x.weight();
            assert(t >= 0 && t <= 16);
            assert(x.index >= level.source.x && x.index <= lastColumn);
            assert(t == 0 || x.index + 1 <= lastColumn);
        }

        Sampler y = level.samplerY(level.destination.y);
        for (int py = 0; py < level.destination.height; ++py, y.advance())
        {
            const int t = y.weight();
            assert(t >= 0 && t <= 16);
            assert(y.index >= level.source.y && y.index <= lastRow);
            assert(t == 0 || y.index + 1 <= lastRow);
        }
    }

    const auto scaled = [](Rect field, int scale, const std::vector<uint16_t>& pixels)
    {
        const Transform level = MakeTransform(field, scale);
        std::vector<uint16_t> out(pixels.size());
        std::vector<uint16_t> rows(ScaleScratchSize(level.destination.width));
        ScaleSharp16(pixels.data(), field.width, out.data(), field.width, level, rows.data());
        return out;
    };

    {
        const Rect field{ 0, 0, 20, 8 };
        std::vector<uint16_t> pixels(20 * 8);
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = static_cast<uint16_t>(i * 7 + 1);
        assert(scaled(field, kMinScale, pixels) == pixels);
    }

    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        const Rect field{ 0, 0, 28, 8 };
        const std::vector<uint16_t> flat(28 * 8, 0x5AD3);
        assert(scaled(field, scale, flat) == flat);
    }

    const auto blended = [&scaled](int scale, int extent, bool vertical)
    {
        const Rect field = vertical ? Rect{ 0, 0, 8, extent } : Rect{ 0, 0, extent, 8 };
        std::vector<uint16_t> pixels(static_cast<size_t>(field.width) * field.height);
        for (int y = 0; y < field.height; ++y)
            for (int x = 0; x < field.width; ++x)
                pixels[y * field.width + x] = ((vertical ? y : x) & 1) ? 0xFFFF : 0x0000;

        int count = 0;
        for (const uint16_t pixel : scaled(field, scale, pixels))
        {
            assert(pixel == 0x0000 || pixel == 0xFFFF || pixel == 0x8410);
            count += pixel == 0x8410;
        }
        return count;
    };

    for (const bool vertical : { false, true })
    {
        assert(blended(5, 20, vertical) == 8 * 20 / 5);
        assert(blended(6, 24, vertical) == 8 * 24 / 3);
        assert(blended(7, 28, vertical) == 8 * 28 / 7);
        assert(blended(kMinScale, 20, vertical) == 0);
        assert(blended(kMaxScale, 20, vertical) == 0);
    }

    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        const Rect field{ 0, 0, 20, 24 };
        const Transform level = MakeTransform(field, scale);
        const int lastColumn = level.source.x + level.source.width - 1;
        const int lastRow = level.source.y + level.source.height - 1;
        std::vector<uint16_t> pixels(20 * 24, 0x1111);
        for (int y = 0; y < 24; ++y)
            pixels[y * 20 + lastColumn] = 0x2222;
        for (int x = 0; x < 20; ++x)
            pixels[lastRow * 20 + x] = 0x3333;
        pixels[lastRow * 20 + lastColumn] = 0x4444;

        const auto out = scaled(field, scale, pixels);
        assert(out[19] == 0x2222);
        assert(out[23 * 20] == 0x3333);
        assert(out[23 * 20 + 19] == 0x4444);
    }

    State state;
    assert(!state.addWheelDelta(120));
    state.setMode(Mode::On);
    assert(!state.addWheelDelta(60));
    assert(state.addWheelDelta(60) && state.scale() == 5);
    state.setInvertZoom(true);
    assert(state.addWheelDelta(-120) && state.scale() == 6); // inverted: negative delta zooms in
    assert(state.addWheelDelta(120) && state.scale() == 5);
    state.setInvertZoom(false);
    state.setBattlefield({ 10, 20, 100, 80 });
    assert(state.transform().destination.x == 10);
    assert(state.mapX(10) == 10 && state.mapX(109) == 109); // target is not presented yet
    state.setPanDirection(State::PanLeft, true);
    state.updatePan(100, 100, 1000);
    state.updatePan(100, 100, 1016);
    assert(state.transform().source.x == 12);
    state.updatePan(90, 100, 1032);
    assert(state.transform().source.x == 20);
    state.setPanDirection(State::PanLeft, false);
    state.setPointer(10, 50);
    state.updatePan(90, 100, 1048);
    assert(state.transform().source.x == 10);
    assert(state.viewportOffsetX(50) == 0);
    state.markPresented();
    assert(state.presentedScale() == 5);
    assert(state.mapX(10) == state.transform().source.x);
    assert(state.mapX(109) == state.transform().source.x + state.transform().source.width - 1);

    State cursorZoom;
    cursorZoom.setMode(Mode::On);
    cursorZoom.setZoomOnCursor(true);
    cursorZoom.setBattlefield({ 0, 0, 100, 80 });
    cursorZoom.setPointer(0, 0);
    assert(cursorZoom.addWheelDelta(120));
    assert(cursorZoom.transform().source.x == 0 && cursorZoom.transform().source.y == 0);
    assert(cursorZoom.addWheelDelta(-120) && cursorZoom.scale() == kMinScale);
    assert(cursorZoom.transform().source.x == 0 && cursorZoom.transform().source.y == 0);
    cursorZoom.setPointer(99, 79);
    assert(cursorZoom.addWheelDelta(480) && cursorZoom.scale() == kMaxScale);
    assert(cursorZoom.transform().source.x == 50 && cursorZoom.transform().source.y == 40);
    cursorZoom.setPointer(50, 40);
    assert(cursorZoom.addWheelDelta(-480) && cursorZoom.scale() == kMinScale);
    assert(cursorZoom.transform().source.x == 0 && cursorZoom.transform().source.y == 0);

    State centeredZoom;
    centeredZoom.setMode(Mode::On);
    centeredZoom.setBattlefield({ 0, 0, 100, 80 });
    centeredZoom.setPointer(0, 0);
    assert(centeredZoom.addWheelDelta(480) && centeredZoom.scale() == kMaxScale);
    assert(centeredZoom.transform().source.x == 25 && centeredZoom.transform().source.y == 20);

    State matchedSpeed;
    matchedSpeed.setMode(Mode::On);
    matchedSpeed.addWheelDelta(120);
    matchedSpeed.setBattlefield({ 10, 20, 100, 80 });
    matchedSpeed.setPanDirection(State::PanLeft, true);
    matchedSpeed.updatePan(100, 100, 1000);
    matchedSpeed.updatePan(94, 100, 1016);
    assert(matchedSpeed.transform().source.x == 20);
    matchedSpeed.updatePan(94, 100, 1032);
    assert(matchedSpeed.transform().source.x == 14);

    state.setDragButton(State::LeftButton, true, true);
    state.setDragButton(State::RightButton, true);
    assert(state.dragging() && state.battlefieldDragging() && !state.addWheelDelta(120));
    state.setDragButton(State::LeftButton, false);
    assert(state.dragging() && !state.battlefieldDragging() && !state.addWheelDelta(120));
    state.setDragButton(State::RightButton, false);
    assert(state.addWheelDelta(-120) && state.scale() == 4);
    state.addWheelDelta(60);
    state.cancelInput();
    assert(!state.addWheelDelta(60) && state.scale() == 4);
    state.addWheelDelta(120);
    state.resetScale();
    assert(state.scale() == 4);
    state.noteZoomInput(1000);
    assert(state.indicatorVisible(1000));
    assert(state.indicatorVisible(1000 + kIndicatorHoldMs - 1));
    assert(!state.indicatorVisible(1000 + kIndicatorHoldMs));
    assert(state.indicatorPending());
    state.finishIndicatorFrame(1000 + kIndicatorHoldMs);
    assert(!state.indicatorPending());
    state.setPersistentIndicator(true);
    state.addWheelDelta(120);
    state.noteZoomInput(2000);
    assert(state.indicatorVisible(2000 + kIndicatorHoldMs));
    assert(state.indicatorOpacity(2000 + kIndicatorHoldMs) == 16);
    state.finishIndicatorFrame(2000 + kIndicatorHoldMs);
    assert(state.indicatorPending());
    state.noteZoomInput(4000);
    state.addWheelDelta(-120);
    assert(state.indicatorVisible(4000 + kIndicatorHoldMs - 1));
    assert(!state.indicatorVisible(4000 + kIndicatorHoldMs));
    state.finishIndicatorFrame(4000 + kIndicatorHoldMs);
    state.addWheelDelta(120);
    assert(state.indicatorVisible(4000 + kIndicatorHoldMs));
    state.setIndicatorAnchor(IndicatorAnchor::Hidden);
    state.noteZoomInput(2000);
    assert(!state.indicatorPending());
    assert(!state.indicatorVisible(2000));

    State movement;
    movement.setMode(Mode::On);
    movement.setBattlefield({ 0, 0, 100, 80 });
    for (int scale = kMinScale; scale <= kMaxScale; ++scale)
    {
        movement.markPresented();
        int movedX = 0;
        int movedY = 0;
        for (int tick = 0; tick < scale; ++tick)
        {
            int dx = 1;
            int dy = -1;
            movement.scaleCameraMovement(dx, dy);
            movedX += dx;
            movedY += dy;
        }
        assert(movedX == kMinScale && movedY == -kMinScale);
        movement.addWheelDelta(120);
    }

    static_assert(ParseIndicatorShape("bars") == IndicatorShape::Bars);
    static_assert(ParseIndicatorShape("unknown") == IndicatorShape::Squares);

    std::array<uint16_t, 64 * 128> indicator{};
    DrawIndicatorSquares16(indicator.data(), 64, 64, 128, static_cast<float>(kMinScale), false);
    assert(indicator[18 * 64 + 12] != 0);
    assert(indicator[98 * 64 + 12] != 0);
    indicator.fill(0);
    DrawIndicatorSquares16(indicator.data(), 64, 64, 128, static_cast<float>(kMaxScale), true);
    assert(indicator[18 * 64 + 40] != 0);
    assert(indicator[18 * 64 + 12] == 0);

    indicator.fill(0);
    DrawIndicatorBars16(indicator.data(), 64, 64, 128, static_cast<float>(kMinScale), false);
    assert(indicator[23 * 64 + 12] != 0);
    assert(indicator[23 * 64 + 32] == 0);
    assert(indicator[103 * 64 + 12] != 0);
    assert(indicator[103 * 64 + 32] != 0);
    indicator.fill(0);
    DrawIndicatorBars16(indicator.data(), 64, 64, 128, static_cast<float>(kMaxScale), true);
    assert(indicator[23 * 64 + 51] != 0);
    assert(indicator[23 * 64 + 12] == 0);
    assert(indicator[103 * 64 + 24] == 0);

    indicator.fill(0);
    DrawIndicator16(IndicatorShape::Bars, indicator.data(), 64, 64, 128, static_cast<float>(kMinScale), false);
    assert(indicator[103 * 64 + 12] != 0);
    indicator.fill(0);
    DrawIndicator16(IndicatorShape::Squares, indicator.data(), 64, 64, 128, static_cast<float>(kMinScale), false);
    assert(indicator[18 * 64 + 12] != 0);

    {
        constexpr int w = 320;
        constexpr int h = 700;
        constexpr int pitch = w + 4;
        constexpr uint16_t untouched = 0xABCD;
        std::vector<uint16_t> big(static_cast<size_t>(pitch) * h, untouched);

        for (IndicatorShape drawn : { IndicatorShape::Squares, IndicatorShape::Bars })
            for (bool right : { false, true })
            {
                std::fill(big.begin(), big.end(), untouched);
                DrawIndicator16(drawn, big.data(), pitch, w, h, static_cast<float>(kMaxScale),
                                right, 16, PanelScale::kMaxQuarters);

                int painted = 0;
                for (int y = 0; y < h; ++y)
                {
                    for (int x = 0; x < w; ++x)
                        if (big[static_cast<size_t>(y) * pitch + x] != untouched)
                            ++painted;
                    for (int x = w; x < pitch; ++x)
                        assert(big[static_cast<size_t>(y) * pitch + x] == untouched);
                }
                assert(painted > 0);
            }
    }

    State indicatorScale;
    assert(indicatorScale.indicatorScaleQuarters() == PanelScale::kMinQuarters);
    indicatorScale.setIndicatorScale(PanelScale::kMaxQuarters);
    assert(indicatorScale.indicatorScaleQuarters() == PanelScale::kMaxQuarters);
    indicatorScale.setIndicatorScale(0);
    assert(indicatorScale.indicatorScaleQuarters() == PanelScale::kMinQuarters);
    indicatorScale.setIndicatorScale(99);
    assert(indicatorScale.indicatorScaleQuarters() == PanelScale::kMaxQuarters);

    State shape;
    assert(shape.indicatorShape() == IndicatorShape::Squares);
    shape.setIndicatorShape(IndicatorShape::Bars);
    assert(shape.indicatorShape() == IndicatorShape::Bars);

    // Indicator easing. kIndicatorAnimMs is a tuning knob and may legitimately
    // be set to 1, which makes the bar snap; assert the contract that holds for
    // any duration, and only check the mid-flight curve when the configured
    // duration can actually express one.
    State anim;
    anim.setMode(Mode::On);
    anim.setBattlefield({ 0, 0, 100, 80 });
    anim.updatePan(0, 0, 0); // primes animatedScale() at the current (min) scale
    assert(anim.animatedScale() == static_cast<float>(kMinScale));
    anim.addWheelDelta(120); // scale steps to 5
    anim.updatePan(0, 0, 0); // no time elapsed: nothing moves yet
    assert(anim.animatedScale() == static_cast<float>(kMinScale));

    if constexpr (kIndicatorAnimMs >= 2)
    {
        anim.updatePan(0, 0, kIndicatorAnimMs / 2); // eases toward the target rather than snapping
        assert(anim.animatedScale() > 4.f && anim.animatedScale() < 5.f);
    }

    anim.updatePan(0, 0, kIndicatorAnimMs * 4);
    assert(anim.animatedScale() == 5.f); // settles once fully eased, without overshooting
    anim.updatePan(0, 0, kIndicatorAnimMs * 8);
    assert(anim.animatedScale() == 5.f); // and stays settled

    State presentation;
    presentation.setMode(Mode::On);
    assert(presentation.ensureBuffers(6, 3));
    std::array<uint16_t, 8> renderer{ 1, 2, 3, 99, 4, 5, 6, 99 };
    void* rendererPtr = renderer.data();
    uint32_t rendererPitch = 4 * sizeof(uint16_t);
    presentation.beginPresentation(rendererPtr, rendererPitch, 3, 2);
    assert(rendererPtr == presentation.presentationBuffer());
    assert(rendererPitch == 3 * static_cast<int>(sizeof(uint16_t)));
    presentation.presentationBuffer()[0] = 7;
    presentation.presentationBuffer()[5] = 8;
    presentation.finishPresentation(rendererPtr, rendererPitch, 3, 2);
    assert(rendererPtr == renderer.data());
    assert(rendererPitch == 4 * static_cast<int>(sizeof(uint16_t)));
    assert((renderer == std::array<uint16_t, 8>{ 7, 2, 3, 99, 4, 5, 8, 99 }));

    State cursor;
    cursor.setMode(Mode::On);
    assert(cursor.ensureBuffers(16, 4));
    std::array<uint16_t, 16> cursorRenderer{};
    void* cursorRendererPtr = cursorRenderer.data();
    uint32_t cursorRendererPitch = 4 * sizeof(uint16_t);
    int cursorX = 1, cursorY = 1, cursorWidth = 2, cursorHeight = 2;
    std::array<uint16_t, 64 * 64> cursorSave{};

    for (int i = 0; i < 16; ++i)
        cursorRenderer[i] = static_cast<uint16_t>(100 + i);
    cursor.beginPresentation(cursorRendererPtr, cursorRendererPitch, 4, 4);
    assert(cursorRendererPtr == cursor.presentationBuffer());
    cursor.refreshCursorSave(
        4, 4, &cursorX, &cursorY, &cursorWidth, &cursorHeight, cursorSave.data());
    // Native erase must restore the composed frame, not the frame drawn on.
    assert(cursorSave[0] == 105 && cursorSave[1] == 106);
    assert(cursorSave[64] == 109 && cursorSave[65] == 110);
    cursor.finishPresentation(cursorRendererPtr, cursorRendererPitch, 4, 4);
    assert(cursorRendererPtr == cursorRenderer.data());
    // Leaving the save-under armed is what keeps a cursor drawn on a frame that
    // never composes again from lingering as a ghost.
    assert(cursorHeight == 2 && cursorWidth == 2);

    int clippedX = 3, clippedY = 3;
    cursorSave.fill(0);
    cursor.refreshCursorSave(
        4, 4, &clippedX, &clippedY, &cursorWidth, &cursorHeight, cursorSave.data());
    assert(cursorSave[0] == 115); // Off-screen rows and columns stay untouched.
    assert(cursorSave[1] == 0 && cursorSave[64] == 0);

    int hidden = 0;
    cursorSave.fill(0);
    cursor.refreshCursorSave(
        4, 4, &cursorX, &cursorY, &cursorWidth, &hidden, cursorSave.data());
    assert(cursorSave[0] == 0); // No cursor drawn, nothing to hand back.

    std::array<uint16_t, 16> overlay{};
    for (int i = 0; i < 16; ++i)
        overlay[i] = static_cast<uint16_t>(200 + i);
    cursorSave.fill(0);
    cursor.refreshCursorSaveRect(
        overlay.data(), 4, { 2, 1, 2, 3 },
        4, 4, &cursorX, &cursorY, &cursorWidth, &cursorHeight, cursorSave.data());
    assert(cursorSave[0] == 0);
    assert(cursorSave[1] == 206);
    assert(cursorSave[64] == 0 && cursorSave[65] == 210);
    cursorSave.fill(0);
    cursor.refreshCursorSaveRect(
        overlay.data(), 4, { 0, 0, 1, 1 },
        4, 4, &cursorX, &cursorY, &cursorWidth, &cursorHeight, cursorSave.data());
    assert(cursorSave[0] == 0 && cursorSave[1] == 0);

    // A cursor band that overlaps the region's rows but none of its columns
    // leaves right < left after clipping. Without an emptiness check that is a
    // std::copy with a negative count, which memmoves a huge block.
    int leftOfX = 0, leftOfY = 1;
    cursorSave.fill(0);
    cursor.refreshCursorSaveRect(
        overlay.data(), 4, { 3, 0, 1, 4 },
        4, 4, &leftOfX, &leftOfY, &cursorWidth, &cursorHeight, cursorSave.data());
    for (uint16_t saved : cursorSave)
        assert(saved == 0);

    // and the mirror case: columns overlap, rows do not
    int aboveX = 1, aboveY = 0;
    cursorSave.fill(0);
    cursor.refreshCursorSaveRect(
        overlay.data(), 4, { 0, 3, 4, 1 },
        4, 4, &aboveX, &aboveY, &cursorWidth, &cursorHeight, cursorSave.data());
    for (uint16_t saved : cursorSave)
        assert(saved == 0);

    State viewport;
    viewport.setMode(Mode::On);
    viewport.setBattlefield({ 0, 0, 64, 32 });
    assert(!viewport.takeViewportChange()); // idle at 1x, nothing to repaint
    assert(viewport.addWheelDelta(120));
    assert(viewport.takeViewportChange()); // zoom step moves the minimap rectangle
    assert(!viewport.takeViewportChange()); // reported once
    viewport.setPanDirection(State::PanRight, true);
    viewport.updatePan(0, 0, 0);
    viewport.updatePan(0, 0, 16); // camera stuck at the map edge, so the view pans
    assert(viewport.takeViewportChange());
    assert(!viewport.takeViewportChange());

    State isolation;
    isolation.setMode(Mode::On);
    isolation.addWheelDelta(120);
    std::array<uint16_t, 8> mainSurface{};
    std::array<uint16_t, 8> backSurface{};
    isolation.beginWorldIsolation(mainSurface.data(), backSurface.data(), mainSurface.size());
    mainSurface[0] = 99;
    backSurface[0] = 98;
    isolation.finishWorldIsolation(mainSurface.data(), backSurface.data());
    assert(mainSurface[0] == 0);
    assert(backSurface[0] == 0);

    std::array<uint16_t, 40> trackedMain{}, trackedBack{};
    for (size_t i = 0; i < trackedMain.size(); ++i)
    {
        trackedMain[i] = static_cast<uint16_t>(i + 1);
        trackedBack[i] = static_cast<uint16_t>(i + 101);
    }
    const auto originalMain = trackedMain;
    const auto originalBack = trackedBack;
    const auto beginTracked = [&]
    {
        isolation.beginWorldIsolation(trackedMain.data(), trackedBack.data(), trackedMain.size(), 8);
    };
    const auto finishTracked = [&]
    {
        isolation.finishWorldIsolation(trackedMain.data(), trackedBack.data());
        assert(trackedMain == originalMain && trackedBack == originalBack);
    };
    beginTracked();
    finishTracked();
    assert(isolation.isolationCopiedBytes() == 0);

    beginTracked();
    isolation.captureWorldWrite(trackedMain.data() + 9, sizeof(uint16_t));
    trackedMain[9] = 99;
    isolation.captureWorldWrite(trackedMain.data() + 10, 2 * sizeof(uint16_t));
    trackedMain[10] = 98;
    assert(isolation.isolationCopiedBytes() == 16);
    isolation.captureWorldWrite(trackedBack.data() + 9, sizeof(uint16_t));
    trackedBack[9] = 97;
    isolation.captureWorldWrite(trackedMain.data() + 31, 3 * sizeof(uint16_t));
    trackedMain[31] = trackedMain[32] = trackedMain[33] = 96;
    finishTracked();
    assert(isolation.isolationCopiedBytes() == 128);

    beginTracked();
    isolation.captureWorldWrite(trackedMain.data() + 9, sizeof(uint16_t));
    trackedMain[9] = 99;
    isolation.captureWholeWorld();
    assert(!isolation.trackingWorldWrites());
    trackedMain[9] = 98;
    trackedMain[0] = trackedBack[39] = 97;
    finishTracked();
    assert(isolation.isolationCopiedBytes() == trackedMain.size() * sizeof(uint16_t) * 4);

    beginTracked();
    isolation.captureWorldWrite(trackedMain.data() + 39, sizeof(uint16_t));
    trackedMain[39] = 99;
    isolation.captureWorldWrite(nullptr, 0);
    finishTracked();
    assert(isolation.isolationCopiedBytes() == 32);

    beginTracked();
    isolation.captureWorldWrite(trackedBack.data(), sizeof(uint16_t));
    trackedBack[0] = 99;
    isolation.setMode(Mode::Off);
    assert(trackedBack == originalBack);
    assert(!isolation.trackingWorldWrites());

}
