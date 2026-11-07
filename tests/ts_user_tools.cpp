// Differential tests for the build/buy-mode user tools (src/ts/UserTools.h): cTSUserToolBase (vtable 0xdfde3c),
// cTSUserToolBuyBuild and cTSUserToolGroundBased (vtable 0xdfe0ec).
//
// Each trial builds one random state and runs the original on one copy of it and our member on another. Both copies
// have the same bytes, the same vtable (a routed copy of the original one) and the same delegate (a fake with its own
// vtable). Every fake records its calls (kind, object offset, arguments) and returns a fixed value for its slot. The
// routed vtables replace only the slots the functions call on `this`, so the original calls the fakes too; the
// recorded calls and their order must then match. Results and the whole state (guard bytes included) are compared.
//
// Results are compared as eax, except where the original returns with eax untouched (no delegate, vf23): then there
// is no defined value and the result is not compared. The bool functions (vf31, vf32) compare the low byte only,
// because the original leaves the upper bits of eax alone.
#include "difftest.h"
#include "ts/UserTools.h"

#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr int kTrials = 4000;
constexpr int kBaseSlots = 38;
constexpr int kGroundSlots = 63;
constexpr int kDelSlots = 64;
constexpr uint32_t kBaseVTable = cTSUserToolBase::kVTable;
constexpr uint32_t kGroundVTable = cTSUserToolGroundBased::kVTable;
constexpr uint32_t kObjSize = 0x80;
constexpr uint32_t kDelSize = 0x20;

// ---- Recording fakes -------------------------------------------------------------------------------------------

// What a fake saw: kind, the object it was called on (relative to the side), and up to five values.
using Event = std::array<uint32_t, 8>;
enum : uint32_t {
    kDel17 = 1, // delegate slot 17 (vf23's target)
    kDel23,     // delegate slot 23 (vf29)
    kDel24,     // delegate slot 24 (vf30)
    kDel25,     // delegate slot 25 (vf31)
    kDel26,     // delegate slot 26 (vf32)
    kDel37,     // delegate slot 37 (vf22)
    kSelf17,    // this's slot 17 (vf8)
    kSelf18,    // this's slot 18 (vf9)
    kSelf57,    // this's slot 57 (vf44 of GroundBased)
    kSelf59,    // this's slot 59 (vf38)
    kSelf60,    // this's slot 60 (vf49)
    kKinds,
};

std::vector<Event> gEvents;
uintptr_t gBase = 0;      // the side being run: pointers are recorded relative to it
uint32_t gRet[kKinds];    // what each fake returns (full eax)

uint32_t Rel(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - gBase);
}

uint32_t __fastcall DelSlot17(void *self, void *, uint32_t a1)
{
    gEvents.push_back({kDel17, Rel(self), a1});
    return gRet[kDel17];
}

uint32_t __fastcall DelSlot23(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    gEvents.push_back({kDel23, Rel(self), a1, a2, a3, a4, a5});
    return gRet[kDel23];
}

uint32_t __fastcall DelSlot24(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3)
{
    gEvents.push_back({kDel24, Rel(self), a1, a2, a3});
    return gRet[kDel24];
}

uint32_t __fastcall DelSlot25(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    gEvents.push_back({kDel25, Rel(self), a1, a2, a3, a4, a5});
    return gRet[kDel25];
}

uint32_t __fastcall DelSlot26(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    gEvents.push_back({kDel26, Rel(self), a1, a2, a3, a4, a5});
    return gRet[kDel26];
}

void __fastcall DelSlot37(void *self, void *)
{
    gEvents.push_back({kDel37, Rel(self)});
}

// a3 is a pointer into the side (the scratch bytes): recorded relative to it, as the object is.
uint32_t __fastcall SelfSlot17(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3)
{
    gEvents.push_back({kSelf17, Rel(self), a1, a2, Rel(reinterpret_cast<void *>(a3))});
    return gRet[kSelf17];
}

uint32_t __fastcall SelfSlot18(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3)
{
    gEvents.push_back({kSelf18, Rel(self), a1, a2, a3});
    return gRet[kSelf18];
}

// a1 is the address of the object's +0x54 sub-object: recorded relative to the side.
void __fastcall SelfSlot57(void *self, void *, uint32_t a1)
{
    gEvents.push_back({kSelf57, Rel(self), Rel(reinterpret_cast<void *>(a1))});
}

void __fastcall SelfSlot59(void *self, void *)
{
    gEvents.push_back({kSelf59, Rel(self)});
}

void __fastcall SelfSlot60(void *self, void *)
{
    gEvents.push_back({kSelf60, Rel(self)});
}

