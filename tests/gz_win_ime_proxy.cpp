// Differential test for cGZWinIMEProxy (lane KN, src/gz/cGZWinIMEProxy.h): the whole class in the standalone shape.
//
// IMEProxy_Run: a proxy built by the original constructor (0x96ca9b, game vtables 0xe5a3cc / 0xe5a40c / 0xe5a420 /
// 0xe5a458) on 0xc5 memory or by the original factory (0x92f6aa) against one built by ours (placement new / Create), each
// driven through its own vtables by the same random operations, then destroyed by the last Release (0x96dcc4 -> Destroy
// 0x96d02c: Shutdown, then the +0xc face's deleting destructor thunk 0x96cd06 -> 0x96cd5a -> the destructor 0x96cc19) or
// by that thunk directly.
// Operations: Init 0x96d8a4 and Shutdown 0x96eb5b on the proxy face or through the service face's thunks 0x96d9c1 /
// 0x96ec3d (and through them GetIMEServer 0x96c8ee); OnTick 0x96da43 runs of 1..9 ticks (every fourth tick checks the
// focus); DoMessage 0x96cf71 (+4 face) with the four IME messages and others, reaching OpenCompositionWindow 0x96ed48,
// OpenCandidateWindow 0x96ec7b, CloseCompositionWindow 0x96cff3, CloseCandidateWindow 0x96cfba and LoadFonts 0x96e5d8;
// every getter / setter slot (EnableCompositionWindow 0x96e16a, EnableCandidateWindow 0x96de80 with raw bytes, the folded
// getters and the font setters); QueryInterface 0x96dbe7 / AddRef 0x96cdd1 / Release on every face (the thunks 0x96dc3b,
// 0x96dc43, 0x96dc4b, 0x96cdd9, 0x96cde1, 0x96cde9, 0x96dcd9, 0x96dce1, 0x96dce9); the service accessors; the
// never-written +0x40 byte set by the test (both sides) so OnTick's early return is reached.
//
// Every object reached is a recording fake (tests/ts_services_fixture.h): the framework (cell 0x10c9378; slot 5 the
// services, 9 / 10 AddToTick / RemoveFromTick), the COM object (the director's slot 12; slot 3 GetClassObject), the
// message server (slot 3 the caret query, which a handler answers; 5 / 6 the notifications), the IME server, the window
// root service (slot 3 the main window, 21 remove, 37 the focus), the font service and window manager, focus windows
// answering QueryInterface by mask, the IME window objects and their cIGZWin (slot 83 SetFlag through a longer table).
// The game's window constructors 0x96ca20 / 0x96c9c9 are stood in on both sides (a fake window back; the memory freed).
// The game's operator new / delete (0x824632 / 0x824624) are ours. Situations where the original dereferences null
// (opening or closing a window without the root service, the caret query without a message server) are kept out of the
// world: the test puts the service there first.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZWinIMEProxy.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace imeproxy_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{9, "dd"}, {10, "dd"}, {11, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Proxy = cGZWinIMEProxy;

constexpr uint32_t kTag = 0x7d000000; // the proxy under test

enum : unsigned {
    kFramework = 0,
    kCom,
    kMsgServer,
    kIMEServer,
    kWinRoot,
    kMainWindow,
    kFontSvc,
    kWinMgr,
    kFontWindow,
    kFocus0, // 3 focus windows
    kCompObj = kFocus0 + 3,
    kCandObj,
    kBuiltComp,
    kBuiltCand,
    kCompWin,
    kCandWin,
    kFont0, // 2 fonts the font service hands out
    kUserFont = kFont0 + 2,
    kFakeCount
};
static_assert(kFakeCount <= svcfx::kFakes);

enum : unsigned { svMsg, svIME, svRoot, svFont, svWinMgr, svCount };
constexpr uint32_t kSvcIds[svCount][3] = {
    {Proxy::kMessageServerId, Proxy::kMessageServerIid, kMsgServer},
    {Proxy::kIMEServerId, cIGZWinIMEServer::kIid, kIMEServer},
    {Proxy::kWinRootServiceId, Proxy::kWinRootServiceIid, kWinRoot},
    {Proxy::kFontServiceId, Proxy::kFontServiceIid, kFontSvc},
    {Proxy::kWinMgrServiceId, Proxy::kWinMgrIid, kWinMgr},
};

