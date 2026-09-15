/*
 * The Visual Studio build exports CADraw_Init via
 *     #pragma comment(linker, "/EXPORT:CADraw_Init=?InitializeModule@@YAPAXXZ")
 * in core/cad.cpp, which GCC ignores. Re-export it here instead. cadMulti.def
 * pins the exported name to exactly "CADraw_Init" (no leading _, no @n).
 */
extern void* InitializeModule();

extern "C" __declspec(dllexport) void* CADraw_Init(void)
{
    return InitializeModule();
}

/*
 * libstdc++'s default terminate handler demangles the exception type name to
 * print it, pulling ~48KB of cp-demangle.o into the static build. A DLL loaded
 * by the game has nowhere to print it. Override with the abort the handler
 * would have reached anyway.
 */
#include <cstdlib>

namespace __gnu_cxx {
void __verbose_terminate_handler()
{
    std::abort();
}
}
