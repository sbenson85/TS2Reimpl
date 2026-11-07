// Differential test for cGZWinProcMgr (lane KM, src/gz/cGZWinProcMgr.h): the whole class in the standalone shape.
//
// WinProcMgr_Run: a manager built by the original constructor (0xcc6cf0, game vtables 0xeb8f40 / 0xeb8f78 / 0xeb8f94) or
// factory (0xcc70c0) against one built by ours (placement new / Create), each driven through its own vtables by the same
// random operations, then destroyed by the last Release (cRZSystemService's, reaching the +4 face's deleting destructor
// thunk 0xcc6dcd, the deleting destructor 0xcc6de0 and the destructor 0xcc6d90) or by that thunk directly.
// Operations: every cIGZWinProcMgr slot (the folded no-ops 3 / 4, GetClassObject 0xcc7130, CreateProcForDefaultWindow
// 0xcc6ed0, CreateProcForWindow 0xcc6e00, AttachProcToDefaultWindow 0xcc7030, AttachProcToWindow 0xcc6fa0, and through them
// FindWindow 0xcc71c0 and AttachProc 0xcc7260), QueryInterface 0xcc7140 on every face (the thunks 0xcc717f / 0xcc7187),
// AddRef / Release, the service accessors, Init and Shutdown 0xcc7190. Shutdown's loop runs on 0..3 keys: the map is
// filled from the test (nothing in the image inserts) and the face's vtable swapped for one whose slot 4 erases the key
// (its own slot 4 is the no-op, and a filled map would loop for ever on both sides).
//
// Every object reached is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's cell
// 0x10c9378) and its window manager service (slot 5), the COM object (the director's slot 12: GetDirectorFrameworkObject,
// slot 3 GetClassObject), windows (QueryInterface by mask, slot 7 set-procedure) and procedures. The game's operator new /
// delete (0x824632 / 0x824624) are ours, so map nodes either side allocates may be freed by the other's code. Compared
// after every operation: the result and out word, the vtable words, the service fields, the map by content, the default
// window id, the event log and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZWinProcMgr.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <vector>

namespace winprocmgr_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{3, "ddp"}, {4, "ddp"}, {5, "ddp"}, {6, "dd"}, {7, "dd"}, {8, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cGZWinProcMgr;
using KeyMap = std::map<uint32_t, WinProcRef>;

constexpr uint32_t kTag = 0x7d000000; // the manager under test

enum : unsigned { kFramework = 0, kWinMgr, kCom, kWin0, kWins = 3, kProc0 = kWin0 + kWins, kProcs = 3 };

constexpr uint32_t kOtherIid = 0x11111111, kOtherIid2 = 0x22222222;
constexpr uint32_t kWindowIds[4] = {Mgr::kDefaultWindowId, 1, 2, 0x77};
constexpr uint32_t kClassIds[4] = {0x10, 0x11, 0x12, 0x13};

std::mt19937 gRng;

struct World {
    bool framework;      // the cell 0x10c9378 holds the framework
    bool winMgr;         // the framework has the window manager
    int windowOf[4];     // kWindowIds[i] -> window fake index or -1
    uint32_t found[4];   // the window manager's answer per id (bool in the low byte, noise above)
    int procOf[4];       // kClassIds[i] -> procedure fake index or -1
};
World gWorld;

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

// The framework's slot 5: the window manager service.
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] != Mgr::kWinMgrServiceId || a[1] != Mgr::kWinMgrIid || !gWorld.winMgr) return 0x100;
    ++F(kWinMgr).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kWinMgr);
    return 0x101;
}

// The control manager's slot 26, CreateControl(type, iid, out): a control for known types.
uint32_t FindWindow(Obj *, uint32_t *a)
{
    for (unsigned i = 0; i < 4; ++i) {
        if (a[0] != kWindowIds[i]) continue;
        if (gWorld.windowOf[i] >= 0) {
            Obj &win = F(unsigned(gWorld.windowOf[i]));
            ++win.refs;
            *reinterpret_cast<void **>(uintptr_t(a[2])) = &win;
        }
        return gWorld.found[i];
    }
    return 0x300;
}

// The COM object's slot 3 (class id, iid, out): a procedure for known class ids. A 32-bit result.
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    for (unsigned i = 0; i < 4; ++i) {
        if (a[0] != kClassIds[i]) continue;
        if (gWorld.procOf[i] < 0) return 0x7000 + i;
        Obj &proc = F(unsigned(gWorld.procOf[i]));
        ++proc.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &proc;
        return 0x8000 + i;
    }
    return 0x9000;
}

