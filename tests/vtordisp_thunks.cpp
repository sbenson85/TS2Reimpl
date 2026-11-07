// Differential tests for the vtordisp thunks (generated from tools/vtordisp.py output, see src/sg/VtordispThunks.h).
// Each thunk is `sub ecx,[ecx-4]; sub ecx,N; jmp body`. The first four bodies (enclosed fields, sign bit, forward to
// slot 5) run on their own small buffers, as before. The bodies on the cSGResource object (0xa3c3c0 to 0xa3cdd0,
// 0xa3c4b0, 0xa3c750, 0xa431c0) and the cSGSecondary bodies run on the shared fixture below: one object, its
// peers and their fakes (each logs its calls), and a vtordisp (-16 to 16) stored at this - 4. The original thunk runs
// on the first copy of the object and our forwarder on the second; the return (al-only where the body returns a byte),
// the object bytes, the out buffer of CopyKeyTo and the call log must match.
#include "difftest.h"
#include "sg/VtordispThunks.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

struct Case {
    uint32_t rva; // the thunk
    uint32_t n;   // its adjustment
    void *replacement;
};

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// Offset of the body's object in the buffers. It is at least 0xfc (the ForwardToSlot5 window) above the start.
constexpr uint32_t kBase = 0x100;

std::vector<uint8_t> *gSide = nullptr; // the buffer the fake was called on
uint32_t gCalls = 0;
uint32_t gSelf = 0;
uint32_t gValue = 0;

uint32_t __fastcall FakeSlot5(void *self, void *)
{
    ++gCalls;
    gSelf = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) - reinterpret_cast<uintptr_t>(gSide->data()));
    return gValue;
}

void *gVTable[6] = {};

// Body 0x9e8850: RZEnclosedFields::GetDwordMinus12 (dword at this-12).
const Case kEnclosedDwordThunk[] = {
    {0x2bba83, 0x134, Addr(&VtordispThunks::EnclosedDwordThunk<0x134>::Call)},
    {0x2ea386, 0x90, Addr(&VtordispThunks::EnclosedDwordThunk<0x90>::Call)},
    {0x2efc23, 0x158, Addr(&VtordispThunks::EnclosedDwordThunk<0x158>::Call)},
    {0x321c27, 0x13c, Addr(&VtordispThunks::EnclosedDwordThunk<0x13c>::Call)},
    {0x326029, 0x120, Addr(&VtordispThunks::EnclosedDwordThunk<0x120>::Call)},
    {0x3326bb, 0x114, Addr(&VtordispThunks::EnclosedDwordThunk<0x114>::Call)},
    {0x33ee03, 0x178, Addr(&VtordispThunks::EnclosedDwordThunk<0x178>::Call)},
    {0x349570, 0x1ac, Addr(&VtordispThunks::EnclosedDwordThunk<0x1ac>::Call)},
    {0x34d5ff, 0x110, Addr(&VtordispThunks::EnclosedDwordThunk<0x110>::Call)},
    {0x350027, 0x10c, Addr(&VtordispThunks::EnclosedDwordThunk<0x10c>::Call)},
    {0x353dd7, 0x12c, Addr(&VtordispThunks::EnclosedDwordThunk<0x12c>::Call)},
    {0x3a5fe7, 0xb0, Addr(&VtordispThunks::EnclosedDwordThunk<0xb0>::Call)},
    {0x3bad11, 0x124, Addr(&VtordispThunks::EnclosedDwordThunk<0x124>::Call)},
    {0x9d3006, 0x128, Addr(&VtordispThunks::EnclosedDwordThunk<0x128>::Call)},
    {0x9ecb36, 0xfc, Addr(&VtordispThunks::EnclosedDwordThunk<0xfc>::Call)},
    {0x9ee4e6, 0x140, Addr(&VtordispThunks::EnclosedDwordThunk<0x140>::Call)},
    {0xa101c4, 0x94, Addr(&VtordispThunks::EnclosedDwordThunk<0x94>::Call)},
    {0xa27256, 0xdc, Addr(&VtordispThunks::EnclosedDwordThunk<0xdc>::Call)},
    {0xa432d7, 0xec, Addr(&VtordispThunks::EnclosedDwordThunk<0xec>::Call)},
    {0xa69052, 0xe0, Addr(&VtordispThunks::EnclosedDwordThunk<0xe0>::Call)},
    {0xcc472f, 0xa8, Addr(&VtordispThunks::EnclosedDwordThunk<0xa8>::Call)},
};

// Body 0x9d9570: RZEnclosedFields::GetByteMinus8 (byte at this-8, al only).
const Case kEnclosedByteThunk[] = {
    {0x2ef119, 0x158, Addr(&VtordispThunks::EnclosedByteThunk<0x158>::Call)},
    {0x3216b0, 0x13c, Addr(&VtordispThunks::EnclosedByteThunk<0x13c>::Call)},
    {0x325d20, 0x120, Addr(&VtordispThunks::EnclosedByteThunk<0x120>::Call)},
    {0x332036, 0x114, Addr(&VtordispThunks::EnclosedByteThunk<0x114>::Call)},
    {0x33d576, 0x178, Addr(&VtordispThunks::EnclosedByteThunk<0x178>::Call)},
    {0x3479a6, 0x1ac, Addr(&VtordispThunks::EnclosedByteThunk<0x1ac>::Call)},
    {0x34d0a6, 0x110, Addr(&VtordispThunks::EnclosedByteThunk<0x110>::Call)},
    {0x34fc96, 0x10c, Addr(&VtordispThunks::EnclosedByteThunk<0x10c>::Call)},
    {0x353bb2, 0x12c, Addr(&VtordispThunks::EnclosedByteThunk<0x12c>::Call)},
    {0x3a575e, 0xb0, Addr(&VtordispThunks::EnclosedByteThunk<0xb0>::Call)},
    {0x3bac61, 0x124, Addr(&VtordispThunks::EnclosedByteThunk<0x124>::Call)},
    {0xa0f4ef, 0x94, Addr(&VtordispThunks::EnclosedByteThunk<0x94>::Call)},
    {0xa427a9, 0xec, Addr(&VtordispThunks::EnclosedByteThunk<0xec>::Call)},
    {0xa6618d, 0xe0, Addr(&VtordispThunks::EnclosedByteThunk<0xe0>::Call)},
    {0xcc4356, 0xa8, Addr(&VtordispThunks::EnclosedByteThunk<0xa8>::Call)},
};

// Body 0xa3bdb0: cSignBitBeforeThis::SignBit (bit 31 of the dword at this-8).
const Case kSignBitThunk[] = {
    {0x2bbb58, 0x140, Addr(&VtordispThunks::SignBitThunk<0x140>::Call)},
    {0x2ea58a, 0x9c, Addr(&VtordispThunks::SignBitThunk<0x9c>::Call)},
    {0x3b69ec, 0x13c, Addr(&VtordispThunks::SignBitThunk<0x13c>::Call)},
    {0x9d30f3, 0x134, Addr(&VtordispThunks::SignBitThunk<0x134>::Call)},
    {0x9ecc02, 0x108, Addr(&VtordispThunks::SignBitThunk<0x108>::Call)},
    {0x9ed582, 0x100, Addr(&VtordispThunks::SignBitThunk<0x100>::Call)},
    {0x9ee7f5, 0x14c, Addr(&VtordispThunks::SignBitThunk<0x14c>::Call)},
    {0xa2728e, 0xe8, Addr(&VtordispThunks::SignBitThunk<0xe8>::Call)},
    {0xa80042, 0x1c0, Addr(&VtordispThunks::SignBitThunk<0x1c0>::Call)},
};