struct World {
    bool framework;
    bool service[svCount];
    int focus;               // kFocus0 + i or -1
    bool comHas[2];          // the COM object has the composition / candidate window class
    bool caretAnswer;        // a handler answers the caret query
    int32_t caretX, caretY;
    bool fontWindow;         // the window manager has window 0x68963c4c
    unsigned fontsLeft;      // fonts the font service still hands out (then null)
};
World gWorld;

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }
void PutOut(uint32_t outArg, Obj &o)
{
    ++o.refs;
    *reinterpret_cast<void **>(uintptr_t(outArg)) = &o;
}

// The framework's slot 5 (service id, iid, out).
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    for (unsigned s = 0; s < svCount; ++s) {
        if (a[0] != kSvcIds[s][0] || a[1] != kSvcIds[s][1] || !gWorld.service[s]) continue;
        PutOut(a[2], F(kSvcIds[s][2]));
        return 0x101;
    }
    return 0x100;
}

// The COM object's slot 3 (class id, iid, out): the IME window objects; the low byte answers (noise above).
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    const unsigned which = a[0] == Proxy::kCompositionClass ? 0 : a[0] == Proxy::kCandidateClass ? 1 : 2;
    if (which == 2 || a[1] != (which ? Proxy::kCandidateIid : Proxy::kCompositionIid) || !gWorld.comHas[which])
        return 0x7700;
    PutOut(a[2], F(which ? kCandObj : kCompObj));
    return 0x7701;
}

// QueryInterface by mask (value[0]): bits 0..2 the three iids OnTick asks for.
uint32_t Query(Obj *self, uint32_t *a)
{
    static const uint32_t kIids[3] = {Proxy::kTextFocusIid, Proxy::kCompositionIid, Proxy::kCandidateIid};
    for (unsigned i = 0; i < 3; ++i) {
        if (a[0] == kIids[i] && (self->value[0] >> i & 1)) {
            PutOut(a[1], *self);
            return 0x201;
        }
    }
    return 0x200 | (self->value[1] & 0xfe00); // false in the low byte
}

// The message server's slot 3: the caret query, answered by the world.
uint32_t SendMessage(Obj *, uint32_t *a)
{
    auto *msg = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(msg[i]);
    if (msg[0] == Proxy::kCaretMessage && gWorld.caretAnswer) {
        msg[1] = 1;
        msg[2] = uint32_t(gWorld.caretX);
        msg[3] = uint32_t(gWorld.caretY);
    }
    return 0x301;
}

uint32_t RootFocus(Obj *, uint32_t *) { return gWorld.focus >= 0 ? P(&F(unsigned(gWorld.focus))) : 0; }
uint32_t FontWindow(Obj *, uint32_t *a) { return a[0] == Proxy::kFontWindowId && gWorld.fontWindow ? P(&F(kFontWindow)) : 0; }
uint32_t MakeFont(Obj *, uint32_t *)
{
    if (!gWorld.fontsLeft) return 0;
    Obj &font = F(kFont0 + (--gWorld.fontsLeft) % 2);
    ++font.refs;
    return P(&font);
}

// cIGZWin's slot 83 (SetFlag), past the fixture's 80 slots: the window fakes get a longer table.
uint32_t __fastcall SetFlag(Obj *self, void *, uint32_t mask, uint32_t value)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(83), Ev(2), Ev(mask), Ev(value);
    return 0x55;
}
void *gWinTables[2][96];

