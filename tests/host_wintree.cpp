// The window tree dump (host/shared/wintree.cpp; the "wintree" control command, host/control_wintree.cpp) on fake
// windows: slot numbers and calling conventions as the dump assumes them (thiscall, callee-cleaned; EnumChildren calls
// a cdecl callback), so a wrong slot or argument count reads the wrong fake function or unbalances the stack. Not a
// differential test (no original function): rva 0.
#include "difftest.h"

#include "../host/shared/wintree.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct FakeWin;
void *gVTable[96];
void *gOtherVTable[96];

struct FakeWin {
    void **vtable = gVTable;
    const char *name = "cGZWin";
    uint32_t id = 0;
    int32_t area[4] = {};
    uint32_t flags = 0;
    FakeWin *parent = nullptr;
    std::vector<FakeWin *> children;
    int countOverride = -1;
    bool faultArea = false;
    bool faultEnum = false;
};

using Callback = bool(__cdecl *)(void *parent, uint32_t id, void *child, void *data);

// __fastcall with an unused edx is thiscall's register use and stack cleanup.
void *__fastcall GetParent(FakeWin *self, void *) { return self->parent; }
uint32_t __fastcall GetChildCount(FakeWin *self, void *)
{
    return self->countOverride >= 0 ? static_cast<uint32_t>(self->countOverride) : static_cast<uint32_t>(self->children.size());
}
bool __fastcall EnumChildren(FakeWin *self, void *, uint32_t query, Callback callback, void *data)
{
    if (self->faultEnum) *static_cast<volatile int *>(nullptr) = 0;
    if (query != 0x22ba0121) return false; // "all children"; another id lists those with that interface
    for (FakeWin *child : self->children) callback(self, child->id, child, data);
    return true;
}
void __fastcall GetArea(FakeWin *self, void *, int32_t *out)
{
    if (self->faultArea) *static_cast<volatile int *>(nullptr) = 0;
    std::memcpy(out, self->area, sizeof(self->area));
}
uint32_t __fastcall GetId(FakeWin *self, void *) { return self->id; }
bool __fastcall IsFlagSet(FakeWin *self, void *, uint32_t mask) { return (self->flags & mask) != 0; }
void __fastcall Wrong(FakeWin *, void *) {}

void FillVTables()
{
    for (void *&slot : gVTable) slot = reinterpret_cast<void *>(&Wrong);
    // The slot numbers as facts (cGZWin's vtable 0xe52f54), not the header's constants, so a changed constant fails.
    gVTable[11] = reinterpret_cast<void *>(&GetParent);
    gVTable[15] = reinterpret_cast<void *>(&GetChildCount);
    gVTable[37] = reinterpret_cast<void *>(&EnumChildren);
    gVTable[55] = reinterpret_cast<void *>(&GetArea);
    gVTable[78] = reinterpret_cast<void *>(&GetId);
    gVTable[82] = reinterpret_cast<void *>(&IsFlagSet);
    for (void *&slot : gOtherVTable) slot = reinterpret_cast<void *>(&Wrong);
}

bool IsFake(const void *object) { return *static_cast<void *const *>(object) == gVTable; }
std::string NameOf(const void *object) { return static_cast<const FakeWin *>(object)->name; }
const wintree::Probe kProbe{&IsFake, &NameOf};

void Add(FakeWin &parent, FakeWin &child)
{
    parent.children.push_back(&child);
    child.parent = &parent;
}

} // namespace

DIFF_TEST(HostWinTree_Dump, 0)
{
    FillVTables();
    FakeWin top, panel, button, label, deep;
    top.name = "cGZWinMgrRoot";
    top.id = 0x1;
    top.area[2] = 1024, top.area[3] = 768;
    top.flags = 3;
    panel.name = "cGZWinGen";
    panel.id = 0xa1b2c3d4;
    panel.area[0] = 10, panel.area[1] = -20, panel.area[2] = 310, panel.area[3] = 180;
    panel.flags = 1;
    button.name = "cGZWinBtn";
    button.id = 0x42;
    button.area[2] = 64, button.area[3] = 32;
    button.flags = 2;
    label.name = "cGZWinText";
    label.id = 0x43;
    deep.name = "cGZWinFlatRect";
    deep.id = 7;
    Add(top, panel);
    Add(panel, button);
    Add(panel, label);
    Add(label, deep);

    if (wintree::Root(&deep, kProbe) != &top) ctx.Fail("Root from a leaf");
    if (wintree::Root(&top, kProbe) != &top) ctx.Fail("Root of the root");
    if (wintree::Root(nullptr, kProbe) != nullptr) ctx.Fail("Root of null");
    ++ctx.cases;

    std::string out;
    const uint32_t n = wintree::Dump(&top, kProbe, out);
    const char *expected = "cGZWinMgrRoot id=00000001 area=0,0,1024,768 vis ena children=1\n"
                           "  cGZWinGen id=a1b2c3d4 area=10,-20,310,180 vis dis children=2\n"
                           "    cGZWinBtn id=00000042 area=0,0,64,32 hid ena children=0\n"
                           "    cGZWinText id=00000043 area=0,0,0,0 hid dis children=1\n"
                           "      cGZWinFlatRect id=00000007 area=0,0,0,0 hid dis children=0\n";
    if (n != 5 || out != expected) ctx.Fail("dump (%u windows):\n%s", n, out.c_str());
    ++ctx.cases;

    // Child order is the list order; a different order is a different dump.
    std::swap(panel.children[0], panel.children[1]);
    std::string swapped;
    wintree::Dump(&top, kProbe, swapped);
    if (swapped == out || swapped.find("cGZWinText") > swapped.find("cGZWinBtn")) ctx.Fail("order:\n%s", swapped.c_str());
    ++ctx.cases;
}