// ---- Vtables ---------------------------------------------------------------------------------------------------

const void *gBaseVT[kBaseSlots];
const void *gGroundVT[kGroundSlots];
const void *gDelVT[kDelSlots];

// Copies the original vtables and routes the slots the tested functions call on `this`. The delegate's vtable holds
// only the fakes the forwarders call.
void SetUpTables(difftest::Context &ctx)
{
    std::memcpy(gBaseVT, ctx.image.At(kBaseVTable), sizeof(gBaseVT));
    gBaseVT[17] = reinterpret_cast<const void *>(&SelfSlot17);
    gBaseVT[18] = reinterpret_cast<const void *>(&SelfSlot18);

    std::memcpy(gGroundVT, ctx.image.At(kGroundVTable), sizeof(gGroundVT));
    gGroundVT[57] = reinterpret_cast<const void *>(&SelfSlot57);
    gGroundVT[59] = reinterpret_cast<const void *>(&SelfSlot59);
    gGroundVT[60] = reinterpret_cast<const void *>(&SelfSlot60);

    std::memset(gDelVT, 0, sizeof(gDelVT));
    gDelVT[17] = reinterpret_cast<const void *>(&DelSlot17);
    gDelVT[23] = reinterpret_cast<const void *>(&DelSlot23);
    gDelVT[24] = reinterpret_cast<const void *>(&DelSlot24);
    gDelVT[25] = reinterpret_cast<const void *>(&DelSlot25);
    gDelVT[26] = reinterpret_cast<const void *>(&DelSlot26);
    gDelVT[37] = reinterpret_cast<const void *>(&DelSlot37);
}

// ---- Sides -----------------------------------------------------------------------------------------------------

struct Side {
    uint8_t front[16];
    uint8_t object[kObjSize];   // +0 vtable, +4 delegate pointer (or null)
    uint8_t delegate[kDelSize]; // +0 the delegate vtable
    uint8_t scratch[8];         // what vf8's third argument points to
    uint8_t back[16];
};

void StoreWord(uint8_t *at, uintptr_t v)
{
    std::memcpy(at, &v, sizeof(v));
}

uintptr_t LoadWord(const uint8_t *at)
{
    uintptr_t v;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

void FillRandom(Side &s, std::mt19937 &rng)
{
    uint8_t *p = reinterpret_cast<uint8_t *>(&s);
    for (size_t i = 0; i < sizeof(s); ++i)
        p[i] = static_cast<uint8_t>(rng());
}

// A copy of `seed` with the vtable `vt` and, if asked, this side's delegate.
void Prepare(Side &s, const Side &seed, const void *vt, bool hasDelegate)
{
    s = seed;
    StoreWord(s.object, reinterpret_cast<uintptr_t>(vt));
    StoreWord(s.object + 4, hasDelegate ? reinterpret_cast<uintptr_t>(s.delegate) : 0);
    StoreWord(s.delegate, reinterpret_cast<uintptr_t>(gDelVT));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<Event> events;
};

template <typename Call> Outcome Run(Side &s, Call call)
{
    gBase = reinterpret_cast<uintptr_t>(&s);
    gEvents.clear();
    Outcome o;
    o.ret = call(s);
    o.events = gEvents;
    return o;
}

// Everything but the result: the object (the delegate pointer by presence and offset), the delegate and the guards.
bool SameState(const Side &a, const Side &b)
{
    if (std::memcmp(a.object, b.object, 4) != 0)
        return false;
    if (std::memcmp(a.object + 8, b.object + 8, kObjSize - 8) != 0)
        return false;
    const uintptr_t pa = LoadWord(a.object + 4), pb = LoadWord(b.object + 4);
    if ((pa == 0) != (pb == 0))
        return false;
    if (pa && pa - reinterpret_cast<uintptr_t>(&a) != pb - reinterpret_cast<uintptr_t>(&b))
        return false;
    return std::memcmp(a.delegate, b.delegate, kDelSize) == 0 && std::memcmp(a.scratch, b.scratch, sizeof(a.scratch)) == 0 &&
           std::memcmp(a.front, b.front, sizeof(a.front)) == 0 && std::memcmp(a.back, b.back, sizeof(a.back)) == 0;
}

// One random state, the same on both sides. `orig` and `mine` take the side and return the function's eax.
template <typename Orig, typename Mine>
void Compare(difftest::Context &ctx, const char *name, const void *vt, bool hasDelegate, bool compareRet,
             std::mt19937 &rng, Orig orig, Mine mine)
{
    Side seed;
    FillRandom(seed, rng);
    for (uint32_t &r : gRet) {
        switch (rng() % 5) {
        case 0: r = 0; break;
        case 1: r = 1; break;
        case 2: r = 0x100; break;
        case 3: r = 0xff; break;
        default: r = static_cast<uint32_t>(rng()); break;
        }
    }

    Side a, b;
    Prepare(a, seed, vt, hasDelegate);
    Prepare(b, seed, vt, hasDelegate);
    const Outcome oa = Run(a, orig);
    const Outcome ob = Run(b, mine);
    ctx.cases++;

    if (compareRet && oa.ret != ob.ret)
        ctx.Fail("%s: returned %08x, reimplementation %08x", name, oa.ret, ob.ret);
    if (oa.events != ob.events)
        ctx.Fail("%s: recorded calls differ (original %zu, reimplementation %zu)", name, oa.events.size(),
                 ob.events.size());
    if (!SameState(a, b))
        ctx.Fail("%s: state after the call differs", name);
}

// Whether a void original is compared by its state only (the result is not used).
constexpr bool kNoResult = false;

} // namespace

