#include "ui/GZWinHelpers.h"

#include "game/vtable.h"
#include "ifaces/cIGZWin.h"
#include "misc/UtilitiesD.h"

namespace {
constexpr uint32_t kFlagQuiet = 0x20; // held clear while the visibility changes, then restored
}

void __cdecl GZWinHideQuiet(void *window)
{
    if (!window) return;
    auto *win = As<ifaces::cIGZWin>(window);
    const bool quiet = win->IsFlagSet(kFlagQuiet);
    win->SetFlag(kFlagQuiet, 0);
    win->Hide();
    win->SetFlag(kFlagQuiet, quiet);
}

void __cdecl GZWinShowQuiet(void *window)
{
    if (!window) return;
    auto *win = As<ifaces::cIGZWin>(window);
    const bool quiet = win->IsFlagSet(kFlagQuiet);
    win->SetFlag(kFlagQuiet, 0);
    win->SetVisible(1);
    win->SetFlag(kFlagQuiet, quiet);
}

void __cdecl GZWinEnableOnlyFromList(void *window, const uint32_t *ids, uint32_t count, uint32_t selected)
{
    for (uint32_t i = 0; i < count; ++i) FindChildAndSetEnabled(window, ids[i], ids[i] == selected);
}

void __cdecl GZWinEnableOnlyInRange(void *window, uint32_t first, uint32_t count, uint32_t selected)
{
    const uint32_t end = first + count;
    for (uint32_t id = first; id < end; ++id) FindChildAndSetEnabled(window, id, id == selected);
}
