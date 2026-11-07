// cRZExceptionNotification in the standalone shape (src/rz/cRZExceptionNotification.h). Built for size (/O1), as the
// game's RZ module.
#include "rz/cRZExceptionNotification.h"

#include "game/game.h"

#include <windows.h>

#include <cstdio>
#include <new>

#pragma warning(disable : 4996) // _snprintf, as the game calls it

namespace {

// MessageBoxA through the game's import address table (a test substitutes it).
constexpr uint32_t kIatMessageBoxA = 0xd9a568;

} // namespace

// 0x1c8d3f: its own interface id or cIGZUnknown's (1) give this, with a reference (AddRef through the vtable).
bool cRZExceptionNotification::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == kIid) {
        *ppvObj = this;
        AddRef();
        return true;
    }
    if (riid == 1) {
        *ppvObj = this;
        AddRef();
        return true;
    }
    return false;
}

// 0x148fb0 (folded with about 600 classes): the new count, not interlocked.
uint32_t cRZExceptionNotification::AddRef()
{
    return ++mRefCount;
}

// 0x1c8d77: a count of exactly 1 deletes the object (the count is left as it was) and gives 0; any other is
// decremented (0 wraps).
uint32_t cRZExceptionNotification::Release()
{
    if (mRefCount == 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// 0x1c8cb7: "Module: <module or 'not specified'>\nLine: <line>" (cut to 199 characters) in a Yes/No box; true when the
// answer is Yes. The original's catch(...) is built /EHa, so it also catches SEH exceptions (a bad module pointer in
// the formatting): then the answer is true.
bool cRZExceptionNotification::Notify(const char *module, int32_t line)
{
    __try {
        char text[200];
        _snprintf(text, sizeof(text), "Module: %s\nLine: %d", module ? module : "not specified", line);
        text[sizeof(text) - 1] = 0;
        auto messageBox = *GameAddress<int(WINAPI *)(HWND, const char *, const char *, UINT)>(kIatMessageBoxA);
        return messageBox(nullptr, text, "Exception about to be thrown, accept?", MB_YESNO | MB_ICONEXCLAMATION) == IDYES;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

// 0x1c8c91: the body only stores the vtable.
cRZExceptionNotification::~cRZExceptionNotification() {}

cRZExceptionNotification *__fastcall cRZExceptionNotificationAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZExceptionNotification;
}
