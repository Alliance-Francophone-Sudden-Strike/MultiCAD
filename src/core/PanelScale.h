#pragma once

namespace PanelScale
{
    constexpr int kStep = 4;
    constexpr int kMinQuarters = 4;
    constexpr int kMaxQuarters = 12;

    constexpr int Quarters(float factor)
    {
        if (!(factor > 1.f))
            return kMinQuarters;
        if (factor >= 3.f)
            return kMaxQuarters;
        return static_cast<int>(factor * kStep + 0.5f);
    }

    constexpr int Size(int value, int quarters)
    {
        return (value * quarters + kStep / 2) / kStep;
    }

    constexpr int Repeat(int value, int quarters)
    {
        const int scaled = value * quarters / kStep;
        return scaled < 1 ? 1 : scaled;
    }
}
