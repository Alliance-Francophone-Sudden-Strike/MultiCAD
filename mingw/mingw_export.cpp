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

extern int SetUiScaleExport(float scale, int logicalWidth, int logicalHeight);

extern "C" __declspec(dllexport) int SetUiScale(float scale, int logicalWidth, int logicalHeight)
{
    return SetUiScaleExport(scale, logicalWidth, logicalHeight);
}

extern int GetPresentedSizeExport(int* width, int* height);
extern int GetViewSizeExport(int* width, int* height);
extern int GetViewOriginExport(int* x, int* y);
struct ZoomIndicatorInfo;  // cad.cpp
extern int GetZoomIndicatorExport(ZoomIndicatorInfo* info);

extern "C" __declspec(dllexport) int GetViewOrigin(int* x, int* y)
{
    return GetViewOriginExport(x, y);
}

extern "C" __declspec(dllexport) int GetViewSize(int* width, int* height)
{
    return GetViewSizeExport(width, height);
}

extern "C" __declspec(dllexport) int GetPresentedSize(int* width, int* height)
{
    return GetPresentedSizeExport(width, height);
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

extern "C" __declspec(dllexport) int GetZoomIndicator(ZoomIndicatorInfo* info)
{
    return GetZoomIndicatorExport(info);
}
