#include "game/espcheck.h"

#ifdef TS2_ESPCHECK
#include <windows.h>

#include <rtcapi.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace {

void (*gSink)(const char *) = nullptr;
uintptr_t gImageBase = 0, gImageEnd = 0;
volatile LONG gReports = 0;

int __cdecl Report(int errorType, const wchar_t *file, int line, const wchar_t *module, const wchar_t *format, ...)
{
    (void)file;
    (void)line;
    (void)module;
    if (InterlockedIncrement(&gReports) > 200) return 0;
    wchar_t message[256] = L"";
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    // The stack: frames in our exe as "ours+rva", in the game image as "image+rva".
    void *frames[24];
    const USHORT n = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    const auto self = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    char text[1024];
    int len = std::snprintf(text, sizeof(text), "espcheck: RTC error %d (%ls); stack:", errorType, message);
    for (USHORT i = 0; i < n && len > 0 && len < static_cast<int>(sizeof(text)) - 32; ++i) {
        const auto a = reinterpret_cast<uintptr_t>(frames[i]);
        if (a >= gImageBase && a < gImageEnd)
            len += std::snprintf(text + len, sizeof(text) - len, " image+0x%x", static_cast<unsigned>(a - gImageBase));
        else
            len += std::snprintf(text + len, sizeof(text) - len, " ours+0x%x", static_cast<unsigned>(a - self));
    }
    if (gSink) gSink(text);
    return 0; // continue: the /Od code restores esp from its frame pointer
}

} // namespace

void InstallEspCheckReporter(void (*sink)(const char *line), uintptr_t imageBase, uintptr_t imageEnd)
{
    gSink = sink;
    gImageBase = imageBase;
    gImageEnd = imageEnd;
    _RTC_SetErrorFuncW(&Report);
}

#else

void InstallEspCheckReporter(void (*)(const char *), uintptr_t, uintptr_t) {}

#endif