// Captions (the host reads cGZWin's caption string; here the fake's caption field): written after the child count when
// not empty, escaped (quote, backslash, bytes outside 0x20..0x7e) and cut at kMaxCaption bytes.
std::string gCaptions[3];
std::string CaptionOf(const void *object)
{
    const uint32_t id = static_cast<const FakeWin *>(object)->id;
    return id < 3 ? gCaptions[id] : std::string();
}

DIFF_TEST(HostWinTree_Caption, 0)
{
    FillVTables();
    const wintree::Probe probe{&IsFake, &NameOf, &CaptionOf};
    FakeWin top, a, b;
    top.id = 0, a.id = 1, b.id = 2;
    gCaptions[0] = "";
    gCaptions[1] = std::string("Say \"hi\"\\\n\xe9", 11);
    gCaptions[2] = std::string(100, 'x');
    Add(top, a);
    Add(top, b);
    std::string out;
    wintree::Dump(&top, probe, out);
    const std::string expected = "cGZWin id=00000000 area=0,0,0,0 hid dis children=2\n"
                                 "  cGZWin id=00000001 area=0,0,0,0 hid dis children=0 text=\"Say \\x22hi\\x22\\x5c\\x0a\\xe9\"\n"
                                 "  cGZWin id=00000002 area=0,0,0,0 hid dis children=0 text=\"" + std::string(80, 'x') + "\"\n";
    if (out != expected) ctx.Fail("dump:\n%s", out.c_str());
    ++ctx.cases;
}

DIFF_TEST(HostWinTree_Odd, 0)
{
    FillVTables();
    FakeWin top, odd, faulty, listed, enumFault;
    top.name = "cTop";
    odd.vtable = gOtherVTable; // a child that isn't a window
    faulty.name = "cFaulty";
    faulty.faultArea = true;
    listed.name = "cListed";
    listed.countOverride = 3; // says 3, lists none
    enumFault.name = "cEnumFault";
    enumFault.faultEnum = true;
    enumFault.countOverride = 1;
    Add(top, odd);
    Add(top, faulty);
    Add(top, listed);
    Add(top, enumFault);
    std::string out;
    const uint32_t n = wintree::Dump(&top, kProbe, out);
    const char *expected = "cTop id=00000000 area=0,0,0,0 hid dis children=4\n"
                           "  ? (not a window)\n"
                           "  cFaulty (fault)\n"
                           "  cListed id=00000000 area=0,0,0,0 hid dis children=3 (listed 0)\n"
                           "  cEnumFault id=00000000 area=0,0,0,0 hid dis children=1 (fault listing children)\n";
    if (n != 4 || out != expected) ctx.Fail("odd dump (%u windows):\n%s", n, out.c_str());
    ++ctx.cases;

    // Root stops below a parent that isn't a window, and at a window that is its own parent.
    FakeWin child, self;
    child.parent = &odd;
    if (wintree::Root(&child, kProbe) != &child) ctx.Fail("Root under a non-window");
    self.parent = &self;
    if (wintree::Root(&self, kProbe) != &self) ctx.Fail("Root of a self-parent");
    if (wintree::Root(&odd, kProbe) != nullptr) ctx.Fail("Root of a non-window");
    ++ctx.cases;

    // A chain deeper than the limit ends in a marker instead of recursing on.
    std::vector<FakeWin> chain(wintree::kMaxDepth + 5);
    for (size_t i = 0; i + 1 < chain.size(); ++i) Add(chain[i], chain[i + 1]);
    std::string deep;
    const uint32_t m = wintree::Dump(&chain[0], kProbe, deep);
    if (m != static_cast<uint32_t>(wintree::kMaxDepth) || deep.find("... (deeper than the dump's limit)") == std::string::npos)
        ctx.Fail("depth limit: %u windows", m);
    ++ctx.cases;
}
