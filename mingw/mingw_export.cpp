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