// ---- cTSUserToolBase ---------------------------------------------------------------------------------------------

DIFF_TEST(ts_user_base_vf22, 0x78d745)
{
    std::mt19937 rng(0x22);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const bool hasDelegate = rng() & 1;
        Compare(
            ctx, "vf22", gBaseVT, hasDelegate, kNoResult, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *)>(0x78d745)(s.object);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolBase *>(s.object)->ForwardSlot37();
                return 0u;
            });
    }
}

DIFF_TEST(ts_user_base_vf23, 0x78d916)
{
    std::mt19937 rng(0x23);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const bool hasDelegate = rng() & 1;
        const uint32_t a1 = static_cast<uint32_t>(rng());
        // Without a delegate the original's eax is left over from the caller: no result to compare.
        Compare(
            ctx, "vf23", gBaseVT, hasDelegate, hasDelegate, rng,
            [&](Side &s) { return ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x78d916)(s.object, a1); },
            [&](Side &s) { return reinterpret_cast<cTSUserToolBase *>(s.object)->ForwardSlot17(a1); });
    }
}

DIFF_TEST(ts_user_base_vf29, 0x78d789)
{
    std::mt19937 rng(0x29);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const bool hasDelegate = rng() & 1;
        const uint32_t a[5] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng())};
        Compare(
            ctx, "vf29", gBaseVT, hasDelegate, true, rng,
            [&](Side &s) {
                return ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
                    0x78d789)(s.object, a[0], a[1], a[2], a[3], a[4]);
            },
            [&](Side &s) {
                return reinterpret_cast<cTSUserToolBase *>(s.object)->ForwardSlot23(a[0], a[1], a[2], a[3], a[4]);
            });
    }
}

DIFF_TEST(ts_user_base_vf30, 0x78d64d)
{
    std::mt19937 rng(0x30);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const bool hasDelegate = rng() & 1;
        const uint32_t a[3] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng())};
        Compare(
            ctx, "vf30", gBaseVT, hasDelegate, true, rng,
            [&](Side &s) {
                return ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x78d64d)(
                    s.object, a[0], a[1], a[2]);
            },
            [&](Side &s) { return reinterpret_cast<cTSUserToolBase *>(s.object)->ForwardSlot24(a[0], a[1], a[2]); });
    }
}

DIFF_TEST(ts_user_base_vf31, 0x78d6c3)
{
    std::mt19937 rng(0x31);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const bool hasDelegate = rng() & 1;
        const uint32_t a[4] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())};
        // The result is al: the original's upper eax bits are left over, so the bool is compared, not eax.
        Compare(
            ctx, "vf31", gBaseVT, hasDelegate, true, rng,
            [&](Side &s) {
                return static_cast<uint32_t>(
                    ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x78d6c3)(
                        s.object, a[0], a[1], a[2], a[3]));
            },
            [&](Side &s) {
                return static_cast<uint32_t>(
                    reinterpret_cast<cTSUserToolBase *>(s.object)->ForwardSlot25Flag(a[0], a[1], a[2], a[3]));
            });
    }
}

DIFF_TEST(ts_user_base_vf32, 0x78d6f0)
{
    std::mt19937 rng(0x32);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const bool hasDelegate = rng() & 1;
        const uint32_t a[4] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())};
        Compare(
            ctx, "vf32", gBaseVT, hasDelegate, true, rng,
            [&](Side &s) {
                return static_cast<uint32_t>(
                    ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x78d6f0)(
                        s.object, a[0], a[1], a[2], a[3]));
            },
            [&](Side &s) {
                return static_cast<uint32_t>(
                    reinterpret_cast<cTSUserToolBase *>(s.object)->ForwardSlot26Flag(a[0], a[1], a[2], a[3]));
            });
    }
}

