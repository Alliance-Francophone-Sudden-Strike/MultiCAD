#pragma once

#include "PanelScale.h"
#include "Parallel.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Zoom
{
    enum class Mode
    {
        Off,
        On
    };

    enum class IndicatorAnchor
    {
        Left,
        Right,
        Hidden
    };

    constexpr Mode ParseMode(std::string_view value)
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);

        const auto equals = [value](std::string_view expected)
        {
            if (value.size() != expected.size())
                return false;
            for (size_t i = 0; i < value.size(); ++i)
            {
                char c = value[i];
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
                if (c != expected[i])
                    return false;
            }
            return true;
        };

        if (equals("on"))
            return Mode::On;
        return Mode::Off;
    }

    constexpr IndicatorAnchor ParseIndicatorAnchor(std::string_view value)
    {
        if (value == "right")
            return IndicatorAnchor::Right;
        if (value == "hidden")
            return IndicatorAnchor::Hidden;
        return IndicatorAnchor::Left;
    }

    enum class IndicatorShape
    {
        Squares,
        Bars
    };

    constexpr IndicatorShape ParseIndicatorShape(std::string_view value)
    {
        if (value == "bars")
            return IndicatorShape::Bars;
        return IndicatorShape::Squares;
    }

    struct Rect
    {
        int x{};
        int y{};
        int width{};
        int height{};
    };

    inline uint16_t Blend565(uint16_t from, uint16_t to, int t)
    {
        const uint32_t a = (from & 0xF81Fu) | ((from & 0x07E0u) << 16);
        const uint32_t b = (to & 0xF81Fu) | ((to & 0x07E0u) << 16);
        const uint32_t blend = ((a * (16 - t) + b * t + 0x01004008u) >> 4) & 0x07E0F81Fu;
        return static_cast<uint16_t>((blend & 0xF81Fu) | ((blend >> 16) & 0x07E0u));
    }

    constexpr int kFilterSharpness = 2;

    // Walks source columns and rows for the scaler without a per-pixel divide:
    // the old loop called Transform::sourceX per pixel, which is a multiply and
    // an integer divide every time. `index` tracks exactly the value sourceX
    // returns, so the image stays aligned with the mapping input hit-testing
    // uses.
    struct Sampler
    {
        int index{};     // current source line
        int remainder{}; // fractional position, in units of span
        int step{};      // source extent
        int span{};      // destination extent

        void advance()
        {
            remainder += step;
            while (remainder >= span)
            {
                remainder -= span;
                ++index;
            }
        }

        int weight() const
        {
            const int over = remainder + step - span;
            if (over <= 0)
                return 0;
            return std::clamp(((over * 16 + step / 2) / step - 8) * kFilterSharpness + 8, 0, 16);
        }
    };

    struct Transform
    {
        Rect source;
        Rect destination;

        int sourceX(int physicalX) const
        {
            const int x = std::clamp(physicalX - destination.x, 0, destination.width - 1);
            return source.x + x * source.width / destination.width;
        }

        int sourceY(int physicalY) const
        {
            const int y = std::clamp(physicalY - destination.y, 0, destination.height - 1);
            return source.y + y * source.height / destination.height;
        }

        Sampler samplerX(int physicalX) const
        {
            const int x = std::clamp(physicalX - destination.x, 0, destination.width - 1);
            const int offset = x * source.width;
            return {
                source.x + offset / destination.width,
                offset % destination.width,
                source.width,
                destination.width
            };
        }

        Sampler samplerY(int physicalY) const
        {
            const int y = std::clamp(physicalY - destination.y, 0, destination.height - 1);
            const int offset = y * source.height;
            return {
                source.y + offset / destination.height,
                offset % destination.height,
                source.height,
                destination.height
            };
        }
    };

    constexpr int kMinScale = 4; // quarter units: 4 == 1x
    constexpr int kMaxScale = 8; // 8 == 2x, the upstream limit
    constexpr int kLimitScale = 12; // 12 == 3x, the HS2Engine limit
    constexpr float kZoomOutScale = 0.5f; // HS2Engine view mode: the zoom-out limit
    constexpr int kReferenceFrameUs = 1'000'000 / 60;
    constexpr uint32_t kIndicatorHoldMs = 1500;
    constexpr uint32_t kIndicatorFadeMs = 250;
    constexpr uint32_t kIndicatorAnimMs = 1;

    // The smooth zoom moves between the quarter steps in fine units: 16 per
    // quarter, so 64 == 1x and 128 == 2x. A quarter scale q is the fine scale
    // 16q exactly, and every size below comes out the same for both.
    constexpr int kFineSteps = 16;
    constexpr int kMinFineScale = kMinScale * kFineSteps;
    constexpr int kMaxFineScale = kLimitScale * kFineSteps;
    constexpr uint32_t kSmoothZoomMs = 150; // one wheel step

    // `fullFine`: the fine scale that shows the whole battlefield. 64 (1x), or less in the
    // HS2Engine view mode, where the game draws a view larger than the screen (zoom out).
    constexpr int ViewportExtentFine(int nativeExtent, int fineScale, int fullFine = kMinFineScale)
    {
        return std::max(1, nativeExtent * fullFine / std::clamp(fineScale, fullFine, kMaxFineScale));
    }

    constexpr int ViewportExtent(int nativeExtent, int scale)
    {
        return ViewportExtentFine(nativeExtent, std::clamp(scale, kMinScale, kLimitScale) * kFineSteps);
    }

    inline Transform MakeTransformFine(Rect battlefield, int fineScale, int panX = 0, int panY = 0,
        int fullFine = kMinFineScale)
    {
        const int width = ViewportExtentFine(battlefield.width, fineScale, fullFine);
        const int height = ViewportExtentFine(battlefield.height, fineScale, fullFine);
        const int marginX = battlefield.width - width;
        const int marginY = battlefield.height - height;
        return {
            {
                battlefield.x + std::clamp(marginX / 2 + panX, 0, marginX),
                battlefield.y + std::clamp(marginY / 2 + panY, 0, marginY),
                width,
                height
            },
            battlefield
        };
    }

    inline Transform MakeTransform(Rect battlefield, int scale = kMinScale, int panX = 0, int panY = 0)
    {
        return MakeTransformFine(battlefield, std::clamp(scale, kMinScale, kLimitScale) * kFineSteps, panX, panY);
    }

    constexpr size_t ScaleScratchSize(size_t destinationWidth)
    {
        return 4 * destinationWidth;
    }

    inline void ScaleSharp16(
        const uint16_t* source, int sourcePitch,
        uint16_t* destination, int destinationPitch,
        const Transform& transform, uint16_t* scratch,
        int first = INT_MIN, int last = INT_MAX)
    {
        const int left = transform.destination.x;
        const int top = std::max(transform.destination.y, first);
        const int bottom = std::min(transform.destination.y + transform.destination.height, last);
        const int width = transform.destination.width;
        const int lastColumn = transform.source.x + transform.source.width - 1;
        const int lastRow = transform.source.y + transform.source.height - 1;

        uint16_t* const columnIndex = scratch + 2 * width;
        uint16_t* const columnWeight = scratch + 3 * width;
        Sampler column = transform.samplerX(left);
        for (int px = 0; px < width; ++px, column.advance())
        {
            columnIndex[px] = static_cast<uint16_t>(column.index);
            columnWeight[px] = static_cast<uint16_t>(
                column.index < lastColumn ? column.weight() : 0);
        }

        const bool columnsExact = transform.source.width == width;
        int cached[2] = { -1, -1 };
        const auto scaledRow = [&](int sy) -> const uint16_t*
        {
            const uint16_t* const src = source + sy * sourcePitch;
            if (columnsExact)
                return src + transform.source.x;

            uint16_t* const row = scratch + (sy & 1) * width;
            if (cached[sy & 1] == sy)
                return row;
            cached[sy & 1] = sy;

            for (int px = 0; px < width; ++px)
            {
                const int t = columnWeight[px];
                const int index = columnIndex[px];
                row[px] = t == 0 ? src[index] : Blend565(src[index], src[index + 1], t);
            }
            return row;
        };

        Sampler y = transform.samplerY(top);
        for (int py = top; py < bottom; ++py, y.advance())
        {
            uint16_t* const dst = destination + py * destinationPitch + left;
            const int t = y.index < lastRow ? y.weight() : 0;
            const uint16_t* const first = scaledRow(y.index);

            if (t == 0)
            {
                std::copy_n(first, width, dst);
                continue;
            }

            const uint16_t* const second = scaledRow(y.index + 1);
            if (t == 16)
            {
                std::copy_n(second, width, dst);
                continue;
            }

            for (int px = 0; px < width; ++px)
                dst[px] = Blend565(first[px], second[px], t);
        }
    }

    class State
    {
    public:
        enum DragButton
        {
            LeftButton = 1,
            RightButton = 2
        };

        enum PanDirection
        {
            PanLeft = 1,
            PanRight = 2,
            PanUp = 4,
            PanDown = 8
        };

        void setMode(Mode mode)
        {
            mode_ = mode;
            reset();
            if (mode == Mode::Off)
            {
                world_.clear();
                presentation_.clear();
                rowScratch_.clear();
                isolatedWorld_.clear();
                isolatedBack_.clear();
                savedWorldRows_.clear();
                savedBackRows_.clear();
            }
        }

        Mode mode() const { return mode_; }
        // Quarters (4 == 1x), rounded down; the state keeps fine units (64 == 1x).
        int scale() const { return scale_ / kFineSteps; }
        int presentedScale() const { return presentedScale_ / kFineSteps; }
        int targetFineScale() const { return scale_; }
        // The fine scale that shows the whole battlefield: 64, or less in the view mode.
        int fullFineScale() const { return fullFine_; }
        int viewportExtent(int nativeExtent) const { return ViewportExtentFine(nativeExtent, fineScale_, engineFine_); }
        // The scale on screen now, in fine units; scale() is where it is going.
        int fineScale() const { return fineScale_; }
        int presentedFineScale() const { return presentedFine_; }
        // View mode: the world always goes through the zoom pass, but at the whole view
        // (the limit, the game's view as large as the frame).
        bool zoomed() const
        {
            const int none = native() ? kMinFineScale : fullFine_;  // native: 1x is not zoomed
            return scale_ != none || fineScale_ != none ||
                (viewMode_ && !native() && (battlefield_.width != viewW_ || battlefield_.height != viewH_));
        }
        // View mode at 1x and above, once the game's view is the screen: the game draws the UI
        // itself in the top-left screen-sized corner of the frame, which is presented 1:1, and
        // the zoom in works there as without the view mode.
        bool native() const
        {
            return viewMode_ && std::min(scale_, animating_ ? animFrom_ : scale_) >= kMinFineScale &&
                battlefield_.width == presentW_ && battlefield_.height == presentH_;
        }
        int unzoomedFineScale() const { return native() ? kMinFineScale : fullFine_; }
        int presentWidth() const { return presentW_; }
        int presentHeight() const { return presentH_; }
        bool animating() const { return animating_; }
        // HS2Engine's zoom: 1x to 3x in steps of 0.5x; with `animate` ([Game] SmoothZoom) each
        // step moves over kSmoothZoomMs, else it jumps. Off: 1x to 2x in steps of 0.25x that
        // jump (upstream).
        void setSmooth(bool hs2Zoom, bool animate = true)
        {
            hs2Zoom_ = hs2Zoom;
            smooth_ = hs2Zoom && animate;
            maxScale_ = hs2Zoom ? kLimitScale : kMaxScale;
            stepScale_ = hs2Zoom ? 2 : 1;
            rebuildLevels();
            resetScale();
        }
        // HS2Engine view mode: the game draws a view `view` pixels wide for a screen `screen`
        // pixels wide (MultiCAD reduces it at present). The zoom then goes below 1x, down to
        // the whole view: 0.5x for a view of screen / 0.5, with a step at 0.75x
        // (a limit of 0.75x has a step at 0.875x instead).
        //
        // The game's view follows the zoom (requestedView): below 1x it is screen * 64 / scale,
        // at 1x and above the screen, at the origin of the frame. The battlefield (its FILD
        // area) is that view; the transform crops it and presents the crop over the whole
        // frame, which the renderer reduces to the screen.
        void setViewRatio(int presentWidth, int presentHeight, int viewWidth, int viewHeight)
        {
            viewMode_ = presentWidth > 0 && presentHeight > 0 && viewWidth > presentWidth;
            presentW_ = presentWidth;
            presentH_ = presentHeight;
            viewW_ = viewWidth;
            viewH_ = viewHeight;
            fullFine_ = viewMode_
                ? std::clamp((kMinFineScale * presentWidth + viewWidth - 1) / viewWidth, kMinFineScale / 2, kMinFineScale)
                : kMinFineScale;
            engineFine_ = kMinFineScale;
            rebuildLevels();
            resetScale();
        }
        bool viewMode() const { return viewMode_; }
        Rect battlefield() const { return battlefield_; }
        // The view the game must draw now: for the target scale, or during a smooth step the
        // larger of its two ends, so a step resizes the game's view once.
        // Where requestedView goes, from the origin of the game's view now, in its pixels.
        // Smaller: on the crop shown now (a zoom in keeps what is on screen, on the cursor
        // with ZoomOnCursor); larger: around the centre (decision 9).
        bool requestedOrigin(int& x, int& y) const
        {
            int width = 0, height = 0;
            if (!requestedView(width, height) || battlefield_.width <= 0 || battlefield_.height <= 0)
                return false;
            const Transform t = transform();
            x = width <= battlefield_.width
                ? t.source.x - battlefield_.x + (t.source.width - width) / 2 : (battlefield_.width - width) / 2;
            y = height <= battlefield_.height
                ? t.source.y - battlefield_.y + (t.source.height - height) / 2 : (battlefield_.height - height) / 2;
            return true;
        }
        bool requestedView(int& width, int& height) const
        {
            if (!viewMode_)
                return false;
            const int fine = std::min({ scale_, animating_ ? animFrom_ : scale_, kMinFineScale });
            width = std::min(viewW_, (presentW_ * kMinFineScale / fine) & ~15);
            height = std::min(viewH_, (presentH_ * kMinFineScale / fine) & ~7);
            return true;
        }
        bool smooth() const { return smooth_; }
        bool hs2Zoom() const { return hs2Zoom_; }
        int maxScale() const { return maxScale_; }   // quarters
        int stepScale() const { return stepScale_; } // quarters for each wheel step
        bool dragging() const { return dragButtons_ != 0; }
        bool battlefieldDragging() const { return battlefieldDragButtons_ != 0; }
        IndicatorAnchor indicatorAnchor() const { return indicatorAnchor_; }
        IndicatorShape indicatorShape() const { return indicatorShape_; }
        void setIndicatorShape(IndicatorShape shape) { indicatorShape_ = shape; }
        int indicatorScaleQuarters() const { return indicatorScale_; }
        void setIndicatorScale(int quarters)
        {
            indicatorScale_ = std::clamp(quarters, PanelScale::kMinQuarters, PanelScale::kMaxQuarters);
        }
        void setPersistentIndicator(bool persistent) { persistentIndicator_ = persistent; }
        void setInvertZoom(bool invert) { invertZoom_ = invert; }
        void setZoomOnCursor(bool enabled) { zoomOnCursor_ = enabled; }
        void setIndicatorAnchor(IndicatorAnchor anchor)
        {
            indicatorAnchor_ = anchor;
            if (anchor == IndicatorAnchor::Hidden)
                indicatorActive_ = false;
        }
        void noteZoomInput(uint32_t tick)
        {
            if (indicatorAnchor_ == IndicatorAnchor::Hidden)
                return;
            indicatorTick_ = tick;
            indicatorActive_ = true;
        }
        bool indicatorVisible(uint32_t tick) const
        {
            return mode_ != Mode::Off && indicatorAnchor_ != IndicatorAnchor::Hidden &&
                ((persistentIndicator_ && scale_ != kMinFineScale) ||
                    (indicatorActive_ && tick - indicatorTick_ < kIndicatorHoldMs));
        }
        int indicatorOpacity(uint32_t tick) const
        {
            if (!indicatorVisible(tick))
                return 0;
            if (persistentIndicator_ && scale_ != kMinFineScale)
                return 16;
            const uint32_t elapsed = tick - indicatorTick_;
            if (elapsed + kIndicatorFadeMs <= kIndicatorHoldMs)
                return 16;
            return static_cast<int>((kIndicatorHoldMs - elapsed) * 16 / kIndicatorFadeMs);
        }
        bool indicatorPending() const { return indicatorActive_; }
        // Eases toward scale() a little each updatePan() tick, so DrawIndicator16
        // can sweep a level's bar smoothly instead of snapping it.
        float animatedScale() const { return animatedScale_; }
        void finishIndicatorFrame(uint32_t tick)
        {
            if (!indicatorVisible(tick))
                indicatorActive_ = false;
        }
        void setBattlefield(Rect battlefield)
        {
            if (battlefield.x == battlefield_.x && battlefield.y == battlefield_.y &&
                battlefield.width == battlefield_.width && battlefield.height == battlefield_.height)
                return;

            battlefield_ = battlefield;
            if (viewMode_ && battlefield.width > 0)  // the scale of the game's view
                engineFine_ = std::clamp((kMinFineScale * presentW_ + battlefield.width / 2) / battlefield.width,
                    fullFine_, kMinFineScale);
            presented_ = viewMode_ ? transform() : MakeTransformFine(battlefield_, fullFine_, 0, 0, fullFine_);
            hasPresented_ = true;
            panX_ = 0;
            panY_ = 0;
            lastCameraStepX_ = 0;
            lastCameraStepY_ = 0;
            cameraValid_ = false;
        }
        void setPanDirection(PanDirection direction, bool pressed)
        {
            if (pressed)
                panDirections_ |= direction;
            else
                panDirections_ &= ~direction;
        }
        int viewportOffsetX(int nativeWidth) const
        {
            return scaledPanOffset(nativeWidth, battlefield_.width, panX_);
        }
        int viewportOffsetY(int nativeHeight) const
        {
            return scaledPanOffset(nativeHeight, battlefield_.height, panY_);
        }
        void setPointer(int x, int y)
        {
            pointerX_ = x;
            pointerY_ = y;
            pointerValid_ = true;
        }
        void updatePan(int cameraX, int cameraY, uint32_t tick)
        {
            advanceScale(tick);
            advanceIndicatorAnimation(tick);

            if (mode_ == Mode::Off || !zoomed())
            {
                lastCameraStepX_ = 0;
                lastCameraStepY_ = 0;
                cameraValid_ = false;
                return;
            }

            if (!cameraValid_)
            {
                cameraX_ = cameraX;
                cameraY_ = cameraY;
                panTick_ = tick;
                cameraValid_ = true;
                return;
            }

            const uint32_t elapsed = std::min<uint32_t>(tick - panTick_, 50);
            const int fallbackStep = std::max(1, static_cast<int>(elapsed / 2));
            int horizontal = ((panDirections_ & PanRight) != 0) - ((panDirections_ & PanLeft) != 0);
            int vertical = ((panDirections_ & PanDown) != 0) - ((panDirections_ & PanUp) != 0);

            if (pointerValid_ && !dragging() &&
                pointerX_ >= battlefield_.x && pointerX_ < battlefield_.x + battlefield_.width &&
                pointerY_ >= battlefield_.y && pointerY_ < battlefield_.y + battlefield_.height)
            {
                constexpr int edge = 8;
                horizontal += (pointerX_ >= battlefield_.x + battlefield_.width - edge) -
                    (pointerX_ < battlefield_.x + edge);
                vertical += (pointerY_ >= battlefield_.y + battlefield_.height - edge) -
                    (pointerY_ < battlefield_.y + edge);
                horizontal = std::clamp(horizontal, -1, 1);
                vertical = std::clamp(vertical, -1, 1);
            }

            const int cameraDeltaX = cameraX >= cameraX_ ? cameraX - cameraX_ : cameraX_ - cameraX;
            const int cameraDeltaY = cameraY >= cameraY_ ? cameraY - cameraY_ : cameraY_ - cameraY;
            if (horizontal && cameraDeltaX)
                lastCameraStepX_ = cameraDeltaX;
            if (vertical && cameraDeltaY)
                lastCameraStepY_ = cameraDeltaY;

            updatePanAxis(
                panX_, horizontal, cameraDeltaX == 0,
                lastCameraStepX_ ? lastCameraStepX_ : fallbackStep, battlefield_.width);
            updatePanAxis(
                panY_, vertical, cameraDeltaY == 0,
                lastCameraStepY_ ? lastCameraStepY_ : fallbackStep, battlefield_.height);
            cameraX_ = cameraX;
            cameraY_ = cameraY;
            panTick_ = tick;
        }
        Transform transform() const
        {
            Transform t = MakeTransformFine(battlefield_, fineScale_, panX_, panY_, engineFine_);
            if (viewMode_ && !native())
            {
                // The game's view is about to shrink to this crop: put it on the game's 16 x 8
                // grid, where the resized view lands (0x1009D460 snaps its target), so the
                // picture does not shift when the view replaces the crop.
                int w = 0, h = 0;
                if (!animating_ && requestedView(w, h) && w <= battlefield_.width && h <= battlefield_.height &&
                    (w < battlefield_.width || h < battlefield_.height))
                {
                    t.source.x = battlefield_.x + ((t.source.x - battlefield_.x) & ~15);
                    t.source.y = battlefield_.y + ((t.source.y - battlefield_.y) & ~7);
                }
                t.destination = { 0, 0, viewW_, viewH_ };
            }
            return t;
        }
        Transform presentedTransform() const { return presented_; }

        // The minimap and strategic-map viewport rectangles are sized from the zoom
        // scale and pan, but the game only repaints them when the camera scrolls.
        // Report a change once so the caller can replay that notification.
        bool takeViewportChange()
        {
            if (fineScale_ == notifiedScale_ && panX_ == notifiedPanX_ && panY_ == notifiedPanY_)
                return false;

            notifiedScale_ = fineScale_;
            notifiedPanX_ = panX_;
            notifiedPanY_ = panY_;
            return true;
        }

        void markPresented()
        {
            presented_ = transform();
            presentedScale_ = scale_;
            presentedFine_ = fineScale_;
            hasPresented_ = true;
        }

        int mapX(int physicalX) const
        {
            return hasPresented_ ? presented_.sourceX(physicalX) : physicalX;
        }

        int mapY(int physicalY) const
        {
            return hasPresented_ ? presented_.sourceY(physicalY) : physicalY;
        }

        void scaleCameraMovement(int& dx, int& dy, int frameUs = kReferenceFrameUs)
        {
            const int movementScale = mode_ == Mode::Off ? kMinFineScale : presentedFine_;
            if (movementScale != cameraMovementScale_)
            {
                cameraMovementScale_ = movementScale;
                cameraRemainderX_ = 0;
                cameraRemainderY_ = 0;
            }

            const int64_t elapsed = std::clamp(frameUs, 1'000, 50'000);
            const int64_t divisor = int64_t{ movementScale } * kReferenceFrameUs;
            const auto scaleDelta = [elapsed, divisor](int delta, int& remainder)
            {
                const int64_t total = delta * kMinFineScale * elapsed + remainder;
                remainder = static_cast<int>(total % divisor);
                return static_cast<int>(total / divisor);
            };
            dx = scaleDelta(dx, cameraRemainderX_);
            dy = scaleDelta(dy, cameraRemainderY_);
        }

        void setDragButton(DragButton button, bool pressed, bool battlefield = false)
        {
            if (pressed)
            {
                dragButtons_ |= button;
                if (battlefield)
                    battlefieldDragButtons_ |= button;
            }
            else
            {
                dragButtons_ &= ~button;
                battlefieldDragButtons_ &= ~button;
            }
        }

        bool addWheelDelta(int delta)
        {
            if (mode_ == Mode::Off || dragging())
                return false;

            wheelRemainder_ += invertZoom_ ? -delta : delta;
            const int oldScale = scale_;
            while (wheelRemainder_ >= 120)
            {
                scale_ = nextLevel(scale_, true);
                wheelRemainder_ -= 120;
            }
            while (wheelRemainder_ <= -120)
            {
                scale_ = nextLevel(scale_, false);
                wheelRemainder_ += 120;
            }
            if (scale_ == oldScale)
                return false;

            if (smooth_)
                startScaleAnimation();
            else
                applyFineScale(scale_);
            return true;
        }

        // The middle button: back to 1x, smoothly when the zoom is smooth.
        void zoomOutFully()
        {
            if (!smooth_)
            {
                resetScale();
                return;
            }
            scale_ = kMinFineScale;
            wheelRemainder_ = 0;
            startScaleAnimation();
        }

        void reset()
        {
            finishWorldIsolation(isolationMain_, isolationBack_);
            resetScale();
            dragButtons_ = 0;
            battlefieldDragButtons_ = 0;
            battlefield_ = {};
            presented_ = {};
            presentedScale_ = kMinFineScale;
            presentedFine_ = kMinFineScale;
            hasPresented_ = false;
            presentationValid_ = false;
            routed_ = false;
            worldIsolated_ = false;
            indicatorActive_ = false;
            cameraMovementScale_ = kMinFineScale;
            cameraRemainderX_ = 0;
            cameraRemainderY_ = 0;
        }

        void resetScale()
        {
            scale_ = kMinFineScale;
            fineScale_ = kMinFineScale;
            animating_ = false;
            wheelRemainder_ = 0;
            panX_ = 0;
            panY_ = 0;
            lastCameraStepX_ = 0;
            lastCameraStepY_ = 0;
            cameraValid_ = false;
        }

        void cancelInput()
        {
            wheelRemainder_ = 0;
            dragButtons_ = 0;
            battlefieldDragButtons_ = 0;
            panDirections_ = 0;
            pointerValid_ = false;
        }

        bool ensureBuffers(size_t pixels, size_t width)
        {
            try
            {
                if (presentation_.size() != pixels)
                    presentationValid_ = false;
                world_.resize(pixels);
                presentation_.resize(pixels);
                rowScratch_.resize(ScaleScratchSize(width) * kMaxRenderThreads);
                return true;
            }
            catch (...)
            {
                setMode(Mode::Off);
                return false;
            }
        }

        uint16_t* worldBuffer() { return world_.data(); }
        uint16_t* presentationBuffer() { return presentation_.data(); }
        uint16_t* rowScratch() { return rowScratch_.data(); }

        // Screen rect the game saved before drawing the cursor, clipped to the
        // surface. Empty when no cursor is currently drawn.
        bool cursorSaveRect(
            int width, int height,
            const int* savedX, const int* savedY,
            const int* savedWidth, const int* savedHeight,
            int& left, int& top, int& right, int& bottom) const
        {
            if (!savedX || !savedY || !savedWidth || !savedHeight ||
                *savedWidth <= 0 || *savedHeight <= 0)
                return false;

            left = std::max(0, *savedX);
            top = std::max(0, *savedY);
            right = std::min(width, *savedX + std::min(*savedWidth, kCursorPitch));
            bottom = std::min(height, *savedY + std::min(*savedHeight, kCursorPitch));
            return right > left && bottom > top;
        }

        // Composition replaced everything under the cursor, so hand the game a
        // save-under that matches it. Its own erase then restores current
        // pixels whenever it redraws the cursor, including on the cursor-only
        // frames that never reach composition again.
        void refreshCursorSave(
            int width, int height,
            const int* savedX, const int* savedY,
            const int* savedWidth, const int* savedHeight,
            uint16_t* savedPixels) const
        {
            int left, top, right, bottom;
            if (!savedPixels || presentation_.size() != static_cast<size_t>(width) * height ||
                !cursorSaveRect(width, height, savedX, savedY, savedWidth, savedHeight,
                    left, top, right, bottom))
                return;

            for (int row = top; row < bottom; ++row)
            {
                const uint16_t* source = presentation_.data() + row * width + left;
                std::copy(source, source + right - left,
                    savedPixels + (row - *savedY) * kCursorPitch + left - *savedX);
            }
        }

        void refreshCursorSaveRect(
            const uint16_t* source, int sourcePitch,
            const Rect& region,
            int width, int height,
            const int* savedX, const int* savedY,
            const int* savedWidth, const int* savedHeight,
            uint16_t* savedPixels) const
        {
            int left, top, right, bottom;
            if (!source || !savedPixels || sourcePitch <= 0 ||
                !cursorSaveRect(width, height, savedX, savedY, savedWidth, savedHeight,
                    left, top, right, bottom))
                return;

            left = std::max(left, region.x);
            top = std::max(top, region.y);
            right = std::min(right, region.x + region.width);
            bottom = std::min(bottom, region.y + region.height);

            if (right <= left || bottom <= top)
                return;

            for (int row = top; row < bottom; ++row)
            {
                const uint16_t* begin = source + static_cast<size_t>(row) * sourcePitch + left;
                std::copy(begin, begin + (right - left),
                    savedPixels + (row - *savedY) * kCursorPitch + left - *savedX);
            }
        }

        // Where the game keeps screen pixel (x, y) while its cursor covers it; null outside
        // the saved rectangle.
        uint16_t* cursorSavePixel(
            int width, int height,
            const int* savedX, const int* savedY,
            const int* savedWidth, const int* savedHeight,
            uint16_t* savedPixels, int x, int y) const
        {
            int left, top, right, bottom;
            if (!savedPixels ||
                !cursorSaveRect(width, height, savedX, savedY, savedWidth, savedHeight,
                    left, top, right, bottom) ||
                x < left || x >= right || y < top || y >= bottom)
                return nullptr;
            return savedPixels + (y - *savedY) * kCursorPitch + x - *savedX;
        }

        void beginWorldIsolation(uint16_t* main, uint16_t* back, size_t pixels, size_t rowWidth = 0)
        {
            finishWorldIsolation(isolationMain_, isolationBack_);
            isolationCopiedBytes_ = 0;
            if (mode_ == Mode::Off || !main || !back || !pixels)
                return;
            if (rowWidth && pixels % rowWidth)
                rowWidth = 0;

            try
            {
                isolatedWorld_.resize(pixels);
                isolatedBack_.resize(pixels);
                savedWorldRows_.assign(rowWidth ? pixels / rowWidth : 0, 0);
                savedBackRows_.assign(rowWidth ? pixels / rowWidth : 0, 0);
            }
            catch (...)
            {
                setMode(Mode::Off);
                return;
            }

            isolationMain_ = main;
            isolationBack_ = back;
            isolationRowWidth_ = rowWidth;
            worldIsolated_ = true;
            if (!rowWidth)
            {
                std::copy_n(main, pixels, isolatedWorld_.data());
                std::copy_n(back, pixels, isolatedBack_.data());
                isolationCopiedBytes_ = pixels * sizeof(uint16_t) * 2;
            }
        }

        bool trackingWorldWrites() const { return worldIsolated_ && isolationRowWidth_ != 0; }
        size_t isolationCopiedBytes() const { return isolationCopiedBytes_; }

        void captureWorldWrite(const void* destination, size_t bytes)
        {
            if (!trackingWorldWrites() || !bytes)
                return;
            captureSurfaceWrite(destination, bytes, isolationMain_, isolatedWorld_, savedWorldRows_);
            captureSurfaceWrite(destination, bytes, isolationBack_, isolatedBack_, savedBackRows_);
        }

        void captureWholeWorld()
        {
            if (!trackingWorldWrites())
                return;
            captureWorldWrite(isolationMain_, isolatedWorld_.size() * sizeof(uint16_t));
            captureWorldWrite(isolationBack_, isolatedBack_.size() * sizeof(uint16_t));
            isolationRowWidth_ = 0;
        }

        void finishWorldIsolation(uint16_t* main, uint16_t* back)
        {
            if (!worldIsolated_)
                return;
            if (isolationRowWidth_)
            {
                restoreSurface(main, isolatedWorld_, savedWorldRows_);
                restoreSurface(back, isolatedBack_, savedBackRows_);
            }
            else
            {
                std::copy(isolatedWorld_.begin(), isolatedWorld_.end(), main);
                std::copy(isolatedBack_.begin(), isolatedBack_.end(), back);
                isolationCopiedBytes_ += isolatedWorld_.size() * sizeof(uint16_t) * 2;
            }
            worldIsolated_ = false;
        }

        // View mode: only these pixels of the frame reach the screen (the renderer's
        // reduction), so the presentation copies move only them. Null: the whole frame.
        void setPresentLattice(const int* columns, const int* rows, int width, int height)
        {
            latticeColumns_ = columns;
            latticeRows_ = rows;
            latticeWidth_ = columns && rows ? width : 0;
            latticeHeight_ = columns && rows ? height : 0;
        }

        void beginPresentation(void*& renderer, uint32_t& pitch, int width, int height)
        {
            const auto* source = static_cast<const uint8_t*>(renderer);
            if (latticeWidth_ > 0)
                ParallelRows(0, latticeHeight_, 8, [&](int first, int last)
                {
                    for (int q = first; q < last; ++q)
                    {
                        const int y = latticeRows_[q];
                        const auto* in = reinterpret_cast<const uint16_t*>(source + static_cast<size_t>(y) * pitch);
                        uint16_t* out = presentation_.data() + static_cast<size_t>(y) * width;
                        for (int p = 0; p < latticeWidth_; ++p)
                            out[latticeColumns_[p]] = in[latticeColumns_[p]];
                    }
                });
            else
                ParallelRows(0, height, 1, [&](int first, int last)
                {
                    for (int y = first; y < last; ++y)
                        std::memcpy(presentation_.data() + y * width, source + y * pitch, width * sizeof(uint16_t));
                });
            presentationValid_ = true;

            actualRenderer_ = renderer;
            actualPitch_ = pitch;
            renderer = presentation_.data();
            pitch = width * sizeof(uint16_t);
            routed_ = true;
        }

        void finishPresentation(void*& renderer, uint32_t& pitch, int width, int height)
        {
            if (!routed_)
                return;

            auto* destination = static_cast<uint8_t*>(actualRenderer_);
            if (latticeWidth_ > 0)
                ParallelRows(0, latticeHeight_, 8, [&](int first, int last)
                {
                    for (int q = first; q < last; ++q)
                    {
                        const int y = latticeRows_[q];
                        const uint16_t* in = presentation_.data() + static_cast<size_t>(y) * width;
                        auto* out = reinterpret_cast<uint16_t*>(destination + static_cast<size_t>(y) * actualPitch_);
                        for (int p = 0; p < latticeWidth_; ++p)
                            out[latticeColumns_[p]] = in[latticeColumns_[p]];
                    }
                });
            else
                ParallelRows(0, height, 1, [&](int first, int last)
                {
                    for (int y = first; y < last; ++y)
                        std::memcpy(destination + y * actualPitch_, presentation_.data() + y * width, width * sizeof(uint16_t));
                });
            renderer = actualRenderer_;
            pitch = actualPitch_;
            routed_ = false;
        }

    private:
        Mode mode_{ Mode::Off };
        int scale_{ kMinFineScale };  // the target, fine units
        int fullFine_{ kMinFineScale };
        int engineFine_{ kMinFineScale };  // view mode: the scale of the game's view (64 = the screen)
        bool viewMode_{};
        int presentW_{}, presentH_{}, viewW_{}, viewH_{};
        std::vector<int> levels_{ 64, 80, 96, 112, 128 };  // the wheel steps, fine units (upstream)
        int wheelRemainder_{};
        int dragButtons_{};
        int battlefieldDragButtons_{};
        Rect battlefield_{};
        Transform presented_{};
        int presentedScale_{ kMinFineScale };
        int fineScale_{ kMinFineScale };
        int presentedFine_{ kMinFineScale };
        bool hs2Zoom_{};
        bool smooth_{};
        int maxScale_{ kMaxScale };
        int stepScale_{ 1 };
        bool animating_{};
        bool animStarted_{};
        int animFrom_{ kMinFineScale };
        uint32_t animStart_{};
        bool hasPresented_{};
        int cameraMovementScale_{ kMinFineScale };
        int cameraRemainderX_{};
        int cameraRemainderY_{};
        bool presentationValid_{};
        bool routed_{};
        bool worldIsolated_{};
        IndicatorAnchor indicatorAnchor_{ IndicatorAnchor::Left };
        IndicatorShape indicatorShape_{ IndicatorShape::Squares };
        int indicatorScale_{ PanelScale::kMinQuarters };
        bool persistentIndicator_{};
        bool invertZoom_{};
        bool zoomOnCursor_{};
        bool indicatorActive_{};
        uint32_t indicatorTick_{};
        float animatedScale_{ static_cast<float>(kMinScale) };
        uint32_t animTick_{};
        bool animValid_{};
        int panX_{};
        int panY_{};
        int notifiedScale_{ kMinFineScale };
        int notifiedPanX_{};
        int notifiedPanY_{};
        int panDirections_{};
        int pointerX_{};
        int pointerY_{};
        int cameraX_{};
        int cameraY_{};
        int lastCameraStepX_{};
        int lastCameraStepY_{};
        uint32_t panTick_{};
        bool pointerValid_{};
        bool cameraValid_{};
        static constexpr int kCursorPitch = 64;
        void* actualRenderer_{};
        const int* latticeColumns_{};
        const int* latticeRows_{};
        int latticeWidth_{};
        int latticeHeight_{};
        uint32_t actualPitch_{};
        std::vector<uint16_t> world_;
        std::vector<uint16_t> presentation_;
        std::vector<uint16_t> rowScratch_;
        std::vector<uint16_t> isolatedWorld_;
        std::vector<uint16_t> isolatedBack_;
        uint16_t* isolationMain_{};
        uint16_t* isolationBack_{};
        size_t isolationRowWidth_{};
        size_t isolationCopiedBytes_{};
        std::vector<uint8_t> savedWorldRows_;
        std::vector<uint8_t> savedBackRows_;

        void captureSurfaceWrite(const void* destination, size_t bytes, uint16_t* surface,
                                 std::vector<uint16_t>& saved, std::vector<uint8_t>& rows)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(destination);
            const uintptr_t base = reinterpret_cast<uintptr_t>(surface);
            const size_t surfaceBytes = saved.size() * sizeof(uint16_t);
            if (address >= base + surfaceBytes || (address < base && bytes <= base - address))
                return;
            const size_t firstByte = address > base ? address - base : 0;
            const size_t available = address < base ? bytes - (base - address) : bytes;
            const size_t lastByte = firstByte + std::min(available, surfaceBytes - firstByte) - 1;
            const size_t rowBytes = isolationRowWidth_ * sizeof(uint16_t);
            const size_t last = lastByte / rowBytes + 1;
            for (size_t row = firstByte / rowBytes; row < last;)
            {
                if (rows[row])
                {
                    ++row;
                    continue;
                }
                const size_t first = row;
                do { rows[row++] = 1; } while (row < last && !rows[row]);
                const size_t count = (row - first) * isolationRowWidth_;
                std::copy_n(surface + first * isolationRowWidth_, count,
                            saved.data() + first * isolationRowWidth_);
                isolationCopiedBytes_ += count * sizeof(uint16_t);
            }
        }

        void restoreSurface(uint16_t* surface, const std::vector<uint16_t>& saved,
                            const std::vector<uint8_t>& rows)
        {
            for (size_t row = 0; row < rows.size();)
            {
                if (!rows[row])
                {
                    ++row;
                    continue;
                }
                const size_t first = row;
                do { ++row; } while (row < rows.size() && rows[row]);
                const size_t count = (row - first) * isolationRowWidth_;
                std::copy_n(saved.data() + first * isolationRowWidth_, count,
                            surface + first * isolationRowWidth_);
                isolationCopiedBytes_ += count * sizeof(uint16_t);
            }
        }


        // A new step (or a new target during a movement) starts from the scale on
        // screen; the first tick after it sets the clock.
        void startScaleAnimation()
        {
            if (!animating_)
                animFrom_ = fineScale_;
            animating_ = true;
            animStarted_ = false;
        }

        void rebuildLevels()
        {
            levels_.clear();
            if (hs2Zoom_ && fullFine_ < kMinFineScale)
            {
                levels_.push_back(fullFine_);  // the whole view
                if (fullFine_ < kMinFineScale * 3 / 4)
                    levels_.push_back(kMinFineScale * 3 / 4);  // 0.75x
                else if (fullFine_ < kMinFineScale * 7 / 8)
                    levels_.push_back(kMinFineScale * 7 / 8);  // 0.875x
            }
            for (int q = kMinScale; q <= maxScale_; q += stepScale_)
                levels_.push_back(q * kFineSteps);
        }

        int nextLevel(int from, bool up) const
        {
            if (up)
            {
                for (int level : levels_)
                    if (level > from)
                        return level;
                return levels_.empty() ? from : levels_.back();
            }
            for (auto it = levels_.rbegin(); it != levels_.rend(); ++it)
                if (*it < from)
                    return *it;
            return levels_.empty() ? from : levels_.front();
        }

        // Ease-out: fast at the start, so the wheel feels direct, slow at the end.
        void advanceScale(uint32_t tick)
        {
            if (!animating_)
                return;
            if (!animStarted_)
            {
                animFrom_ = fineScale_;
                animStart_ = tick;
                animStarted_ = true;
            }

            const int target = scale_;
            const uint32_t elapsed = tick - animStart_;
            int next = target;
            if (elapsed < kSmoothZoomMs)
            {
                const int64_t duration = kSmoothZoomMs;
                const int64_t left = duration - elapsed;
                next = animFrom_ + static_cast<int>(
                    (target - animFrom_) * (duration * duration - left * left) / (duration * duration));
            }
            else
            {
                animating_ = false;
            }
            applyFineScale(next);

            if (!animating_ && target == fullFine_)
            {
                panX_ = 0;
                panY_ = 0;
                lastCameraStepX_ = 0;
                lastCameraStepY_ = 0;
                cameraValid_ = false;
            }
        }

        void applyFineScale(int fineScale)
        {
            fineScale = std::clamp(fineScale, fullFine_, kMaxFineScale);
            if (fineScale == fineScale_)
                return;
            const int oldFineScale = fineScale_;
            fineScale_ = fineScale;
            if (hs2Zoom_ && fineScale < oldFineScale)
            {
                // HS2Engine: a zoom on the cursor zooms out around the centre of the screen;
                // without it, the zoom out retraces the zoom in (no slide at 1x).
                if (zoomOnCursor_)
                    anchorPanToCentre();
                else
                    anchorPanToVanishingPoint(oldFineScale);
            }
            else if (zoomOnCursor_)
                anchorPanToPointer(oldFineScale);
        }

        // The point at the centre of the screen stays there while the margin allows it;
        // the pan is clamped, so an off-centre crop slides on the way to 1x.
        void anchorPanToCentre()
        {
            const auto clamp = [this](int& pan, int extent)
            {
                const int margin = extent - ViewportExtentFine(extent, fineScale_, engineFine_);
                pan = margin > 0 ? std::clamp(pan, -margin / 2, margin - margin / 2) : 0;
            };
            clamp(panX_, battlefield_.width);
            clamp(panY_, battlefield_.height);
        }

        // Zooming out around the point that stays still all the way to 1x: for a crop at
        // offset o with margin m, the point o * extent / m. A step around it keeps it the
        // same point (o' = o * m' / m), so the zoom out ends at 1x without a slide, and it
        // retraces the zoom in: after a zoom in on the cursor at c, that point is c.
        void anchorPanToVanishingPoint(int oldFineScale)
        {
            const auto anchor = [oldFineScale, this](int& pan, int extent)
            {
                const int oldMargin = extent - ViewportExtentFine(extent, oldFineScale, engineFine_);
                const int margin = extent - ViewportExtentFine(extent, fineScale_, engineFine_);
                if (oldMargin <= 0 || margin <= 0)
                {
                    pan = 0;
                    return;
                }
                const int offset = std::clamp(oldMargin / 2 + pan, 0, oldMargin);
                pan = (offset * margin + oldMargin / 2) / oldMargin - margin / 2;
            };

            anchor(panX_, battlefield_.width);
            anchor(panY_, battlefield_.height);
        }

        void advanceIndicatorAnimation(uint32_t tick)
        {
            if (smooth_)
            {
                animatedScale_ = static_cast<float>(fineScale_) / kFineSteps;
                animValid_ = true;
                return;
            }
            if (!animValid_)
            {
                animatedScale_ = static_cast<float>(scale_) / kFineSteps;
                animTick_ = tick;
                animValid_ = true;
                return;
            }
            const uint32_t elapsed = tick - animTick_;
            animTick_ = tick;
            const float factor = std::min(1.f, elapsed / static_cast<float>(kIndicatorAnimMs));
            animatedScale_ += (static_cast<float>(scale_) / kFineSteps - animatedScale_) * factor;
        }

        int scaledPanOffset(int nativeExtent, int battlefieldExtent, int pan) const
        {
            const int margin = nativeExtent - ViewportExtentFine(nativeExtent, fineScale_, engineFine_);
            const int battlefieldMargin = battlefieldExtent - ViewportExtentFine(battlefieldExtent, fineScale_, engineFine_);
            return battlefieldMargin > 0 ?
                std::clamp(margin / 2 + pan * margin / battlefieldMargin, 0, margin) : 0;
        }

        void anchorPanToPointer(int oldFineScale)
        {
            if (!pointerValid_ ||
                pointerX_ < battlefield_.x || pointerX_ >= battlefield_.x + battlefield_.width ||
                pointerY_ < battlefield_.y || pointerY_ >= battlefield_.y + battlefield_.height)
                return;

            const auto anchor = [oldFineScale, this](int& pan, int pointer, int origin, int extent)
            {
                const int cursor = std::clamp(pointer - origin, 0, extent - 1);
                const int oldViewport = ViewportExtentFine(extent, oldFineScale, engineFine_);
                const int viewport = ViewportExtentFine(extent, fineScale_, engineFine_);
                const int oldMargin = extent - oldViewport;
                const int margin = extent - viewport;
                const int offset = std::clamp(oldMargin / 2 + pan, 0, oldMargin) +
                    cursor * oldViewport / extent - cursor * viewport / extent;
                pan = std::clamp(offset, 0, margin) - margin / 2;
            };

            anchor(panX_, pointerX_, battlefield_.x, battlefield_.width);
            anchor(panY_, pointerY_, battlefield_.y, battlefield_.height);
        }

        void updatePanAxis(int& pan, int direction, bool cameraStopped, int step, int battlefieldExtent)
        {
            if (direction && cameraStopped)
                pan += direction * step;
            else if (!cameraStopped)
                pan += pan < 0 ? std::min(step, -pan) : -std::min(step, pan);

            const int margin = battlefieldExtent - ViewportExtentFine(battlefieldExtent, fineScale_, engineFine_);
            pan = std::clamp(pan, -margin / 2, margin - margin / 2);
        }
    };

    inline State& GetState()
    {
        static State state;
        return state;
    }
}