// The game's window constructors (0x96ca20 composition, 0x96c9c9 candidate): a fake window back, the memory freed.
void *__fastcall BuildComposition(void *memory, void *)
{
    Ev(0x7c000000);
    ::operator delete(memory);
    return &F(kBuiltComp);
}
void *__fastcall BuildCandidate(void *memory, void *)
{
    Ev(0x7c000001);
    ::operator delete(memory);
    return &F(kBuiltCand);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void RandomWorld(std::mt19937 &rng)
{
    gWorld.framework = rng() % 8 != 0;
    for (bool &s : gWorld.service) s = rng() % 5 != 0;
    gWorld.focus = rng() % 4 ? int(kFocus0 + rng() % 3) : -1;
    gWorld.comHas[0] = rng() % 2 != 0;
    gWorld.comHas[1] = rng() % 2 != 0;
    gWorld.caretAnswer = rng() % 2 != 0;
    gWorld.caretX = int32_t(rng() % 3 ? rng() % 800 : 0x7fffffff);
    gWorld.caretY = int32_t(rng() % 600);
    gWorld.fontWindow = rng() % 3 != 0;
    gWorld.fontsLeft = rng() % 4;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11);
    svcfx::ResetAll();
    RandomWorld(rng);
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kFramework).Set(9, "p", 0x401);
    F(kFramework).Set(10, "p", 0x402);
    F(kCom).Set(3, "ddp", 0, &GetClassObject);
    F(kMsgServer).Set(3, "p", 0, &SendMessage);
    F(kMsgServer).Set(5, "pd", 0x501);
    F(kMsgServer).Set(6, "pd", 0x601);
    // The IME server: IsAvailable (5), SetEnabled (7), IsOpen (8), SetOpen (9); answers in the low byte, noise above.
    F(kIMEServer).Set(5, 0, (rng() % 2) | 0x3300);
    F(kIMEServer).Set(7, "d", 0x700);
    F(kIMEServer).Set(8, 0, (rng() % 2) | 0x8800);
    F(kIMEServer).Set(9, 1, 0x901);
    F(kWinRoot).Set(3, 0, &F(kMainWindow));
    F(kWinRoot).Set(21, "p", 0x2100);
    F(kWinRoot).Set(37, 0, 0, &RootFocus);
    F(kMainWindow).Set(19, "p", 0x1900);
    F(kFontSvc).Set(11, "p-d-d", 0, &MakeFont);
    F(kWinMgr).Set(5, "d", 0, &FontWindow);
    F(kFontWindow).Set(5, "p", 0x500);
    for (unsigned i = 0; i < 3; ++i) {
        Obj &w = F(kFocus0 + i);
        w.Set(0, "dp", 0, &Query);
        w.value[0] = rng() % 8;
        w.value[1] = rng();
    }
    // The IME window objects: slot 3 their cIGZWin, slot 7 the font.
    for (unsigned i : {unsigned(kCompObj), unsigned(kBuiltComp)}) F(i).Set(3, 0, &F(kCompWin)), F(i).Set(7, "p", 0x700);
    for (unsigned i : {unsigned(kCandObj), unsigned(kBuiltCand)}) F(i).Set(3, 0, &F(kCandWin)), F(i).Set(7, "p", 0x700);
    for (unsigned w = 0; w < 2; ++w) {
        Obj &win = F(w ? kCandWin : kCompWin);
        win.Set(28, 0, 0x2800);
        win.Set(66, "dd", 0x6600);
        std::memset(gWinTables[w], 0, sizeof(gWinTables[w]));
        std::memcpy(gWinTables[w], win.table, sizeof(win.table));
        gWinTables[w][83] = reinterpret_cast<void *>(&SetFlag);
        win.vt = gWinTables[w];
    }
    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kFramework) : nullptr;
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
    if (gFactory) return ours ? static_cast<void *>(Proxy::Create()) : ctx.Original<void *(__cdecl *)()>(0x92f6aa)();
    void *memory = ::operator new(Proxy::kSize);
    std::memset(memory, 0xc5, Proxy::kSize);
    if (ours) ::new (memory) Proxy;
    else ctx.Original<void *(__thiscall *)(void *)>(0x96ca9b)(memory);
    return memory;
}

// The object's words as Dump logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] = "-----ddd dddddpdd dpppdppd pp";
void Dump(Blob &b, void *object, bool ours)
{

    const auto *m = static_cast<const uint8_t *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[4][2] = {{0, 0xe5a3cc}, {4, 0xe5a40c}, {8, 0xe5a420}, {0xc, 0xe5a458}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    // Every other word; the byte fields' padding only on 0xc5 memory (from a factory it is the allocator's).
    for (uint32_t off = 0x10; off < Proxy::kSize; off += 4) {
        const bool byteField = off == 0x20 || off == 0x2c || off == 0x40 || off == 0x50 || off == 0x5c;
        if (byteField && gFactory) b.push_back(m[off]);
        else svcfx::Field(b, Word(m, off), kObjectKinds, off);
    }
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    const unsigned n = 10 + rng() % 40;
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 14, rng(), rng()});
    return ops;
}

