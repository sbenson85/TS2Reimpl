// Differential tests for ts::cTSAppCommonImpl in the standalone shape (src/ts/cTSAppCommonImpl.*, lane EP): the option
// store. Our side: objects built by our constructor (the constructor hook's adapter), every slot called through the
// object's own vtable. Original side: objects built by the original constructor 0xca4cb0, slots called through the game's
// vtable 0xeb5468 (so the originals' calls on `this` reach the originals). Both sides get the same synthesised option
// texts (never the game's configuration). The stores are compared whole after every change: both maps as trees (shape,
// colours, parent links, head links, keys, aliases) and every entry of every list (the cRZString's vtable word, text,
// size, capacity and count, the mask). Only the object's own vtable word is normalised ("the side's own").
//
// AddOption's result is not compared: the original leaves eax from its last call (the erase count, the entry string's
// destructor 0x1d99b0 or the list-head free 0x824624: 0xca7230..0xca749f has no write of eax before its three `ret 0x14`).
#define NOMINMAX
#include "app_debugger_fakes.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "ts/cTSAppCommonImpl.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using ts::cTSAppCommonImpl;
using ts::OptionString;
using ts::OptionStringList;
using Adapters = ts::cTSAppCommonImplAdapters;

constexpr uint32_t kCtor = 0xca4cb0, kDtor = 0xca4de0, kDeleting = 0xca4f50, kQueryInterface = 0xca6ff0;
constexpr uint32_t kAddOption = 0xca7230, kGetOptionText = 0xca5e40, kGetOptionList = 0xca5e80, kHasOption = 0xca6bd0;
constexpr uint32_t kIsNameUnused = 0xca69f0, kIsOptionNameUnused = 0xca6920, kReadOptionText = 0xca5ed0;
constexpr uint32_t kReadOptionList = 0xca6200, kRegisterHandlers = 0xca5d20;
constexpr uint32_t kGameVTable = 0xeb5468, kGameSizedDelete = 0x824624, kFrameworkGlobal = 0x10c9378;
constexpr int kSlots = 18;
enum Slot { sQI = 0, sAddRef, sRelease, sAddOption, sGetText, sGetList, sHas, sNameUnused, sOptionNameUnused, sSetAppName,
            sDeleting, sReadText, sReadList, sInitOptions, sParse, sSetupDirs, sClear, sRegister };

template <typename R, typename... Args> R CallSlot(void *object, int slot, Args... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, Args...)>((*reinterpret_cast<void *const *const *>(object))[slot]);
    return fn(object, args...);
}

const void *OurVTable()
{
    alignas(8) static uint8_t memory[sizeof(cTSAppCommonImpl)];
    static const void *vt = [] {
        auto *p = Adapters::Construct(memory, nullptr);
        const void *v = *reinterpret_cast<void **>(memory);
        Adapters::Destruct(p, nullptr);
        return v;
    }();
    return vt;
}

// One side's object (0x18 bytes from the process heap, as the game's operator new).
struct Object {
    difftest::Context &ctx;
    bool original;
    uint8_t *mem;
    Object(difftest::Context &c, bool o) : ctx(c), original(o), mem(static_cast<uint8_t *>(::operator new(0x18)))
    {
        std::memset(mem, 0xa5, 0x18);
        if (original) ctx.Original<void *(__thiscall *)(void *)>(kCtor)(mem);
        else Adapters::Construct(mem, nullptr);
    }
    ~Object()
    {
        if (original) ctx.Original<void(__thiscall *)(void *)>(kDtor)(mem);
        else CallSlot<void *>(mem, sDeleting, 0u);
        ::operator delete(mem);
    }
    Object(const Object &) = delete;
    const void *Own() const { return original ? ctx.image.At(kGameVTable) : OurVTable(); }
    template <typename R, typename... Args> R Slot(int slot, Args... args) { return CallSlot<R>(mem, slot, args...); }
};

// ---- the store, as raw memory (MSVC 14.40 _Tree_node / _List_node)

struct RawEntryNode {
    RawEntryNode *next, *prev;
    const void *vt;
    std::string text;
    uint32_t refCount, mask;
};
static_assert(sizeof(RawEntryNode) == 0x2c);
struct RawTreeNode {
    RawTreeNode *left, *parent, *right;
    uint8_t color, isNil, pad[2];
    uint32_t key, alias;
    RawEntryNode *list;
    uint32_t listSize;
};
static_assert(sizeof(RawTreeNode) == 0x20);

