#pragma once

#include <cstddef>
#include <cstdint>

#include "types.h"

#include <array>

inline constexpr size_t kMaxIsolationUi = 10;
inline constexpr size_t kMaxIsolationDecor = 5;

struct WorldIsolationAddresses
{
    uintptr_t decorHead;
    uintptr_t cursorDraw;
    uintptr_t fnBlendMainWithWarFog;
    uintptr_t fnGetFirstDecorUi;
    uintptr_t fnGetNextDecorUi;

    std::array<std::array<uintptr_t, 4>, kMaxIsolationUi> ui;
    std::array<std::array<uintptr_t, 6>, kMaxIsolationDecor> decor;
};

template<GameVersion Version>
struct WorldIsolationTraits;

template<>
struct WorldIsolationTraits<GameVersion::SS_2>
{
    static constexpr WorldIsolationAddresses addresses
    {
        0x103B6EC,
        0x106E864,
        0x982B0,
        0x79B10,
        0x79B60,

        std::array<std::array<uintptr_t, 4>, kMaxIsolationUi>
        {{
            { 0xEF148, 0xA1000, 0xA98D0, 0xA95A0 },
            { 0xEF19C, 0xA1000, 0xA10F0, 0xA1110 },
            { 0xEF90C, 0xA1000, 0x9A6F0, 0xA1110 },
            { 0xEFACC, 0xA1000, 0xA10F0, 0xA1110 },
            { 0xEFB58, 0xA1000, 0xA2000, 0xA1110 },
            { 0xEFBD8, 0xA1000, 0xA5A60, 0xA5AA0 },
            { 0xEFC2C, 0xA1000, 0xA6030, 0xA1110 },
            { 0xEFF2C, 0xA1000, 0xA10F0, 0xACDE0 },
            { 0xEFF94, 0xA1000, 0xAD740, 0xA1110 },
            { 0xF0250, 0xA1000, 0xC2BC0, 0xC2BD0 },
        }},
        std::array<std::array<uintptr_t, 6>, kMaxIsolationDecor>
        {{
            { 0xEF84C, 0xA04F0, 0xA0620, 0xA0450, 0x97610, 0x976A0 },
            { 0xEF874, 0xA04F0, 0xA0620, 0xA0450, 0xA0460, 0xA0490 },
            { 0xEFA64, 0xA04F0, 0xA0620, 0xA0450, 0xA0460, 0xA0490 },
            { 0xEFBB4, 0xA04F0, 0xA0620, 0xA0450, 0xA42D0, 0xA4310 },
            { 0xEFCC4, 0xA04F0, 0xA0620, 0xA0450, 0xA6400, 0xA6490 },
        }},
    };
};

template<>
struct WorldIsolationTraits<GameVersion::SS_RW_V2_4>
{
    static constexpr WorldIsolationAddresses addresses
    {
        0x107AAC4,
        0x10ADC3C,
        0x95490,
        0x790A0,
        0x790F0,

        std::array<std::array<uintptr_t, 4>, kMaxIsolationUi>
        {{
            { 0xE2360, 0x9DAD0, 0xA5D80, 0xA5A50 },
            { 0xE23B4, 0x9DAD0, 0x9DBC0, 0x9DBE0 },
            { 0xE298C, 0x9DAD0, 0x978F0, 0x9DBE0 },
            { 0xE2B4C, 0x9DAD0, 0x9DBC0, 0x9DBE0 },
            { 0xE2BD8, 0x9DAD0, 0x9EAD0, 0x9DBE0 },
            { 0xE2C58, 0x9DAD0, 0xA2440, 0xA2480 },
            { 0xE2CAC, 0x9DAD0, 0xA2970, 0x9DBE0 },
            { 0xE2FAC, 0x9DAD0, 0x9DBC0, 0xA9060 },
            { 0xE300C, 0x9DAD0, 0xA9C30, 0x9DBE0 },
            { 0xE32D0, 0x9DAD0, 0xAA920, 0xBDAD0 },
        }},
        std::array<std::array<uintptr_t, 6>, kMaxIsolationDecor>
        {{
            { 0xE28CC, 0x9D180, 0x9D2B0, 0x9D0E0, 0x94900, 0x94990 },
            { 0xE28F4, 0x9D180, 0x9D2B0, 0x9D0E0, 0x9D0F0, 0x9D120 },
            { 0xE2AE4, 0x9D180, 0x9D2B0, 0x9D0E0, 0x9D0F0, 0x9D120 },
            { 0xE2C34, 0x9D180, 0x9D2B0, 0x9D0E0, 0xA0D50, 0xA0D90 },
            { 0xE2D44, 0x9D180, 0x9D2B0, 0x9D0E0, 0xA2D40, 0xA2DD0 },
        }},
    };
};

