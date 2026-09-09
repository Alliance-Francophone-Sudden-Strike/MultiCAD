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

    std::array<uint16_t, 24> source{}; // 4 rows, pitch 6
    std::array<uint16_t, 40> destination{}; // 4 rows, pitch 10
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            source[y * 6 + x] = static_cast<uint16_t>(y * 10 + x);
    ScaleNearest16(source.data(), 6, destination.data(), 10, two);
    assert(destination[0] == 12 && destination[7] == 15);
    assert(destination[30] == 22 && destination[37] == 25);
    assert(destination[8] == 0 && destination[38] == 0); // padded pitch untouched

    State state;
    assert(!state.addWheelDelta(120));
    state.setMode(Mode::On);
    assert(!state.addWheelDelta(60));
    assert(state.addWheelDelta(60) && state.scale() == 5);
    state.setBattlefield({ 10, 20, 100, 80 });
    assert(state.transform().destination.x == 10);
    assert(state.mapX(10) == 10 && state.mapX(109) == 109); // target is not presented yet
    state.markPresented();
    assert(state.presentedScale() == 5);
    assert(state.mapX(10) == state.transform().source.x);
    assert(state.mapX(109) == state.transform().source.x + state.transform().source.width - 1);
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
    state.setIndicatorAnchor(IndicatorAnchor::Hidden);
    state.noteZoomInput(2000);
    assert(!state.indicatorPending());

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
    cursor.beginPresentation(cursorRendererPtr, cursorRendererPitch, 4, 4);
    int cursorX = 1, cursorY = 1, cursorWidth = 2, cursorHeight = 2;
    std::array<uint16_t, 64 * 64> cursorSave{};
    cursorSave[0] = 10;
    cursorSave[1] = 11;
    cursorSave[64] = 12;
    cursorSave[65] = 13;
    cursor.beginCursorFrame(
        &cursorX, &cursorY, &cursorWidth, &cursorHeight, cursorSave.data());
    assert(cursorHeight == 0);
    cursorHeight = 2; // Native cursor captured its new background before unlock.
    cursor.finishPresentation(cursorRendererPtr, cursorRendererPitch, 4, 4);
    assert(cursorHeight == 0); // Native code cannot restore scaled pixels next frame.
    std::array<uint16_t, 16> cursorClean{}, cursorFrame{};
    cursor.restoreCursor(
        cursorClean.data(), cursorFrame.data(), 4, 4,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    assert(cursorFrame[5] == 10 && cursorFrame[6] == 11);
    assert(cursorFrame[9] == 12 && cursorFrame[10] == 13);

    cursor.beginPresentation(cursorRendererPtr, cursorRendererPitch, 4, 4);
    cursor.beginCursorFrame(
        &cursorX, &cursorY, &cursorWidth, &cursorHeight, cursorSave.data());
    cursor.finishPresentation(cursorRendererPtr, cursorRendererPitch, 4, 4);
    cursorFrame.fill(0);
    cursor.restoreCursor(
        cursorClean.data(), cursorFrame.data(), 4, 4,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    assert(cursorFrame[5] == 10 && cursorFrame[6] == 11);
    assert(cursorFrame[9] == 12 && cursorFrame[10] == 13);

    int movedX = 2, movedY = 0, movedWidth = 2, movedHeight = 2;
    std::array<uint16_t, 64 * 64> movedSave{};
    movedSave[0] = 20;
    movedSave[1] = 21;
    movedSave[64] = 22;
    movedSave[65] = 23;
    cursorFrame.fill(99);
    cursor.restoreCursor(
        cursorClean.data(), cursorFrame.data(), 4, 4,
        &movedX, &movedY, &movedWidth, &movedHeight, movedSave.data());
    assert(cursorFrame[5] == 10 && cursorFrame[6] == 11 && cursorFrame[9] == 12); // Previous cursor erased last.
    assert(cursorFrame[2] == 20 && cursorFrame[3] == 21); // Newly moved cursor erased too.

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