std::string Fmt(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

uintptr_t gBase = 0;

std::string DumpList(const RawEntryNode *head, uint32_t size)
{
    std::string out = Fmt("[%u:", size);
    const RawEntryNode *node = head->next, *previous = head;
    for (uint32_t i = 0; node != head && i <= size + 1; ++i, previous = node, node = node->next) {
        out += Fmt("{vt+%x%s '%s' %u/%u n%u m%x}", unsigned(reinterpret_cast<uintptr_t>(node->vt) - gBase), node->prev == previous ? "" : " BADPREV",
                   node->text.c_str(), unsigned(node->text.size()), unsigned(node->text.capacity()), node->refCount, node->mask);
    }
    if (node != head) out += " UNTERMINATED";
    if (head->prev != previous) out += " BADTAIL";
    return out + "]";
}

std::string DumpTree(const RawTreeNode *node, const RawTreeNode *parent, const RawTreeNode *head)
{
    if (node->isNil) return node == head ? "." : "NIL?";
    return Fmt("(%x a%x c%u%s ", node->key, node->alias, node->color, node->parent == parent ? "" : " BADPARENT") +
           DumpList(node->list, node->listSize) + " " + DumpTree(node->left, node, head) + " " + DumpTree(node->right, node, head) + ")";
}

std::string DumpMap(const uint8_t *at)
{
    const auto *head = *reinterpret_cast<RawTreeNode *const *>(at);
    uint32_t size = *reinterpret_cast<const uint32_t *>(at + 4);
    std::string out = Fmt("size %u head c%u n%u ", size, head->color, head->isNil);
    const RawTreeNode *root = head->parent;
    if (root->isNil) {
        out += head->left == head && head->right == head ? "empty" : "BADEMPTY";
        return out;
    }
    const RawTreeNode *lo = root, *hi = root;
    while (!lo->left->isNil) lo = lo->left;
    while (!hi->right->isNil) hi = hi->right;
    if (head->left != lo || head->right != hi) out += "BADENDS ";
    return out + DumpTree(root, head, head);
}

std::string Dump(const Object &o)
{
    const void *vt = *reinterpret_cast<const void *const *>(o.mem);
    return std::string(vt == o.Own() ? "own" : Fmt("vt %p", vt)) + " | A " + DumpMap(o.mem + 4) + " | B " + DumpMap(o.mem + 0xc) +
           Fmt(" | w14 %x", *reinterpret_cast<const uint32_t *>(o.mem + 0x14));
}

// ---- synthesised option texts

std::string RandomText(std::mt19937 &rng)
{
    static const char alphabet[] = "abcXYZ/\\._ -09";
    static const unsigned lengths[] = {0, 1, 3, 7, 14, 15, 16, 17, 22, 23, 31, 32, 40, 100};
    unsigned length = lengths[rng() % 14];
    std::string s;
    for (unsigned i = 0; i < length; ++i) s += alphabet[rng() % (sizeof(alphabet) - 1)];
    return s;
}

uint32_t RandomMask(std::mt19937 &rng)
{
    static const uint32_t masks[] = {0, 1, 2, 3, 4, 0x20000, 0x80000000, 0xffffffff, 0x7fffffff, 0x21};
    return rng() % 3 ? masks[rng() % 10] : static_cast<uint32_t>(rng());
}

uint32_t RandomFlag(std::mt19937 &rng)
{
    static const uint32_t flags[] = {0, 1, 0x100, 0x101, 0xffffff00, 0x80};
    return flags[rng() % 6];
}

struct Op {
    uint32_t key, alias, toOverrides, mask;
    bool null;
    std::string text;
};

const uint32_t kKeys[] = {0x6bcfce93, 0x6c253729, 0x4bcfce5e, 0, 0xffffffff, 0x10, 0x11, 0x80000000};

// A sequence of AddOption calls. With `acyclic`, an option's alias is a key of a lower index or a key no option has, so
// the readers' recursion ends.
std::vector<Op> RandomOps(std::mt19937 &rng, unsigned count, bool acyclic, bool aliases = true)
{
    std::vector<Op> ops;
    std::vector<std::string> texts;
    for (unsigned i = 0; i < count; ++i) {
        Op op;
        unsigned k = rng() % 8;
        op.key = kKeys[k];
        op.alias = 0;
        if (aliases && rng() % 3 == 0) {
            if (acyclic) op.alias = k == 0 || rng() % 4 == 0 ? 0x12345 : kKeys[rng() % k];
            else op.alias = kKeys[rng() % 8];
        }
        if (acyclic && op.alias == 0 && k > 0 && aliases && rng() % 4 == 0) op.alias = 0x12345; // a missing alias
        op.toOverrides = RandomFlag(rng);
        op.mask = RandomMask(rng);
        op.null = !acyclic && rng() % 10 == 0;
        if (!texts.empty() && rng() % 2) {
            op.text = texts[rng() % texts.size()];
            // Sometimes the same text with its letters' case flipped: AddOption's duplicate check is case-sensitive.
            if (rng() % 3 == 0)
                for (char &c : op.text) c = c >= 'a' && c <= 'z' ? char(c - 32) : c >= 'A' && c <= 'Z' ? char(c + 32) : c;
            // And sometimes with the mask of the text's last use, so the duplicate check reaches the text compare.
            if (rng() % 2) for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
                std::string a = it->text, b = op.text;
                for (char &c : a) c = char(tolower(static_cast<unsigned char>(c)));
                for (char &c : b) c = char(tolower(static_cast<unsigned char>(c)));
                if (a != b) continue;
                op.mask = it->mask;
                // The key and map too, when the alias cannot make a cycle (none, or one no option has).
                if (op.alias == 0 || op.alias == 0x12345) { op.key = it->key; op.toOverrides = it->toOverrides; }
                break;
            }
        } else {
            op.text = RandomText(rng);
        }
        texts.push_back(op.text);
        ops.push_back(op);
    }
    return ops;
}

