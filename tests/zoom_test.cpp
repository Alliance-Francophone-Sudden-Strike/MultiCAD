#include "Zoom.h"

#include <array>
#include <cassert>

int main()
{
    using namespace Zoom;

    static_assert(ParseMode("off") == Mode::Off);
    static_assert(ParseMode(" Steps ") == Mode::Steps);
    static_assert(ParseMode("SMOOTH") == Mode::Smooth);
    static_assert(ParseMode("unknown") == Mode::Off);

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
    state.setMode(Mode::Steps);
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

    std::array<uint16_t, 4> main{ 1, 2, 3, 4 };
    assert(state.ensureBuffers(main.size()));
    std::copy(main.begin(), main.end(), state.cleanBuffer());
    main[0] = 9;
    state.deferMainRestore();
    state.restoreMain(main.data(), main.size());
    assert((main == std::array<uint16_t, 4>{ 1, 2, 3, 4 }));

    State presentation;
    presentation.setMode(Mode::Steps);
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

    State isolation;
    isolation.setMode(Mode::Steps);
    isolation.addWheelDelta(120);
    std::array<uint16_t, 8> mainSurface{};
    std::array<uint16_t, 8> backSurface{};
    isolation.beginWorldIsolation(mainSurface.data(), backSurface.data(), mainSurface.size());
    mainSurface[0] = 99;
    backSurface[0] = 98;
    isolation.finishWorldIsolation(mainSurface.data(), backSurface.data());
    assert(mainSurface[0] == 0);
    assert(backSurface[0] == 0);

    isolation.beginFrame(true);
    assert(isolation.suppressed());
    isolation.beginWorldIsolation(mainSurface.data(), backSurface.data(), mainSurface.size());
    mainSurface[0] = 7;
    isolation.finishWorldIsolation(mainSurface.data(), backSurface.data());
    assert(mainSurface[0] == 7); // native text frame is untouched
    isolation.beginFrame(false);
    assert(isolation.suppressed()); // one native frame clears the text
    isolation.beginFrame(false);
    assert(!isolation.suppressed());
}
