#pragma once

// Reads live control-group state out of the running game.
//
// Fail-closed by construction: unless the game version is bound AND every
// signature matches, the reader stays inert and reports no groups. The walk is
// structurally safe rather than SEH-guarded, because the MinGW build strips
// __try/__except (see mingw/prep.py) and would otherwise be unprotected.

#include "GameGlobals.h"
#include "GroupPanel.h"
#include "GroupPanelTraits.h"

#include <array>
#include <cstdint>

#include <windows.h>

class GroupPanelReader
{
public:
    // Refresh cadence. The panel is a coarse indicator, so re-walking the unit
    // list ten times a second is plenty and keeps the per-unit probe cheap
    // enough not to matter.
    static constexpr uint32_t kRefreshMs = 100;

    // Upper bound on the unit list. A live mission ran ~400; this only has to
    // stop a corrupt or cyclic list from spinning forever.
    static constexpr int kMaxUnits = 8192;

    bool bind(GameGlobals& globals, GameVersion version)
    {
        globals_ = nullptr;
        addresses_ = nullptr;
        active_.fill(false);
        primed_ = false;

        const GroupPanelAddresses* addresses = TryGetGroupPanelAddresses(version);
        if (!addresses)
            return false;

        for (const GroupPanelSignature& signature : addresses->signatures)
        {
            const auto* code = globals.getPtr<const uint8_t>(signature.rva);
            const int length = GroupPanel::SignatureLength(signature.pattern);
            if (!isReadable(code, static_cast<size_t>(length)))
                return false;
            if (!GroupPanel::Matches(code, signature.pattern))
                return false;
        }

        globals_ = &globals;
        addresses_ = addresses;
        return true;
    }

    bool bound() const { return addresses_ != nullptr; }

    // Active groups indexed by panel slot. Cached between refreshes; returns
    // all-false while unbound.
    const std::array<bool, GroupPanel::kCount>& groups(uint32_t tick)
    {
        if (!bound())
            return active_;

        if (!primed_ || tick - lastTick_ >= kRefreshMs)
        {
            refresh();
            lastTick_ = tick;
            primed_ = true;
        }
        return active_;
    }

    // Runs the game's own group-select, exactly as the number-row key does.
    bool select(int slot) const
    {
        if (!bound() || slot < 0 || slot >= GroupPanel::kCount)
            return false;

        void* self = globals_->getPtr<void>(addresses_->groupCommandThis);
        if (!isReadable(self, sizeof(void*)))
            return false;

        using SelectFn = void(__thiscall)(void*, int, int);
        auto* fn = globals_->getFn<SelectFn>(addresses_->fnGroupSelect);
        if (!fn)
            return false;

        fn(self, GroupPanel::KeyIndexForSlot(slot), 0);
        return true;
    }

private:
    void refresh()
    {
        active_.fill(false);

        auto* headSlot = globals_->getPtr<uint8_t*>(addresses_->unitListHead);
        if (!isReadable(headSlot, sizeof(uint8_t*)))
            return;

        // Enough of a unit to cover both fields we touch.
        const size_t probe = static_cast<size_t>(
            (addresses_->unitGroupOffset > addresses_->unitNextOffset
                 ? addresses_->unitGroupOffset
                 : addresses_->unitNextOffset) + sizeof(void*));

        uint8_t* unit = *headSlot;
        for (int guard = 0; unit && guard < kMaxUnits; ++guard)
        {
            if (!isReadable(unit, probe))
                return;                       // corrupt list: keep what we have

            const int slot = GroupPanel::SlotForStoredValue(unit[addresses_->unitGroupOffset]);
            if (slot >= 0)
                active_[static_cast<size_t>(slot)] = true;

            unit = *reinterpret_cast<uint8_t**>(unit + addresses_->unitNextOffset);
        }
    }

    // Same shape as GameDllHooks::is_valid_ptr, which is private to that class.
    static bool isReadable(const void* p, size_t bytes)
    {
        if (!p || bytes == 0)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;

        // The range must not run off the end of the queried region.
        const auto start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto end = start + mbi.RegionSize;
        const auto first = reinterpret_cast<uintptr_t>(p);
        return first >= start && first + bytes <= end;
    }

    GameGlobals* globals_{ nullptr };
    const GroupPanelAddresses* addresses_{ nullptr };
    std::array<bool, GroupPanel::kCount> active_{};
    uint32_t lastTick_{ 0 };
    bool primed_{ false };
};