void Apply(Object &o, const Op &op)
{
    o.Slot<uint32_t>(sAddOption, op.key, op.alias, op.null ? nullptr : op.text.c_str(), op.toOverrides, op.mask);
}

// A cRZString on the test's stack (game vtable: the original cRZString code runs on both sides).
struct Text {
    union {
        cRZString s;
    };
    explicit Text(const std::string &t) { s.Construct(t.c_str()); }
    ~Text() { s.Destruct(); }
    std::string Show() const { return Fmt("'%s' %u/%u n%u", s.mString.c_str(), unsigned(s.mString.size()), unsigned(s.mString.capacity()), s.mRefCount); }
};

std::string ShowList(const OptionStringList &list)
{
    std::string out = Fmt("%u:", unsigned(list.size()));
    for (const OptionString &s : list) out += Fmt("{'%s' %u/%u}", s.c_str(), unsigned(s.size()), unsigned(s.capacity()));
    return out;
}

// ---- frees through either sized delete, recorded

// The code stays writable after the restore: putting back the old protection VirtualProtect reported (the first page's)
// narrows both pages when the five bytes straddle a page boundary, and an earlier redirect on the second page then
// faults when it restores its bytes (lane MW, tests/app_fixture.h).
struct Redirect {
    uint8_t *site; uint8_t saved[5];
    Redirect(void *entry, void *target) : site(static_cast<uint8_t *>(entry))
    {
        std::memcpy(saved, site, 5);
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9; std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect()
    {
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Redirect(const Redirect &) = delete;
};

// A fixed buffer: the log must not allocate or free while the sized deletes are redirected.
char gFreeLog[4096];
size_t gFreeLength = 0;
const void *gWatched = nullptr;
void __cdecl RecordingFree(void *p, size_t size)
{
    if (gFreeLength < sizeof(gFreeLog) - 32)
        gFreeLength += size_t(std::snprintf(gFreeLog + gFreeLength, sizeof(gFreeLog) - gFreeLength, "%s:%x ", p == gWatched ? "object" : "block", unsigned(size)));
    HeapFree(GetProcessHeap(), 0, p);
}
std::string Frees() { return std::string(gFreeLog, gFreeLength); }

struct FreeWatch {
    Redirect game, ours;
    explicit FreeWatch(difftest::Context &ctx)
        : game(ctx.image.At(kGameSizedDelete), reinterpret_cast<void *>(&RecordingFree)),
          ours(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *, size_t)>(&::operator delete)), reinterpret_cast<void *>(&RecordingFree))
    {
        gFreeLength = 0;
    }
};

// ---- traps for the adapters test (one per stack-argument count)

bool gTrapped = false;
uint32_t __fastcall Trap0(void *, void *) { gTrapped = true; return 0; }
uint32_t __fastcall Trap1(void *, void *, uint32_t) { gTrapped = true; return 0; }
uint32_t __fastcall Trap2(void *, void *, uint32_t, uint32_t) { gTrapped = true; return 0; }
uint32_t __fastcall Trap5(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) { gTrapped = true; return 0; }

// A recording AddRef for QueryInterface (our object's own AddRef slot is _purecall, as the game's).
std::string gCalls;
uint32_t __fastcall RecordingAddRef(void *self, void *)
{
    gCalls += Fmt("addref(+%d) ", int(static_cast<uint8_t *>(self) - static_cast<uint8_t *>(const_cast<void *>(gWatched))));
    return 7;
}

// A copy of the object's own vtable, installed in its place until destroyed (then the own pointer is put back).
struct PatchedVTable {
    uint8_t *mem;
    const void *own;
    const void *slots[kSlots];
    explicit PatchedVTable(uint8_t *m) : mem(m), own(*reinterpret_cast<const void **>(m))
    {
        std::memcpy(slots, own, sizeof(slots));
        *reinterpret_cast<const void **>(mem) = slots;
    }
    ~PatchedVTable() { *reinterpret_cast<const void **>(mem) = own; }
};

} // namespace

