// Differential tests for cIStreamStringBehaviourAdaptor (src/gz/cIStreamStringBehaviourAdaptor.cpp, lane QQ): the
// cRZIStream the scene-graph factory's Read (0xa4d750) wraps a record's cIGZIStream in. The constructor is inlined into
// that Read, so the original side builds its object as Read does: cRZIStream's base constructor 0x1b4d6d, the image's
// table 0xe6c648, the inner stream at +0x10 and its AddRef. Ours runs Construct (whose stored table word is checked
// against the image's), then takes our own table (OwnVTable, the face an image-free program stores) so every slot runs
// our members. Both sides then run the same random script through the object's table, slot by slot, over the test's own
// fakes:
// - an inner stream (24 slots, every call logged with its arguments): QueryInterface answers by a rule and gives a
//   sentinel, AddRef / Release count, Skip answers a scripted word, the raw read (17) serves bytes from a script or
//   refuses (filling the target with a fixed byte: what a target holds when nothing wrote it is per side),
//   GetError answers a scripted full word (so the forwarders must keep all of eax);
// - an owner (SetOwner, released by the destructor) and a cIGZString (FromChar, Append: GetGZStr's targets), logged.
// After every step: the answer (full eax), the call log, the object's words (count, flag byte, owner, inner; the table as
// "own" / "base" / other), the buffer GetRZCharStr and the getters write (with canaries), and every fake's count. The end:
// the deleting destructor (flags 0 or 1, directly or through the last Release), the blocks it frees (FreeRecorder) and,
// for flags 0, the words it leaves.
// The script covers the adaptor's own slots: 0 (0xa4d6d0), 3 (0xa4da80), 17 (0xa4d650), 18 (0xa4d640), 21 (0xa4d4b0).
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"

#include "gz/cIStreamStringBehaviourAdaptor.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using A = cIStreamStringBehaviourAdaptor;
constexpr uint32_t kBaseCtor = 0x1b4d6d;

// ---- the log ----
std::vector<uint32_t> gLog;
void Log(std::initializer_list<uint32_t> words)
{
    difftest::NotCounted bookkeeping;
    gLog.insert(gLog.end(), words);
}

// ---- the script the fakes answer from ----
struct Script {
    std::vector<uint8_t> data;
    size_t pos = 0;
    bool refuse = false;
    uint32_t skipAnswer = 0;
    uint32_t errorAnswer = 0;
    uint32_t qiRule = 0;
};
Script gScript;

uint32_t Tag(const void *p);

// The inner stream: a cIGZIStream whose every slot logs.
struct FakeInner {
    virtual bool QueryInterface(uint32_t id, void **out) // 0
    {
        Log({0x100, id});
        const bool yes = ((id ^ gScript.qiRule) & 3) != 0;
        if (yes) {
            *out = reinterpret_cast<void *>(uintptr_t(0x5e000000u | (id & 0xffff)));
            ++refs;
        }
        return yes;
    }
    virtual uint32_t AddRef() { Log({0x101}); return ++refs; }        // 1
    virtual uint32_t Release() { Log({0x102}); return --refs; }       // 2
    virtual uint32_t Skip(uint32_t n) { Log({0x103, n}); return gScript.skipAnswer; } // 3
#define BAD(n) virtual uint32_t Slot##n() { Log({0x1ff, n}); return 0xdead0000u | n; }
    BAD(4) BAD(5) BAD(6) BAD(7) BAD(8) BAD(9) BAD(10) BAD(11) BAD(12) BAD(13) BAD(14) BAD(15) BAD(16)
    virtual uint32_t GetVoid(void *dst, uint32_t n) // 17
    {
        Log({0x111, n});
        difftest::NotCounted bookkeeping;
        if (gScript.refuse || gScript.pos + n > gScript.data.size()) {
            // A failed read still fills its target: what cRZIStream's readers leave in a target a read did not write is
            // their own stack (or, for the packed length, the bytes of `this`), which differs between the two sides.
            std::memset(dst, 0x5a, n); // no continuation bit: see the data's runs below
            return 0x7700u; // al 0: failed
        }
        std::memcpy(dst, gScript.data.data() + gScript.pos, n);
        gScript.pos += n;
        return 0x7701u;
    }
    virtual uint32_t GetError() { Log({0x112}); return gScript.errorAnswer; } // 18
    BAD(19) BAD(20) BAD(21) BAD(22) BAD(23)
#undef BAD
    int32_t refs = 0;
};

