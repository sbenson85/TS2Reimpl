// Differential test for cTSUIServiceManager (lane KM, src/ui/cTSUIServiceManager.h): the whole class in the standalone shape.
//
// UIServiceMgr_Run: a manager built by the original constructor (0x73f21e, game vtables 0xdf5d24 / 0xdf5d5c / 0xdf5d78 /
// 0xdf5db8) or factory (0x73f8af) against one built by ours (placement new / Create), each driven through its own vtables
// by the same random operations, then destroyed by the last Release (cRZSystemService's, reaching the +4 face's deleting
// destructor thunk 0x73f3f7, the deleting destructor 0x73f3ff and the destructor 0x73f2d8) or by that thunk directly.
// Operations: Init 0x73f8f2, Shutdown 0x740697, OnTick 0x73fa1d, DoMessage 0x73f524 (through the +0x20 face: the init
// message with InitService 0x73f41e for the eleven services, the shutdown message with 0x744844, 0xd03ffba0, others, a
// message refusing the cIGZMessage2 query, null), AddTickService 0x73f4b2, RemoveTickService 0x73fa94, the ten folded
// getters, QueryInterface 0x73fa3f on every face (the thunks 0x73fa7c / 0x73fa84 / 0x73fa8c), AddRef / Release and the
// service accessors.
//
// Every object reached is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's cell 0x10c9378:
// GetSystemService, AddToTick, RemoveFromTick) and the services it hands out (the window manager, the message server the
// message walkers 0x1c0458 / 0x1c081e query, the shutdown message's 0xa417445e service and the object its slot 3 returns,
// with slots 5 and 85), the COM object (the director's slot 12; GetClassObject answering with a noisy high byte, so the
// low-byte test is checked), the UI services (QueryInterface by mask, initialise / shut down / tick), the cell 0x10de488's
// object, and the messages. The message server is also the cache cell 0x10f519c (GetCachedMessageServer 0x9c5a89 runs).
// The posted message is a real cRZMessage2Standard (0x1c033d on RZMemory, so the game allocator is built for the test):
// the post logs its contents. Stood in on both sides (CodePatch), each logging its arguments: the UI set-up steps 0x7408f4,
// 0x7406c1, 0x740c99, 0x7436b1, 0x73ffae and 0x740422 (original UI code, not part of this class's test). The game's
// operator new / delete (0x824632 / 0x824624) are ours, so nodes either side allocates may be freed by the other's code.
// Compared after every operation: the result, the vtable words, the service fields, the tick list, the held services and
// the message types by content, the event log and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "ui/cTSUIServiceManager.h"

#include <cstdio>
#include <cstring>
#include <list>
#include <new>
#include <random>
#include <set>
#include <vector>

namespace uisvcmgr_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{6, "dd"}, {7, "dd"}, {8, "dd"}, {9, "dd"}, {10, "dd"}, {11, "dd"}, {12, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cTSUIServiceManager;

constexpr uint32_t kTag = 0x7d000000; // the manager under test

enum : unsigned {
    kFramework = 0,
    kWinMgr,
    kServer,
    kQuitSvc,  // the 0xa417445e service of the shutdown message
    kCom,
    kCellObj,  // the cell 0x10de488's object
    kMsg,      // the message handed to DoMessage
    kSvc0,     // UI services
    kSvcs = 6,
};

constexpr uint32_t kOtherIid = 0x11111111;

struct World {
    bool framework;     // the cell 0x10c9378 holds the framework
    bool winMgr;        // the framework has the window manager
    bool quitSvc;       // ... and the 0xa417445e service
    bool bigObject;     // that service's slot 3 answers the object
    bool cell;          // the cell 0x10de488 holds an object
    int svcOf[Mgr::kServices]; // the COM object's answer per class id (fake index or -1)
    uint32_t comNoise;  // the high bytes of GetClassObject's result
};
World gWorld;

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

// The object the shutdown message's service hands out: slots 5 and 85 (beyond the fakes' 80 slots).
struct BigObject {
    void **vt;
};
void *gBigTable[90];
BigObject gBig{gBigTable};
uint32_t __fastcall BigSlot5(BigObject *, void *) { Ev(0x55000005); return 0; }
uint32_t __fastcall BigSlot85(BigObject *, void *) { Ev(0x55000085); return 0; }

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Obj *answer = nullptr;
    if (a[0] == 0xc2c2eb0f && a[1] == 0x22c2eb1f) answer = gWorld.winMgr ? &F(kWinMgr) : nullptr;
    else if (a[0] == 0xa417445e && a[1] == 0x5a4) answer = gWorld.quitSvc ? &F(kQuitSvc) : nullptr;
    else answer = &F(kServer); // the message server the walkers ask for
    if (!answer) return 0x100;
    ++answer->refs;
    *out = answer;
    return 0x101;
}
uint32_t QuitSvcObject(Obj *, uint32_t *) { return gWorld.bigObject ? P(&gBig) : 0; }

