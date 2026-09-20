#pragma once

#include <cstdint>

#include "types.h"

#include <array>
#include <string_view>

struct ZeppelinSignature
{
    uintptr_t rva;
    std::string_view pattern;
};

struct ZeppelinAddresses
{
    uintptr_t modeFlag;
    uintptr_t objectPtr;
    uintptr_t localPlayer;
    uintptr_t playerTeam;
    uintptr_t playerStride;

    uintptr_t objectSize;
    uintptr_t groupCount;
    uintptr_t records;
    uintptr_t recordStride;
    uintptr_t captureSeconds;
    uintptr_t heldZeppelins;

    uintptr_t colorRoot;
    uintptr_t colorFirst;
    uintptr_t colorSecond;
    uintptr_t colorOffset;

    uintptr_t recordMask;
    uintptr_t recordOwner;
    uintptr_t recordCaptor;
    uintptr_t recordProgress;

    std::array<ZeppelinSignature, 4> signatures;
};

template<GameVersion Version>
struct ZeppelinTraits;

template<>
struct ZeppelinTraits<GameVersion::HS_2>
{
    static constexpr ZeppelinAddresses addresses
    {
        0x106F6B0,
        0x106F69C,
        0x106F05C,
        0x892FD4,
        0xB5,

        0x152C,
        0x480,
        0x484,
        0x160,
        0x1508,
        0x340,

        0x1528,
        0x24,
        0x1E,
        0x1A00,

        0x00,
        0x04,
        0x148,
        0x14C,

        std::array<ZeppelinSignature, 4>
        {{
            { 0xC99D0, "a1????????5683f801752468????????682c150000e8????????83c40885c0743b8bc8e8????????a3????????5ec368????????683c030000e83205" },
            { 0xB47E0, "568b742408578bf96a0068????????8bcee8????????85c075138b46088987241500005fb8010000005ec204" },
            { 0xB53A7, "8bc1992bc2d1f803c7eb098bc7992bc2d1f803c13b86241500007d338bcbe8????????84c0742833c033d28a451c8d0c808d0cc98a9488d42f????" },
            { 0xB54A3, "81c630050000c74424284c010000897424388b442418c78544010000ff????????40030000c744242400000000894424" },
        }},
    };
};

inline const ZeppelinAddresses* TryGetZeppelinAddresses(GameVersion version)
{
    if (version == GameVersion::HS_2)
        return &ZeppelinTraits<GameVersion::HS_2>::addresses;
    return nullptr;
}
