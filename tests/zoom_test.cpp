#include "Zoom.h"

#include <array>
#include <cassert>

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
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            source[y * 6 + x] = static_cast<uint16_t>(y * 10 + x);
    ScaleNearest16(source.data(), 6, destination.data(), 10, two);
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

    std::array<uint16_t, 64 * 128> indicator{};
    DrawIndicator16(indicator.data(), 64, 64, 128, kMinScale, false);
    assert(indicator[18 * 64 + 12] != 0); // Highest level has an outline.
    assert(indicator[98 * 64 + 12] != 0); // Current level is filled.
    indicator.fill(0);
    DrawIndicator16(indicator.data(), 64, 64, 128, kMaxScale, true);
    assert(indicator[18 * 64 + 40] != 0);
    assert(indicator[18 * 64 + 12] == 0);

    std::array<uint16_t, 4> main{ 1, 2, 3, 4 };
    assert(state.ensureBuffers(main.size()));
    std::copy(main.begin(), main.end(), state.cleanBuffer());
    main[0] = 9;
    state.deferMainRestore();
    state.restoreMain(main.data(), main.size());
    assert((main == std::array<uint16_t, 4>{ 1, 2, 3, 4 }));

    State presentation;
    presentation.setMode(Mode::On);
    assert(presentation.ensureBuffers(6));
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
    assert(cursor.ensureBuffers(16));
    std::array<uint16_t, 16> cursorRenderer{};
    void* cursorRendererPtr = cursorRenderer.data();
    uint32_t cursorRendererPitch = 4 * sizeof(uint16_t);
    int cursorX = 1, cursorY = 1, cursorWidth = 2, cursorHeight = 2;
    std::array<uint16_t, 64 * 64> cursorSave{};
    cursorSave[0] = 10;
    cursorSave[1] = 11;
    cursorSave[64] = 12;
    cursorSave[65] = 13;

    // Composition rebuilds the frame from the pre-cursor snapshot, so the
    // cursor has to be erased out of both the panel source and the frame.
    std::array<uint16_t, 16> cursorClean{}, cursorFrame{};
    cursorFrame.fill(99);
    cursor.restoreCursor(
        cursorClean.data(), cursorFrame.data(), 4, 4,
        &cursorX, &cursorY, &cursorWidth, &cursorHeight, cursorSave.data());
    assert(cursorFrame[5] == 10 && cursorFrame[6] == 11);
    assert(cursorFrame[9] == 12 && cursorFrame[10] == 13);
    assert(cursorClean[5] == 10 && cursorClean[10] == 13);
    assert(cursorFrame[0] == 99 && cursorFrame[15] == 99); // Only the cursor rect.

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
    cursorFrame.fill(99);
    cursor.restoreCursor(
        cursorClean.data(), cursorFrame.data(), 4, 4,
        &cursorX, &cursorY, &cursorWidth, &hidden, cursorSave.data());
    assert(cursorFrame[5] == 99); // No cursor drawn, nothing to erase.

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

}