uint32_t GetClassObject(Obj *, uint32_t *a)
{
    for (unsigned i = 0; i < Mgr::kServices; ++i) {
        if (a[0] != Mgr::kServiceIds[i].classId) continue;
        if (gWorld.svcOf[i] < 0) return gWorld.comNoise;
        Obj &svc = F(unsigned(gWorld.svcOf[i]));
        ++svc.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &svc;
        return gWorld.comNoise | 1;
    }
    return 0x9000;
}

// A UI service's QueryInterface by mask (value[0]): bit i answers the i-th held service's interface, bit 11 kOtherIid.
uint32_t SvcQuery(Obj *self, uint32_t *a)
{
    for (unsigned i = 0; i <= Mgr::kServices; ++i) {
        const uint32_t iid = i < Mgr::kServices ? Mgr::kServiceIds[i].iid : kOtherIid;
        if (a[0] == iid && (self->value[0] >> i & 1)) {
            ++self->refs;
            *reinterpret_cast<void **>(uintptr_t(a[1])) = self;
            return 0x101;
        }
    }
    return 0x100;
}
uint32_t SvcInit(Obj *self, uint32_t *) { return 0x700u | (self->value[1] & 1); }

// The message: answers cIGZMessage2 by value[1], its type in value[0].
uint32_t MsgQuery(Obj *self, uint32_t *a)
{
    if (a[0] != Mgr::kMessage2Iid || !self->value[1]) return 0x200;
    ++self->refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self;
    return 0x201;
}
uint32_t MsgType(Obj *self, uint32_t *) { return self->value[0]; }

// The server's post (slot 4: message, flag): the message's contents (a real cRZMessage2Standard).
uint32_t Post(Obj *, uint32_t *a)
{
    svcfx::LogMessage(a[0]);
    return 0x301;
}