// The owner: only Release is expected (cRZIStream's destructor), and AddRef (SetOwner may take a reference).
struct FakeOwner {
    virtual bool QueryInterface(uint32_t id, void **) { Log({0x200, id}); return false; }
    virtual uint32_t AddRef() { Log({0x201}); return ++refs; }
    virtual uint32_t Release() { Log({0x202}); return --refs; }
    int32_t refs = 0;
};

// The cIGZString GetGZStr fills: FromChar (slot 4) and Append (slot 20) logged with the text.
struct FakeString {
#define BAD(n) virtual uint32_t Slot##n() { Log({0x3ff, n}); return 0; }
    BAD(0) BAD(1) BAD(2) BAD(3)
    virtual uint32_t FromChar(const char *text) // 4
    {
        difftest::NotCounted bookkeeping;
        Log({0x304, uint32_t(std::strlen(text))});
        value = text;
        return 1;
    }
    BAD(5) BAD(6) BAD(7) BAD(8) BAD(9) BAD(10) BAD(11) BAD(12) BAD(13) BAD(14) BAD(15) BAD(16) BAD(17) BAD(18) BAD(19)
    virtual uint32_t Append(const char *text, uint32_t n) // 20
    {
        difftest::NotCounted bookkeeping;
        Log({0x314, n});
        value.append(text, n);
        return 1;
    }
    BAD(21) BAD(22) BAD(23) BAD(24) BAD(25) BAD(26) BAD(27) BAD(28) BAD(29) BAD(30) BAD(31)
#undef BAD
    std::string value;
};

FakeInner gInner;
FakeOwner gOwner;
uint8_t *gObject = nullptr;
const void *gOwnTable = nullptr;
const void *gBaseTable = nullptr;

uint32_t Tag(const void *p)
{
    if (p == nullptr) return 0;
    if (p == &gInner) return 0x1001;
    if (p == &gOwner) return 0x1002;
    if (p == gObject) return 0x1003;
    if (p == gOwnTable) return 0x1004;
    if (p == gBaseTable) return 0x1005;
    return uint32_t(uintptr_t(p)) >= 0x5e000000u && uint32_t(uintptr_t(p)) < 0x5f000000u ? uint32_t(uintptr_t(p)) : 0x1fff;
}

void LogObject()
{
    const A *a = reinterpret_cast<const A *>(gObject);
    Log({0x40, Tag(a->mVTable), a->mRefCount, a->mSkipByteSwap, Tag(a->mOwner), Tag(a->mInner),
         uint32_t(gInner.refs), uint32_t(gOwner.refs)});
}

template <typename Fn, typename... Args> uint32_t Slot(int slot, Args... args)
{
    return uint32_t(VirtualSlot<Fn>(gObject, slot)(gObject, args...));
}
using U32 = uint32_t(__thiscall *)(void *);
using U32P = uint32_t(__thiscall *)(void *, void *);
using U8P = uint8_t(__thiscall *)(void *, void *);

void LogBuffer(const uint8_t *buf, size_t n)
{
    difftest::NotCounted bookkeeping;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w;
        std::memcpy(&w, buf + i, 4);
        gLog.push_back(w);
    }
}