// Body 0x9529e5: cGZWinMsgBoxAt252::ForwardToSlot5 (object at this-0xfc).
const Case kForwardToSlot5Thunk[] = {
    {0xc15b85, 0xa4, Addr(&VtordispThunks::ForwardToSlot5Thunk<0xa4>::Call)},
    {0xc167eb, 0xb0, Addr(&VtordispThunks::ForwardToSlot5Thunk<0xb0>::Call)},
    {0xc20f18, 0x90, Addr(&VtordispThunks::ForwardToSlot5Thunk<0x90>::Call)},
    {0xc22958, 0xbc, Addr(&VtordispThunks::ForwardToSlot5Thunk<0xbc>::Call)},
    {0xc23117, 0x8c, Addr(&VtordispThunks::ForwardToSlot5Thunk<0x8c>::Call)},
    {0xc243a2, 0x190, Addr(&VtordispThunks::ForwardToSlot5Thunk<0x190>::Call)},
};

// Runs one thunk on random buffers. `window` puts a vtable with the fake slot 5 at object - 0xfc.
template <typename R> void RunCase(difftest::Context &ctx, std::mt19937 &rng, const Case &c, bool window, int trials)
{
    using Fn = R(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(c.rva);
    auto ours = reinterpret_cast<Fn>(c.replacement);
    gVTable[5] = reinterpret_cast<void *>(&FakeSlot5);
    // vtordisp values are signed 32-bit: negative ones catch a narrower or unsigned read.
    static const int32_t kDisplacements[] = {0, 4, 8, 16, -4, -8, -16};
    const uint32_t size = kBase + c.n + 0x40;
    for (int i = 0; i < trials; ++i) {
        std::vector<uint8_t> a(size);
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        const uint32_t d = static_cast<uint32_t>(kDisplacements[rng() % 7]);
        const uint32_t thisOff = kBase + c.n + d;
        std::memcpy(&a[thisOff - 4], &d, sizeof(d));
        if (window) {
            void *vt = gVTable;
            std::memcpy(&a[kBase - 0xfc], &vt, sizeof(vt));
        }
        std::vector<uint8_t> b = a;
        gValue = static_cast<uint32_t>(rng());

        gSide = &a;
        gCalls = 0;
        gSelf = 0xffffffffu;
        const R ra = orig(a.data() + thisOff);
        const uint32_t callsA = gCalls, selfA = gSelf;

        gSide = &b;
        gCalls = 0;
        gSelf = 0xffffffffu;
        const R rb = ours(b.data() + thisOff);
        const uint32_t callsB = gCalls, selfB = gSelf;

        ++ctx.cases;
        if (ra != rb) ctx.Fail("thunk 0x%x (adj 0x%x): result differs", c.rva, c.n);
        if (callsA != callsB || selfA != selfB) ctx.Fail("thunk 0x%x (adj 0x%x): slot calls differ", c.rva, c.n);
        if (a != b) ctx.Fail("thunk 0x%x (adj 0x%x): buffer differs", c.rva, c.n);
    }
}

} // namespace

DIFF_TEST(VtordispThunks_EnclosedDword, 0x9e8850)
{
    std::mt19937 rng(0x7e01);
    for (const Case &c : kEnclosedDwordThunk) RunCase<uint32_t>(ctx, rng, c, false, 2000);
}

DIFF_TEST(VtordispThunks_EnclosedByte, 0x9d9570)
{
    std::mt19937 rng(0x7e02);
    for (const Case &c : kEnclosedByteThunk) RunCase<uint8_t>(ctx, rng, c, false, 2000);
}

DIFF_TEST(VtordispThunks_SignBit, 0xa3bdb0)
{
    std::mt19937 rng(0x7e03);
    for (const Case &c : kSignBitThunk) RunCase<uint32_t>(ctx, rng, c, false, 2000);
}

DIFF_TEST(VtordispThunks_ForwardToSlot5, 0x9529e5)
{
    std::mt19937 rng(0x7e04);
    for (const Case &c : kForwardToSlot5Thunk) RunCase<uint32_t>(ctx, rng, c, true, 2000);
}

// ---- Shared fixture for the cSGResource and cSGSecondary bodies ----
//
// The object is gObj (primary vtable at +0, vbptr at +0x1c, a ServiceCell dword at +0x20, the resource at +0x24,
// flag A at +0x28, flag B at +0x29). The cSGResource subobject S is gObj + 0x30, the body's this for the subobject
// bodies. cGeometryNodeSecondary8 (0xa431c0) is at gObj - 0x0c, so its add ecx,0x3c reaches S. The virtual base sits
// at gObj + 0x7c (vbptr + 0x60). A thunk with adjustment N and vtordisp d is entered with this = body's this + N + d,
// the dword d stored at this - 4. Every fake logs its call. Both sides of a trial start from the same bytes (the seed
// in Cfg), so the pointers in the logs match. The fakes follow the bodies' own tests in sg_anim_resource.cpp,
// sg_resource_helpers.cpp and static_getters2.cpp.
namespace {

constexpr uint32_t kServiceGlobal = 0x10c9378;     // GetServiceObject's service (game global)
constexpr uint32_t kCacheObjectGlobal = 0x10f9070; // GetCachedObjectB's cache (game global)
constexpr uint32_t kKeyServiceGlobal = 0x10f8f2c;  // ParseKey's cached service object (game global)
constexpr int kVbDisp = 0x60;                       // the virtual base at vbptr + 0x60 = gObj + 0x7c
constexpr int kBodyS = 0x30;                        // body this (the cSGResource subobject S), from gObj
constexpr int kBodyGeometry = -0x0c;                // body this (cGeometryNodeSecondary8), from gObj

constexpr uint32_t kArenaSize = 0x500;
alignas(16) uint8_t gArena[kArenaSize];
uint8_t *const gObj = gArena + 0x40; // the pad before it leaves room for the cGeometryNode prefix (-0x0c)

enum ThrowAt : int {
    kNoThrow,
    kThrowBase4,
    kThrowKey,
    kThrowP8,
    kThrowP9,
    kThrowP10,
    kThrowH10,
    kThrowH9,
    kThrowBase15,
    kThrowBase17,
    kThrowP4,
    kThrowCache5,
    kThrowCache6Open,
    kThrowCache6Assign,
    kThrowH14,
};
constexpr int kThrowLast = kThrowH14;

enum PeerKind : int { kPeerNull, kPeerArg, kPeerBase, kPeerRes };

struct Ev {
    int slot;
    const void *self;
    const void *p1;
    const void *p2;
    uint32_t value;
    uint32_t words[4];
};
std::vector<Ev> gLog;
uint32_t gEscapes = 0; // runs (either side) whose call threw out of the body

Ev Make(int slot, const void *self, const void *p1 = nullptr, const void *p2 = nullptr, uint32_t value = 0,
        const void *key = nullptr)
{
    Ev e{};
    e.slot = slot;
    e.self = self;
    e.p1 = p1;
    e.p2 = p2;
    e.value = value;
    if (key) std::memcpy(e.words, key, sizeof(e.words));
    return e;
}

const uint32_t kGoodKey[4] = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u}; // written by primary slot 3
const char *const kKeyTexts[] = {"Alpha!Beta", "*!Gamma", "#1f!Delta", "##12!Eps", "plain text", "!Zeta", "Name!"};
uint32_t gOut[4];                                      // CopyKeyTo's out buffer, compared after every run

