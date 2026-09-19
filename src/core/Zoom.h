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
    constexpr int kMaxScale = 8; // 8 == 2x
    constexpr uint32_t kIndicatorHoldMs = 1500;
    constexpr uint32_t kIndicatorFadeMs = 250;
    constexpr uint32_t kIndicatorAnimMs = 1;

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

    constexpr size_t ScaleScratchSize(size_t destinationWidth)
    {
        return 4 * destinationWidth;
    }

    inline void ScaleSharp16(
        const uint16_t* source, int sourcePitch,
        uint16_t* destination, int destinationPitch,
        const Transform& transform, uint16_t* scratch)
    {
        const int left = transform.destination.x;
        const int top = transform.destination.y;
        const int bottom = top + transform.destination.height;
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
        int scale() const { return scale_; }
        int presentedScale() const { return presentedScale_; }
        bool dragging() const { return dragButtons_ != 0; }
        bool battlefieldDragging() const { return battlefieldDragButtons_ != 0; }
        IndicatorAnchor indicatorAnchor() const { return indicatorAnchor_; }
        IndicatorShape indicatorShape() const { return indicatorShape_; }
        void setIndicatorShape(IndicatorShape shape) { indicatorShape_ = shape; }
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
            advanceIndicatorAnimation(tick);

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
            finishWorldIsolation(isolationMain_, isolationBack_);
            resetScale();
            dragButtons_ = 0;
            battlefieldDragButtons_ = 0;
            battlefield_ = {};
            presented_ = {};
            presentedScale_ = kMinScale;
            hasPresented_ = false;
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

        bool ensureBuffers(size_t pixels, size_t width)
        {
            try
            {
                if (presentation_.size() != pixels)
                    presentationValid_ = false;
                world_.resize(pixels);
                presentation_.resize(pixels);
                rowScratch_.resize(ScaleScratchSize(width));
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

        void beginPresentation(void*& renderer, uint32_t& pitch, int width, int height)
        {
            const auto* source = static_cast<const uint8_t*>(renderer);
            for (int y = 0; y < height; ++y)
                std::memcpy(presentation_.data() + y * width, source + y * pitch, width * sizeof(uint16_t));
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
            for (int y = 0; y < height; ++y)
                std::memcpy(destination + y * actualPitch_, presentation_.data() + y * width, width * sizeof(uint16_t));
            renderer = actualRenderer_;
            pitch = actualPitch_;
            routed_ = false;
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
        bool presentationValid_{};
        bool routed_{};
        bool worldIsolated_{};
        IndicatorAnchor indicatorAnchor_{ IndicatorAnchor::Left };
        IndicatorShape indicatorShape_{ IndicatorShape::Squares };
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


        void advanceIndicatorAnimation(uint32_t tick)
        {
            if (!animValid_)
            {
                animatedScale_ = static_cast<float>(scale_);
                animTick_ = tick;
                animValid_ = true;
                return;
            }
            const uint32_t elapsed = tick - animTick_;
            animTick_ = tick;
            const float factor = std::min(1.f, elapsed / static_cast<float>(kIndicatorAnimMs));
            animatedScale_ += (static_cast<float>(scale_) - animatedScale_) * factor;
        }

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