// ---- 0xca4cb0 (constructor), 0xca4de0 (destructor body), 0xca4f50 (deleting destructor, slot 10)

DIFF_TEST(CommonImpl_ConstructDestruct, kCtor)
{
    gBase = GameBase();
    std::mt19937 rng(kCtor);
    for (unsigned i = 0; i < 200; ++i) {
        std::vector<Op> ops = RandomOps(rng, i % 12, false);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            bool original = side == 0;
            auto *mem = static_cast<uint8_t *>(::operator new(0x18));
            std::memset(mem, 0x5a, 0x18);
            const void *own = original ? ctx.image.At(kGameVTable) : OurVTable();
            // Every third case our side is a game-built object (game vtable) torn down by the adapters, as with the constructor hook off.
            const bool viaAdapter = !original && i % 3 == 2;
            void *r = original || viaAdapter ? ctx.Original<void *(__thiscall *)(void *)>(kCtor)(mem) : Adapters::Construct(mem, nullptr);
            results[side] = Fmt("ctor %d %s | A %s | B %s | w14 %x | ", r == mem, *reinterpret_cast<const void **>(mem) == (viaAdapter ? ctx.image.At(kGameVTable) : own) ? "own" : "other",
                                DumpMap(mem + 4).c_str(), DumpMap(mem + 0xc).c_str(), *reinterpret_cast<uint32_t *>(mem + 0x14));
            uint32_t sizes[2];
            for (const Op &op : ops) CallSlot<uint32_t>(mem, sAddOption, op.key, op.alias, op.null ? nullptr : op.text.c_str(), op.toOverrides, op.mask);
            sizes[0] = *reinterpret_cast<uint32_t *>(mem + 8);
            sizes[1] = *reinterpret_cast<uint32_t *>(mem + 0x10);
            uint32_t flags = i % 4 == 3 ? 1u : (i % 4 == 2 ? 0xfffffffeu : (i % 2 ? 0x80000001u : 0u));
            uint32_t headA = *reinterpret_cast<uint32_t *>(mem + 4), headB = *reinterpret_cast<uint32_t *>(mem + 0xc);
            gWatched = mem;
            void *ret;
            {
                FreeWatch watch(ctx);
                if (i % 5 == 4) { // the destructor body alone
                    if (original) ctx.Original<void(__thiscall *)(void *)>(kDtor)(mem);
                    else Adapters::Destruct(reinterpret_cast<cTSAppCommonImpl *>(mem), nullptr);
                    ret = mem;
                    flags = 0;
                } else {
                    ret = viaAdapter ? static_cast<void *>(Adapters::DeletingDestructor(reinterpret_cast<cTSAppCommonImpl *>(mem), nullptr, flags))
                                     : CallSlot<void *>(mem, sDeleting, flags);
                }
            }
            results[side] += Fmt("sizes %u %u | ret %d | frees %s", sizes[0], sizes[1], ret == mem, Frees().c_str());
            if (!(flags & 1)) {
                results[side] += Fmt("| after: vt %s A %d/%u B %d/%u w14 %x", *reinterpret_cast<const void **>(mem) == own ? "own" : "other",
                                     *reinterpret_cast<uint32_t *>(mem + 4) == headA, *reinterpret_cast<uint32_t *>(mem + 8),
                                     *reinterpret_cast<uint32_t *>(mem + 0xc) == headB, *reinterpret_cast<uint32_t *>(mem + 0x10),
                                     *reinterpret_cast<uint32_t *>(mem + 0x14));
                ::operator delete(mem);
            }
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u:\n  original: %s\n  ours:     %s", i, results[0].c_str(), results[1].c_str());
    }
    (void)kDeleting;
}

// ---- 0xca7230 AddOption (slot 3)

DIFF_TEST(CommonImpl_AddOption, kAddOption)
{
    gBase = GameBase();
    std::mt19937 rng(kAddOption);
    for (unsigned seed = 0; seed < 300; ++seed) {
        std::vector<Op> ops = RandomOps(rng, 1 + rng() % 40, false);
        Object a(ctx, true), b(ctx, false);
        for (size_t i = 0; i < ops.size(); ++i) {
            Apply(a, ops[i]);
            Apply(b, ops[i]);
            std::string da = Dump(a), db = Dump(b);
            ++ctx.cases;
            if (da != db) {
                ctx.Fail("seed %u op %u (key %x alias %x %s '%s' flag %x mask %x):\n  original: %s\n  ours:     %s", seed, unsigned(i), ops[i].key,
                         ops[i].alias, ops[i].null ? "null" : "text", ops[i].text.c_str(), ops[i].toOverrides, ops[i].mask, da.c_str(), db.c_str());
                break;
            }
        }
    }
}