struct FakeObject {
    void *vtable;
    uint32_t pad;
};
FakeObject gArgPeer{}, gResPeer{}, gSvc{}, gH1{}, gH2{}, gX{}, gCache{}, gKeyObj{};
void *gPeerVt[24], *gPrimVt[24], *gBaseVt[24], *gSvcVt[24], *gH1Vt[24], *gH2Vt[24], *gXVt[24], *gCacheVt[24],
    *gKeyVt[24];
uint32_t gVbTable[2];

// Scripted answers, set per run by Setup.
bool gQIOk[2];
int gQIOut[2];
uint32_t gQIGarbage = 0;
uint32_t gQICount = 0;
uint32_t gRet21 = 0;
uint32_t gBaseAnswer = 0;
void *gBaseOut = nullptr; // what the virtual base's slot 4 writes to its out pointer
uint32_t gRBlock[4] = {}; // what slot 4 returns (gRBlock[1] is read at R + 4)
uint32_t gH9Answer = 0;
uint32_t gH14Answer = 0;
uint32_t gCache5Answer = 0;
void *gSvcHolders[2] = {};
uint32_t gSvcCalls = 0;
uint32_t gCache6Calls = 0;
int gKeyIndex = 0;
int gThrowAt = kNoThrow;

void *PeerFor(int kind)
{
    switch (kind) {
    case kPeerArg: return &gArgPeer;
    case kPeerBase: return gObj + 0x7c;
    case kPeerRes: return &gResPeer;
    default: return nullptr;
    }
}

// Pointer argument from a 32-bit value (or null).
void *Ptr(uint32_t v, bool isNull)
{
    return isNull ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(v));
}

// The fakes are __fastcall with a dummy edx, so the stack arguments are popped by the callee like the original's.
uint32_t __fastcall FakeQI(void *self, void *, const void *iid, void **out)
{
    gLog.push_back(Make(0, self, nullptr, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(iid))));
    const uint32_t i = gQICount++ & 1;
    *out = PeerFor(gQIOut[i]);
    return (gQIOk[i] ? 1u : 0u) | gQIGarbage;
}

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gLog.push_back(Make(1, self));
    return 0x1234;
}

uint32_t __fastcall FakeRelease(void *self, void *)
{
    gLog.push_back(Make(2, self));
    return 0x5678;
}

uint32_t __fastcall FakeSlot21(void *self, void *, void *key)
{
    gLog.push_back(Make(21, self, nullptr, nullptr, 0, key));
    return gRet21;
}

void *__fastcall PrimKey(void *self, void *, void *out)
{
    std::memcpy(out, kGoodKey, sizeof(kGoodKey));
    gLog.push_back(Make(3, self, nullptr, nullptr, 0, out));
    if (gThrowAt == kThrowKey) throw 42;
    return out;
}

void __fastcall PrimP4(void *self, void *, void *out)
{
    gLog.push_back(Make(4, self, nullptr, nullptr, 0, out));
    if (gThrowAt == kThrowP4) throw 42;
}

void __fastcall PrimP8(void *self, void *, uint32_t a)
{
    gLog.push_back(Make(8, self, nullptr, nullptr, a));
    if (gThrowAt == kThrowP8) throw 42;
}

void __fastcall PrimP9(void *self, void *, void *a)
{
    gLog.push_back(Make(9, self, nullptr, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))));
    if (gThrowAt == kThrowP9) throw 42;
}

void __fastcall PrimP10(void *self, void *, void *a)
{
    gLog.push_back(Make(10, self, nullptr, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))));
    if (gThrowAt == kThrowP10) throw 42;
}

void *__fastcall BaseSlot4(void *self, void *, void **out)
{
    *out = gBaseOut;
    gLog.push_back(Make(4, self, nullptr, nullptr, gBaseOut != nullptr));
    if (gThrowAt == kThrowBase4) throw 42;
    return gRBlock;
}

void __fastcall BaseSlot15(void *self, void *, void *a)
{
    gLog.push_back(Make(15, self, a));
    if (gThrowAt == kThrowBase15) throw 42;
}

void *__fastcall BaseSlot17(void *self, void *)
{
    gLog.push_back(Make(17, self, nullptr, nullptr, static_cast<uint32_t>(gKeyIndex)));
    if (gThrowAt == kThrowBase17) throw 42;
    return const_cast<char *>(kKeyTexts[gKeyIndex]);
}

uint32_t __fastcall BaseSlot23(void *self, void *)
{
    gLog.push_back(Make(23, self, nullptr, nullptr, gBaseAnswer));
    return gBaseAnswer; // only al matters to the caller; the upper bits are noise on purpose
}

void __fastcall SvcSlot5(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Ev e = Make(5, self, nullptr, nullptr, a);
    e.words[0] = b;
    e.words[1] = gSvcCalls;
    gLog.push_back(e);
    *out = gSvcCalls < 2 ? gSvcHolders[gSvcCalls] : nullptr;
    ++gSvcCalls;
}

void __fastcall HolderSlot10(void *self, void *, void *key)
{
    gLog.push_back(Make(10, self, nullptr, nullptr, 0, key));
    if (gThrowAt == kThrowH10) throw 42;
}

uint32_t __fastcall HolderSlot9(void *self, void *, void *key, void *object)
{
    gLog.push_back(Make(9, self, object, nullptr, gH9Answer, key));
    if (gThrowAt == kThrowH9) throw 42;
    return gH9Answer;
}

uint32_t __fastcall HolderSlot14(void *self, void *, void *object, void *key, void *resource)
{
    gLog.push_back(Make(14, self, object, resource, gH14Answer, key));
    if (gThrowAt == kThrowH14) throw 42;
    return gH14Answer;
}

uint32_t __fastcall CacheSlot5(void *self, void *, uint32_t a, uint32_t b)
{
    Ev e = Make(5, self, nullptr, nullptr, gCache5Answer);
    e.words[0] = a;
    e.words[1] = b;
    gLog.push_back(e);
    if (gThrowAt == kThrowCache5) throw 42;
    return gCache5Answer;
}

uint32_t __fastcall CacheSlot6(void *self, void *, uint32_t a)
{
    ++gCache6Calls;
    gLog.push_back(Make(6, self, nullptr, nullptr, a));
    if ((gThrowAt == kThrowCache6Open && gCache6Calls == 1) || (gThrowAt == kThrowCache6Assign && gCache6Calls == 2))
        throw 42;
    return 0x66;
}

void __fastcall CacheSlot7(void *self, void *, uint32_t value)
{
    gLog.push_back(Make(7, self, nullptr, nullptr, value));
}

// Slot 8 of GetCachedObjectB's object: the Dispatch8 call (object, dword); the answer depends on the argument.
uint32_t __fastcall CacheSlot8(void *self, void *, uint32_t a)
{
    gLog.push_back(Make(8, self, nullptr, nullptr, a));
    return a ^ 0x5a5a5a5au;
}

uint32_t __fastcall KeyLookup(void *self, void *, const char *name)
{
    uint32_t h = 0; // the hash of the name (ParseKey's copy of the name is at a heap address)
    for (const char *p = name; *p; ++p) h = h * 31u + static_cast<uint8_t>(*p);
    gLog.push_back(Make(0, self, nullptr, nullptr, h));
    return h;
}

