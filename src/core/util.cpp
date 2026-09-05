#include "pch.h"
#include "util.h"
#include <thread>

void ShowErrorNow(const std::string& message, bool isCritical)
{
    MessageBoxA(NULL, message.c_str(), "MultiCAD error", MB_OK | MB_ICONERROR);
    if (isCritical)
        ExitProcess(EXIT_FAILURE);
}

void ShowErrorAsync(std::string message, bool isCritical)
{
    // The thread outlives the caller, so it has to own the text.
    std::thread([message = std::move(message), isCritical]() {
        ShowErrorNow(message, isCritical);
        }).detach();
}
