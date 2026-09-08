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
        Steps,
        Smooth
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

        if (equals("steps"))
            return Mode::Steps;
        if (equals("smooth"))
            return Mode::Smooth;
        return Mode::Off;
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
        bool suppressed() const { return suppressFrames_ != 0; }

        void beginFrame(bool hasTextOverlay)
        {
            if (hasTextOverlay)
                suppressFrames_ = 2;
            else if (suppressFrames_)
                --suppressFrames_;
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
            suppressFrames_ = 0;
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

        void beginWorldIsolation(uint16_t* main, uint16_t* back, size_t pixels)
        {
            finishWorldIsolation(main, back);
            if (mode_ == Mode::Off || suppressed() || !main || !back)
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
        int suppressFrames_{};
        Rect battlefield_{};
        Transform presented_{};
        int presentedScale_{ kMinScale };
        bool hasPresented_{};
        bool restorePending_{};
        bool presentationValid_{};
        bool routed_{};
        bool worldIsolated_{};
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