void InitFakes()
{
    for (void **vt : {gPeerVt, gPrimVt, gBaseVt, gSvcVt, gH1Vt, gH2Vt, gXVt, gCacheVt, gKeyVt})
        std::memset(vt, 0, sizeof(gPeerVt));
    gPeerVt[0] = reinterpret_cast<void *>(&FakeQI);
    gPeerVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gPeerVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gPeerVt[21] = reinterpret_cast<void *>(&FakeSlot21);
    gPrimVt[3] = reinterpret_cast<void *>(&PrimKey);
    gPrimVt[4] = reinterpret_cast<void *>(&PrimP4);
    gPrimVt[8] = reinterpret_cast<void *>(&PrimP8);
    gPrimVt[9] = reinterpret_cast<void *>(&PrimP9);
    gPrimVt[10] = reinterpret_cast<void *>(&PrimP10);
    gBaseVt[0] = reinterpret_cast<void *>(&FakeQI);
    gBaseVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gBaseVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gBaseVt[4] = reinterpret_cast<void *>(&BaseSlot4);
    gBaseVt[15] = reinterpret_cast<void *>(&BaseSlot15);
    gBaseVt[17] = reinterpret_cast<void *>(&BaseSlot17);
    gBaseVt[21] = reinterpret_cast<void *>(&FakeSlot21);
    gBaseVt[23] = reinterpret_cast<void *>(&BaseSlot23);
    gSvcVt[5] = reinterpret_cast<void *>(&SvcSlot5);
    gH1Vt[2] = reinterpret_cast<void *>(&FakeRelease);
    gH1Vt[10] = reinterpret_cast<void *>(&HolderSlot10);
    gH1Vt[14] = reinterpret_cast<void *>(&HolderSlot14);
    gH2Vt[2] = reinterpret_cast<void *>(&FakeRelease);
    gH2Vt[9] = reinterpret_cast<void *>(&HolderSlot9);
    gH2Vt[14] = reinterpret_cast<void *>(&HolderSlot14);
    gXVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gCacheVt[5] = reinterpret_cast<void *>(&CacheSlot5);
    gCacheVt[6] = reinterpret_cast<void *>(&CacheSlot6);
    gCacheVt[7] = reinterpret_cast<void *>(&CacheSlot7);
    gCacheVt[8] = reinterpret_cast<void *>(&CacheSlot8);
    gKeyVt[0] = reinterpret_cast<void *>(&KeyLookup);
    gArgPeer.vtable = gPeerVt;
    gResPeer.vtable = gPeerVt;
    gSvc.vtable = gSvcVt;
    gH1.vtable = gH1Vt;
    gH2.vtable = gH2Vt;
    gX.vtable = gXVt;
    gCache.vtable = gCacheVt;
    gKeyObj.vtable = gKeyVt;
}

// One trial's state, all drawn from the trial's rng so that both sides get the same setup.
struct Cfg {
    uint32_t seed;     // fills the object's bytes
    int32_t n;         // the thunk's adjustment
    int32_t disp;      // the vtordisp
    int32_t bodyDelta; // the body's this, from gObj
    uint32_t cell;     // the ServiceCell dword at +0x20
    uint8_t flagA;     // at +0x28
    uint8_t flagB;     // at +0x29
    int resKind;       // the resource at +0x24 (PeerKind)
    int argKind;       // the argument peer (PeerKind, see each test)
    int newKind;       // SetResource's argument
    int throwAt;       // ThrowAt
    int keyIndex;      // kKeyTexts index
    uint32_t cache5;   // the cache's slot 5 answer
    uint32_t baseAnswer;
    uint32_t rval;     // the dword at R + 4
    uint32_t h9;       // holder 2's slot 9 answer
    uint32_t h14;      // the holders' slot 14 answer
    uint32_t ret21;    // the resource's slot 21 answer
    uint32_t a1;       // uint32 argument (SetFlag's value, the ServiceCell value)
    uint32_t a2, a3;   // pointer arguments, as values
    bool a2Null, a3Null;
    bool xSet;         // the virtual base's slot 4 gives an out object
    bool qiOk[2];
    int qiOut[2];      // PeerKind of QueryInterface's out pointer
    uint32_t garbage;  // bits above al in QueryInterface's answer
};

Cfg MakeCfg(std::mt19937 &rng, uint32_t n, int bodyDelta, bool allowThrow)
{
    static const int32_t kDisps[] = {0, 4, 8, 16, -4, -8, -16};
    Cfg c{};
    c.seed = rng();
    c.n = static_cast<int32_t>(n);
    c.bodyDelta = bodyDelta;
    c.disp = kDisps[rng() % 7];
    c.cell = rng() % 3 == 0 ? rng() : (rng() % 2 ? 0xffffffffu : 7u);
    c.flagA = rng() % 3 == 0 ? 0x80 : static_cast<uint8_t>(rng() % 2);
    c.flagB = static_cast<uint8_t>(rng());
    c.resKind = static_cast<int>(rng() % 4);
    c.argKind = static_cast<int>(rng() % 4);
    c.newKind = static_cast<int>(rng() % 4);
    c.throwAt = allowThrow && rng() % 3 == 0 ? static_cast<int>(rng() % (kThrowLast + 1)) : kNoThrow;
    c.keyIndex = static_cast<int>(rng() % std::size(kKeyTexts));
    c.cache5 = rng();
    c.baseAnswer = rng();
    c.rval = rng();
    c.h9 = rng();
    c.h14 = rng();
    c.ret21 = rng();
    c.a1 = rng();
    c.a2 = rng();
    c.a3 = rng();
    c.a2Null = rng() % 4 == 0;
    c.a3Null = rng() % 4 == 0;
    c.xSet = rng() % 2 != 0;
    for (int k = 0; k < 2; ++k) {
        c.qiOk[k] = rng() % 4 != 0;
        c.qiOut[k] = static_cast<int>(rng() % 4);
    }
    c.garbage = rng() % 2 ? 0xffffff00u : 0u;
    return c;
}

// The thunk's entry this: the body's this plus the adjustment and the vtordisp.
uint8_t *ThisOf(const Cfg &c)
{
    return gObj + (c.bodyDelta + c.n + c.disp);
}

void Setup(difftest::Context &ctx, const Cfg &c)
{
    std::mt19937 fill(c.seed);
    for (auto &b : gArena) b = static_cast<uint8_t>(fill());
    *reinterpret_cast<void **>(gObj) = gPrimVt;
    gVbTable[0] = 0;
    gVbTable[1] = kVbDisp;
    *reinterpret_cast<void **>(gObj + 0x1c) = gVbTable;
    *reinterpret_cast<void **>(gObj + 0x7c) = gBaseVt;
    *reinterpret_cast<uint32_t *>(gObj + 0x20) = c.cell;
    *reinterpret_cast<void **>(gObj + 0x24) = PeerFor(c.resKind);
    gObj[0x28] = c.flagA;
    gObj[0x29] = c.flagB;
    std::memcpy(ThisOf(c) - 4, &c.disp, sizeof(c.disp));

    gBaseAnswer = c.baseAnswer;
    gBaseOut = c.xSet ? static_cast<void *>(&gX) : nullptr;
    gRBlock[0] = 0;
    gRBlock[1] = c.rval;
    gRBlock[2] = 0;
    gRBlock[3] = 0;
    gH9Answer = c.h9;
    gH14Answer = c.h14;
    gCache5Answer = c.cache5;
    gRet21 = c.ret21;
    for (int k = 0; k < 2; ++k) {
        gQIOk[k] = c.qiOk[k];
        gQIOut[k] = c.qiOut[k];
    }
    gQIGarbage = c.garbage;
    gQICount = 0;
    gSvcHolders[0] = c.flagA ? static_cast<void *>(&gH1) : static_cast<void *>(&gH2);
    gSvcHolders[1] = &gH2;
    gSvcCalls = 0;
    gCache6Calls = 0;
    gKeyIndex = c.keyIndex;
    gThrowAt = c.throwAt;
    std::memset(gOut, 0xA5, sizeof(gOut));
    gLog.clear();
    *ctx.image.At<void *>(kServiceGlobal) = &gSvc;
    *ctx.image.At<void *>(kCacheObjectGlobal) = &gCache;
    *ctx.image.At<void *>(kKeyServiceGlobal) = &gKeyObj;
}