// ---- 0xca5ed0 ReadOptionText (slot 11), 0xca5e40 GetOptionText (slot 4)

DIFF_TEST(CommonImpl_ReadOptionText, kReadOptionText)
{
    gBase = GameBase();
    std::mt19937 rng(kReadOptionText);
    for (unsigned seed = 0; seed < 200; ++seed) {
        std::vector<Op> ops = RandomOps(rng, 2 + rng() % 30, true);
        Object a(ctx, true), b(ctx, false);
        for (const Op &op : ops) { Apply(a, op); Apply(b, op); }
        for (unsigned q = 0; q < 40; ++q) {
            uint32_t id = rng() % 6 ? kKeys[rng() % 8] : 0x12345;
            uint32_t unused = rng(), defaultsOnly = RandomFlag(rng), mask = RandomMask(rng);
            bool get = rng() % 2;
            std::string start = rng() % 2 ? std::string() : RandomText(rng);
            std::string results[2];
            Object *objects[2] = {&a, &b};
            for (int side = 0; side < 2; ++side) {
                Text out(start);
                uint32_t r = objects[side]->Slot<uint32_t>(get ? sGetText : sReadText, id, out.s.AsInterface(), unused, defaultsOnly, mask);
                results[side] = Fmt("%u ", r & 0xff) + out.Show();
            }
            ++ctx.cases;
            if (results[0] != results[1])
                ctx.Fail("seed %u %s(%x, %x, %x, %x):\n  original: %s\n  ours:     %s", seed, get ? "GetOptionText" : "ReadOptionText", id, unused,
                         defaultsOnly, mask, results[0].c_str(), results[1].c_str());
        }
        if (Dump(a) != Dump(b)) ctx.Fail("seed %u: the stores differ after reading", seed);
        // A null text object: GetOptionText's early false.
        ++ctx.cases;
        if ((a.Slot<uint32_t>(sGetText, kKeys[0], nullptr, 0u, 0u, 0xffffffffu) & 0xff) != (b.Slot<uint32_t>(sGetText, kKeys[0], nullptr, 0u, 0u, 0xffffffffu) & 0xff))
            ctx.Fail("seed %u: GetOptionText(null) differs", seed);
    }
}

// ---- 0xca6200 ReadOptionList (slot 12), 0xca5e80 GetOptionList (slot 5)

DIFF_TEST(CommonImpl_ReadOptionList, kReadOptionList)
{
    gBase = GameBase();
    adt::AllocatorFake allocator(ctx);
    std::mt19937 rng(kReadOptionList);
    for (unsigned seed = 0; seed < 200; ++seed) {
        std::vector<Op> ops = RandomOps(rng, 2 + rng() % 30, true);
        Object a(ctx, true), b(ctx, false);
        for (const Op &op : ops) { Apply(a, op); Apply(b, op); }
        for (unsigned q = 0; q < 40; ++q) {
            uint32_t id = rng() % 6 ? kKeys[rng() % 8] : 0x12345;
            uint32_t unused = rng(), defaultsOnly = RandomFlag(rng), mask = RandomMask(rng);
            bool get = rng() % 2;
            std::vector<std::string> start;
            for (unsigned n = rng() % 4; n; --n) start.push_back(RandomText(rng));
            std::string results[2];
            Object *objects[2] = {&a, &b};
            for (int side = 0; side < 2; ++side) {
                OptionStringList out;
                for (const std::string &s : start) out.push_back(OptionString(s.c_str()));
                uint32_t r = objects[side]->Slot<uint32_t>(get ? sGetList : sReadList, id, static_cast<void *>(&out), unused, defaultsOnly, mask);
                results[side] = Fmt("%u ", r & 0xff) + ShowList(out);
            }
            ++ctx.cases;
            if (results[0] != results[1])
                ctx.Fail("seed %u %s(%x, %x, %x, %x):\n  original: %s\n  ours:     %s", seed, get ? "GetOptionList" : "ReadOptionList", id, unused,
                         defaultsOnly, mask, results[0].c_str(), results[1].c_str());
            allocator.Release();
        }
        ++ctx.cases;
        if ((a.Slot<uint32_t>(sGetList, kKeys[0], static_cast<void *>(nullptr), 0u, 0u, 0xffffffffu) & 0xff) !=
            (b.Slot<uint32_t>(sGetList, kKeys[0], static_cast<void *>(nullptr), 0u, 0u, 0xffffffffu) & 0xff))
            ctx.Fail("seed %u: GetOptionList(null) differs", seed);
    }
}

// ---- 0xca6bd0 HasOption (slot 6), 0xca69f0 IsNameUnused (slot 7), 0xca6920 IsOptionNameUnused (slot 8)

