// The "wintree" control command (docs/ui-testing.md): a diagnostic that reads the game's window tree, changing nothing,
// and appends it to ts2wintree.txt next to the host log, so the trees of a reference and a replacement run at the same
// frame compare with a plain diff. The walk itself is host/shared/wintree.cpp (unit-tested on fakes).
//
// Where the tree starts: the window manager is the object cached at 0x10f51ac by its getter 0x9c5bd6 (the getter
// TS2-Extender calls RZ::WM; db/community-hooks.jsonl, "WinManager"); it is read here without calling the getter, so a
// dump before the manager exists creates nothing. Its class is cGZWinMgrW32 (ours since lane KQ, src/gz/cGZWinMgrW32.h:
// the object carries our vtables, not the game's), whose slot 37 (GetFocus; the game's 0x1694c0) returns the
// focused window (+0x38, TS2-Extender's GZGetFocus). The tree dumped is the one above the focused window: Root climbs
// GetParent to the top. (Slot 3 is the manager's own top window, GetMainWindow.)
#include "control.h"

#include "shared/wintree.h"

#include "game/game.h"
#include "hook/log.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr uint32_t kManagerGlobal = 0x10f51ac;
constexpr int kManagerGetFocus = 37;
constexpr uint32_t kImageSize = 0x120f000;    // SizeOfImage

uint32_t Rva(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(GameAddress<void>(0))); }

// The decorated RTTI name of the object's class (".?AVcGZWinBtn@@"; ".?AU" for a struct), or null: the vtable must lie in the image and the
// complete object locator before it must point at a type descriptor in the image. Only reads, guarded.
const char *RttiName(const void *object)
{
    __try {
        const auto *vtable = *static_cast<const uint32_t *const *>(object);
        if (Rva(vtable) >= kImageSize || Rva(vtable) < 0x1000) return nullptr;
        const auto *locator = reinterpret_cast<const uint32_t *>(vtable[-1]);
        if (Rva(locator) >= kImageSize || locator[0] != 0) return nullptr; // signature 0 (x86)
        const auto *type = reinterpret_cast<const char *>(locator[3]);
        if (Rva(type) >= kImageSize) return nullptr;
        const char *name = type + 8;
        if (std::strncmp(name, ".?AV", 4) != 0 && std::strncmp(name, ".?AU", 4) != 0) return nullptr; // class, struct
        return name;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// A window: its class (by its RTTI class hierarchy) derives from cIGZWin, and the vtable seen is that cIGZWin face's,
// where the cIGZWin slots are: the locator's offset (where this vtable sits in the complete object) equals the cIGZWin
// base's member displacement (the base class descriptor's mdisp, word 2; pdisp, word 3, is -1 for a non-virtual base).
// Most window classes have cIGZWin first (offset 0); cGZWinBtn and its subclasses have cIGZWinBtn first and cIGZWin at
// +4, and window children lists hold that +4 face (before this check they showed as "? (not a window)").
bool IsWindow(const void *object)
{
    if (!object || !RttiName(object)) return false;
    __try {
        const auto *vtable = *static_cast<const uint32_t *const *>(object);
        const auto *locator = reinterpret_cast<const uint32_t *>(vtable[-1]);
        const auto *hierarchy = reinterpret_cast<const uint32_t *>(locator[4]);
        if (Rva(hierarchy) >= kImageSize) return false;
        const uint32_t count = hierarchy[2];
        const auto *bases = reinterpret_cast<const uint32_t *>(hierarchy[3]);
        if (count > 64 || Rva(bases) >= kImageSize) return false;
        for (uint32_t i = 0; i < count; ++i) {
            const auto *base = reinterpret_cast<const uint32_t *>(bases[i]);
            if (Rva(base) >= kImageSize || Rva(reinterpret_cast<const void *>(base[0])) >= kImageSize) continue;
            if (std::strcmp(reinterpret_cast<const char *>(base[0]) + 8, ".?AVcIGZWin@@") != 0) continue;
            if (base[2] == locator[1] && static_cast<int32_t>(base[3]) == -1) return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string ClassName(const void *object)
{
    const char *name = RttiName(object);
    if (!name) return "?";
    std::string s = name + 4;
    const size_t end = s.find("@@");
    return end == std::string::npos ? s : s.substr(0, end);
}

// The caption: cGZWin's cRZString at +0x60 (src/ui/cGZWinText.h), whose std::string (MSVC x86: 16-byte buffer or
// pointer at +0x64, size +0x74, capacity +0x78) holds the text. The cIGZWin face is the cGZWin object (cGZWinBtn's
// face at +4 is its cGZWin base), so the offsets hold for every window the probe accepts. Only reads, guarded, and
// only a plausible string (size <= capacity, both small) is returned.
uint32_t ReadCaption(const void *object, char *out, uint32_t room)
{
    __try {
        const auto *p = static_cast<const uint8_t *>(object);
        const uint32_t size = *reinterpret_cast<const uint32_t *>(p + 0x74);
        const uint32_t capacity = *reinterpret_cast<const uint32_t *>(p + 0x78);
        if (size == 0 || size > capacity || capacity < 15 || capacity > 0x10000) return 0;
        const char *text = capacity >= 16 ? *reinterpret_cast<const char *const *>(p + 0x64)
                                          : reinterpret_cast<const char *>(p + 0x64);
        const uint32_t n = size < room ? size : room;
        std::memcpy(out, text, n);
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::string Caption(const void *object)
{
    char text[128];
    return std::string(text, ReadCaption(object, text, sizeof(text)));
}

void *ReadFocus(void *manager)
{
    __try {
        using Fn = void *(__thiscall *)(void *);
        return reinterpret_cast<Fn>((*static_cast<void ***>(manager))[kManagerGetFocus])(manager);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

} // namespace

void WinTree(const std::wstring &directory, const char *label)
{
    void *manager = *GameAddress<void *>(kManagerGlobal);
    if (!manager) {
        Log("control: wintree: no window manager yet (frame %u)", ControlFrame());
        return;
    }
    void *focus = ReadFocus(manager);
    const wintree::Probe probe{&IsWindow, &ClassName, &Caption};
    void *root = wintree::Root(focus, probe);
    if (!root) {
        Log("control: wintree: no focused window (frame %u)", ControlFrame());
        return;
    }
    std::string text;
    const uint32_t count = wintree::Dump(root, probe, text);
    char head[128];
    std::snprintf(head, sizeof(head), "wintree %u %s windows=%u\n", ControlFrame(), *label ? label : "-", count);
    const std::wstring path = directory + L"ts2wintree.txt";
    FILE *f = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
    if (!f) {
        Log("control: wintree: cannot open ts2wintree.txt");
        return;
    }
    std::fputs(head, f);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    Log("control: wintree \"%s\": %u windows under %s (frame %u, ts2wintree.txt)", label, count, ClassName(root).c_str(),
        ControlFrame());
}