// The UI set-up steps stood in on both sides.
void __fastcall Stand7408f4(void *self, void *) { Ev(0x56000001), Ev(svcfx::Norm(P(self))); }
void __fastcall Stand7406c1(void *self, void *, void *winMgr) { Ev(0x56000002), Ev(svcfx::Norm(P(self))), Ev(svcfx::Norm(P(winMgr))); }
void __fastcall Stand740c99(void *self, void *) { Ev(0x56000003), Ev(svcfx::Norm(P(self))); }
void __cdecl Stand7436b1() { Ev(0x56000004); }
void __fastcall Stand73ffae(void *self, void *) { Ev(0x56000005), Ev(svcfx::Norm(P(self))); }
void __fastcall Stand740422(void *self, void *) { Ev(0x56000006), Ev(svcfx::Norm(P(self))); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 9);
    svcfx::ResetAll();
    gWorld.framework = rng() % 12 != 0;
    gWorld.winMgr = rng() % 4 != 0;
    gWorld.quitSvc = rng() % 4 != 0;
    gWorld.bigObject = rng() % 3 != 0;
    gWorld.cell = rng() % 2 != 0;
    gWorld.comNoise = rng() % 2 ? 0x5500u : 0u;
    for (int &s : gWorld.svcOf) s = rng() % 5 ? int(kSvc0 + rng() % kSvcs) : -1;
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kFramework).Set(9, "p", 0x801);
    F(kFramework).Set(10, "p", 0x802);
    F(kServer).Set(4, "p-", 0, &Post);
    F(kServer).Set(5, "pd", 0x501);
    F(kServer).Set(6, "pd", 0x601);
    F(kQuitSvc).Set(3, 0, 0, &QuitSvcObject);
    F(kCom).Set(3, "ddp", 0, &GetClassObject);
    F(kCellObj).Set(5, 0, 0x55);
    F(kMsg).Set(0, "dp", 0, &MsgQuery);
    F(kMsg).Set(4, 0, 0, &MsgType);
    for (unsigned i = 0; i < kSvcs; ++i) {
        Obj &s = F(kSvc0 + i);
        s.Set(0, "dp", 0, &SvcQuery);
        s.Set(3, 0, 0, &SvcInit);
        s.Set(4, 0, 0x404);
        s.Set(5, "d", 0x505);
        s.value[0] = rng() & 0xfff;
        s.value[1] = rng() % 4 != 0;
    }
    gBigTable[5] = reinterpret_cast<void *>(&BigSlot5);
    gBigTable[85] = reinterpret_cast<void *>(&BigSlot85);
    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kFramework) : nullptr;
    *GameAddress<void *>(svcfx::kMessageServerCell) = &F(kServer);
    if (gWorld.cell) ++F(kCellObj).refs;
    *GameAddress<void *>(0x10de488) = gWorld.cell ? &F(kCellObj) : nullptr;
}

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x73f8af)();
        return Face(face, 0) - 0x1c;
    }
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) ::new (memory) Mgr;
    else ctx.Original<void *(__thiscall *)(void *)>(0x73f21e)(memory);
    return memory;
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[4][2] = {{0, 0xdf5d24}, {4, 0xdf5d5c}, {0x1c, 0xdf5d78}, {0x20, 0xdf5db8}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? m[0x18] : Word(m, 0x18)); // the running flag; its padding is the allocator's from a factory
    const auto &list = *reinterpret_cast<const std::list<void *> *>(m + 0x24); // (the counted pointers' words)
    b.push_back(uint32_t(list.size()));
    for (void *p : list) b.push_back(svcfx::Norm(P(p)));
    for (uint32_t off = 0x2c; off < 0x58; off += 4) b.push_back(svcfx::Norm(Word(m, off)));
    const auto &set = *reinterpret_cast<const std::set<uint32_t> *>(m + 0x58);
    b.push_back(uint32_t(set.size()));
    for (uint32_t v : set) b.push_back(v);
    b.push_back(svcfx::Norm(*GameAddress<uint32_t>(0x10de488)));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 11);
    std::vector<Op> ops;
    const unsigned n = 8 + rng() % 30;
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 12, rng(), rng()});
    return ops;
}