struct Shot {
    uint64_t ret = 0;
    bool escaped = false;
    std::vector<uint8_t> arena;
    std::vector<uint8_t> out;
    std::vector<Ev> log;
};

template <typename Call> Shot Capture(difftest::Context &ctx, const Cfg &c, Call call)
{
    Setup(ctx, c);
    Shot s;
    try {
        s.ret = call(ThisOf(c));
    } catch (int) {
        s.escaped = true; // only the throwing cases expect this
        ++gEscapes;
    }
    s.arena.assign(gArena, gArena + sizeof(gArena));
    const uint8_t *out = reinterpret_cast<const uint8_t *>(gOut);
    s.out.assign(out, out + sizeof(gOut));
    s.log = gLog;
    return s;
}

bool SameLog(const std::vector<Ev> &a, const std::vector<Ev> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Ev &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.p1 != y.p1 || x.p2 != y.p2 || x.value != y.value ||
            std::memcmp(x.words, y.words, sizeof(x.words)) != 0)
            return false;
    }
    return true;
}

void Compare(difftest::Context &ctx, const char *name, uint32_t rva, const Cfg &c, const Shot &a, const Shot &b)
{
    ++ctx.cases;
    if (a.ret != b.ret || a.escaped != b.escaped) {
        ctx.Fail("%s thunk 0x%x (adj 0x%x, vtordisp %d): result original %llx (escaped %d), ours %llx (escaped %d)",
                 name, rva, c.n, c.disp, static_cast<unsigned long long>(a.ret), a.escaped,
                 static_cast<unsigned long long>(b.ret), b.escaped);
    }
    if (a.arena != b.arena) ctx.Fail("%s thunk 0x%x (adj 0x%x, vtordisp %d): object bytes differ", name, rva, c.n, c.disp);
    if (a.out != b.out) ctx.Fail("%s thunk 0x%x (adj 0x%x, vtordisp %d): out buffer differs", name, rva, c.n, c.disp);
    if (!SameLog(a.log, b.log)) {
        ctx.Fail("%s thunk 0x%x (adj 0x%x, vtordisp %d): calls differ (%zu original, %zu ours)", name, rva, c.n, c.disp,
                 a.log.size(), b.log.size());
    }
}

struct SgCase {
    uint32_t rva; // the thunk
    uint32_t n;   // its adjustment
    void *replacement;
};

// Runs every thunk of a table `trials` times on random states. Call(fn, this, cfg) returns the result as 64 bits.
template <typename Fn, size_t Count, typename Call>
void RunThunks(difftest::Context &ctx, const char *name, const SgCase (&cases)[Count], std::mt19937 &rng, int bodyDelta,
               int trials, bool allowThrow, Call call)
{
    for (const SgCase &t : cases) {
        Fn orig = ctx.Original<Fn>(t.rva);
        Fn ours = reinterpret_cast<Fn>(t.replacement);
        for (int k = 0; k < trials; ++k) {
            const Cfg c = MakeCfg(rng, t.n, bodyDelta, allowThrow);
            Shot a = Capture(ctx, c, [&](void *self) { return call(orig, self, c); });
            Shot b = Capture(ctx, c, [&](void *self) { return call(ours, self, c); });
            Compare(ctx, name, t.rva, c, a, b);
        }
    }
}

// ---- Tables: each thunk, its adjustment and the forwarder (rva, n from db/status/VtordispThunks.jsonl) ----

// Body 0xa3c3c0, cSGResourceSubobject::GetOwner (14 thunks).
const SgCase kSubobjectOwnerThunks[] = {
    {0x2ecf78, 0x158, Addr(&VtordispThunks::SubobjectOwnerThunk<0x158>::Call)},
    {0x31e986, 0x13c, Addr(&VtordispThunks::SubobjectOwnerThunk<0x13c>::Call)},
    {0x323a21, 0x120, Addr(&VtordispThunks::SubobjectOwnerThunk<0x120>::Call)},
    {0x3315df, 0x114, Addr(&VtordispThunks::SubobjectOwnerThunk<0x114>::Call)},
    {0x339232, 0x178, Addr(&VtordispThunks::SubobjectOwnerThunk<0x178>::Call)},
    {0x34685a, 0x1ac, Addr(&VtordispThunks::SubobjectOwnerThunk<0x1ac>::Call)},
    {0x34c849, 0x110, Addr(&VtordispThunks::SubobjectOwnerThunk<0x110>::Call)},
    {0x34f59d, 0x10c, Addr(&VtordispThunks::SubobjectOwnerThunk<0x10c>::Call)},
    {0x3523b6, 0x12c, Addr(&VtordispThunks::SubobjectOwnerThunk<0x12c>::Call)},
    {0x3a4f2c, 0xb0, Addr(&VtordispThunks::SubobjectOwnerThunk<0xb0>::Call)},
    {0x3b9ac1, 0x124, Addr(&VtordispThunks::SubobjectOwnerThunk<0x124>::Call)},
    {0xa3e86a, 0xec, Addr(&VtordispThunks::SubobjectOwnerThunk<0xec>::Call)},
    {0xa64c99, 0xe0, Addr(&VtordispThunks::SubobjectOwnerThunk<0xe0>::Call)},
    {0xcc42c5, 0xa8, Addr(&VtordispThunks::SubobjectOwnerThunk<0xa8>::Call)},
};

// Body 0xa3c3d0, cSGResourceSubobject::CompareIdentity (14 thunks).
const SgCase kSubobjectCompareIdentityThunks[] = {
    {0x2ef3e8, 0x158, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x158>::Call)},
    {0x3218cf, 0x13c, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x13c>::Call)},
    {0x325f0d, 0x120, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x120>::Call)},
    {0x33221d, 0x114, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x114>::Call)},
    {0x33e024, 0x178, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x178>::Call)},
    {0x348157, 0x1ac, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x1ac>::Call)},
    {0x34d277, 0x110, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x110>::Call)},
    {0x34fefc, 0x10c, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x10c>::Call)},
    {0x353cac, 0x12c, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x12c>::Call)},
    {0x3a58a8, 0xb0, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0xb0>::Call)},
    {0x3bac6f, 0x124, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0x124>::Call)},
    {0xa427b7, 0xec, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0xec>::Call)},
    {0xa686b4, 0xe0, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0xe0>::Call)},
    {0xcc4379, 0xa8, Addr(&VtordispThunks::SubobjectCompareIdentityThunk<0xa8>::Call)},
};

