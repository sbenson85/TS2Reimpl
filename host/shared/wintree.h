#pragma once
// The window tree dump (the "wintree" control command, host/control_wintree.cpp; docs/ui-testing.md): a diagnostic
// that reads the game's windows through their cIGZWin slots and writes one line per window, pointer-free, so the dumps
// of two runs at the same frame compare with a plain diff. Shared with the difftest runner (tests/host_wintree.cpp).
//
// Slots used (cIGZWin, every window class's primary vtable; RVAs are cGZWin's, vtable 0xe52f54):
//   11  GetParent            0x1dff70  returns +0x58                            (lane UA, tentative; checked: no args)
//   15  GetChildCount        0x9312f7  no args                                  (lane UA, tentative)
//   37  EnumChildren         0x930cff  (query, callback, data), ret 0xc: with query 0x22ba0121 (all) calls
//                                       callback(parent, child id, child, data) (cdecl) for each child in list order
//   55  GetArea              0x931197  copies left, top, right, bottom (+0xc8) to its argument, ret 4
//   78  GetID                0x149cf0  returns +0x10                            (lane UA, tentative)
//   82  IsFlagSet            0x931733  (mask): (+0xe8 & mask) != 0, ret 4; flag 1 visible, 2 enabled
// Captions: the host's probe reads cGZWin's caption (a cRZString at +0x60, its std::string at +0x64; lane JH) directly,
// without a slot.
#include <cstdint>
#include <string>

namespace wintree {

constexpr int kGetParent = 11, kGetChildCount = 15, kEnumChildren = 37, kGetArea = 55, kGetId = 78, kIsFlagSet = 82;
constexpr uint32_t kEnumAll = 0x22ba0121;
constexpr int kMaxDepth = 40;
constexpr uint32_t kMaxWindows = 20000;
constexpr size_t kMaxCaption = 80;

// What the dump may assume about an object: the host checks the vtable lies in the image and has RTTI, the tests that
// it is one of their fakes. Both only read memory.
struct Probe {
    bool (*isWindow)(const void *object);     // safe to call the slots above on it
    std::string (*className)(const void *object);
    std::string (*caption)(const void *object) = nullptr; // the window's caption, "" if none; null: not dumped
};

// The topmost window above `window` by GetParent (the window itself if it has none); null if `window` isn't one.
void *Root(void *window, const Probe &probe);

// Appends the tree under `root`, depth-first in child order, one line per window:
//   <2 spaces per level><class> id=<hex> area=<l>,<t>,<r>,<b> <vis|hid> <ena|dis> children=<n>[ text="<caption>"]
// (the caption when the probe reads one and it isn't empty: at most 80 bytes, '"', '\' and bytes outside 0x20..0x7e
// written as \xNN). A child that isn't a window is written as "<indent>? (not a window)"; a slot that faults ends that window's line in
// " (fault)" and skips its children. Returns the number of windows written.
uint32_t Dump(void *root, const Probe &probe, std::string &out);

} // namespace wintree