uint32_t PickType(uint32_t a)
{
    static const uint32_t kTypes[6] = {Mgr::kMsgAppInit, Mgr::kMsgAppShutdown, Mgr::kMsgD03ffba0, 0xc7, 0xca, 0x8d504495};
    return kTypes[a % 6];
}
cIGZUnknown *PickSvc(uint32_t a) { return reinterpret_cast<cIGZUnknown *>(&F(kSvc0 + a % kSvcs)); }

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const service = object;
    void *const face = Face(object, 0x1c);
    void *const target = Face(object, 0x20);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: // Init (needs the framework: the original calls it unchecked)
        if (!gWorld.framework) return;
        b.push_back(Slot<uint32_t>(service, 8) & 0xff);
        break;
    case 1: // Shutdown (the same)
        if (!gWorld.framework) return;
        b.push_back(Slot<uint32_t>(service, 9) & 0xff);
        break;
    case 2: b.push_back(Slot<uint32_t>(service, 10, c) & 0xff); break;
    case 3: case 4: { // DoMessage
        Obj &msg = F(kMsg);
        msg.value[0] = PickType(a);
        msg.value[1] = (a >> 4) % 6 != 0;
        const bool null = (a >> 8) % 11 == 0;
        b.push_back(Slot<uint32_t>(target, 3, null ? nullptr : static_cast<void *>(&msg)) & 0xff);
        break;
    }
    case 5: b.push_back(Slot<uint32_t>(face, 13, PickSvc(a)) & 0xff); break;
    case 6: b.push_back(Slot<uint32_t>(face, 14, a % 9 == 0 ? nullptr : PickSvc(a)) & 0xff); break;
    case 7: // the getters
        for (unsigned s = 3; s <= 12; ++s) b.push_back(svcfx::Norm(Slot<uint32_t>(face, s)));
        break;
    case 8: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cITSUIServiceManager::kIid, 0x287fb697, Mgr::kServiceId, 0x12345678};
        void *faces[] = {service, Face(object, 4), face, target};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ask = kAsk[(a >> 4) % 5];
        const bool nullOut = (a >> 7) % 4 == 0;
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, ask, nullOut ? nullptr : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok && !nullOut) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 9: // the cIGZSystemService accessors and the base's counts
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<void>(service, 7, a % 2);
        if (a % 5 == 0) Slot<void>(service, 4, c);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    case 10: { // the world changes
        gWorld.winMgr = a % 4 != 0;
        gWorld.quitSvc = (a >> 2) % 4 != 0;
        gWorld.bigObject = (a >> 4) % 3 != 0;
        gWorld.svcOf[c % Mgr::kServices] = (c >> 4) % 5 ? int(kSvc0 + (c >> 8) % kSvcs) : -1;
        F(kSvc0 + (c >> 12) % kSvcs).value[1] ^= 1;
        break;
    }
    default: // the init message again (the most work)
        F(kMsg).value[0] = Mgr::kMsgAppInit;
        F(kMsg).value[1] = 1;
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMsg))) & 0xff);
        break;
    }
}

} // namespace uisvcmgr_test

using namespace uisvcmgr_test;

DIFF_TEST(UIServiceMgr_Run, 0x73f21e)
{
    // Also reached (see the file comment): 0x73f8af (factory), 0x73f2d8, 0x73f3ff, 0x73f3f7, 0x73fa3f, 0x73fa7c, 0x73fa84,
    // 0x73fa8c, 0x73f8f2, 0x740697, 0x73fa1d, 0x73f524, 0x73f41e, 0x744844, 0x73f4b2, 0x73fa94.
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    globals.SetWord(svcfx::kMessageServerCell, 0);
    globals.SetWord(0x10de488, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch s1(0x7408f4, shape::CodeOf(&Stand7408f4));
    shape::CodePatch s2(0x7406c1, shape::CodeOf(&Stand7406c1));
    shape::CodePatch s3(0x740c99, shape::CodeOf(&Stand740c99));
    shape::CodePatch s4(0x7436b1, shape::CodeOf(&Stand7436b1));
    shape::CodePatch s5(0x73ffae, shape::CodeOf(&Stand73ffae));
    shape::CodePatch s6(0x740422, shape::CodeOf(&Stand740422));
    svcfx::gFrameworkIndex = kCom;

    bool failed = false;
    for (uint32_t seed = 1; seed <= 3000 && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it; every fifth seed the +4 face's deleting destructor (flags 1) instead.
                    if (seed % 5) b.push_back(Slot<uint32_t>(object, 2));
                    else b.push_back(svcfx::Norm(Slot<uint32_t>(Face(object, 4), 3, 1u)));
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "UIServiceMgr_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                failed = true;
                break;
            }
        }
    }
    svcfx::gFrameworkIndex = 0;
}