template<>
struct WorldIsolationTraits<GameVersion::SS_GOLD_HD_1_2_INT>
{
    static constexpr WorldIsolationAddresses addresses
    {
        0x34FF04,
        0x382FFC,
        0x6AEA0,
        0x564F0,
        0x56530,

        std::array<std::array<uintptr_t, 4>, kMaxIsolationUi>
        {{
            { 0xA8070, 0x72560, 0x6CE00, 0x72680 },
            { 0xA80C8, 0x72560, 0x72660, 0x72680 },
            { 0xA8288, 0x72560, 0x72660, 0x72680 },
            { 0xA8318, 0x72560, 0x73600, 0x72680 },
            { 0xA8390, 0x72560, 0x755A0, 0x755E0 },
            { 0xA83E8, 0x72560, 0x75B00, 0x72680 },
            { 0xA8670, 0x72560, 0x791E0, 0x78590 },
            { 0xA8740, 0x72560, 0x7C070, 0x72680 },
        }},
        std::array<std::array<uintptr_t, 6>, kMaxIsolationDecor>
        {{
            { 0xA8218, 0x71CD0, 0x71E70, 0x71C50, 0x71C60, 0x71C80 },
            { 0xA836C, 0x71CD0, 0x71E70, 0x71C50, 0x73F00, 0x73F50 },
            { 0xA8484, 0x71CD0, 0x71E70, 0x71C50, 0x75E90, 0x75F20 },
        }},
    };
};

template<>
struct WorldIsolationTraits<GameVersion::HS_2> : WorldIsolationTraits<GameVersion::SS_2> {};

template<>
struct WorldIsolationTraits<GameVersion::SS_RW_V2_3> : WorldIsolationTraits<GameVersion::SS_RW_V2_4> {};

template<>
struct WorldIsolationTraits<GameVersion::SS_EUROPE_2015> : WorldIsolationTraits<GameVersion::SS_RW_V2_4> {};

template<>
struct WorldIsolationTraits<GameVersion::SS_BLACK_SEA> : WorldIsolationTraits<GameVersion::SS_RW_V2_4> {};

template<>
struct WorldIsolationTraits<GameVersion::SS_GOLD_HD_1_2_RU> : WorldIsolationTraits<GameVersion::SS_GOLD_HD_1_2_INT> {};

template<>
struct WorldIsolationTraits<GameVersion::SS_GOLD_EN> : WorldIsolationTraits<GameVersion::SS_GOLD_HD_1_2_INT> {};

inline const WorldIsolationAddresses* TryGetWorldIsolationAddresses(GameVersion version)
{
    switch (version)
    {
    case GameVersion::SS_2:               return &WorldIsolationTraits<GameVersion::SS_2>::addresses;
    case GameVersion::HS_2:               return &WorldIsolationTraits<GameVersion::HS_2>::addresses;
    case GameVersion::SS_RW_V2_4:         return &WorldIsolationTraits<GameVersion::SS_RW_V2_4>::addresses;
    case GameVersion::SS_RW_V2_3:         return &WorldIsolationTraits<GameVersion::SS_RW_V2_3>::addresses;
    case GameVersion::SS_EUROPE_2015:     return &WorldIsolationTraits<GameVersion::SS_EUROPE_2015>::addresses;
    case GameVersion::SS_BLACK_SEA:       return &WorldIsolationTraits<GameVersion::SS_BLACK_SEA>::addresses;
    case GameVersion::SS_GOLD_HD_1_2_INT: return &WorldIsolationTraits<GameVersion::SS_GOLD_HD_1_2_INT>::addresses;
    case GameVersion::SS_GOLD_HD_1_2_RU:  return &WorldIsolationTraits<GameVersion::SS_GOLD_HD_1_2_RU>::addresses;
    case GameVersion::SS_GOLD_EN:         return &WorldIsolationTraits<GameVersion::SS_GOLD_EN>::addresses;
    default:                              return nullptr;
    }
}
