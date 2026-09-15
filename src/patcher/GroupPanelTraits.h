#pragma once

// Control-group state binding for Sudden Strike 2 v2.2 and the HS_2 profile.
// Every address below is still checked against the live Game_Dll before use
// (see control-group-panel-plan.md, "GROUP STATE FOUND").

#include <cstdint>   // before types.h: it uses uintptr_t without including this

#include "types.h"

#include <array>
#include <string_view>

// Byte pattern expected at an RVA. "??" covers bytes holding a baked-in
// absolute address, which ASLR moves on every run.
struct GroupPanelSignature
{
    uintptr_t rva;
    std::string_view pattern;
};

struct GroupPanelAddresses
{
    // Membership is answered by each unit's own virtual predicate, not by
    // reading its group byte: a garrisoned squad is absorbed into its
    // building's occupant records, and only the building's override of that
    // predicate can still see it. Walking the list and calling the same two
    // virtuals fnGroupSelect calls is what keeps the panel and the number-row
    // key in agreement.
    uintptr_t unitListHead;          // RVA of the pointer to the first unit
    uintptr_t unitNextOffset;        // offset of the next-unit pointer inside a unit
    uintptr_t unitAliveVtableOffset; // vtable offset of the owner+liveness predicate
    uintptr_t unitGroupVtableOffset; // vtable offset of the "is in group N" predicate

    uintptr_t unitGroupOffset;

    uintptr_t fnGroupSelect;    // RVA, __thiscall (groupKeyIndex, modifiers)
    uintptr_t groupCommandThis; // RVA used as `this` for fnGroupSelect

    std::array<GroupPanelSignature, 4> signatures;
};

template<GameVersion Version>
struct GroupPanelTraits;   // no primary definition: unsupported by default

template<>
struct GroupPanelTraits<GameVersion::SS_2>
{
    static constexpr GroupPanelAddresses addresses
    {
        0x10F258,
        0xA,
        0x34,
        0x1C,
        0x44,

        0xB3FC0,
        0x106F470,

        std::array<GroupPanelSignature, 4>
        {{
            // group select - the function a number-row key ends up calling.
            // Covers both virtual calls the reader mirrors: [vtable+0x34] then
            // [vtable+0x1c](groupValue, modifiers & 2).
            { 0xB3FC0, "83ec088b4424105355565750894c2418e82bf1ffff8b35????????33db33ed3bf3895c241074678b168bceff523485c074558b4c24208b54241c8b0683e1024251528bceff501c" },
            // group assign - also proves unitListHead, loaded at its +0x8
            { 0xB40F0, "8b015683f80275428b35????????85f67438538b5c????????8bceff501085c074098acbfec1884e44eb17f644241001" },
            // owner + liveness predicate reached through [unit_vtable+0x34]
            { 0x49280, "8a511ab80100000084d075158b15????????538a59238a92????????84d35b750233c0c3" },
            // digit hotkey loop - proves the key table order the slot mapping
            // depends on, so an off-by-one build is rejected rather than shown
            { 0xB1F4B, "33d2b8????????8b3081c6000000013bce746d83c014423d????????7ce9a1" },
        }},
    };

    // Documented but unused: assign is the game's own Ctrl+number path, and it
    // only acts while *(int*)groupCommandThis == 2. Kept so the offset is
    // recorded next to the one the panel does use.
    static constexpr uintptr_t fnGroupAssign = 0xB40F0;
};

template<>
struct GroupPanelTraits<GameVersion::HS_2> : GroupPanelTraits<GameVersion::SS_2> {};