void *gObject;

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const proxy = object;
    void *const target = Face(object, 4);
    void *const service = Face(object, 8);
    void *const mt = Face(object, 0xc);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: // Init on the proxy face or the service face's thunk
        b.push_back(Slot<uint32_t>(a % 2 ? proxy : service, a % 2 ? 3 : 8) & 0xff);
        break;
    case 1: // Shutdown
        b.push_back(Slot<uint32_t>(a % 2 ? proxy : service, a % 2 ? 4 : 9) & 0xff);
        break;
    case 2: case 3: { // OnTick runs (the caret query needs the message server once it is reached)
        const unsigned n = 1 + a % 9;
        for (unsigned i = 0; i < n; ++i) {
            const uint8_t *m = static_cast<uint8_t *>(object);
            if (!Word(m, 0x34) && Word(m, 0x44) && Word(m, 0x48)) return;
            b.push_back(Slot<uint32_t>(service, 10, c + i) & 0xff);
        }
        break;
    }
    case 4: case 5: { // DoMessage (the window paths need the framework and the root service)
        static const uint32_t kIds[6] = {Proxy::kMsgOpenComposition, Proxy::kMsgCloseComposition,
                                         Proxy::kMsgOpenCandidates, Proxy::kMsgCloseCandidates, 0x2ac44c0f, 0};
        gWorld.framework = true;
        gWorld.service[svRoot] = true;
        *GameAddress<void *>(0x10c9378) = &F(kFramework);
        cGZMessage msg = {kIds[a % 6], c, &F(kUserFont)};
        b.push_back(Slot<uint32_t>(target, 3, &msg) & 0xff);
        break;
    }
    case 6: { // the getters and setters
        const unsigned slot = 5 + a % 10;
        cIGZUnknown *font = c % 3 ? reinterpret_cast<cIGZUnknown *>(&F(c % 3 == 1 ? kUserFont : kFont0)) : nullptr;
        const uint32_t byte = (c >> 4) % 3 == 0 ? 0 : (c >> 4) % 3 == 1 ? 1 : 0x80; // raw bytes kept
        switch (slot) {
        case 6: case 11: Slot<void>(proxy, slot, byte | (c & 0xff00)); break;
        case 9: case 14: b.push_back(svcfx::Norm(Slot<uint32_t>(proxy, slot, font))); break;
        case 5: case 10: b.push_back(Slot<uint32_t>(proxy, slot) & 0xff); break;
        default: b.push_back(svcfx::Norm(Slot<uint32_t>(proxy, slot))); break;
        }
        break;
    }
    case 7: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cIGZWinIMEProxy::kIid, Proxy::kMessageTargetIid, 0x287fb697, Proxy::kServiceId,
                                        0x12345678};
        void *faces[] = {proxy, target, service, mt};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kAsk[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 8: // the cIGZSystemService accessors and the MT part's counts
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<void>(service, 7, a % 2);
        if (a % 5 == 0) Slot<void>(service, 4, c);
        b.push_back(Slot<uint32_t>(mt, 5));
        break;
    case 9: case 10: { // the world changes
        std::mt19937 rng(a);
        RandomWorld(rng);
        F(kIMEServer).ret[5] = (c & 1) | 0x3300;
        F(kIMEServer).ret[8] = (c >> 1 & 1) | 0x8800;
        F(kFocus0 + c % 3).value[0] = (c >> 4) % 8;
        *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kFramework) : nullptr;
        break;
    }
    case 11: // the never-written +0x40 byte (OnTick returns early while it is set)
        static_cast<uint8_t *>(object)[0x40] = a % 3 == 0;
        break;
    default: // more ticks
        RunOp(b, object, {2, a, c});
        break;
    }
}

} // namespace imeproxy_test

using namespace imeproxy_test;

DIFF_TEST(IMEProxy_Run, 0x96ca9b)
{
    // Also reached (see the file comment): 0x92f6aa (factory), 0x96cc19, 0x96cd5a, 0x96cd06, 0x96dbe7, 0x96dc3b, 0x96dc43,
    // 0x96dc4b, 0x96cdd1, 0x96cdd9, 0x96cde1, 0x96cde9, 0x96dcc4, 0x96dcd9, 0x96dce1, 0x96dce9, 0x96d02c, 0x96d8a4,
    // 0x96d9c1, 0x96eb5b, 0x96ec3d, 0x96c8ee, 0x96da43, 0x96cf71, 0x96ed48, 0x96ec7b, 0x96cff3, 0x96cfba, 0x96e5d8,
    // 0x96e16a, 0x96de80.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch compCtor(0x96ca20, shape::CodeOf(&BuildComposition));
    shape::CodePatch candCtor(0x96c9c9, shape::CodeOf(&BuildCandidate));
    svcfx::gFrameworkIndex = kCom;

    bool failed = false;
    for (uint32_t seed = 1; seed <= 3000 && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            gObject = object;
            svcfx::AddRange(object, Proxy::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it (Shutdown first); every fifth seed the +0xc face's deleting destructor.
                    gWorld.service[svMsg] = true; // (Shutdown's notifications are logged either way)
                    if (seed % 5) b.push_back(Slot<uint32_t>(Face(object, (seed >> 3) % 4 * 4), 2));
                    else b.push_back(svcfx::Norm(Slot<uint32_t>(Face(object, 0xc), 3, 1u)));
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
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "IMEProxy_Run", seed)) {
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                failed = true;
                break;
            }
        }
    }
    svcfx::gFrameworkIndex = 0;
}