// Body 0xa3c4a0, cSGResourceSubobject::GetFlag (14 thunks).
const SgCase kSubobjectGetFlagThunks[] = {
    {0x2ef3f6, 0x158, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x158>::Call)},
    {0x3218dd, 0x13c, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x13c>::Call)},
    {0x325f1b, 0x120, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x120>::Call)},
    {0x33222b, 0x114, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x114>::Call)},
    {0x33e032, 0x178, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x178>::Call)},
    {0x348165, 0x1ac, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x1ac>::Call)},
    {0x34d285, 0x110, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x110>::Call)},
    {0x34ff0a, 0x10c, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x10c>::Call)},
    {0x353cba, 0x12c, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x12c>::Call)},
    {0x3a58cc, 0xb0, Addr(&VtordispThunks::SubobjectGetFlagThunk<0xb0>::Call)},
    {0x3bac7d, 0x124, Addr(&VtordispThunks::SubobjectGetFlagThunk<0x124>::Call)},
    {0xa427c5, 0xec, Addr(&VtordispThunks::SubobjectGetFlagThunk<0xec>::Call)},
    {0xa686e8, 0xe0, Addr(&VtordispThunks::SubobjectGetFlagThunk<0xe0>::Call)},
    {0xcc4387, 0xa8, Addr(&VtordispThunks::SubobjectGetFlagThunk<0xa8>::Call)},
};

// Body 0xa3c6d0, cSGResourceSubobject::ForwardKey (14 thunks).
const SgCase kSubobjectForwardKeyThunks[] = {
    {0x2ef901, 0x158, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x158>::Call)},
    {0x321bd3, 0x13c, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x13c>::Call)},
    {0x325fe3, 0x120, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x120>::Call)},
    {0x3322d3, 0x114, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x114>::Call)},
    {0x33e793, 0x178, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x178>::Call)},
    {0x3482b3, 0x1ac, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x1ac>::Call)},
    {0x34d353, 0x110, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x110>::Call)},
    {0x34ffd3, 0x10c, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x10c>::Call)},
    {0x353d83, 0x12c, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x12c>::Call)},
    {0x3a5ce9, 0xb0, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0xb0>::Call)},
    {0x3baccb, 0x124, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0x124>::Call)},
    {0xa42f0f, 0xec, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0xec>::Call)},
    {0xa68d95, 0xe0, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0xe0>::Call)},
    {0xcc46d0, 0xa8, Addr(&VtordispThunks::SubobjectForwardKeyThunk<0xa8>::Call)},
};

// Body 0xa3c720, cSGResourceSubobject::GetVirtualBase (14 thunks).
const SgCase kSubobjectGetVirtualBaseThunks[] = {
    {0x2efbf9, 0x158, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x158>::Call)},
    {0x321bfd, 0x13c, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x13c>::Call)},
    {0x325fff, 0x120, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x120>::Call)},
    {0x332691, 0x114, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x114>::Call)},
    {0x33edd9, 0x178, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x178>::Call)},
    {0x349546, 0x1ac, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x1ac>::Call)},
    {0x34d5d5, 0x110, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x110>::Call)},
    {0x34fffd, 0x10c, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x10c>::Call)},
    {0x353dad, 0x12c, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x12c>::Call)},
    {0x3a5f6f, 0xb0, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0xb0>::Call)},
    {0x3bace7, 0x124, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0x124>::Call)},
    {0xa43196, 0xec, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0xec>::Call)},
    {0xa69028, 0xe0, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0xe0>::Call)},
    {0xcc4705, 0xa8, Addr(&VtordispThunks::SubobjectGetVirtualBaseThunk<0xa8>::Call)},
};

// Body 0xa3c770, cSGResourceSubobject::CopyKeyTo (14 thunks).
const SgCase kSubobjectCopyKeyToThunks[] = {
    {0x2efc15, 0x158, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x158>::Call)},
    {0x321c19, 0x13c, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x13c>::Call)},
    {0x32601b, 0x120, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x120>::Call)},
    {0x3326ad, 0x114, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x114>::Call)},
    {0x33edf5, 0x178, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x178>::Call)},
    {0x349562, 0x1ac, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x1ac>::Call)},
    {0x34d5f1, 0x110, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x110>::Call)},
    {0x350019, 0x10c, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x10c>::Call)},
    {0x353dc9, 0x12c, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x12c>::Call)},
    {0x3a5fce, 0xb0, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0xb0>::Call)},
    {0x3bad03, 0x124, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0x124>::Call)},
    {0xa431c8, 0xec, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0xec>::Call)},
    {0xa69044, 0xe0, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0xe0>::Call)},
    {0xcc4721, 0xa8, Addr(&VtordispThunks::SubobjectCopyKeyToThunk<0xa8>::Call)},
};

// Body 0xa3c870, cSGResourceSubobject::SetFlag (14 thunks).
const SgCase kSubobjectSetFlagThunks[] = {
    {0x2efc3f, 0x158, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x158>::Call)},
    {0x321c43, 0x13c, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x13c>::Call)},
    {0x326045, 0x120, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x120>::Call)},
    {0x3326ea, 0x114, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x114>::Call)},
    {0x33ee1f, 0x178, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x178>::Call)},
    {0x34959f, 0x1ac, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x1ac>::Call)},
    {0x34d61b, 0x110, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x110>::Call)},
    {0x350043, 0x10c, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x10c>::Call)},
    {0x353df3, 0x12c, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x12c>::Call)},
    {0x3a61cc, 0xb0, Addr(&VtordispThunks::SubobjectSetFlagThunk<0xb0>::Call)},
    {0x3bad2d, 0x124, Addr(&VtordispThunks::SubobjectSetFlagThunk<0x124>::Call)},
    {0xa43501, 0xec, Addr(&VtordispThunks::SubobjectSetFlagThunk<0xec>::Call)},
    {0xa693c1, 0xe0, Addr(&VtordispThunks::SubobjectSetFlagThunk<0xe0>::Call)},
    {0xcc474b, 0xa8, Addr(&VtordispThunks::SubobjectSetFlagThunk<0xa8>::Call)},
};

// Body 0xa3cdd0, cSGResourceSubobject::SetResource (14 thunks).
const SgCase kSubobjectSetResourceThunks[] = {
    {0x2f06fc, 0x158, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x158>::Call)},
    {0x321c7b, 0x13c, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x13c>::Call)},
    {0x326096, 0x120, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x120>::Call)},
    {0x332761, 0x114, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x114>::Call)},
    {0x33efbb, 0x178, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x178>::Call)},
    {0x349703, 0x1ac, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x1ac>::Call)},
    {0x34d676, 0x110, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x110>::Call)},
    {0x35007b, 0x10c, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x10c>::Call)},
    {0x353e2b, 0x12c, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x12c>::Call)},
    {0x3a632f, 0xb0, Addr(&VtordispThunks::SubobjectSetResourceThunk<0xb0>::Call)},
    {0x3bad65, 0x124, Addr(&VtordispThunks::SubobjectSetResourceThunk<0x124>::Call)},
    {0xa435c1, 0xec, Addr(&VtordispThunks::SubobjectSetResourceThunk<0xec>::Call)},
    {0xa695cd, 0xe0, Addr(&VtordispThunks::SubobjectSetResourceThunk<0xe0>::Call)},
    {0xcc4783, 0xa8, Addr(&VtordispThunks::SubobjectSetResourceThunk<0xa8>::Call)},
};

