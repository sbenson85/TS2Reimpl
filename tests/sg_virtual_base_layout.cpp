// Lane NY (scenegraph N0 part 1): the virtual-base layout decision for nGZSceneGraph::cSGResource
// (docs/standalone-shape.md, "Virtual bases and vtordisp"; docs/subsystems/scenegraph.md).
//
// The question: does MSVC 14.40, given cSGResource written as plain C++
//     class cSGResource : public cGZPersistResource, public virtual cISGResource { ctor; dtor; overrides; members }
// lay the object out as the game's does (vbptr +0x1c, members +0x20..+0x29, vtordisp +0x2c, the virtual base's vtable at
// +0x30, 0x34 bytes), with the same vbtable, the same vtordisp values (also when a derived class moves the virtual base
// further out) and the same thunk kinds in the virtual base's vtable? Then cSGResource and its ~40 derived classes take
// the ordinary standalone shape with real virtual inheritance; otherwise they need an explicit layout (cBaseParser).
//
// The probe below is that declaration with stand-in bodies (the 25-slot cISGResource face with two pure slots, as the
// game's 18 and 19 are _purecall). Its constructor snapshots the object at the end of its body, the state the game's
// constructor 0xa3c270 leaves; the game's constructor runs on a buffer whose vbptr a "derived class" has set (it never
// sets the vbptr itself: cSGResource is abstract, so it is only ever a base). Compared: every byte of the object, the
// vtable words as the face they are (primary / virtual base), the vbptr by the vbtable's contents; the vtordisp after a
// derived constructor; the thunk in every overridden slot of the virtual base's vtables (cSGResource's 0xe695dc and a
// derived class's, cGeometryDataContainer 0xe73314, against ours at two placements); and the `this` our bodies take,
// called as the game calls them and through the adapter shape an RVA hook would use.
//
// Mutants (lane NY): the base not virtual, the adapter's distance, a member's width, the constructor's helper value:
// killed. Equivalent: the bases declared in the other order (MSVC puts non-virtual bases first whatever the order) and
// `#pragma vtordisp(0)` on the derived class (the vtordisp is cSGResource's; a derived class inherits it).
#include "difftest.h"
#include "gz/cGZPersistResource.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

// The virtual base's face: cIGZUnknown's 3 slots and 22 more (the game's 0xe695dc has 25).
class ProbeISGResource : public cIGZUnknown {
public:
    virtual uint32_t S3() = 0;
    virtual uint32_t S4() = 0;
    virtual uint32_t S5() = 0;
    virtual uint32_t S6() = 0;
    virtual uint32_t S7() = 0;
    virtual uint32_t S8() = 0;
    virtual uint32_t S9() = 0;
    virtual uint32_t S10() = 0;
    virtual uint32_t S11() = 0;
    virtual uint32_t S12() = 0;
    virtual uint32_t S13() = 0;
    virtual uint32_t S14() = 0;
    virtual uint32_t S15() = 0;
    virtual uint32_t S16() = 0;
    virtual uint32_t S17() = 0;
    virtual uint32_t S18() = 0;
    virtual uint32_t S19() = 0;
    virtual uint32_t S20() = 0;
    virtual uint32_t S21() = 0;
    virtual uint32_t S22() = 0;
    virtual uint32_t S23() = 0;
    virtual uint32_t S24() = 0;
};

uint32_t gSnapshot[16];
const void *gSeenThis; // the `this` S3's body received
void *gOwnPrimary, *gOwnSecondary; // the probe's own vtables (what its constructor stores), the 0xe695a4 / 0xe695dc analogues

// cSGResource as plain C++ (abstract: S18 / S19 stay pure).
class ProbeSGResource : public cGZPersistResource, public virtual ProbeISGResource {
public:
    ProbeSGResource() : mHelper(0xffffffffu), mResource(nullptr), mFlag28(0), mFlag29(0)
    {
        std::memcpy(gSnapshot, this, sizeof(gSnapshot));
        gOwnPrimary = *reinterpret_cast<void **>(this);
        gOwnSecondary = *reinterpret_cast<void **>(static_cast<ProbeISGResource *>(this));
    }
    ~ProbeSGResource() override {}