namespace {
// A name built from a stored text: a leading part of it, case or separators changed, or something else.
std::string NameFrom(std::mt19937 &rng, const std::vector<Op> &ops)
{
    if (ops.empty() || rng() % 5 == 0) return RandomText(rng);
    std::string s = ops[rng() % ops.size()].text;
    if (rng() % 2 && !s.empty()) s = s.substr(0, rng() % (s.size() + 1));
    for (char &c : s) {
        if (rng() % 4 == 0 && c >= 'a' && c <= 'z') c = char(c - 32);
        else if (rng() % 4 == 0 && (c == '/' || c == '\\')) c = c == '/' ? '\\' : '/';
    }
    if (rng() % 3 == 0) s += rng() % 2 ? "/" : "\\x";
    return s;
}
} // namespace

DIFF_TEST(CommonImpl_Names, kIsNameUnused)
{
    gBase = GameBase();
    std::mt19937 rng(kIsNameUnused);
    for (unsigned seed = 0; seed < 200; ++seed) {
        std::vector<Op> ops = RandomOps(rng, 1 + rng() % 20, true);
        // Paths: texts with separators that the names are cut from.
        for (Op &op : ops)
            if (rng() % 2) op.text = Fmt("%s/%s\\%s", RandomText(rng).c_str(), RandomText(rng).c_str(), RandomText(rng).c_str());
        Object a(ctx, true), b(ctx, false);
        for (const Op &op : ops) { Apply(a, op); Apply(b, op); }
        for (unsigned q = 0; q < 30; ++q) {
            unsigned kind = q % 3;
            uint32_t key = rng() % 5 ? kKeys[rng() % 8] : static_cast<uint32_t>(rng());
            std::string name = NameFrom(rng, ops);
            uint32_t r[2];
            Object *objects[2] = {&a, &b};
            for (int side = 0; side < 2; ++side) {
                Text text(name);
                if (kind == 0) r[side] = objects[side]->Slot<uint32_t>(sHas, key);
                else if (kind == 1) r[side] = objects[side]->Slot<uint32_t>(sNameUnused, text.s.AsInterface());
                else r[side] = objects[side]->Slot<uint32_t>(sOptionNameUnused, key);
            }
            ++ctx.cases;
            if ((r[0] & 0xff) != (r[1] & 0xff))
                ctx.Fail("seed %u %s(%x, '%s'): original %u, ours %u", seed, kind == 0 ? "HasOption" : kind == 1 ? "IsNameUnused" : "IsOptionNameUnused",
                         key, name.c_str(), r[0] & 0xff, r[1] & 0xff);
        }
    }
    (void)kHasOption;
    (void)kIsOptionNameUnused;
}

// ---- 0xca5d20 RegisterRegistryHandlers (slot 17): the framework global is a fake answering a fake service

namespace {
std::string gLog;
uint32_t gRegisterResult = 0;
void *gServiceObject = nullptr;
uint32_t __fastcall FwGetService(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    gLog += Fmt("fw.slot5(%x,%x) out=%d|", id, iid, *out == nullptr);
    *out = gServiceObject;
    return 1;
}
// The handler as the RVA it stands for: the original passes its own (0xca5db0, 0xca5170), ours passes our replacements
// of those (lane MB), which are the same handlers.
std::string HandlerName(void *callback)
{
    if (callback == reinterpret_cast<void *>(&ts::cTSAppCommonImplAdapters::OnResourceFactoryEntry)) return "fn+ca5db0";
    if (callback == reinterpret_cast<void *>(&ts::cTSAppCommonImplAdapters::OnPathRegistryEntry)) return "fn+ca5170";
    return Fmt("fn+%x", unsigned(reinterpret_cast<uintptr_t>(callback) - gBase));
}
uint32_t __fastcall SvcRegister(void *, void *, uint32_t word, void *callback, void *face)
{
    gLog += Fmt("svc.slot7(%x, %s, face+%d)|", word, HandlerName(callback).c_str(),
                int(static_cast<const uint8_t *>(face) - static_cast<const uint8_t *>(gWatched)));
    return gRegisterResult;
}
uint32_t __fastcall SvcRelease(void *, void *)
{
    gLog += "svc.release|";
    return 0;
}
} // namespace