// Body 0xa3c790, cSGSecondary::SubmitKeyToService (14 thunks, and the cAnimResource thunk 0xa101d2, adjustment 0x94).
const SgCase kSecondarySubmitThunks[] = {
    {0x2efc31, 0x158, Addr(&VtordispThunks::SecondarySubmitThunk<0x158>::Call)},
    {0x321c35, 0x13c, Addr(&VtordispThunks::SecondarySubmitThunk<0x13c>::Call)},
    {0x326037, 0x120, Addr(&VtordispThunks::SecondarySubmitThunk<0x120>::Call)},
    {0x3326c9, 0x114, Addr(&VtordispThunks::SecondarySubmitThunk<0x114>::Call)},
    {0x33ee11, 0x178, Addr(&VtordispThunks::SecondarySubmitThunk<0x178>::Call)},
    {0x34957e, 0x1ac, Addr(&VtordispThunks::SecondarySubmitThunk<0x1ac>::Call)},
    {0x34d60d, 0x110, Addr(&VtordispThunks::SecondarySubmitThunk<0x110>::Call)},
    {0x350035, 0x10c, Addr(&VtordispThunks::SecondarySubmitThunk<0x10c>::Call)},
    {0x353de5, 0x12c, Addr(&VtordispThunks::SecondarySubmitThunk<0x12c>::Call)},
    {0x3a6165, 0xb0, Addr(&VtordispThunks::SecondarySubmitThunk<0xb0>::Call)},
    {0x3bad1f, 0x124, Addr(&VtordispThunks::SecondarySubmitThunk<0x124>::Call)},
    {0xa4345b, 0xec, Addr(&VtordispThunks::SecondarySubmitThunk<0xec>::Call)},
    {0xa692ef, 0xe0, Addr(&VtordispThunks::SecondarySubmitThunk<0xe0>::Call)},
    {0xcc473d, 0xa8, Addr(&VtordispThunks::SecondarySubmitThunk<0xa8>::Call)},
    {0xa101d2, 0x94, Addr(&VtordispThunks::SecondarySubmitThunk<0x94>::Call)},
};

// Body 0xa3c750, cSGSecondary::VirtualBaseIfObject (1 thunk).
const SgCase kSecondaryVirtualBaseThunks[] = {
    {0xcc4713, 0xa8, Addr(&VtordispThunks::SecondaryVirtualBaseThunk<0xa8>::Call)},
};

// Body 0xa3c890, cSGSecondary::ResetAndSubmit (14 thunks, and the cAnimResource thunk 0xa102a9, adjustment 0x94).
const SgCase kSecondaryResetAndSubmitThunks[] = {
    {0x2f02c1, 0x158, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x158>::Call)},
    {0x321c51, 0x13c, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x13c>::Call)},
    {0x326053, 0x120, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x120>::Call)},
    {0x3326f8, 0x114, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x114>::Call)},
    {0x33ee2d, 0x178, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x178>::Call)},
    {0x349653, 0x1ac, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x1ac>::Call)},
    {0x34d629, 0x110, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x110>::Call)},
    {0x350051, 0x10c, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x10c>::Call)},
    {0x353e01, 0x12c, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x12c>::Call)},
    {0x3a61f0, 0xb0, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0xb0>::Call)},
    {0x3bad3b, 0x124, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x124>::Call)},
    {0xa43597, 0xec, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0xec>::Call)},
    {0xa693f8, 0xe0, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0xe0>::Call)},
    {0xcc4759, 0xa8, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0xa8>::Call)},
    {0xa102a9, 0x94, Addr(&VtordispThunks::SecondaryResetAndSubmitThunk<0x94>::Call)},
};

// Body 0xa3ca00, cSGSecondary::OpenAssignAndSubmit (15 thunks).
const SgCase kSecondaryOpenAssignThunks[] = {
    {0x2f06e0, 0x158, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x158>::Call)},
    {0x321c5f, 0x13c, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x13c>::Call)},
    {0x32607a, 0x120, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x120>::Call)},
    {0x332719, 0x114, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x114>::Call)},
    {0x33ee7a, 0x178, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x178>::Call)},
    {0x34968b, 0x1ac, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x1ac>::Call)},
    {0x34d637, 0x110, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x110>::Call)},
    {0x35005f, 0x10c, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x10c>::Call)},
    {0x353e0f, 0x12c, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x12c>::Call)},
    {0x3a6214, 0xb0, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0xb0>::Call)},
    {0x3bad49, 0x124, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x124>::Call)},
    {0xa1030a, 0x94, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0x94>::Call)},
    {0xa435a5, 0xec, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0xec>::Call)},
    {0xa69439, 0xe0, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0xe0>::Call)},
    {0xcc4767, 0xa8, Addr(&VtordispThunks::SecondaryOpenAssignAndSubmitThunk<0xa8>::Call)},
};

// Body 0xa3cbf0, cSGSecondary::AssignAndSubmit (15 thunks).
const SgCase kSecondaryAssignThunks[] = {
    {0x2f06ee, 0x158, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x158>::Call)},
    {0x321c6d, 0x13c, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x13c>::Call)},
    {0x326088, 0x120, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x120>::Call)},
    {0x332727, 0x114, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x114>::Call)},
    {0x33ee88, 0x178, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x178>::Call)},
    {0x349699, 0x1ac, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x1ac>::Call)},
    {0x34d645, 0x110, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x110>::Call)},
    {0x35006d, 0x10c, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x10c>::Call)},
    {0x353e1d, 0x12c, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x12c>::Call)},
    {0x3a6238, 0xb0, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0xb0>::Call)},
    {0x3bad57, 0x124, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x124>::Call)},
    {0xa10318, 0x94, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0x94>::Call)},
    {0xa435b3, 0xec, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0xec>::Call)},
    {0xa69447, 0xe0, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0xe0>::Call)},
    {0xcc4775, 0xa8, Addr(&VtordispThunks::SecondaryAssignAndSubmitThunk<0xa8>::Call)},
};

// Body 0xa3c4b0, Dispatch8Adjusted (15 thunks).
const SgCase kDispatch8Thunks[] = {
    {0x2ef7d4, 0x158, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x158>::Call)},
    {0x321b18, 0x13c, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x13c>::Call)},
    {0x325f29, 0x120, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x120>::Call)},
    {0x332239, 0x114, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x114>::Call)},
    {0x33e6d2, 0x178, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x178>::Call)},
    {0x348173, 0x1ac, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x1ac>::Call)},
    {0x34d293, 0x110, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x110>::Call)},
    {0x34ff18, 0x10c, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x10c>::Call)},
    {0x353cc8, 0x12c, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x12c>::Call)},
    {0x3a5964, 0xb0, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0xb0>::Call)},
    {0x3bac8b, 0x124, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x124>::Call)},
    {0xa0f6e4, 0x94, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0x94>::Call)},
    {0xa42815, 0xec, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0xec>::Call)},
    {0xa686f6, 0xe0, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0xe0>::Call)},
    {0xcc4395, 0xa8, Addr(&VtordispThunks::Dispatch8AdjustedThunk<0xa8>::Call)},
};

// Body 0xa431c0, cGeometryNodeSecondary8::VirtualBaseOfResource (11 thunks).
const SgCase kVirtualBaseOfResourceThunks[] = {
    {0x2efc07, 0x194, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x194>::Call)},
    {0x321c0b, 0x178, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x178>::Call)},
    {0x32600d, 0x15c, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x15c>::Call)},
    {0x33269f, 0x150, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x150>::Call)},
    {0x33ede7, 0x1b4, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x1b4>::Call)},
    {0x349554, 0x1e8, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x1e8>::Call)},
    {0x34d5e3, 0x14c, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x14c>::Call)},
    {0x35000b, 0x148, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x148>::Call)},
    {0x353dbb, 0x168, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x168>::Call)},
    {0x3bacf5, 0x160, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x160>::Call)},
    {0xa431a4, 0x128, Addr(&VtordispThunks::VirtualBaseOfResourceThunk<0x128>::Call)},
};