std::vector<uint32_t> RunSide(bool original, difftest::Context &ctx, uint32_t seed)
{
    {
        difftest::NotCounted bookkeeping;
        gLog.clear();
        gScript = Script{};
        gInner.refs = 3;
        gOwner.refs = 2;
    }
    std::mt19937 rng(seed);
    auto pick = [&](uint32_t n) { return uint32_t(rng() % n); };

    gObject = static_cast<uint8_t *>(::operator new(sizeof(A)));
    std::memset(gObject, 0xcd, sizeof(A));
    if (original) {
        ctx.Original<void *(__thiscall *)(void *)>(kBaseCtor)(gObject);
        gBaseTable = *reinterpret_cast<void **>(gObject);
        *reinterpret_cast<const void **>(gObject) = ctx.image.At(A::kVTable);
        reinterpret_cast<A *>(gObject)->mInner = &gInner;
        gInner.AddRef();
        gOwnTable = ctx.image.At(A::kVTable);
    } else {
        A *a = reinterpret_cast<A *>(gObject);
        const void *ret = a->Construct(&gInner);
        Log({0x41, ret == gObject ? 1u : 0u, a->mVTable == ctx.image.At(A::kVTable) ? 1u : 0u});
        gOwnTable = A::OwnVTable();
        gBaseTable = cRZIStream::VTable();
        a->mVTable = gOwnTable;
    }
    if (original) Log({0x41, 1u, 1u}); // the original's "constructor" is the factory's inline sequence above
    LogObject();
    Log({Slot<U32>(1)}); // the reference the factory's holder takes

    // Data the raw read serves: random bytes, with packed lengths and text the string getters read.
    {
        difftest::NotCounted bookkeeping;
        const uint32_t len = 16 + pick(400);
        gScript.data.resize(len);
        for (auto &b : gScript.data) b = uint8_t(rng());
        for (uint32_t k = 0; k < len; ++k)
            if (pick(3) == 0) gScript.data[k] = uint8_t(pick(0x50)); // short lengths are common
        // At most 1 byte in a row with the continuation bit: lengths stay under 2^14, and the original's packed-length
        // reader (an 8-byte buffer) smashes its stack on 8 or more (cRZIStream::GetPackedUint32's ORIGINAL-BUG).
        for (uint32_t k = 0, run = 0; k < len; ++k) {
            run = (gScript.data[k] & 0x80) ? run + 1 : 0;
            if (run > 1) {
                gScript.data[k] &= 0x7f;
                run = 0;
            }
        }
        gScript.qiRule = rng();
    }

    const unsigned steps = 10 + pick(40);
    for (unsigned step = 0; step < steps; ++step) {
        uint8_t buf[0x48];
        std::memset(buf, 0xee, sizeof(buf));
        uint32_t r = 0;
        const uint32_t op = pick(17);
        Log({0x20, op});
        switch (op) {
        case 0: { // QueryInterface
            static const uint32_t ids[] = {1, 0x051b11bd, A::kInnerId, 0x051b11bc, 0, 0xc56b8f09, 0x9b11bf70};
            const uint32_t id = pick(4) == 0 ? uint32_t(rng()) : ids[pick(7)];
            void *out = reinterpret_cast<void *>(uintptr_t(0x5eeeeeee));
            r = Slot<bool(__thiscall *)(void *, uint32_t, void **)>(0, id, &out) & 0xff;
            Log({r, Tag(out)});
            if (out == gObject) Slot<U32>(2); // drop the reference QueryInterface added
            break;
        }
        case 1: r = Slot<U32>(1); Log({r}); r = Slot<U32>(2); Log({r}); break;
        case 2: gScript.skipAnswer = rng(); r = Slot<uint32_t(__thiscall *)(void *, uint32_t)>(3, uint32_t(rng())); Log({r}); break;
        case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: { // the getters, slots 4..13
            const int slot = int(4 + pick(10));
            r = Slot<U8P>(slot, buf + 8) & 0xff;
            Log({uint32_t(slot), r});
            LogBuffer(buf, 24);
            break;
        }
        case 11: { // GetRZCharStr
            const uint32_t size = pick(5) == 0 ? 0 : 1 + pick(40);
            r = Slot<uint8_t(__thiscall *)(void *, char *, uint32_t)>(14, reinterpret_cast<char *>(buf + 8), size);
            Log({size, r});
            LogBuffer(buf, sizeof(buf));
            break;
        }
        case 12: { // GetGZStr
            FakeString s;
            r = Slot<uint8_t(__thiscall *)(void *, void *)>(15, &s);
            Log({r, uint32_t(s.value.size())});
            difftest::NotCounted bookkeeping;
            for (char c : s.value) gLog.push_back(uint8_t(c));
            break;
        }
        case 13: { // the raw read and GetError, through the slots
            const uint32_t n = pick(12);
            r = Slot<uint32_t(__thiscall *)(void *, void *, uint32_t)>(17, buf + 8, n);
            Log({r});
            LogBuffer(buf, 24);
            gScript.errorAnswer = rng();
            r = Slot<U32>(18);
            Log({r});
            break;
        }
        case 14: { // SetOwner / GetOwner
            void *owner = pick(3) == 0 ? nullptr : static_cast<void *>(&gOwner);
            r = Slot<U32P>(19, owner);
            Log({Tag(reinterpret_cast<void *>(uintptr_t(r)))});
            r = Slot<U32>(20);
            Log({Tag(reinterpret_cast<void *>(uintptr_t(r)))});
            break;
        }
        case 15: r = Slot<uint32_t(__thiscall *)(void *, uint32_t)>(22, pick(3)) & 0xff; Log({r}); break;
        case 16: {
            difftest::NotCounted bookkeeping;
            gScript.refuse = pick(4) == 0;
            break;
        }
        }
        if (Slot<U32>(23) & 0xff) Log({0x23});
        LogObject();
    }

    // The end: the deleting destructor directly (flags 0: the object stays; 1: freed) or through the last Release.
    const uint32_t end = pick(3);
    Log({0x30, end});
    difftest::FreeRecorder frees; // the block itself is freed only for flags & 1 (the test frees it after flags 0)
    if (end == 0) {
        const uint32_t ret = Slot<uint32_t(__thiscall *)(void *, uint32_t)>(21, 0u);
        Log({Tag(reinterpret_cast<void *>(uintptr_t(ret)))});
        LogObject();
        frees.Stop();
        Log({0x32, uint32_t(frees.Count())});
        ::operator delete(gObject);
    } else if (end == 1) {
        const uint32_t ret = Slot<uint32_t(__thiscall *)(void *, uint32_t)>(21, 1u);
        Log({Tag(reinterpret_cast<void *>(uintptr_t(ret))), uint32_t(gInner.refs), uint32_t(gOwner.refs)});
    } else {
        const uint32_t ret = Slot<U32>(2); // the reference taken after construction: the last one
        Log({ret, uint32_t(gInner.refs), uint32_t(gOwner.refs)});
    }
    frees.Stop();
    Log({0x31, uint32_t(frees.Count()), frees.Freed(gObject) ? 1u : 0u});
    gObject = nullptr;
    difftest::NotCounted bookkeeping;
    return gLog;
}

} // namespace