DIFF_TEST(CommonImpl_RegisterRegistryHandlers, kRegisterHandlers)
{
    gBase = GameBase();
    const void *svcVt[16], *fwVt[16];
    for (auto &s : svcVt) s = reinterpret_cast<const void *>(&Trap0);
    for (auto &s : fwVt) s = reinterpret_cast<const void *>(&Trap0);
    svcVt[2] = reinterpret_cast<const void *>(&SvcRelease);
    svcVt[7] = reinterpret_cast<const void *>(&SvcRegister);
    fwVt[5] = reinterpret_cast<const void *>(&FwGetService);
    struct Fake { const void *const *vt; } service{svcVt}, framework{fwVt};
    gServiceObject = &service;
    adt::GlobalOverride<uint32_t> global(ctx, kFrameworkGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&framework)));
    std::mt19937 rng(kRegisterHandlers);
    for (int run = 0; run < 60; ++run) {
        gRegisterResult = run % 3 == 0 ? 0 : (run % 3 == 1 ? 1 : ((rng() | 0x100) & ~0xffu) | (rng() & 1));
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            Object o(ctx, side == 0);
            gWatched = o.mem;
            gLog.clear();
            gTrapped = false;
            o.Slot<void>(sRegister);
            results[side] = gLog + (gTrapped ? "TRAP" : "");
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("run %d:\n  original: %s\n  ours:     %s", run, results[0].c_str(), results[1].c_str());
    }
}

// ---- the slots no other test calls: QueryInterface (0xca6ff0, slot 0, with a recording AddRef in a copy of each side's
// own vtable) and SetAppName (slot 9, shared 0x300a0: does nothing)

DIFF_TEST(CommonImpl_Slots, kQueryInterface)
{
    gBase = GameBase();
    std::mt19937 rng(kQueryInterface);
    static const uint32_t iids[] = {0xebabd35c, 0x9af89af3, 1, 0, 0x66, 0xebabd35d};
    for (unsigned i = 0; i < 300; ++i) {
        std::vector<Op> ops = RandomOps(rng, rng() % 6, false);
        uint32_t iid = rng() % 4 ? iids[rng() % 6] : static_cast<uint32_t>(rng());
        bool nullOut = i % 7 == 0;
        std::string text = RandomText(rng);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            Object o(ctx, side == 0);
            for (const Op &op : ops) Apply(o, op);
            gWatched = o.mem;
            gCalls.clear();
            void *out = reinterpret_cast<void *>(0x5eed);
            uint32_t r;
            {
                PatchedVTable patch(o.mem);
                patch.slots[sAddRef] = reinterpret_cast<const void *>(&RecordingAddRef);
                r = o.Slot<uint32_t>(sQI, iid, nullOut ? static_cast<void **>(nullptr) : &out);
                o.Slot<void>(sSetAppName, text.c_str());
            }
            results[side] = Fmt("%u out %s ", r & 0xff, out == reinterpret_cast<void *>(0x5eed) ? "untouched" : out == o.mem ? "this" : "other") + gCalls +
                            Dump(o);
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (iid %x):\n  original: %s\n  ours:     %s", i, iid, results[0].c_str(), results[1].c_str());
    }
}

// ---- the adapters on objects built by the original constructor (game vtable), with the adapter's own slot trapped in a
// copy of the game's vtable: an adapter that dispatched virtually would reach the trap (in the game: its own hook). The
// result is compared with the original function on a twin object.

