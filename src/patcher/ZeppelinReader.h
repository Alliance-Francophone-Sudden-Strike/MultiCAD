#pragma once

#include "GameGlobals.h"
#include "GroupPanel.h"
#include "MemoryProbe.h"
#include "ZeppelinPanel.h"
#include "ZeppelinTraits.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

#include <windows.h>

class ZeppelinReader
{
public:
    static constexpr uint32_t kRefreshMs = 100;
    static constexpr int kMaxGroups = 12;
    static constexpr int kTeams = 4;
    static constexpr int kMaxPlayers = 12;
    static constexpr int kTicksPerSecond = 25;
    static constexpr uint16_t kFallbackColor = 0x8410;

    void bind(GameGlobals& globals, GameVersion version)
    {
        *this = ZeppelinReader{};

        const ZeppelinAddresses* candidate = TryGetZeppelinAddresses(version);
        if (!candidate)
            return;

        for (const auto& signature : candidate->signatures)
        {
            if (signature.rva == 0 || signature.pattern.empty())
                continue;

            const auto* code = globals.getPtr<uint8_t>(signature.rva);
            const size_t length = GroupPanel::SignatureLength(signature.pattern);
            if (length == 0 ||
                !MemoryProbe::IsReadable(code, length) ||
                !GroupPanel::Matches(code, signature.pattern))
                return;
        }

        globals_ = &globals;
        addresses_ = candidate;
    }

    bool bound() const { return addresses_ != nullptr; }

    int rowCount() const { return rowCount_; }

    const ZeppelinPanel::Rows& rows() const { return rows_; }

    const ZeppelinPanel::Rows& groups(uint32_t tick)
    {
        if (primed_ && tick - lastTick_ < kRefreshMs)
            return rows_;

        lastTick_ = tick;
        primed_ = true;
        rows_ = {};
        rowCount_ = 0;

        scan();
        return rows_;
    }

private:
    void scan()
    {
        if (!globals_ || !addresses_)
            return;

        const ZeppelinAddresses& at = *addresses_;

        const auto* modeSlot = globals_->getPtr<int>(at.modeFlag);
        if (!MemoryProbe::IsReadable(modeSlot, sizeof(int)) || *modeSlot != 1)
            return;

        const auto* playerSlot = globals_->getPtr<int>(at.localPlayer);
        if (!MemoryProbe::IsReadable(playerSlot, sizeof(int)))
            return;

        const int player = *playerSlot;
        if (player < 0 || player >= kMaxPlayers)
            return;

        const auto* const teamSlot = globals_->getPtr<uint8_t>(at.playerTeam) +
            at.playerStride * static_cast<size_t>(player);
        if (!MemoryProbe::IsReadable(teamSlot, sizeof(uint8_t)))
            return;

        const int team = *teamSlot;
        if (team >= kTeams)
            return;

        auto* const objectSlot = globals_->getPtr<uint8_t*>(at.objectPtr);
        if (!MemoryProbe::IsReadable(objectSlot, sizeof(uint8_t*)))
            return;

        uint8_t* const object = *objectSlot;
        if (!MemoryProbe::IsReadableCached(object_, object, at.objectSize))
            return;

        const int groups = read<int>(object + at.groupCount);
        if (groups <= 0 || groups > kMaxGroups)
            return;

        const int seconds = read<int>(object + at.captureSeconds);
        if (seconds <= 0)
            return;

        const int target = seconds * kTicksPerSecond;
        const uint16_t* const colors = colorTable(object, at);
        const uint32_t held =
            read<uint32_t>(object + at.heldZeppelins + sizeof(uint32_t) * team);

        for (int index = 0; index < groups; ++index)
        {
            const uint8_t* const record = object + at.records + at.recordStride * index;
            const uint32_t owner = read<uint32_t>(record + at.recordOwner);
            if ((owner & (1u << team)) == 0)
                continue;

            const uint32_t mask = read<uint32_t>(record + at.recordMask);
            if (mask == 0)
                continue;

            const int raw = read<int>(record + at.recordProgress + sizeof(int) * team);
            const int progress = std::clamp(raw, 0, target);

            rows_[static_cast<size_t>(rowCount_)] =
            {
                color(colors, index + 1),
                ZeppelinPanel::SecondsLeft(progress, target, kTicksPerSecond),
                std::popcount(held & mask),
                std::popcount(mask),
                progress > 0,
            };
            ++rowCount_;
        }
    }

    const uint16_t* colorTable(const uint8_t* object, const ZeppelinAddresses& at)
    {
        const uint8_t* root = read<uint8_t*>(object + at.colorRoot);
        if (!MemoryProbe::IsReadableCached(color_, root, at.colorFirst + sizeof(void*)))
            return nullptr;

        const uint8_t* first = read<uint8_t*>(root + at.colorFirst);
        if (!MemoryProbe::IsReadableCached(color_, first, at.colorSecond + sizeof(void*)))
            return nullptr;

        const uint8_t* second = read<uint8_t*>(first + at.colorSecond);
        const size_t span = at.colorOffset + sizeof(uint16_t) * (kMaxGroups + 1);
        if (!MemoryProbe::IsReadableCached(color_, second, span))
            return nullptr;

        return reinterpret_cast<const uint16_t*>(second + at.colorOffset);
    }

    static uint16_t color(const uint16_t* colors, int index)
    {
        if (!colors || index < 0 || index > kMaxGroups)
            return kFallbackColor;

        uint16_t value = 0;
        std::memcpy(&value, colors + index, sizeof(value));
        return value ? value : kFallbackColor;
    }

    template<typename T>
    static T read(const uint8_t* at)
    {
        T value{};
        std::memcpy(&value, at, sizeof(value));
        return value;
    }

    GameGlobals* globals_{ nullptr };
    const ZeppelinAddresses* addresses_{ nullptr };
    ZeppelinPanel::Rows rows_{};
    int rowCount_{ 0 };
    MemoryProbe::Region object_{};
    MemoryProbe::Region color_{};
    uint32_t lastTick_{ 0 };
    bool primed_{ false };
};