DIFF_TEST(ts_user_base_vf9, 0x78d755)
{
    std::mt19937 rng(0x09);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const uint32_t a[4] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                               static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())};
        Compare(
            ctx, "vf9", gBaseVT, rng() & 1, true, rng,
            [&](Side &s) {
                return ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x78d755)(
                    s.object, a[0], a[1], a[2], a[3]);
            },
            [&](Side &s) {
                return reinterpret_cast<cTSUserToolBase *>(s.object)->SelfSlot18(a[0], a[1], a[2], a[3]);
            });
    }
}

DIFF_TEST(ts_user_base_vf8, 0x78d769)
{
    std::mt19937 rng(0x08);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const uint32_t a1 = static_cast<uint32_t>(rng());
        const uint32_t a2 = static_cast<uint32_t>(rng());
        const uint32_t a4 = static_cast<uint32_t>(rng());
        // The third argument is a pointer to the scratch bytes of the side being run.
        Compare(
            ctx, "vf8", gBaseVT, rng() & 1, true, rng,
            [&](Side &s) {
                const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.scratch));
                return ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x78d769)(
                    s.object, a1, a2, p, a4);
            },
            [&](Side &s) {
                const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.scratch));
                return reinterpret_cast<cTSUserToolBase *>(s.object)->StoreAndSelfSlot17(a1, a2, p, a4);
            });
    }
}

// ---- cTSUserToolBuyBuild -----------------------------------------------------------------------------------------

DIFF_TEST(ts_user_buybuild_vf44, 0x78e747)
{
    std::mt19937 rng(0x44);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const uint32_t a1 = static_cast<uint32_t>(rng());
        Compare(
            ctx, "BuyBuild vf44", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x78e747)(s.object, a1);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolBuyBuild *>(s.object)->ClearByte4C(a1);
                return 0u;
            });
    }
}

// ---- cTSUserToolGroundBased --------------------------------------------------------------------------------------

DIFF_TEST(ts_user_ground_vf38, 0x78eb3c)
{
    std::mt19937 rng(0x38);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        // With the flag set the original returns with eax untouched; the result is not compared (void anyway).
        Compare(
            ctx, "GroundBased vf38", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *)>(0x78eb3c)(s.object);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolGroundBased *>(s.object)->EnsureFlag50();
                return 0u;
            });
    }
}

DIFF_TEST(ts_user_ground_vf44, 0x78ec49)
{
    std::mt19937 rng(0x45);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        const uint32_t a1 = static_cast<uint32_t>(rng());
        Compare(
            ctx, "GroundBased vf44", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x78ec49)(s.object, a1);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolGroundBased *>(s.object)->ClearByte4CAndNotify(a1);
                return 0u;
            });
    }
}

DIFF_TEST(ts_user_ground_vf49, 0x78eb4b)
{
    std::mt19937 rng(0x49);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        Compare(
            ctx, "GroundBased vf49", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *)>(0x78eb4b)(s.object);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolGroundBased *>(s.object)->ClearFlagsAfterSlot60();
                return 0u;
            });
    }
}

DIFF_TEST(ts_user_ground_vf59, 0x67d331)
{
    std::mt19937 rng(0x59);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        Compare(
            ctx, "GroundBased vf59", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *)>(0x67d331)(s.object);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolGroundBased *>(s.object)->SetFlag50();
                return 0u;
            });
    }
}

DIFF_TEST(ts_user_ground_vf60, 0x67d336)
{
    std::mt19937 rng(0x60);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        Compare(
            ctx, "GroundBased vf60", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *)>(0x67d336)(s.object);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolGroundBased *>(s.object)->ClearFlag50();
                return 0u;
            });
    }
}

DIFF_TEST(ts_user_ground_vf62, 0x78eca1)
{
    std::mt19937 rng(0x62);
    SetUpTables(ctx);
    for (int i = 0; i < kTrials; ++i) {
        // Only the low byte is stored: the argument's upper bytes are random.
        const uint32_t a1 = static_cast<uint32_t>(rng());
        Compare(
            ctx, "GroundBased vf62", gGroundVT, false, false, rng,
            [&](Side &s) {
                ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x78eca1)(s.object, a1);
                return 0u;
            },
            [&](Side &s) {
                reinterpret_cast<cTSUserToolGroundBased *>(s.object)->SetByte61(a1);
                return 0u;
            });
    }
}
