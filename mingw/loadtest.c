/* Tiny host used by `make test`: LoadLibrary the given DLL and resolve the
 * CADraw_Init export. Not a functional test of the mod, just a load check. */
#include <windows.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: loadtest <dll>\n"); return 2; }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary FAILED: %lu\n", (unsigned long)GetLastError()); return 1; }
    printf("LoadLibrary OK: %p\n", (void*)h);

    FARPROC p = GetProcAddress(h, "CADraw_Init");
    printf("GetProcAddress(CADraw_Init) = %p\n", (void*)p);
    fflush(stdout);
    return p ? 0 : 3;
}