// QueryInterface by mask (value[0]): bit 0 the window / procedure interface, bits 1 / 2 the other two test iids.
uint32_t Query(Obj *self, uint32_t *a)
{
    const uint32_t mine = self->id < 200 + kProc0 ? Mgr::kWinIid : Mgr::kWinProcIid;
    const uint32_t iids[3] = {mine, kOtherIid, kOtherIid2};
    for (unsigned i = 0; i < 3; ++i) {
        if (a[0] == iids[i] && (self->value[0] >> i & 1)) {
            ++self->refs;
            *reinterpret_cast<void **>(uintptr_t(a[1])) = self;
            return 0x101;
        }
    }
    return 0x100;
}

// A window's slot 7 (set procedure): accepts by value[1] (noise above the low byte).
uint32_t SetProc(Obj *self, uint32_t *a)
{
    self->value[2] = a[0]; // the procedure it holds
    return 0x400u | (self->value[1] & 1);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 5);
    svcfx::ResetAll();
    gWorld.framework = rng() % 6 != 0;
    gWorld.winMgr = rng() % 5 != 0;
    for (unsigned i = 0; i < 4; ++i) {
        gWorld.windowOf[i] = rng() % 4 ? int(kWin0 + rng() % kWins) : -1;
        gWorld.found[i] = (rng() % 3 ? 1u : 0u) | (rng() % 2 ? 0x5500u : 0u);
        gWorld.procOf[i] = rng() % 4 ? int(kProc0 + rng() % kProcs) : -1;
    }
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kWinMgr).Set(26, "ddp", 0, &FindWindow);
    F(kCom).Set(3, "ddp", 0, &GetClassObject);
    for (unsigned i = 0; i < kWins + kProcs; ++i) {
        Obj &o = F(kWin0 + i);
        o.Set(0, "dp", 0, &Query);
        o.Set(7, "p", 0, &SetProc);
        o.value[0] = rng() % 8 | (rng() % 3 ? 1u : 0u);
        o.value[1] = rng() % 3 != 0;
    }
    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kFramework) : nullptr;
}

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
KeyMap &Keys(void *object) { return *reinterpret_cast<KeyMap *>(Face(object, 0x20)); }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0xcc70c0)();
        return Face(face, 0) - 0x1c;
    }
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) ::new (memory) Mgr;
    else ctx.Original<void *(__thiscall *)(void *)>(0xcc6cf0)(memory);
    return memory;
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[3][2] = {{0, 0xeb8f40}, {4, 0xeb8f78}, {0x1c, 0xeb8f94}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? m[0x18] : Word(m, 0x18)); // the running flag; its padding is the allocator's from a factory
    const KeyMap &keys = Keys(object);
    b.push_back(uint32_t(keys.size()));
    for (const auto &e : keys) b.push_back(e.first), b.push_back(svcfx::Norm(P(e.second.Get())));
    b.push_back(Word(m, 0x28));
}

// Shutdown with keys: the face's vtable is swapped for a copy whose slot 4 erases the key (and logs it).
uint8_t *gObject;
bool __fastcall EraseKey(void *face, void *, uint32_t key)
{
    Ev(0x44000000 | key);
    KeyMap &keys = Keys(static_cast<uint8_t *>(face) - 0x1c);
    keys.erase(key);
    return true;
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    const unsigned n = 8 + rng() % 30;
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 16, rng(), rng()});
    return ops;
}

uint32_t PickIid(uint32_t a)
{
    static const uint32_t kIids[4] = {kOtherIid, kOtherIid2, Mgr::kWinIid, 0x33333333};
    return kIids[a % 4];
}
uint32_t PickWindowId(uint32_t a) { return kWindowIds[a % 4]; }
uint32_t PickClassId(uint32_t a) { return a % 9 == 0 ? 0x55 : kClassIds[a % 4]; }
void *PickProc(uint32_t a)
{
    if (a % 7 == 0) return nullptr;
    return &F(kWin0 + a % (kWins + kProcs)); // windows too (they don't answer the procedure interface)
}

