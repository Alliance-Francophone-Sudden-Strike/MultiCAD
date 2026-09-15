#pragma once

// Reads live control-group state out of the running game.
//
// Fail-closed by construction: unless the game version is bound AND every
// signature matches, the reader stays inert and reports no groups. The walk is
// structurally safe rather than SEH-guarded, because the MinGW build strips
// __try/__except (see mingw/prep.py) and would otherwise be unprotected.
//
// Membership is decided by the game's own per-unit virtuals, in the same order
// fnGroupSelect uses them, so whatever a class does to answer "am I in group
// N" - a building answering for the squad garrisoned inside it, a vehicle for
// its crew - the panel inherits for free. Reading the unit's group byte
// directly cannot see any of that. Both virtuals are pure reads, and both
// pointers are validated before the call.

#include "GameGlobals.h"
#include "GroupPanel.h"
#include "GroupPanelTraits.h"

#include <array>
#include <cstdint>

#include <windows.h>

class GroupPanelReader
{
    // GCC parses `RET(__thiscall)(ARGS)` but silently drops the convention,
    // which would push `this` on the stack and then over-pop by the callee's
    // own `ret 8`. prep.py only rewrites the form spelled inside getFn<>, so
    // spell these out per compiler instead.
#if defined(__GNUC__)
    typedef void SelectFn(void*, int, int) __attribute__((__thiscall__));
    typedef int AliveFn(void*) __attribute__((__thiscall__));
    typedef int InGroupFn(void*, int, int) __attribute__((__thiscall__));
#else
    using SelectFn = void(__thiscall)(void*, int, int);
    using AliveFn = int(__thiscall)(void*);
    using InGroupFn = int(__thiscall)(void*, int, int);
#endif

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
        units_ = {};
        vtables_ = {};
        code_ = {};

        auto* headSlot = globals_->getPtr<uint8_t*>(addresses_->unitListHead);
        if (!isReadable(headSlot, sizeof(uint8_t*)))
            return;

        // Enough of a unit to cover its vtable pointer and its next pointer.
        const size_t probe = static_cast<size_t>(addresses_->unitNextOffset) + sizeof(void*);

        int missing = GroupPanel::kCount;
        uint8_t* unit = *headSlot;
        for (int guard = 0; unit && guard < kMaxUnits; ++guard)
        {
            if (!isReadableCached(units_, unit, probe))
                return;                       // corrupt list: keep what we have

            auto* const alive = vtableFn<AliveFn>(unit, addresses_->unitAliveVtableOffset);
            auto* const inGroup = vtableFn<InGroupFn>(unit, addresses_->unitGroupVtableOffset);

            if (alive && inGroup && alive(unit))
            {
                for (int slot = 0; slot < GroupPanel::kCount; ++slot)
                {
                    if (active_[static_cast<size_t>(slot)] ||
                        !inGroup(unit, GroupPanel::StoredValueForSlot(slot), 0))
                        continue;

                    active_[static_cast<size_t>(slot)] = true;
                    if (--missing == 0)
                        return;
                }
            }

            unit = *reinterpret_cast<uint8_t**>(unit + addresses_->unitNextOffset);
        }
    }

    template<typename Fn>
    Fn* vtableFn(uint8_t* unit, uintptr_t offset)
    {
        auto* const vtable = *reinterpret_cast<uint8_t**>(unit);
        if (!isReadableCached(vtables_, vtable, static_cast<size_t>(offset) + sizeof(void*)))
            return nullptr;

        auto* const fn = *reinterpret_cast<Fn**>(vtable + offset);
        if (!isReadableCached(code_, reinterpret_cast<const void*>(fn), 1))
            return nullptr;

        return fn;
    }

    // One cached region per pointer kind the walk follows, so a unit, a vtable
    // and a game function never evict each other's region.
    struct Region
    {
        uintptr_t start{ 0 };
        uintptr_t end{ 0 };
    };

    static bool isReadableCached(Region& region, const void* p, size_t bytes)
    {
        const auto first = reinterpret_cast<uintptr_t>(p);
        if (region.end && first >= region.start && first + bytes <= region.end)
            return true;

        return isReadable(p, bytes, &region.start, &region.end);
    }

    // Same shape as GameDllHooks::is_valid_ptr, which is private to that class.
    static bool isReadable(const void* p, size_t bytes,
                           uintptr_t* regionStart = nullptr, uintptr_t* regionEnd = nullptr)
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
        if (first < start || first + bytes > end)
            return false;

        if (regionStart && regionEnd)
        {
            *regionStart = start;
            *regionEnd = end;
        }
        return true;
    }

    GameGlobals* globals_{ nullptr };
    const GroupPanelAddresses* addresses_{ nullptr };
    std::array<bool, GroupPanel::kCount> active_{};
    Region units_{};
    Region vtables_{};
    Region code_{};
    uint32_t lastTick_{ 0 };
    bool primed_{ false };
};
