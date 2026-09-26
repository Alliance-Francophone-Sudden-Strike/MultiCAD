#include "pch.h"
#include "GameIni.h"

namespace
{
    std::string ToLowerAscii(std::string s)
    {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        return s;
    }

    // True when [Game] has an SSDraw* entry referencing dllName, i.e. this ini loaded us.
    bool IniLoadsDll(const std::string& iniPath, const std::string& dllName)
    {
        char section[8192] = { 0 };
        if (GetPrivateProfileSectionA("Game", section, sizeof(section), iniPath.c_str()) == 0)
            return false;

        const std::string needle = ToLowerAscii(dllName);

        // section is a run of "key=value\0" entries ending with an extra '\0'.
        for (const char* entry = section; *entry; entry += std::strlen(entry) + 1)
        {
            if (_strnicmp(entry, "SSDraw", 6) != 0)
                continue;

            if (ToLowerAscii(entry).find(needle) != std::string::npos)
                return true;
        }

        return false;
    }

    // Directory of the given module (nullptr = the game exe), without trailing slash.
    std::string ModuleDir(HMODULE mod)
    {
        char path[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(mod, path, MAX_PATH) == 0)
            return {};

        char* lastSlash = strrchr(path, '\\');
        if (!lastSlash)
            return {};

        *lastSlash = '\0';
        return path;
    }

    std::string WorkingDir()
    {
        char path[MAX_PATH] = { 0 };
        DWORD n = GetCurrentDirectoryA(MAX_PATH, path);
        if (n == 0 || n >= MAX_PATH)
            return {};

        return path; // no trailing slash
    }
}

namespace GameIni
{
    std::string Path()
    {
        // Resolve our own module from an in-module address (name-agnostic).
        HMODULE self = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&WorkingDir),
            &self))
            return {};

        char selfPath[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(self, selfPath, MAX_PATH) == 0)
            return {};

        const char* slash = strrchr(selfPath, '\\');
        const std::string dllName = slash ? std::string(slash + 1) : std::string(selfPath);

        // The ini lives next to the exe, which may not be the dll's folder.
        const std::string searchDirs[] = { ModuleDir(nullptr), ModuleDir(self), WorkingDir() };

        // Ini name differs per game version; pick the one that references us.
        static constexpr const char* kCandidateInis[] = {
            "sudtest.ini",   // base Sudden Strike
            "sudfmrm.ini",   // mods that ship their own ini
            "aprmnew.ini",   // Hidden Stroke 2 / APRM
            "gulfwar.ini",   // addons
            "blackgold.ini",
            "blacksea.ini",
            "euro2015.ini",
        };

        std::string firstExisting;

        for (const std::string& dir : searchDirs)
        {
            if (dir.empty())
                continue;

            for (const char* name : kCandidateInis)
            {
                std::string path = dir + "\\" + name;

                if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
                    continue;

                if (firstExisting.empty())
                    firstExisting = path;

                if (!dllName.empty() && IniLoadsDll(path, dllName))
                    return path;
            }
        }

        // No SSDraw match: fall back to the first existing ini.
        return firstExisting;
    }

    std::string Dir()
    {
        const std::string path = Path();
        const size_t slash = path.find_last_of('\\');
        if (slash == std::string::npos)
            return {};

        return path.substr(0, slash);
    }

    std::string Read(const char* section, const char* key)
    {
        const std::string path = Path();
        if (path.empty())
            return {};

        char buffer[512] = { 0 };
        const DWORD length = GetPrivateProfileStringA(
            section, key, "", buffer, sizeof(buffer), path.c_str());

        return std::string(buffer, length);
    }

    bool Write(const char* section, const char* key, const std::string& value)
    {
        const std::string path = Path();
        if (path.empty())
            return false;

        return WritePrivateProfileStringA(section, key, value.c_str(), path.c_str()) != FALSE;
    }
}
