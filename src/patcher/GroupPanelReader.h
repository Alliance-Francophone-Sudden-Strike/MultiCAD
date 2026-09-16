#pragma once

// Reads live control-group state out of the running game.
//
// Fail-closed by construction: unless the game version is bound AND every
// signature matches, the reader stays inert and reports no groups. The walk is
// structurally safe rather than SEH-guarded, because the MinGW build strips
// __try/__except (see mingw/prep.py) and would otherwise be unprotected.
//
// Membership is decided by the game's own per-unit virtuals, in the same order
// fnGroupSelect uses them. Vehicle crew is the one exception: the common-unit
// predicate only searches its passenger list, so its adjacent crew list is
// read directly - both to light the panel and, in select(), to make the game's
// own walk find a vehicle it would otherwise never reach. All pointers are
// validated before use.

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
    static constexpr int kMaxVehicleSlots = 64;

    static constexpr int kMaxCrewPatches = 32;

    static constexpr int kAltModifier = 0x2;

    bool bind(GameGlobals& globals, GameVersion version)
    {
        globals_ = nullptr;
        addresses_ = nullptr;
        active_.fill(false);
        house_.fill(false);
        wheel_.fill(false);
        transport_.fill(false);
        gun_.fill(false);
        counts_.fill(0);
        primed_ = false;

        const GroupPanelAddresses* addresses = TryGetGroupPanelAddresses(version);
        if (!addresses)
            return false;

        for (const GroupPanelSignature& signature : addresses->signatures)
        {
            if (signature.rva == 0)
                continue;
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

    const std::array<bool, GroupPanel::kCount>& house() const { return house_; }
    const std::array<bool, GroupPanel::kCount>& wheel() const { return wheel_; }
    const std::array<bool, GroupPanel::kCount>& transport() const { return transport_; }
    const std::array<bool, GroupPanel::kCount>& gun() const { return gun_; }
    const GroupPanel::Counts& counts() const { return counts_; }

    // Runs the game's own group-select, exactly as the number-row key does.
    bool select(int slot)
    {
        if (!bound() || slot < 0 || slot >= GroupPanel::kCount)
            return false;

        void* self = globals_->getPtr<void>(addresses_->groupCommandThis);
        if (!isReadable(self, sizeof(void*)))
            return false;

        auto* fn = globals_->getFn<SelectFn>(addresses_->fnGroupSelect);
        if (!fn)
            return false;

        std::array<GroupBytePatch, kMaxCrewPatches> patches{};
        const int patched = lendGroupByteToCrewVehicles(slot, patches);

        const auto index = static_cast<size_t>(slot);
        fn(self, GroupPanel::KeyIndexForSlot(slot),
           wheel_[index] || transport_[index] || gun_[index] ? kAltModifier : 0);

        for (int i = patched - 1; i >= 0; --i)
            *patches[static_cast<size_t>(i)].byte = patches[static_cast<size_t>(i)].previous;
        return true;
    }

    bool assign(int slot) const
    {
        if (!bound() || slot < 0 || slot >= GroupPanel::kCount || !addresses_->fnGroupAssign)
            return false;

        void* self = globals_->getPtr<void>(addresses_->groupCommandThis);
        if (!isReadable(self, sizeof(void*)))
            return false;

        auto* fn = globals_->getFn<SelectFn>(addresses_->fnGroupAssign);
        if (!fn)
            return false;

        fn(self, GroupPanel::KeyIndexForSlot(slot), 0);
        return true;
    }

private:
    static constexpr size_t kCrewData = 0x4;
    static constexpr size_t kCrewCount = 0xC;
    static constexpr size_t kPassengerData = 0x10;
    static constexpr size_t kPassengerCount = 0x18;
    static constexpr size_t kContainerSize = kPassengerCount + sizeof(int);

    struct GroupBytePatch
    {
        uint8_t* byte;
        uint8_t previous;
    };

    void refresh()
    {
        units_ = {};
        vtables_ = {};
        code_ = {};
        occupants_ = {};
        vtableCount_ = 0;

        auto* headSlot = globals_->getPtr<uint8_t*>(addresses_->unitListHead);
        if (!isReadable(headSlot, sizeof(uint8_t*)))
            return;

        const size_t next = static_cast<size_t>(addresses_->unitNextOffset) + sizeof(void*);
        const size_t group = static_cast<size_t>(addresses_->unitGroupOffset) + 1;
        const size_t probe = next > group ? next : group;

        std::array<bool, GroupPanel::kCount> active{};
        std::array<bool, GroupPanel::kCount> house{};
        std::array<bool, GroupPanel::kCount> wheel{};
        std::array<bool, GroupPanel::kCount> transport{};
        std::array<bool, GroupPanel::kCount> gun{};
        GroupPanel::Counts counts{};

        uint8_t* unit = *headSlot;
        for (int guard = 0; unit && guard < kMaxUnits; ++guard)
        {
            if (!isReadableCached(units_, unit, probe))
                return;                       // corrupt list: keep what we have

            AliveFn* alive = nullptr;
            InGroupFn* inGroup = nullptr;
            bool gunCrew = false;
            uint8_t vehicleContainer = 0;

            if (resolveUnit(unit, alive, inGroup, gunCrew, vehicleContainer) && alive(unit))
            {
                if (vehicleContainer &&
                    !scanVehicleGroups(unit + vehicleContainer, wheel, transport, active, counts))
                    return;

                const int ownSlot = GroupPanel::SlotForStoredValue(unit[addresses_->unitGroupOffset]);
                if (ownSlot >= 0)
                {
                    active[static_cast<size_t>(ownSlot)] = true;
                    ++counts[static_cast<size_t>(ownSlot)];
                }

                for (int slot = 0; slot < GroupPanel::kCount; ++slot)
                {
                    const auto index = static_cast<size_t>(slot);

                    if (slot == ownSlot)
                        continue;

                    const uint8_t stored = GroupPanel::StoredValueForSlot(slot);
                    if (!inGroup(unit, stored, kAltModifier))
                        continue;

                    active[index] = true;
                    if (!vehicleContainer) // vehicle occupants were counted from their records
                        ++counts[index];

                    if (inGroup(unit, stored, 0))
                        house[index] = true;
                    else if (gunCrew)
                        gun[index] = true;
                    else
                        transport[index] = true;
                }
            }

            unit = *reinterpret_cast<uint8_t**>(unit + addresses_->unitNextOffset);
        }

        active_ = active;
        house_ = house;
        wheel_ = wheel;
        transport_ = transport;
        gun_ = gun;
        counts_ = counts;
    }

    int lendGroupByteToCrewVehicles(int slot, std::array<GroupBytePatch, kMaxCrewPatches>& patches)
    {
        auto* headSlot = globals_->getPtr<uint8_t*>(addresses_->unitListHead);
        if (!isReadable(headSlot, sizeof(uint8_t*)))
            return 0;

        const size_t next = static_cast<size_t>(addresses_->unitNextOffset) + sizeof(void*);
        const size_t group = static_cast<size_t>(addresses_->unitGroupOffset) + 1;
        const size_t probe = next > group ? next : group;
        const uint8_t stored = GroupPanel::StoredValueForSlot(slot);

        int patched = 0;
        uint8_t* unit = *headSlot;
        for (int guard = 0; unit && guard < kMaxUnits && patched < kMaxCrewPatches; ++guard)
        {
            if (!isReadableCached(units_, unit, probe))
                return patched;

            AliveFn* alive = nullptr;
            InGroupFn* inGroup = nullptr;
            bool gunCrew = false;
            uint8_t vehicleContainer = 0;

            uint8_t* const groupByte = unit + addresses_->unitGroupOffset;
            if (resolveUnit(unit, alive, inGroup, gunCrew, vehicleContainer) && alive(unit) &&
                vehicleContainer && *groupByte != stored &&
                crewHoldsSlot(unit + vehicleContainer, slot) &&
                isReadable(groupByte, 1, nullptr, nullptr, kWritable))
            {
                patches[static_cast<size_t>(patched++)] = { groupByte, *groupByte };
                *groupByte = stored;
            }

            unit = *reinterpret_cast<uint8_t**>(unit + addresses_->unitNextOffset);
        }
        return patched;
    }

    bool crewHoldsSlot(uint8_t* container, int slot)
    {
        if (!isReadableCached(units_, container, kContainerSize))
            return false;

        std::array<bool, GroupPanel::kCount> crew{};
        std::array<bool, GroupPanel::kCount> ignoredActive{};
        GroupPanel::Counts ignoredCounts{};
        if (!scanOccupantGroups(*reinterpret_cast<uint8_t**>(container + kCrewData),
                                *reinterpret_cast<int*>(container + kCrewCount),
                                crew, ignoredActive, ignoredCounts))
            return false;

        return crew[static_cast<size_t>(slot)];
    }

    bool resolveUnit(uint8_t* unit, AliveFn*& alive, InGroupFn*& inGroup,
                     bool& gun, uint8_t& vehicleContainer)
    {
        auto* const vtable = *reinterpret_cast<uint8_t**>(unit);

        for (int i = 0; i < vtableCount_; ++i)
        {
            if (vtableCache_[static_cast<size_t>(i)].vtable == vtable)
            {
                alive = vtableCache_[static_cast<size_t>(i)].alive;
                inGroup = vtableCache_[static_cast<size_t>(i)].inGroup;
                gun = vtableCache_[static_cast<size_t>(i)].gun;
                vehicleContainer = vtableCache_[static_cast<size_t>(i)].vehicleContainer;
                return alive && inGroup;
            }
        }

        alive = vtableFn<AliveFn>(vtable, addresses_->unitAliveVtableOffset);
        inGroup = vtableFn<InGroupFn>(vtable, addresses_->unitGroupVtableOffset);
        gun = isGunPredicate(inGroup);
        vehicleContainer = vehicleContainerOffset(inGroup);

        if (vtableCount_ < kVtableCacheSize)
            vtableCache_[static_cast<size_t>(vtableCount_++)] =
                { vtable, alive, inGroup, gun, vehicleContainer };

        return alive && inGroup;
    }

    bool isGunPredicate(InGroupFn* fn)
    {
        constexpr std::string_view ss2Pattern =
            "33c0568a41??8b7424083bc67509b8010000005ec208008b44240c85c0742033d2"
            "8d81????????8b48f285c9740833c98a083bce74d84283c0??83fa027ce833c05ec20800";
        constexpr std::string_view goldPattern =
            "33c08a41??568b7424083bc67509b8010000005ec208008b44240c85c0742033d2"
            "8d81????????8b48f285c9740833c98a083bce74d84283c0??83fa027ce833c05ec20800";
        if (!fn || !isReadableCached(code_, fn, GroupPanel::SignatureLength(ss2Pattern)))
            return false;

        const auto* code = reinterpret_cast<const uint8_t*>(fn);
        return GroupPanel::Matches(code, ss2Pattern) || GroupPanel::Matches(code, goldPattern);
    }

    uint8_t vehicleContainerOffset(InGroupFn* fn)
    {
        // The common-unit predicate adds this container offset before calling
        // the game's passenger-list search. The same container holds crew at
        // +4/+0xc and passengers at +0x10/+0x18.
        constexpr std::string_view pattern =
            "8b44240433d28a51??3bd0741a8b54240885d2740d5083c1??e8????????"
            "85c0750533c0c20800b801000000c20800";
        if (!fn || !isReadableCached(code_, fn, GroupPanel::SignatureLength(pattern)))
            return 0;

        const auto* code = reinterpret_cast<const uint8_t*>(fn);
        return GroupPanel::Matches(code, pattern) ? code[24] : 0;
    }

    bool scanVehicleGroups(uint8_t* container,
                           std::array<bool, GroupPanel::kCount>& crew,
                           std::array<bool, GroupPanel::kCount>& passengers,
                           std::array<bool, GroupPanel::kCount>& active,
                           GroupPanel::Counts& counts)
    {
        if (!isReadableCached(units_, container, kContainerSize))
            return false;

        auto* crewData = *reinterpret_cast<uint8_t**>(container + kCrewData);
        const int crewCount = *reinterpret_cast<int*>(container + kCrewCount);
        auto* passengerData = *reinterpret_cast<uint8_t**>(container + kPassengerData);
        const int passengerCount = *reinterpret_cast<int*>(container + kPassengerCount);
        return scanOccupantGroups(crewData, crewCount, crew, active, counts) &&
               scanOccupantGroups(passengerData, passengerCount, passengers, active, counts);
    }

    bool scanOccupantGroups(uint8_t* records, int count,
                            std::array<bool, GroupPanel::kCount>& badges,
                            std::array<bool, GroupPanel::kCount>& active,
                            GroupPanel::Counts& counts)
    {
        constexpr size_t kRecordSize = 0x15;
        constexpr size_t kGroupOffset = 0xE;
        if (count < 0 || count > kMaxVehicleSlots)
            return false;
        if (count == 0)
            return true;
        if (!isReadableCached(occupants_, records, static_cast<size_t>(count) * kRecordSize))
            return false;

        for (int i = 0; i < count; ++i)
        {
            const int slot = GroupPanel::SlotForStoredValue(
                records[static_cast<size_t>(i) * kRecordSize + kGroupOffset]);
            if (slot >= 0)
            {
                badges[static_cast<size_t>(slot)] = true;
                active[static_cast<size_t>(slot)] = true;
                ++counts[static_cast<size_t>(slot)];
            }
        }
        return true;
    }

    template<typename Fn>
    Fn* vtableFn(uint8_t* vtable, uintptr_t offset)
    {
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

    static constexpr DWORD kReadable =
        PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    static constexpr DWORD kWritable =
        PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    // Same shape as GameDllHooks::is_valid_ptr, which is private to that class.
    static bool isReadable(const void* p, size_t bytes,
                           uintptr_t* regionStart = nullptr, uintptr_t* regionEnd = nullptr,
                           DWORD protect = kReadable)
    {
        if (!p || bytes == 0)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (!(mbi.Protect & protect))
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
    std::array<bool, GroupPanel::kCount> house_{};
    std::array<bool, GroupPanel::kCount> wheel_{};
    std::array<bool, GroupPanel::kCount> transport_{};
    std::array<bool, GroupPanel::kCount> gun_{};
    GroupPanel::Counts counts_{};

    struct VtableEntry
    {
        const void* vtable;
        AliveFn* alive;
        InGroupFn* inGroup;
        bool gun;
        uint8_t vehicleContainer;
    };

    static constexpr int kVtableCacheSize = 64;
    std::array<VtableEntry, kVtableCacheSize> vtableCache_{};
    int vtableCount_{ 0 };

    Region units_{};
    Region vtables_{};
    Region code_{};
    Region occupants_{};
    uint32_t lastTick_{ 0 };
    bool primed_{ false };
};