template<>
struct GroupPanelTraits<GameVersion::SS_RW_V2_4>
{
    static constexpr GroupPanelAddresses addresses
    {
        0xFCC90,
        0xA,
        0x38,
        0x1C,
        0x44,

        0xB0250,
        0x10AE848,

        std::array<GroupPanelSignature, 4>
        {{
            { 0xB0250, "83ec08????????5355565750894c2418e8????????8b35????????33db33ed3bf3????????74678b" },
            { 0xB0380, "8b015683f802753d8b35????????85f67433538b5c240c8b068b????????85c074098acbfec1884e44" },
            { 0x48EC0, "8a511ab80100000084d075158b15????????538a59238a92????????84d35b750233c0c390909090" },
            { 0xAE213, "33d2b9????????8b3181c6000000013bc6747083c1144281f9????????7ce8" },
        }},
    };

    static constexpr uintptr_t fnGroupAssign = 0xB0380;
};

template<>
struct GroupPanelTraits<GameVersion::SS_GOLD_HD_1_2_INT>
{
    static constexpr GroupPanelAddresses addresses
    {
        0xC050C,
        0xA,
        0x74,
        0xC,
        0x40,

        0x803E0,
        0x3840C8,

        std::array<GroupPanelSignature, 4>
        {{
            { 0x803E0, "83ec10538b5c241c555633edf6c30157894c241cbefffe00007529a1????????3bc574208b4c2424" },
            { 0x807A0, "8b410485c05674418b35????????85f67437538b5c240c8b068bceff1085c074098acbfec1884e40" },
            { 0x7EDCC, "33c9b8????????8b3881c7000000013bd7746083c014413d????????7ce9" },
            { 0, "" },
        }},
    };

    static constexpr uintptr_t fnGroupAssign = 0x807A0;
};

template<>
struct GroupPanelTraits<GameVersion::SS_GOLD_HD_1_2_RU> : GroupPanelTraits<GameVersion::SS_GOLD_HD_1_2_INT> {};

template<>
struct GroupPanelTraits<GameVersion::SS_GOLD_EN> : GroupPanelTraits<GameVersion::SS_GOLD_HD_1_2_INT> {};

template<>
struct GroupPanelTraits<GameVersion::SS_RW_V2_3> : GroupPanelTraits<GameVersion::SS_RW_V2_4> {};

template<>
struct GroupPanelTraits<GameVersion::SS_EUROPE_2015> : GroupPanelTraits<GameVersion::SS_RW_V2_4> {};

template<>
struct GroupPanelTraits<GameVersion::SS_BLACK_SEA> : GroupPanelTraits<GameVersion::SS_RW_V2_4> {};

// Runtime lookup: nullptr for every version the panel is not proved against,
// so an unsupported hash or a forced profile simply has no panel.
inline const GroupPanelAddresses* TryGetGroupPanelAddresses(GameVersion version)
{
    if (version == GameVersion::SS_2)
        return &GroupPanelTraits<GameVersion::SS_2>::addresses;
    if (version == GameVersion::HS_2)
        return &GroupPanelTraits<GameVersion::HS_2>::addresses;
    if (version == GameVersion::SS_RW_V2_4)
        return &GroupPanelTraits<GameVersion::SS_RW_V2_4>::addresses;
    if (version == GameVersion::SS_RW_V2_3)
        return &GroupPanelTraits<GameVersion::SS_RW_V2_3>::addresses;
    if (version == GameVersion::SS_EUROPE_2015)
        return &GroupPanelTraits<GameVersion::SS_EUROPE_2015>::addresses;
    if (version == GameVersion::SS_BLACK_SEA)
        return &GroupPanelTraits<GameVersion::SS_BLACK_SEA>::addresses;
    if (version == GameVersion::SS_GOLD_HD_1_2_INT)
        return &GroupPanelTraits<GameVersion::SS_GOLD_HD_1_2_INT>::addresses;
    if (version == GameVersion::SS_GOLD_HD_1_2_RU)
        return &GroupPanelTraits<GameVersion::SS_GOLD_HD_1_2_RU>::addresses;
    if (version == GameVersion::SS_GOLD_EN)
        return &GroupPanelTraits<GameVersion::SS_GOLD_EN>::addresses;
    return nullptr;
}
