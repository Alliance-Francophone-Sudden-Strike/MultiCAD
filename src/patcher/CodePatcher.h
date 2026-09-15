#pragma once

#include <vector>
#include <cstring>

class CodePatcher final
{
public:
    bool apply(const ModuleInfo& mod, const std::span<const HookSpec>& hooks, const std::span<const PatchSpec>& patches)
    {
        return applyCodeHooks(mod, hooks) && applyMemoryPatches(mod, patches);
    }

    bool skippedUnverified() const { return skippedUnverified_; }

private:
    static constexpr size_t jumpSize{ 5 };
    bool skippedUnverified_{ false };

    static bool siteMatches(const uint8_t* target, const HookSpec& h, uintptr_t base)
    {
        if (h.expectedCallee != 0)
        {
            if (target[0] != h.opcode)
                return false;
            int32_t rel{};
            std::memcpy(&rel, target + 1, sizeof(rel));
            if (reinterpret_cast<uintptr_t>(target) + jumpSize + rel != base + h.expectedCallee)
                return false;
        }
        if (h.expectedOperandRva != 0)
        {
            uint32_t operand{};
            std::memcpy(&operand, target + 2, sizeof(operand));
            if (operand != base + h.expectedOperandRva)
                return false;
        }
        return true;
    }
    bool applyCodeHooks(const ModuleInfo& mod, const std::span<const HookSpec>& hooks)
    {
        for (const auto& h : hooks)
        {
            if (h.targetRva == 0 || h.detour == 0 || !h.verified())
                continue;
            if (!siteMatches(reinterpret_cast<const uint8_t*>(mod.base + h.targetRva), h, mod.base))
            {
                skippedUnverified_ = true;
                break;
            }
        }

        for (const auto& h : hooks)
        {
            if (h.targetRva == 0 || h.detour == 0)
                continue;

            if (h.overwriteSize < jumpSize)
                return false;

            uint8_t* target = reinterpret_cast<uint8_t*>(mod.base + h.targetRva);
            uint8_t* detour = reinterpret_cast<uint8_t*>(h.detour);

            if (skippedUnverified_ && h.verified())
                continue;

            DWORD oldProtect{};
            if (!VirtualProtect(target, h.overwriteSize, PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;

            intptr_t relAddr = reinterpret_cast<intptr_t>(detour) -
                reinterpret_cast<intptr_t>(target) - jumpSize;

            target[0] = h.opcode;
            *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(relAddr);
            std::fill(target + jumpSize, target + h.overwriteSize, 0x90);

            VirtualProtect(target, h.overwriteSize, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), target, h.overwriteSize);
        }
        return true;
    }

    bool applyMemoryPatches(const ModuleInfo& mod, const std::span<const PatchSpec>& patches)
    {
        for (const auto& p : patches)
        {
            if (p.targetRva == 0)
                continue;

            if (p.srcRva != 0)
            {
                if (p.size == 0)
                    continue;

                uint8_t* target = reinterpret_cast<uint8_t*>(mod.base + p.targetRva);
                uint8_t* source = reinterpret_cast<uint8_t*>(mod.base + p.srcRva);

                DWORD oldProtect{};
                if (!VirtualProtect(target, p.size, PAGE_EXECUTE_READWRITE, &oldProtect))
                    return false;

                std::memmove(target, source, p.size);

                VirtualProtect(target, p.size, oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), target, p.size);
            }
            else
            {
                if (p.data.empty())
                    continue;

                uint8_t* target = reinterpret_cast<uint8_t*>(mod.base + p.targetRva);

                DWORD oldProtect{};
                if (!VirtualProtect(target, p.data.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
                    return false;

                std::copy(p.data.begin(), p.data.end(), target);

                VirtualProtect(target, p.data.size(), oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), target, p.data.size());
            }
        }
        return true;
    }
};
