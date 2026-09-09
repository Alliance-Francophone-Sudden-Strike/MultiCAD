#pragma once

#include <algorithm>
#include <array>
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
    };

    constexpr int kMinScale = 4; // quarter units: 4 == 1x
    constexpr int kMaxScale = 8; // 8 == 2x
    constexpr uint32_t kIndicatorHoldMs = 1500;
    constexpr uint32_t kIndicatorFadeMs = 250;

    constexpr int ViewportExtent(int nativeExtent, int scale)
    {
        return std::max(1, nativeExtent * kMinScale / std::clamp(scale, kMinScale, kMaxScale));
    }

    inline Transform MakeTransform(Rect battlefield, int scale = kMinScale)
    {
        scale = std::clamp(scale, kMinScale, kMaxScale);
        const int width = std::max(1, battlefield.width * kMinScale / scale);
        const int height = std::max(1, battlefield.height * kMinScale / scale);
        return {
            {
                battlefield.x + (battlefield.width - width) / 2,
                battlefield.y + (battlefield.height - height) / 2,
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
        for (int y = transform.destination.y;
            y < transform.destination.y + transform.destination.height; ++y)
        {
            const uint16_t* src = source + transform.sourceY(y) * sourcePitch;
            uint16_t* dst = destination + y * destinationPitch;
            for (int x = transform.destination.x;
                x < transform.destination.x + transform.destination.width; ++x)
            {
                dst[x] = src[transform.sourceX(x)];
            }
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
        }
        Transform transform() const { return MakeTransform(battlefield_, scale_); }
        Transform presentedTransform() const { return presented_; }

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

            wheelRemainder_ += delta;
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
            return scale_ != oldScale;
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
            cursorValid_ = false;
            cursorSavedX_ = nullptr;
            cursorSavedY_ = nullptr;
            cursorSavedWidth_ = nullptr;
            cursorSavedHeight_ = nullptr;
            cursorSavedPixels_ = nullptr;
        }

        void resetScale()
        {
            scale_ = kMinScale;
            wheelRemainder_ = 0;
        }

        void cancelInput()
        {
            wheelRemainder_ = 0;
            dragButtons_ = 0;
            battlefieldDragButtons_ = 0;
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

        void restoreCursor(
            uint16_t* clean, uint16_t* presentation, int width, int height,
            const int* nativeX, const int* nativeY,
            const int* nativeWidth, const int* nativeHeight,
            const uint16_t* nativePixels) const
        {
            if (!clean || !presentation)
                return;

            const auto restore = [&](int x, int y, int savedWidth, int savedHeight, const uint16_t* savedPixels)
            {
                if (!savedPixels || savedWidth <= 0 || savedHeight <= 0)
                    return;
                const int left = std::max(0, x);
                const int top = std::max(0, y);
                const int right = std::min(width, x + std::min(savedWidth, kCursorPitch));
                const int bottom = std::min(height, y + std::min(savedHeight, kCursorPitch));
                for (int row = top; row < bottom; ++row)
                {
                    const uint16_t* source = savedPixels + (row - y) * kCursorPitch + left - x;
                    std::copy(source, source + right - left, clean + row * width + left);
                    std::copy(source, source + right - left, presentation + row * width + left);
                }
            };

            if (nativeX && nativeY && nativeWidth && nativeHeight)
                restore(*nativeX, *nativeY, *nativeWidth, *nativeHeight, nativePixels);
            if (cursorValid_)
                restore(cursorX_, cursorY_, cursorWidth_, cursorHeight_, cursorPixels_.data());
        }

        bool cursorRect(int& left, int& top, int& right, int& bottom) const
        {
            if (!cursorValid_)
                return false;
            left = cursorX_;
            top = cursorY_;
            right = left + cursorWidth_;
            bottom = top + cursorHeight_;
            return true;
        }

        void beginCursorFrame(
            int* savedX, int* savedY, int* savedWidth, int* savedHeight,
            uint16_t* savedPixels)
        {
            cursorSavedX_ = savedX;
            cursorSavedY_ = savedY;
            cursorSavedWidth_ = savedWidth;
            cursorSavedHeight_ = savedHeight;
            cursorSavedPixels_ = savedPixels;
            if (cursorSavedHeight_)
                *cursorSavedHeight_ = 0;
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

            if (cursorSavedX_ && cursorSavedY_ && cursorSavedWidth_ &&
                cursorSavedHeight_ && cursorSavedPixels_)
            {
                if (*cursorSavedHeight_ > 0)
                {
                    cursorWidth_ = std::clamp(*cursorSavedWidth_, 0, kCursorPitch);
                    cursorHeight_ = std::clamp(*cursorSavedHeight_, 0, kCursorPitch);
                    cursorValid_ = cursorWidth_ > 0 && cursorHeight_ > 0;
                    cursorX_ = *cursorSavedX_;
                    cursorY_ = *cursorSavedY_;
                    for (int row = 0; row < cursorHeight_; ++row)
                        std::copy_n(
                            cursorSavedPixels_ + row * kCursorPitch,
                            cursorWidth_,
                            cursorPixels_.data() + row * kCursorPitch);
                }
                *cursorSavedHeight_ = 0;
            }

            auto* destination = static_cast<uint8_t*>(actualRenderer_);
            for (int y = 0; y < height; ++y)
                std::memcpy(destination + y * actualPitch_, presentation_.data() + y * width, width * sizeof(uint16_t));
            renderer = actualRenderer_;
            pitch = actualPitch_;
            routed_ = false;
            cursorSavedX_ = nullptr;
            cursorSavedY_ = nullptr;
            cursorSavedWidth_ = nullptr;
            cursorSavedHeight_ = nullptr;
            cursorSavedPixels_ = nullptr;
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
        bool restorePending_{};
        bool presentationValid_{};
        bool routed_{};
        bool worldIsolated_{};
        IndicatorAnchor indicatorAnchor_{ IndicatorAnchor::Left };
        bool persistentIndicator_{};
        bool indicatorActive_{};
        uint32_t indicatorTick_{};
        static constexpr int kCursorPitch = 64;
        int cursorX_{};
        int cursorY_{};
        int cursorWidth_{};
        int cursorHeight_{};
        bool cursorValid_{};
        int* cursorSavedX_{};
        int* cursorSavedY_{};
        int* cursorSavedWidth_{};
        int* cursorSavedHeight_{};
        uint16_t* cursorSavedPixels_{};
        std::array<uint16_t, kCursorPitch * kCursorPitch> cursorPixels_{};
        void* actualRenderer_{};
        uint32_t actualPitch_{};
        std::vector<uint16_t> clean_;
        std::vector<uint16_t> world_;
        std::vector<uint16_t> presentation_;
        std::vector<uint16_t> isolatedWorld_;
        std::vector<uint16_t> isolatedBack_;
    };

    inline State& GetState()
    {
        static State state;
        return state;
    }
}
