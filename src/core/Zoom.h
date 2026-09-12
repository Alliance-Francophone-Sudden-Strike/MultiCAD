#pragma once

#include <algorithm>
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

    struct Rect
    {
        int x{};
        int y{};
        int width{};
        int height{};
    };

    // Walks source columns for the scaler without a per-pixel divide: the old
    // loop called Transform::sourceX per pixel, which is a multiply and an
    // integer divide every time. `index` tracks exactly the value sourceX
    // returns, so the image stays aligned with the mapping input hit-testing
    // uses. Rows need no walk -- sourceY is called once per row, not per pixel.
    struct Sampler
    {
        int index{};     // current source column
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
    };

    constexpr int kMinScale = 4; // quarter units: 4 == 1x
    constexpr int kMaxScale = 8; // 8 == 2x
    constexpr uint32_t kIndicatorHoldMs = 1500;
    constexpr uint32_t kIndicatorFadeMs = 250;

    constexpr int ViewportExtent(int nativeExtent, int scale)
    {
        return std::max(1, nativeExtent * kMinScale / std::clamp(scale, kMinScale, kMaxScale));
    }

    inline Transform MakeTransform(Rect battlefield, int scale = kMinScale, int panX = 0, int panY = 0)
    {
        scale = std::clamp(scale, kMinScale, kMaxScale);
        const int width = std::max(1, battlefield.width * kMinScale / scale);
        const int height = std::max(1, battlefield.height * kMinScale / scale);
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

    inline void ScaleNearest16(
        const uint16_t* source, int sourcePitch,
        uint16_t* destination, int destinationPitch,
        const Transform& transform)
    {
        const int left = transform.destination.x;
        const int right = left + transform.destination.width;

        for (int y = transform.destination.y;
            y < transform.destination.y + transform.destination.height; ++y)
        {
            const uint16_t* const src = source + transform.sourceY(y) * sourcePitch;
            uint16_t* const dst = destination + y * destinationPitch;

            Sampler x = transform.samplerX(left);
            for (int px = left; px < right; ++px, x.advance())
                dst[px] = src[x.index];
        }
    }

    inline void DrawIndicator16(
        uint16_t* destination, int pitch, int width, int height, int scale, bool right,
        int opacity = 16)
    {
        constexpr int size = 12;
        constexpr int gap = 8;
        constexpr int margin = 12;
        constexpr uint16_t outline = 0x8410;
        constexpr uint16_t fill = 0xC618;
        constexpr int totalHeight = (kMaxScale - kMinScale + 1) * size +
            (kMaxScale - kMinScale) * gap;
        const int left = right ? width - margin - size : margin;
        const int top = (height - totalHeight) / 2;

        if (!destination || left < 0 || top < 0 || left + size > width || top + totalHeight > height)
            return;

        scale = std::clamp(scale, kMinScale, kMaxScale);
        opacity = std::clamp(opacity, 0, 16);
        for (int dot = 0; dot <= kMaxScale - kMinScale; ++dot)
        {
            const bool reached = scale >= kMaxScale - dot;
            const int y = top + dot * (size + gap);
            for (int dy = 0; dy < size; ++dy)
                for (int dx = 0; dx < size; ++dx)
                {
                    const bool border = dx == 0 || dx == size - 1 || dy == 0 || dy == size - 1;
                    if (!reached && !border)
                        continue;

                    uint16_t& pixel = destination[(y + dy) * pitch + left + dx];
                    const uint16_t color = reached ? fill : outline;
                    if (opacity == 16)
                    {
                        pixel = color;
                        continue;
                    }

                    const int sourceRed = (color >> 11) & 0x1F;
                    const int sourceGreen = (color >> 5) & 0x3F;
                    const int sourceBlue = color & 0x1F;
                    const int targetRed = (pixel >> 11) & 0x1F;
                    const int targetGreen = (pixel >> 5) & 0x3F;
                    const int targetBlue = pixel & 0x1F;
                    pixel = static_cast<uint16_t>(
                        (((sourceRed * opacity + targetRed * (16 - opacity) + 8) / 16) << 11) |
                        (((sourceGreen * opacity + targetGreen * (16 - opacity) + 8) / 16) << 5) |
                        ((sourceBlue * opacity + targetBlue * (16 - opacity) + 8) / 16));
                }
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
                clean_.clear();
                world_.clear();
                presentation_.clear();
                isolatedWorld_.clear();
                isolatedBack_.clear();
            }
        }

        Mode mode() const { return mode_; }
        int scale() const { return scale_; }
        int presentedScale() const { return presentedScale_; }
        bool dragging() const { return dragButtons_ != 0; }
        bool battlefieldDragging() const { return battlefieldDragButtons_ != 0; }
        IndicatorAnchor indicatorAnchor() const { return indicatorAnchor_; }
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
                ((persistentIndicator_ && scale_ != kMinScale) ||
                    (indicatorActive_ && tick - indicatorTick_ < kIndicatorHoldMs));
        }
        int indicatorOpacity(uint32_t tick) const
        {
            if (!indicatorVisible(tick))
                return 0;
            if (persistentIndicator_ && scale_ != kMinScale)
                return 16;
            const uint32_t elapsed = tick - indicatorTick_;
            if (elapsed + kIndicatorFadeMs <= kIndicatorHoldMs)
                return 16;
            return static_cast<int>((kIndicatorHoldMs - elapsed) * 16 / kIndicatorFadeMs);
        }
        bool indicatorPending() const { return indicatorActive_; }
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
            presented_ = MakeTransform(battlefield_);
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
            if (mode_ == Mode::Off || scale_ == kMinScale)
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
            return MakeTransform(battlefield_, scale_, panX_, panY_);
        }
        Transform presentedTransform() const { return presented_; }

        // The minimap and strategic-map viewport rectangles are sized from the zoom
        // scale and pan, but the game only repaints them when the camera scrolls.
        // Report a change once so the caller can replay that notification.
        bool takeViewportChange()
        {
            if (scale_ == notifiedScale_ && panX_ == notifiedPanX_ && panY_ == notifiedPanY_)
                return false;

            notifiedScale_ = scale_;
            notifiedPanX_ = panX_;
            notifiedPanY_ = panY_;
            return true;
        }

        void markPresented()
        {
            presented_ = transform();
            presentedScale_ = scale_;
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

        void scaleCameraMovement(int& dx, int& dy)
        {
            const int movementScale = mode_ == Mode::Off ? kMinScale : presentedScale_;
            if (movementScale != cameraMovementScale_)
            {
                cameraMovementScale_ = movementScale;
                cameraRemainderX_ = 0;
                cameraRemainderY_ = 0;
            }

            const auto scaleDelta = [movementScale](int delta, int& remainder)
            {
                const int total = delta * kMinScale + remainder;
                remainder = total % movementScale;
                return total / movementScale;
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
                scale_ = std::min(scale_ + 1, kMaxScale);
                wheelRemainder_ -= 120;
            }
            while (wheelRemainder_ <= -120)
            {
                scale_ = std::max(scale_ - 1, kMinScale);
                wheelRemainder_ += 120;
            }
            if (scale_ == oldScale)
                return false;

            if (zoomOnCursor_)
                anchorPanToPointer(oldScale);
            return true;
        }

        void reset()
        {
            resetScale();
            dragButtons_ = 0;
            battlefieldDragButtons_ = 0;
            battlefield_ = {};
            presented_ = {};
            presentedScale_ = kMinScale;
            hasPresented_ = false;
            restorePending_ = false;
            presentationValid_ = false;
            routed_ = false;
            worldIsolated_ = false;
            indicatorActive_ = false;
            cameraMovementScale_ = kMinScale;
            cameraRemainderX_ = 0;
            cameraRemainderY_ = 0;
        }

        void resetScale()
        {
            scale_ = kMinScale;
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

        bool ensureBuffers(size_t pixels)
        {
            try
            {
                if (presentation_.size() != pixels)
                    presentationValid_ = false;
                clean_.resize(pixels);
                world_.resize(pixels);
                presentation_.resize(pixels);
                return true;
            }
            catch (...)
            {
                setMode(Mode::Off);
                return false;
            }
        }

        uint16_t* cleanBuffer() { return clean_.data(); }
        uint16_t* worldBuffer() { return world_.data(); }
        uint16_t* presentationBuffer() { return presentation_.data(); }

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

        void restoreCursor(
            uint16_t* clean, uint16_t* presentation, int width, int height,
            const int* nativeX, const int* nativeY,
            const int* nativeWidth, const int* nativeHeight,
            const uint16_t* nativePixels) const
        {
            int left, top, right, bottom;
            if (!clean || !presentation || !nativePixels ||
                !cursorSaveRect(width, height, nativeX, nativeY, nativeWidth, nativeHeight,
                    left, top, right, bottom))
                return;

            for (int row = top; row < bottom; ++row)
            {
                const uint16_t* source =
                    nativePixels + (row - *nativeY) * kCursorPitch + left - *nativeX;
                std::copy(source, source + right - left, clean + row * width + left);
                std::copy(source, source + right - left, presentation + row * width + left);
            }
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

        void beginWorldIsolation(uint16_t* main, uint16_t* back, size_t pixels)
        {
            finishWorldIsolation(main, back);
            if (mode_ == Mode::Off || !main || !back)
                return;

            try
            {
                isolatedWorld_.resize(pixels);
                isolatedBack_.resize(pixels);
            }
            catch (...)
            {
                setMode(Mode::Off);
                return;
            }

            std::copy_n(main, pixels, isolatedWorld_.data());
            std::copy_n(back, pixels, isolatedBack_.data());
            worldIsolated_ = true;
        }

        void finishWorldIsolation(uint16_t* main, uint16_t* back)
        {
            if (!worldIsolated_)
                return;
            std::copy(isolatedWorld_.begin(), isolatedWorld_.end(), main);
            std::copy(isolatedBack_.begin(), isolatedBack_.end(), back);
            worldIsolated_ = false;
        }

        void beginPresentation(void*& renderer, uint32_t& pitch, int width, int height)
        {
            const auto* source = static_cast<const uint8_t*>(renderer);
            for (int y = 0; y < height; ++y)
                std::memcpy(presentation_.data() + y * width, source + y * pitch, width * sizeof(uint16_t));
            presentationValid_ = true;

            std::copy(presentation_.begin(), presentation_.end(), clean_.begin());
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
            for (int y = 0; y < height; ++y)
                std::memcpy(destination + y * actualPitch_, presentation_.data() + y * width, width * sizeof(uint16_t));
            renderer = actualRenderer_;
            pitch = actualPitch_;
            routed_ = false;
        }

        void deferMainRestore() { restorePending_ = true; }

        void restoreMain(uint16_t* main, size_t pixels)
        {
            if (restorePending_ && clean_.size() == pixels)
                std::copy(clean_.begin(), clean_.end(), main);
            restorePending_ = false;
        }

    private:
        Mode mode_{ Mode::Off };
        int scale_{ kMinScale };
        int wheelRemainder_{};
        int dragButtons_{};
        int battlefieldDragButtons_{};
        Rect battlefield_{};
        Transform presented_{};
        int presentedScale_{ kMinScale };
        bool hasPresented_{};
        int cameraMovementScale_{ kMinScale };
        int cameraRemainderX_{};
        int cameraRemainderY_{};
        bool restorePending_{};
        bool presentationValid_{};
        bool routed_{};
        bool worldIsolated_{};
        IndicatorAnchor indicatorAnchor_{ IndicatorAnchor::Left };
        bool persistentIndicator_{};
        bool invertZoom_{};
        bool zoomOnCursor_{};
        bool indicatorActive_{};
        uint32_t indicatorTick_{};
        int panX_{};
        int panY_{};
        int notifiedScale_{ kMinScale };
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
        uint32_t actualPitch_{};
        std::vector<uint16_t> clean_;
        std::vector<uint16_t> world_;
        std::vector<uint16_t> presentation_;
        std::vector<uint16_t> isolatedWorld_;
        std::vector<uint16_t> isolatedBack_;

        int scaledPanOffset(int nativeExtent, int battlefieldExtent, int pan) const
        {
            const int margin = nativeExtent - ViewportExtent(nativeExtent, scale_);
            const int battlefieldMargin = battlefieldExtent - ViewportExtent(battlefieldExtent, scale_);
            return battlefieldMargin > 0 ?
                std::clamp(margin / 2 + pan * margin / battlefieldMargin, 0, margin) : 0;
        }

        void anchorPanToPointer(int oldScale)
        {
            if (!pointerValid_ ||
                pointerX_ < battlefield_.x || pointerX_ >= battlefield_.x + battlefield_.width ||
                pointerY_ < battlefield_.y || pointerY_ >= battlefield_.y + battlefield_.height)
                return;

            const auto anchor = [oldScale, this](int& pan, int pointer, int origin, int extent)
            {
                const int cursor = std::clamp(pointer - origin, 0, extent - 1);
                const int oldViewport = ViewportExtent(extent, oldScale);
                const int viewport = ViewportExtent(extent, scale_);
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

            const int margin = battlefieldExtent - ViewportExtent(battlefieldExtent, scale_);
            pan = std::clamp(pan, -margin / 2, margin - margin / 2);
        }
    };

    inline State& GetState()
    {
        static State state;
        return state;
    }
}