// The whole object: construction, every slot, the destructor.
DIFF_TEST(IStreamStringBehaviourAdaptor_Script, 0xa4d6d0)
{
    for (uint32_t seed = 0; seed < 400; ++seed) {
        ++ctx.cases;
        const std::vector<uint32_t> a = RunSide(true, ctx, seed);
        const std::vector<uint32_t> b = RunSide(false, ctx, seed);
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        if (i == a.size() && i == b.size()) continue;
        ctx.Fail("seed %u: log word %zu of %zu / %zu: original 0x%x, ours 0x%x", seed, i, a.size(), b.size(),
                 i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
        return;
    }
}

// The image's table holds, slot by slot, what ours replaces (0, 3, 21) and the forwarders folded with
// cOStreamStringBehaviourAdaptor's (17, 18); the rest are cRZIStream's.
DIFF_TEST(IStreamStringBehaviourAdaptor_VTable, 0xe6c648)
{
    struct VSlot {
        unsigned slot;
        uint32_t rva;
    };
    static const VSlot kSlots[] = {{0, 0xa4d6d0}, {1, 0x148fb0}, {2, 0x1b5fc4},  {3, 0xa4da80},  {4, 0x1b5e0f},
                                   {5, 0x1b5e0f}, {6, 0x1b5c5b}, {8, 0x1b5cca},  {10, 0x1b5da9}, {12, 0x1b5982},
                                   {13, 0x1b59c0}, {14, 0x1b5b86}, {15, 0x1b5a4d}, {16, 0x1b59ce}, {17, 0xa4d650},
                                   {18, 0xa4d640}, {19, 0x1b6617}, {20, 0x145770}, {21, 0xa4d4b0}, {22, 0x1b5f3c},
                                   {23, 0x144910}};
    void *const *table = static_cast<void *const *>(ctx.image.At(A::kVTable));
    for (const VSlot &v : kSlots) {
        ++ctx.cases;
        if (table[v.slot] != ctx.image.At(v.rva)) ctx.Fail("vtable slot %u is not 0x%x", v.slot, v.rva);
    }
}
