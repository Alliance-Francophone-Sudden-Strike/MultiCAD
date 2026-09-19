#include "WorldIsolationTraits.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace
{
    constexpr GameVersion kBound[] = {
        GameVersion::SS_2,
        GameVersion::HS_2,
        GameVersion::SS_RW_V2_3,
        GameVersion::SS_RW_V2_4,
        GameVersion::SS_EUROPE_2015,
        GameVersion::SS_BLACK_SEA,
        GameVersion::SS_GOLD_HD_1_2_INT,
        GameVersion::SS_GOLD_HD_1_2_RU,
        GameVersion::SS_GOLD_EN,
    };

    constexpr GameVersion kUnbound[] = {
        GameVersion::UNKNOWN,
        GameVersion::SS_V1_0,
        GameVersion::SS_V1_2,
        GameVersion::SS_GOLD_DE,
        GameVersion::SS_GOLD_FR,
        GameVersion::SS_GOLD_RU,
        GameVersion::SS_HD_V1_1_RU,
        GameVersion::SS_BLACK_GOLD,
    };

    template<size_t N, size_t M>
    void CheckFamily(const std::array<std::array<uintptr_t, N>, M>& family, size_t sharedSlots)
    {
        std::vector<uintptr_t> vtables;
        bool padding = false;
        for (const auto& entry : family)
        {
            if (!entry[0])
            {
                padding = true;
                continue;
            }
            assert(!padding);
            for (size_t i = 1; i < N; ++i)
                assert(entry[i]);
            for (size_t i = 1; i <= sharedSlots; ++i)
                assert(entry[i] == family[0][i]);
            vtables.push_back(entry[0]);
        }
        assert(vtables.size() >= 3);
        std::sort(vtables.begin(), vtables.end());
        assert(std::adjacent_find(vtables.begin(), vtables.end()) == vtables.end());
    }
}

int main()
{
    for (const GameVersion version : kUnbound)
        assert(TryGetWorldIsolationAddresses(version) == nullptr);

    for (const GameVersion version : kBound)
    {
        const WorldIsolationAddresses* addresses = TryGetWorldIsolationAddresses(version);
        assert(addresses);

        assert(addresses->decorHead);
        assert(addresses->cursorDraw);
        assert(addresses->fnBlendMainWithWarFog);
        assert(addresses->fnGetFirstDecorUi);
        assert(addresses->fnGetNextDecorUi);
        assert(addresses->fnGetFirstDecorUi != addresses->fnGetNextDecorUi);
        assert(addresses->decorHead != addresses->cursorDraw);

        CheckFamily(addresses->ui, 1);
        CheckFamily(addresses->decor, 3);

        for (const auto& ui : addresses->ui)
            for (const auto& decor : addresses->decor)
                assert(!ui[0] || !decor[0] || ui[0] != decor[0]);
    }

    assert(TryGetWorldIsolationAddresses(GameVersion::HS_2)
        == TryGetWorldIsolationAddresses(GameVersion::SS_2));
    assert(TryGetWorldIsolationAddresses(GameVersion::SS_EUROPE_2015)
        == TryGetWorldIsolationAddresses(GameVersion::SS_RW_V2_4));
    assert(TryGetWorldIsolationAddresses(GameVersion::SS_GOLD_EN)
        == TryGetWorldIsolationAddresses(GameVersion::SS_GOLD_HD_1_2_INT));

    return 0;
}