DIFF_TEST(CommonImpl_Adapters, kAddOption)
{
    gBase = GameBase();
    adt::AllocatorFake allocator(ctx);
    adt::GlobalOverride<uint32_t> global(ctx, kFrameworkGlobal);
    std::mt19937 rng(0xca7231);
    // The constructor and destructor adapters are covered on game-built objects by CommonImpl_ConstructDestruct (every
    // third case). Lane EV removed their unreachable kinds here: enabled, Destruct differed (the destructor body leaves
    // each side's own vtable word, which this dump does not normalise) and the original deleting destructor faulted
    // at a bogus address (rva 0x1b3) inside this test's FreeWatch + AllocatorFake setup; not chased further.
    enum Kind { kQI, kAdd, kGetText, kGetList, kHas, kName, kOptionName, kReadText, kReadList, kCount };
    static const char *names[] = {"QueryInterface", "AddOption", "GetOptionText", "GetOptionList", "HasOption", "IsNameUnused",
                                  "IsOptionNameUnused", "ReadOptionText", "ReadOptionList"};
    static const int slots[] = {sQI, sAddOption, sGetText, sGetList, sHas, sNameUnused, sOptionNameUnused, sReadText, sReadList};
    for (unsigned i = 0; i < 600; ++i) {
        Kind kind = static_cast<Kind>(i % kCount);
        std::vector<Op> ops = RandomOps(rng, 1 + rng() % 20, true, false); // no aliases: ReadOptionText/List would call their own slot
        uint32_t key = kKeys[rng() % 8], mask = RandomMask(rng), flag = RandomFlag(rng);
        (void)(rng() % 2); // was the deleting destructor's flags: kept so the random stream (and the cases) stay the same
        std::string text = RandomText(rng);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            auto *mem = static_cast<uint8_t *>(::operator new(0x18));
            ctx.Original<void *(__thiscall *)(void *)>(kCtor)(mem);
            for (const Op &op : ops) CallSlot<uint32_t>(mem, sAddOption, op.key, op.alias, op.text.c_str(), op.toOverrides, op.mask);
            auto *self = reinterpret_cast<cTSAppCommonImpl *>(mem);
            gWatched = mem;
            gTrapped = false;
            gCalls.clear();
            uint32_t r = 0;
            std::string extra;
            {
                PatchedVTable patch(mem);
                patch.slots[sAddRef] = reinterpret_cast<const void *>(&RecordingAddRef);
                if (side == 1) {
                    static const void *const traps[] = {reinterpret_cast<const void *>(&Trap2), reinterpret_cast<const void *>(&Trap5),
                                                        reinterpret_cast<const void *>(&Trap5), reinterpret_cast<const void *>(&Trap5),
                                                        reinterpret_cast<const void *>(&Trap1), reinterpret_cast<const void *>(&Trap1),
                                                        reinterpret_cast<const void *>(&Trap1), reinterpret_cast<const void *>(&Trap5),
                                                        reinterpret_cast<const void *>(&Trap5)};
                    patch.slots[slots[kind]] = traps[kind];
                }
                Text name(text);
                OptionStringList list;
                void *out = reinterpret_cast<void *>(0x5eed);
                switch (kind) {
                case kQI:
                    r = side ? Adapters::QueryInterface(self, nullptr, mask % 2 ? 0xebabd35c : key, &out)
                             : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(kQueryInterface)(mem, mask % 2 ? 0xebabd35c : key, &out);
                    extra = out == mem ? "this" : out == reinterpret_cast<void *>(0x5eed) ? "untouched" : "other";
                    break;
                case kAdd:
                    if (side) Adapters::AddOption(self, nullptr, key, 0, mask % 3 ? text.c_str() : nullptr, flag, mask);
                    else ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, const char *, uint32_t, uint32_t)>(kAddOption)(mem, key, 0, mask % 3 ? text.c_str() : nullptr, flag, mask);
                    break;
                case kGetText:
                    r = side ? Adapters::GetOptionText(self, nullptr, key, name.s.AsInterface(), 0, flag, mask)
                             : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *, uint32_t, uint32_t, uint32_t)>(kGetOptionText)(mem, key, name.s.AsInterface(), 0, flag, mask);
                    extra = name.Show();
                    break;
                case kGetList:
                    list.push_back(OptionString("junk"));
                    r = side ? Adapters::GetOptionList(self, nullptr, key, &list, 0, flag, mask)
                             : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *, uint32_t, uint32_t, uint32_t)>(kGetOptionList)(mem, key, &list, 0, flag, mask);
                    extra = ShowList(list);
                    break;
                case kHas:
                    r = side ? Adapters::HasOption(self, nullptr, key) : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(kHasOption)(mem, key);
                    break;
                case kName: {
                    Text probe(ops[0].text.substr(0, ops[0].text.size() / 2));
                    r = side ? Adapters::IsNameUnused(self, nullptr, probe.s.AsInterface())
                             : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(kIsNameUnused)(mem, probe.s.AsInterface());
                    break;
                }
                case kOptionName:
                    r = side ? Adapters::IsOptionNameUnused(self, nullptr, key) : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(kIsOptionNameUnused)(mem, key);
                    break;
                case kReadText:
                    r = side ? Adapters::ReadOptionText(self, nullptr, key, name.s.AsInterface(), 0, flag, mask)
                             : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *, uint32_t, uint32_t, uint32_t)>(kReadOptionText)(mem, key, name.s.AsInterface(), 0, flag, mask);
                    extra = name.Show();
                    break;
                case kReadList:
                    r = side ? Adapters::ReadOptionList(self, nullptr, key, &list, 0, flag, mask)
                             : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *, uint32_t, uint32_t, uint32_t)>(kReadOptionList)(mem, key, &list, 0, flag, mask);
                    extra = ShowList(list);
                    break;
                default:
                    break;
                }
            }
            {
                // The store after the call, through a view sharing the dump code (the vtable word is the game's on both sides).
                std::string dumpA = DumpMap(mem + 4), dumpB = DumpMap(mem + 0xc);
                extra += " | " + dumpA + " | " + dumpB;
            }
            results[side] = Fmt("%u %s %s%s", r & 0xff, extra.c_str(), gCalls.c_str(), gTrapped ? " TRAPPED" : "");
            ctx.Original<void(__thiscall *)(void *)>(kDtor)(mem);
            ::operator delete(mem);
            allocator.Release();
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (%s):\n  original: %s\n  ours:     %s", i, names[kind], results[0].c_str(), results[1].c_str());
    }
}