// Trials per thunk.
constexpr int kTrials = 2000;

} // namespace

// Each DIFF_TEST loops over every thunk of its body; the forwarder and the thunk are compared on the same state.
// The result is al for the byte-returning bodies (the rest of eax is not defined there).

DIFF_TEST(VtordispThunks_SubobjectOwner, 0xa3c3c0)
{
    InitFakes();
    std::mt19937 rng(0x7f01);
    using Fn = void *(__thiscall *)(void *);
    RunThunks<Fn>(ctx, "GetOwner", kSubobjectOwnerThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return reinterpret_cast<uintptr_t>(f(self)); });
}

DIFF_TEST(VtordispThunks_SubobjectCompareIdentity, 0xa3c3d0)
{
    InitFakes();
    std::mt19937 rng(0x7f02);
    using Fn = int32_t(__thiscall *)(void *, void *);
    RunThunks<Fn>(ctx, "CompareIdentity", kSubobjectCompareIdentityThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t {
                      // The argument is a peer, never the host object: the arg kinds 1 to 3.
                      return static_cast<uint32_t>(f(self, PeerFor(1 + c.argKind % 3)));
                  });
}

DIFF_TEST(VtordispThunks_SubobjectGetFlag, 0xa3c4a0)
{
    InitFakes();
    std::mt19937 rng(0x7f03);
    using Fn = uint8_t(__thiscall *)(void *);
    RunThunks<Fn>(ctx, "GetFlag", kSubobjectGetFlagThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return f(self) & 0xffu; });
}

DIFF_TEST(VtordispThunks_SubobjectForwardKey, 0xa3c6d0)
{
    InitFakes();
    std::mt19937 rng(0x7f04);
    using Fn = uint32_t(__thiscall *)(void *);
    RunThunks<Fn>(ctx, "ForwardKey", kSubobjectForwardKeyThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return f(self); });
}

DIFF_TEST(VtordispThunks_SubobjectGetVirtualBase, 0xa3c720)
{
    InitFakes();
    std::mt19937 rng(0x7f05);
    using Fn = void *(__thiscall *)(void *, void *);
    RunThunks<Fn>(ctx, "GetVirtualBase", kSubobjectGetVirtualBaseThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t {
                      return reinterpret_cast<uintptr_t>(f(self, PeerFor(c.argKind)));
                  });
}

DIFF_TEST(VtordispThunks_SubobjectCopyKeyTo, 0xa3c770)
{
    InitFakes();
    std::mt19937 rng(0x7f06);
    using Fn = void *(__thiscall *)(void *, void *);
    RunThunks<Fn>(ctx, "CopyKeyTo", kSubobjectCopyKeyToThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t {
                      return reinterpret_cast<uintptr_t>(f(self, static_cast<void *>(gOut)));
                  });
}

DIFF_TEST(VtordispThunks_SubobjectSetFlag, 0xa3c870)
{
    InitFakes();
    std::mt19937 rng(0x7f07);
    using Fn = void(__thiscall *)(void *, uint32_t);
    RunThunks<Fn>(ctx, "SetFlag", kSubobjectSetFlagThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t {
                      f(self, c.a1);
                      return 0;
                  });
}

DIFF_TEST(VtordispThunks_SubobjectSetResource, 0xa3cdd0)
{
    InitFakes();
    std::mt19937 rng(0x7f08);
    using Fn = uint8_t(__thiscall *)(void *, void *);
    RunThunks<Fn>(ctx, "SetResource", kSubobjectSetResourceThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t { return f(self, PeerFor(c.newKind)) & 0xffu; });
}

DIFF_TEST(VtordispThunks_SecondarySubmit, 0xa3c790)
{
    InitFakes();
    std::mt19937 rng(0x7f09);
    using Fn = uint8_t(__thiscall *)(void *);
    gEscapes = 0;
    RunThunks<Fn>(ctx, "SubmitKeyToService", kSecondarySubmitThunks, rng, kBodyS, kTrials, true,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return f(self) & 0xffu; });
    if (gEscapes == 0) ctx.Fail("SubmitKeyToService thunks: no run threw (the holder's slot 14 throw is not exercised)");
}

DIFF_TEST(VtordispThunks_SecondaryVirtualBase, 0xa3c750)
{
    InitFakes();
    std::mt19937 rng(0x7f0a);
    using Fn = void *(__thiscall *)(void *);
    RunThunks<Fn>(ctx, "VirtualBaseIfObject", kSecondaryVirtualBaseThunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return reinterpret_cast<uintptr_t>(f(self)); });
}

DIFF_TEST(VtordispThunks_SecondaryResetAndSubmit, 0xa3c890)
{
    InitFakes();
    std::mt19937 rng(0x7f0b);
    using Fn = uint8_t(__thiscall *)(void *, void *, void *);
    gEscapes = 0;
    RunThunks<Fn>(ctx, "ResetAndSubmit", kSecondaryResetAndSubmitThunks, rng, kBodyS, kTrials, true,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t {
                      return f(self, Ptr(c.a2, c.a2Null), Ptr(c.a3, c.a3Null)) & 0xffu;
                  });
    if (gEscapes == 0) ctx.Fail("ResetAndSubmit thunks: no run threw");
}

DIFF_TEST(VtordispThunks_SecondaryOpenAssignAndSubmit, 0xa3ca00)
{
    InitFakes();
    std::mt19937 rng(0x7f0c);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, void *, void *);
    gEscapes = 0;
    RunThunks<Fn>(ctx, "OpenAssignAndSubmit", kSecondaryOpenAssignThunks, rng, kBodyS, kTrials, true,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t {
                      return f(self, c.a1, Ptr(c.a2, c.a2Null), Ptr(c.a3, c.a3Null)) & 0xffu;
                  });
    if (gEscapes == 0) ctx.Fail("OpenAssignAndSubmit thunks: no run threw");
}

DIFF_TEST(VtordispThunks_SecondaryAssignAndSubmit, 0xa3cbf0)
{
    InitFakes();
    std::mt19937 rng(0x7f0d);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, void *);
    gEscapes = 0;
    RunThunks<Fn>(ctx, "AssignAndSubmit", kSecondaryAssignThunks, rng, kBodyS, kTrials, true,
                  [](Fn f, void *self, const Cfg &c) -> uint64_t {
                      return f(self, c.a1, Ptr(c.a2, c.a2Null)) & 0xffu;
                  });
    if (gEscapes == 0) ctx.Fail("AssignAndSubmit thunks: no run threw");
}

DIFF_TEST(VtordispThunks_Dispatch8Adjusted, 0xa3c4b0)
{
    InitFakes();
    std::mt19937 rng(0x7f0e);
    using Fn = uint32_t(__thiscall *)(void *);
    RunThunks<Fn>(ctx, "Dispatch8Adjusted", kDispatch8Thunks, rng, kBodyS, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return f(self); });
}

DIFF_TEST(VtordispThunks_VirtualBaseOfResource, 0xa431c0)
{
    InitFakes();
    std::mt19937 rng(0x7f0f);
    using Fn = void *(__thiscall *)(void *);
    RunThunks<Fn>(ctx, "VirtualBaseOfResource", kVirtualBaseOfResourceThunks, rng, kBodyGeometry, kTrials, false,
                  [](Fn f, void *self, const Cfg &) -> uint64_t { return reinterpret_cast<uintptr_t>(f(self)); });
}
