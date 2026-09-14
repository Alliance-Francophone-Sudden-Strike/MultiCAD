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
    // Group membership is a byte on each unit, not a slot table. Walk the unit
    // list and read the byte: the game clears it itself when a unit dies, and
    // never replicates it into another player's process.
    uintptr_t unitListHead;     // RVA of the pointer to the first unit
    uintptr_t unitNextOffset;   // offset of the next-unit pointer inside a unit
    uintptr_t unitGroupOffset;  // offset of the group byte inside a unit

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
        0x44,

        0xB3FC0,
        0x106F470,

        std::array<GroupPanelSignature, 4>
        {{
            // group select - the function a number-row key ends up calling
            { 0xB3FC0, "83ec088b4424105355565750894c2418e82bf1ffff8b35????????33db33ed3bf3895c241074678b168bceff523485c0" },
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

    // Offset in a unit's vtable of the owner+liveness predicate. The reader
    // does NOT call it: group bytes are cleared on death and never replicated,
    // so membership alone is already correct, and calling a game function from
    // the render path would add risk for no gain. Recorded because the panel's
    // safety argument rests on it.
    static constexpr uintptr_t unitAliveVtableOffset = 0x34;
};

template<>
struct GroupPanelTraits<GameVersion::HS_2> : GroupPanelTraits<GameVersion::SS_2> {};

// Runtime lookup: nullptr for every version the panel is not proved against,
// so an unsupported hash or a forced profile simply has no panel.
inline const GroupPanelAddresses* TryGetGroupPanelAddresses(GameVersion version)
{
    if (version == GameVersion::SS_2)
        return &GroupPanelTraits<GameVersion::SS_2>::addresses;
    if (version == GameVersion::HS_2)
        return &GroupPanelTraits<GameVersion::HS_2>::addresses;
    return nullptr;
}