    bool QueryInterface(uint32_t, void **) override { return false; }
    uint32_t AddRef() override { return cGZPersistResource::AddRef(); }
    uint32_t Release() override { return cGZPersistResource::Release(); }
    uint32_t S3() override
    {
        gSeenThis = this;
        return 3 + mHelper;
    }
    uint32_t S4() override { return 4 + mHelper; }
    uint32_t S5() override { return 5 + mHelper; }
    uint32_t S6() override { return 6 + mHelper; }
    uint32_t S7() override { return 7 + mHelper; }
    uint32_t S8() override { return 8 + mHelper; }
    uint32_t S9() override { return 9 + mHelper; }
    uint32_t S10() override { return 10 + mHelper; }
    uint32_t S11() override { return 11 + mHelper; }
    uint32_t S12() override { return 12 + mHelper; }
    uint32_t S13() override { return 13 + mHelper; }
    uint32_t S14() override { return 14 + mHelper; }
    uint32_t S15() override { return 15 + mHelper; }
    uint32_t S16() override { return 16 + mHelper; }
    uint32_t S17() override { return 17 + mHelper; }
    uint32_t S20() override { return 20 + mHelper; }
    uint32_t S21() override { return 21 + mHelper; }
    uint32_t S22() override { return 22 + mHelper; }
    uint32_t S23() override { return 23 + mHelper; }
    uint32_t S24() override { return 24 + mHelper; }

    uint32_t mHelper;  // +0x20 (the game's: set to -1 by 0x18bf80)
    void *mResource;   // +0x24
    uint8_t mFlag28;   // +0x28
    uint8_t mFlag29;   // +0x29
};

// Most-derived classes: the same layout (no members of its own), and the virtual base 12 bytes further out. Both have a
// user-declared constructor, as the game's derived classes do (cGeometryDataContainer 0xa6ed50): that is what gives a
// derived class vtordisp thunks of its own.
void *gDisplacedSecondary; // ProbeDisplaced's virtual-base vtable (the 0xe73314 analogue)
class ProbeConcrete final : public ProbeSGResource {
public:
    ProbeConcrete() {}
    uint32_t S18() override { return 18; }
    uint32_t S19() override { return 19; }
};
class ProbeDisplaced final : public ProbeSGResource {
public:
    ProbeDisplaced() { gDisplacedSecondary = *reinterpret_cast<void **>(static_cast<ProbeISGResource *>(this)); }
    uint32_t extra[3] = {1, 2, 3};
    uint32_t S18() override { return 18; }
    uint32_t S19() override { return 19; }
};

// The adapter shape for an RVA hook of a cSGResource slot body (they take the virtual base's `this`: 0xa3c4b0 is reached
// from cGeometryDataContainer's 0xe73314 slot 17 through `sub ecx, [ecx-4]; sub ecx, 0x50`): ecx = object + 0x30, back
// to the class by the static distance, then a direct call. No static_cast: C++ has no static_cast from a virtual base.
uint32_t __fastcall SGResourceAdapter(void *face, void *)
{
    auto *self = reinterpret_cast<ProbeSGResource *>(static_cast<uint8_t *>(face) - 0x30);
    [[msvc::forceinline_calls]] return self->ProbeSGResource::S3();
}

uint32_t Word(const void *p, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + offset, 4);
    return w;
}

} // namespace