// What the map holds (a fake, or null).
cIGZUnknown *PickHeld(uint32_t a)
{
    return a % 5 == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(&F(kWin0 + a % (kWins + kProcs)));
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const service = object;
    void *const face = Face(object, 0x1c);
    const uint32_t a = op.arg, c = op.arg2;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    switch (op.kind) {
    case 0:
        b.push_back(Slot<uint32_t>(face, 3 + a % 2, c) & 0xff); break;
    case 1: // GetClassObject (32-bit result)
        b.push_back(Slot<uint32_t>(face, 5, PickClassId(a), c % 3 ? 1u : kOtherIid, &out));
        b.push_back(svcfx::NormOut(P(out)));
        if (out != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e))) static_cast<cIGZUnknown *>(out)->Release();
        break;
    case 2: case 3: // CreateProcForDefaultWindow / CreateProcForWindow
        if (op.kind == 2) b.push_back(Slot<uint32_t>(face, 6, PickClassId(a), PickIid(c), &out) & 0xff);
        else b.push_back(Slot<uint32_t>(face, 7, PickClassId(a), PickWindowId(c >> 4), PickIid(c), &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    case 4: case 5: // AttachProcToDefaultWindow / AttachProcToWindow
        if (op.kind == 4) b.push_back(Slot<uint32_t>(face, 8, PickProc(a), PickIid(c), &out) & 0xff);
        else b.push_back(Slot<uint32_t>(face, 9, PickProc(a), PickWindowId(c >> 4), PickIid(c), &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    case 6: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cIGZWinProcMgr::kIid, 0x287fb697, Mgr::kServiceId, 0x12345678};
        void *faces[] = {service, Face(object, 4), face};
        void *q = out;
        const uint32_t ask = kAsk[(a >> 4) % 5];
        const bool nullOut = (a >> 7) % 4 == 0;
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, ask, nullOut ? nullptr : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 3], 2));
        if (ok && !nullOut) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 7: // the cIGZSystemService accessors and the base's counts
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 8) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<void>(service, 7, a % 2);
        if (a % 5 == 0) Slot<void>(service, 4, c);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    case 8: // Shutdown on an empty map
        if (!Keys(object).empty()) return;
        b.push_back(Slot<uint32_t>(service, 9) & 0xff);
        break;
    case 9: { // Shutdown on 0..3 keys through the swapped slot 4
        KeyMap &keys = Keys(object);
        const unsigned n = a % 4;
        for (unsigned i = 0; i < n; ++i) keys.emplace((c >> (i * 3)) % 8 + 1, WinProcRef(PickHeld(a >> (i * 4))));
        void **const saved = *reinterpret_cast<void ***>(face);
        void *table[10];
        std::memcpy(table, saved, sizeof(table));
        table[4] = reinterpret_cast<void *>(&EraseKey);
        *reinterpret_cast<void ***>(face) = table;
        b.push_back(Slot<uint32_t>(service, 9) & 0xff);
        *reinterpret_cast<void ***>(face) = saved;
        break;
    }
    case 10: { // keys left in the map (the destructor frees them)
        KeyMap &keys = Keys(object);
        keys.emplace(a % 5, WinProcRef(PickHeld(c)));
        break;
    }
    case 11: { // the world changes
        gWorld.winMgr = a % 5 != 0;
        gWorld.found[a % 4] ^= 1;
        F(kWin0 + c % (kWins + kProcs)).value[(c >> 4) % 2] ^= 1;
        break;
    }
    default: // more of the window slots
        RunOp(b, object, {2 + (op.kind - 12) % 4, a, c});
        break;
    }
}

} // namespace winprocmgr_test

using namespace winprocmgr_test;

DIFF_TEST(WinProcMgr_Run, 0xcc6cf0)
{
    // Also reached (see the file comment): 0xcc70c0 (factory), 0xcc6d90, 0xcc6de0, 0xcc6dcd, 0xcc7140, 0xcc717f, 0xcc7187,
    // 0xcc7190, 0xcc7130, 0xcc6ed0, 0xcc6e00, 0xcc7030, 0xcc6fa0, 0xcc71c0, 0xcc7260.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    svcfx::gFrameworkIndex = kCom;

    bool failed = false;
    for (uint32_t seed = 1; seed <= 3000 && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            gObject = static_cast<uint8_t *>(object);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it; every fifth seed the +4 face's deleting destructor (flags 1) instead.
                    if (seed % 5) b.push_back(Slot<uint32_t>(object, 2));
                    else b.push_back(svcfx::Norm(Slot<uint32_t>(Face(object, 4), 3, 1u)));
                    gObject = nullptr;
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "WinProcMgr_Run", seed)) {
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                failed = true;
                break;
            }
        }
    }
    svcfx::gFrameworkIndex = 0;
}
