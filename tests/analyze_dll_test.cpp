// Differential check: AnalyzeDll over real PE files must hash identically
// whether it reads via std::ifstream (old) or Win32 ReadFile (new).
#include "pch.h"
#include "DllVersionDetector.h"
#include <cstdio>

int main(int argc, char** argv)
{
    int analyzed = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring path;
        for (const char* p = argv[i]; *p; ++p) path.push_back((wchar_t)(unsigned char)*p);

        std::array<uint8_t, 32> hash{};
        ModuleInfo info{};
        const bool ok = DllVersionDetector::GetInstance().AnalyzeDll(path, hash, info);

        printf("%-22s ok=%d imageSize=%8lu relocSize=%8lu hash=", argv[i], (int)ok,
               (unsigned long)info.imageSize, (unsigned long)info.relocSize);
        for (auto b : hash) printf("%02x", b);
        printf("\n");
        if (ok) ++analyzed;
    }
    // A run where nothing parsed would "pass" a diff trivially.
    if (analyzed == 0) { printf("FAIL: no PE analyzed\n"); return 1; }
    return 0;
}
