// A crash in an image-free program prints where it happened, as an offset into the exe that the linker map
// (gmdc_decode.map, /MAP) names, then the process exits as it would have. No debugger is needed to read it.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

namespace {

LONG WINAPI Report(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD &r = *info->ExceptionRecord;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto at = reinterpret_cast<uintptr_t>(r.ExceptionAddress);
    std::fflush(stdout);
    std::fprintf(stderr, "crash: exception 0x%08lx at 0x%08x (exe offset 0x%08x, preferred 0x%08x)",
                 static_cast<unsigned long>(r.ExceptionCode), unsigned(at), unsigned(at - base),
                 unsigned(at - base + 0x400000));
    if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2)
        std::fprintf(stderr, ", %s 0x%08x", r.ExceptionInformation[0] ? "writing" : "reading",
                     unsigned(r.ExceptionInformation[1]));
    const CONTEXT &c = *info->ContextRecord;
    std::fprintf(stderr, "\n  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", c.Eax,
                 c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp);
    // The words at the top of the stack: return addresses among them name the callers.
    const auto *stack = reinterpret_cast<const uintptr_t *>(c.Esp);
    std::fprintf(stderr, "  stack (exe offsets where in the image):");
    for (int i = 0; i < 48; ++i) {
        const uintptr_t w = stack[i];
        if (w >= base && w < base + 0x01000000) std::fprintf(stderr, " [%d]0x%08x", i, unsigned(w - base + 0x400000));
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

struct Install {
    Install() { SetUnhandledExceptionFilter(&Report); }
} gInstall;

} // namespace