DIFF_TEST(SGResource_VirtualBaseLayout, 0xa3c270)
{
    auto gameCtor = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xa3c270);
    const uint32_t gamePrimary = uint32_t(reinterpret_cast<uintptr_t>(ctx.image.At(0xe695a4)));
    const uint32_t gameSecondary = uint32_t(reinterpret_cast<uintptr_t>(ctx.image.At(0xe695dc)));

    // Two placements of the virtual base: where cSGResource alone puts it (+0x30) and 12 bytes further out (a derived
    // class with members, as cResourceNode / cShape / cGeometryNode do).
    for (unsigned displaced = 0; displaced < 2; ++displaced) {
        alignas(8) static uint8_t ours[0x48], game[0x48];
        std::memset(ours, 0xcd, sizeof(ours));
        std::memset(game, 0xcd, sizeof(game));
        ProbeSGResource *object = displaced ? static_cast<ProbeSGResource *>(::new (ours) ProbeDisplaced)
                                            : static_cast<ProbeSGResource *>(::new (ours) ProbeConcrete);
        const uint32_t vbase = displaced ? 0x3c : 0x30;
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(object) != ours ||
            reinterpret_cast<uint8_t *>(static_cast<ProbeISGResource *>(object)) != ours + vbase)
            ctx.Fail("%s: the virtual base is not at +0x%x", displaced ? "displaced" : "plain", vbase);
        if (!displaced && (sizeof(ProbeConcrete) != 0x34 || reinterpret_cast<uint8_t *>(&object->mHelper) != ours + 0x20 ||
                           reinterpret_cast<uint8_t *>(&object->mResource) != ours + 0x24 ||
                           reinterpret_cast<uint8_t *>(&object->mFlag28) != ours + 0x28))
            ctx.Fail("plain: size 0x%x or member offsets differ from the game's (0x34; +0x20, +0x24, +0x28)",
                     unsigned(sizeof(ProbeConcrete)));

        // Our vbtable (at the vbptr +0x1c): [0] back to the object, [1] to the virtual base.
        const int32_t *vbtable = reinterpret_cast<const int32_t *>(uintptr_t(Word(ours, 0x1c)));
        ++ctx.cases;
        if (vbtable[0] != -0x1c || uint32_t(vbtable[1]) != vbase - 0x1c)
            ctx.Fail("%s: our vbtable is {%d, %d}, the layout needs {-28, %u}", displaced ? "displaced" : "plain", vbtable[0],
                     vbtable[1], vbase - 0x1c);

        // The game's constructor on a buffer whose vbptr the derived class set (to a vbtable of the same contents).
        static int32_t gameVbtable[2];
        gameVbtable[0] = vbtable[0];
        gameVbtable[1] = vbtable[1];
        const uint32_t vbptr = uint32_t(reinterpret_cast<uintptr_t>(gameVbtable));
        std::memcpy(game + 0x1c, &vbptr, 4);
        gameCtor(game, 0);

        // Compare the first vbase + 4 bytes: our snapshot at the end of our constructor's body against the game's.
        uint32_t a[16], b[16];
        std::memcpy(a, game, sizeof(a));
        std::memcpy(b, gSnapshot, sizeof(b));
        auto code = [&](uint32_t w) -> uint32_t {
            if (w == gamePrimary || w == uint32_t(reinterpret_cast<uintptr_t>(gOwnPrimary))) return 0xd0000000u;
            if (w == gameSecondary || w == uint32_t(reinterpret_cast<uintptr_t>(gOwnSecondary))) return 0xd0000001u;
            if (w == vbptr || w == Word(ours, 0x1c)) return 0xd0000002u;
            return w;
        };
        const unsigned words = (vbase + 4) / 4;
        for (unsigned i = 0; i < words; ++i) {
            ++ctx.cases;
            if (displaced && i >= 0x2c / 4 && i < 0x38 / 4) continue; // ProbeDisplaced::extra: set after our snapshot
            if (code(a[i]) != code(b[i]))
                ctx.Fail("%s: +0x%02x game %08x ours %08x", displaced ? "displaced" : "plain", i * 4, a[i], b[i]);
        }
        // The vtordisp cSGResource's constructor wrote (in the snapshot above for ours): the virtual base's distance from
        // the vbptr less cSGResource's own (0x14: 0xa3c2b5 `lea eax, [ecx-0x14]`), so 0 alone and 12 displaced.
        ++ctx.cases;
        if (Word(game, vbase - 4) != vbase - 0x30)
            ctx.Fail("%s: vtordisp after the game's constructor %u, expected %u", displaced ? "displaced" : "plain",
                     Word(game, vbase - 4), vbase - 0x30);
        // A derived constructor then writes its own: the distance less its own static distance (cGeometryDataContainer
        // 0xa6edb5 `lea eax, [ecx-0x64]`), 0 in a complete object. Ours after the derived constructor ran:
        ++ctx.cases;
        if (Word(ours, vbase - 4) != 0)
            ctx.Fail("%s: our vtordisp after the derived constructor %u, a derived game constructor leaves 0",
                     displaced ? "displaced" : "plain", Word(ours, vbase - 4));
        // The `this` a cSGResource override's body gets in ecx is the object + 0x30 (where cSGResource alone puts the
        // virtual base), wherever the virtual base really is: the game's thunks deliver that (0xe73314's `sub ecx,
        // [ecx-4]; sub ecx, 0x50`: the virtual base at +0x80 less 0x50) and ours do (the thunk checks below). The body
        // subtracts 0x30 without looking at the vbtable. Checked by calling our body (the target of our thunk in slot 3)
        // the way the game's thunks and callers do, ecx = object + 0x30, in both placements; and through the adapter an
        // RVA hook of such a body uses (SGResourceAdapter below).
        {
            const uint8_t *t = static_cast<const uint8_t *>(static_cast<void *const *>(gOwnSecondary)[3]);
            int32_t rel;
            std::memcpy(&rel, t + 4, 4);
            auto body = reinterpret_cast<uint32_t(__thiscall *)(void *)>(uintptr_t(t + 8 + rel));
            gSeenThis = nullptr;
            ++ctx.cases;
            if (t[3] != 0xe9 || (body(ours + 0x30), gSeenThis != ours))
                ctx.Fail("%s: our S3 body called with ecx = object+0x30 sees this = object%+d", displaced ? "displaced" : "plain",
                         int(static_cast<const uint8_t *>(gSeenThis) - ours));
            gSeenThis = nullptr;
            ++ctx.cases;
            if (SGResourceAdapter(ours + 0x30, nullptr) != 3 + 0xffffffffu || gSeenThis != ours)
                ctx.Fail("%s: the adapter called with ecx = object+0x30 sees this = object%+d",
                         displaced ? "displaced" : "plain", int(static_cast<const uint8_t *>(gSeenThis) - ours));
        }
        object->~ProbeSGResource();
    }

    // The virtual base's vtables: every overridden slot holds a vtordisp thunk, `sub ecx, [ecx-4]` then `sub ecx, n`
    // (n = the virtual base's static offset less the offset of the subobject whose function overrides it; absent when 0)
    // and a jmp. -1: not such a thunk; 18 and 19 are _purecall in cSGResource.
    auto thunk = [](const void *code) -> int32_t {
        const uint8_t *c = static_cast<const uint8_t *>(code);
        if (c[0] != 0x2b || c[1] != 0x49 || c[2] != 0xfc) return -1;
        if (c[3] == 0xe9) return 0;
        if (c[3] == 0x83 && c[4] == 0xe9 && c[6] == 0xe9) return c[5];
        int32_t n;
        std::memcpy(&n, c + 5, 4);
        if (c[3] == 0x81 && c[4] == 0xe9 && c[9] == 0xe9) return n;
        return -1;
    };
    // The expected n per slot, for a virtual base at vbase with cSGResource's part at +0: slots 0-2 (QueryInterface,
    // AddRef, Release) are overridden by functions of the primary base, so the whole distance; the slots cSGResource
    // overrides vbase - 0x30 (their `this` is the virtual base as cSGResource placed it, object + 0x30); 18 and 19 pure in
    // cSGResource, and 0 in a derived class: an override of a function the virtual base introduced takes the virtual
    // base's own `this` (the game's 0xe73314 slots 18 and 19 are `sub ecx, [ecx-4]; jmp`).
    auto expected = [](unsigned s, uint32_t vbase, bool derived) -> int32_t {
        if (s < 3) return int32_t(vbase);
        if (s == 18 || s == 19) return derived ? 0 : -2;
        return int32_t(vbase - 0x30);
    };
    struct Table {
        const char *what;
        void *const *vt;
        uint32_t vbase;
        bool derived;
    };
    const Table tables[] = {
        {"game cSGResource 0xe695dc", static_cast<void *const *>(ctx.image.At(0xe695dc)), 0x30, false},
        {"our cSGResource", static_cast<void *const *>(gOwnSecondary), 0x30, false},
        {"game cGeometryDataContainer 0xe73314", static_cast<void *const *>(ctx.image.At(0xe73314)), 0x80, true},
        {"our displaced class", static_cast<void *const *>(gDisplacedSecondary), 0x3c, true},
    };
    for (const Table &t : tables) {
        for (unsigned s = 0; s < 25; ++s) {
            const int32_t want = expected(s, t.vbase, t.derived);
            if (want == -2) continue; // _purecall
            // cGeometryDataContainer overrides 3-6 itself (in its +0x2c face) and some cSGResource slots have bodies
            // of its own; compare only the slots that keep cSGResource's functions or are the primary's.
            if (t.vbase == 0x80 && !(s < 3 || s == 18 || s == 19 || s == 10 || s == 12 || s == 13 || s == 14 || s == 17 || s == 20))
                continue;
            ++ctx.cases;
            const int32_t got = thunk(t.vt[s]);
            if (got != want) ctx.Fail("%s slot %u: thunk subtracts %d, the layout needs %d", t.what, s, got, want);
        }
    }
}
