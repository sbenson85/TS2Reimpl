#include "wintree.h"

#include <windows.h>

#include <cstdio>
#include <vector>

namespace wintree {
namespace {

using GetParentFn = void *(__thiscall *)(void *);
using GetIntFn = uint32_t(__thiscall *)(void *);
using EnumCallback = bool(__cdecl *)(void *parent, uint32_t id, void *child, void *data);
using EnumChildrenFn = bool(__thiscall *)(void *, uint32_t, EnumCallback, void *);
using GetAreaFn = void(__thiscall *)(void *, int32_t *);
using IsFlagSetFn = bool(__thiscall *)(void *, uint32_t);

template <typename Fn> Fn Slot(void *object, int slot) { return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot]); }

struct Info {
    uint32_t id = 0, children = 0;
    int32_t area[4] = {};
    bool visible = false, enabled = false;
};

// The slot calls, each guarded: a fault (a slot the facts got wrong, a window freed meanwhile) fails the probe.
bool ReadInfo(void *window, Info *info)
{
    __try {
        info->id = Slot<GetIntFn>(window, kGetId)(window);
        info->children = Slot<GetIntFn>(window, kGetChildCount)(window);
        Slot<GetAreaFn>(window, kGetArea)(window, info->area);
        info->visible = Slot<IsFlagSetFn>(window, kIsFlagSet)(window, 1);
        info->enabled = Slot<IsFlagSetFn>(window, kIsFlagSet)(window, 2);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool __cdecl Collect(void *, uint32_t, void *child, void *data)
{
    static_cast<std::vector<void *> *>(data)->push_back(child);
    return true;
}

bool ReadChildren(void *window, std::vector<void *> *children)
{
    __try {
        Slot<EnumChildrenFn>(window, kEnumChildren)(window, kEnumAll, &Collect, children);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadParent(void *window, void **parent)
{
    __try {
        *parent = Slot<GetParentFn>(window, kGetParent)(window);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Walk(void *window, int depth, const Probe &probe, std::string &out, uint32_t &count)
{
    out.append(static_cast<size_t>(depth) * 2, ' ');
    if (!window || !probe.isWindow(window)) {
        out += "? (not a window)\n";
        return;
    }
    if (count >= kMaxWindows) {
        out += "... (more than the dump's limit)\n";
        return;
    }
    ++count;
    out += probe.className(window);
    Info info;
    if (!ReadInfo(window, &info)) {
        out += " (fault)\n";
        return;
    }
    char text[160];
    std::snprintf(text, sizeof(text), " id=%08x area=%d,%d,%d,%d %s %s children=%u", info.id, info.area[0], info.area[1],
                  info.area[2], info.area[3], info.visible ? "vis" : "hid", info.enabled ? "ena" : "dis", info.children);
    out += text;
    if (probe.caption) {
        const std::string caption = probe.caption(window);
        if (!caption.empty()) {
            out += " text=\"";
            for (size_t i = 0; i < caption.size() && i < kMaxCaption; ++i) {
                const unsigned char c = static_cast<unsigned char>(caption[i]);
                if (c < 0x20 || c > 0x7e || c == '"' || c == '\\') {
                    std::snprintf(text, sizeof(text), "\\x%02x", c);
                    out += text;
                } else {
                    out += static_cast<char>(c);
                }
            }
            out += '"';
        }
    }
    std::vector<void *> children;
    if (info.children == 0) {
        out += '\n';
        return;
    }
    if (!ReadChildren(window, &children)) {
        out += " (fault listing children)\n";
        return;
    }
    if (children.size() != info.children) {
        std::snprintf(text, sizeof(text), " (listed %u)", static_cast<unsigned>(children.size()));
        out += text;
    }
    out += '\n';
    if (depth + 1 >= kMaxDepth) {
        out.append(static_cast<size_t>(depth + 1) * 2, ' ');
        out += "... (deeper than the dump's limit)\n";
        return;
    }
    for (void *child : children) Walk(child, depth + 1, probe, out, count);
}

} // namespace

void *Root(void *window, const Probe &probe)
{
    if (!window || !probe.isWindow(window)) return nullptr;
    for (int i = 0; i < kMaxDepth; ++i) {
        void *parent = nullptr;
        if (!ReadParent(window, &parent) || !parent || parent == window || !probe.isWindow(parent)) return window;
        window = parent;
    }
    return window;
}

uint32_t Dump(void *root, const Probe &probe, std::string &out)
{
    uint32_t count = 0;
    Walk(root, 0, probe, out, count);
    return count;
}

} // namespace wintree
