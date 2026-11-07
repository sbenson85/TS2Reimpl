// cTSDecalEffect (src/ts/cTSDecalEffect.h), lane WS: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xbfd440 (or constructor 0xbfc3f0), mode 1 with ours; every slot then runs
// through the object's own vtable. After each step: the result, the log of every fake's calls, both vtable words and every field.
// At the end the destructor through the +4 face's deleting slot (flags 0 leaves the block to compare, flags 1 frees it), with the
// frees by size. Everything runs at 53 and at 24 bits (difftest::X87Precision): the curve products are x87 sites and follow the
// thread's precision control. The game's random generator (0x10fd138) is seeded alike on both sides.
//
// Fakes: an owner whose slot 26 answers the overlay manager (one of two by the water flag, or none), managers that hand out decals
// from a pool (or none, when failing), decals that hold their numbers and give an overlay, overlays that log. Floats by their bits.
//
// A world is `tame` or not. An untamed one has a single colour and takes any time step (NaN, infinities, huge). A tamed one keeps
// the time finite, not negative outside the loop mode, because of the ORIGINAL-BUG: t outside [0, 1] reads past a colour list.
// Curves, scales, placements, events and offsets take NaNs (signalling too) in both. Not run: a null owner (called through) and an
// empty colour list (read at null): the two crash on both sides.
//
// Covered RVAs: 0xbfd440 (Create), 0xbfc3f0 (constructor), 0xbfc700 (destructor), 0xbfc800 (deleting destructor), 0xbfc7ed (its +4
// thunk), 0xbfcbe0 (slot 0), 0xbfd010 (slot 2), 0xbfd3f0 (slot 3), 0xbfc830 (slot 5), 0xbfcd90 (slot 6), 0xbfcce0 (slot 7),
// 0xbfcd70 (slot 9).
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSDecalEffect.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace {

using namespace fxtest;

constexpr uint32_t kGameTables[2] = {0xe91214, 0xe91250};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8};
constexpr uint32_t kWords = sizeof(cTSDecalEffect) / 4;

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t {
    evOwner = 0xc1000000,    // owner's slot 26: the owner, the flag
    evRef = 0xc2000000,      // | delta byte: the fake's id
    evCreate = 0xc3000000,   // manager's slot 5: the manager, position, size, rotation, the texture's length and bytes
    evRemove = 0xc4000000,   // manager's slot 9: the manager, the overlay
    evSet = 0xc5000000,      // decal's slot 4: the decal, the ten numbers
    evGet = 0xc6000000,      // decal's slot 5
    evOverlay = 0xc7000000,  // decal's slot 6
    evVisible = 0xc8000000,
    evAlpha = 0xc9000000,
    evColor = 0xca000000,
    evDraw = 0xcb000000,
    evOther = 0xcc000000,    // | slot: a slot the effect has no business calling
};

struct Fake {
    void *const *vt;
    uint32_t id;
    uint32_t refs;
};
struct FakeOverlay : Fake {};
struct FakeDecal : Fake {
    FakeOverlay *overlay;
    uint32_t params[11]; // 0x2c bytes, from GetParams' and SetParams' own code (0xbdad90, 0xbdbea0), not from our struct
    bool spareKnown;     // the last word's value is only defined after a GetParams: AddToScene hands over a stale stack word
};
struct FakeManager : Fake {};
struct FakeOwnerWithManager : Fake {
    FakeManager *answer[2];
};

struct World {
    FakeOverlay overlays[8];
    FakeDecal decals[8];
    FakeManager managers[2];
    FakeOwnerWithManager owners[2];
    uint32_t created = 0;
    uint32_t failEvery = 0; // 0: never
};
World *gDecalWorld = nullptr;

uint32_t PointerId(uint32_t word) { return word ? reinterpret_cast<const Fake *>(uintptr_t(word))->id : 0; }

uint32_t __fastcall AddRefOf(Fake *f, void *)
{
    Ev({evRef | 1, f->id});
    return ++f->refs;
}
uint32_t __fastcall ReleaseOf(Fake *f, void *)
{
    Ev({evRef | 2, f->id});
    return --f->refs;
}
template <unsigned Slot> uint32_t __fastcall OtherSlot(Fake *f, void *)
{
    Ev({evOther | Slot, f->id});
    return 0;
}

FakeManager *__fastcall OwnerManager(FakeOwnerWithManager *o, void *, uint32_t flag)
{
    Ev({evOwner, o->id, flag});
    return o->answer[flag & 1];
}

FakeDecal *__fastcall CreateDecal(FakeManager *m, void *, const char *texture, const float *position, float size, float rotation)
{
    const uint32_t length = uint32_t(std::strlen(texture));
    Ev({evCreate, m->id, Bits(position[0]), Bits(position[1]), Bits(position[2]), Bits(size), Bits(rotation), length});
    for (uint32_t i = 0; i < length; ++i) Ev({uint32_t(uint8_t(texture[i]))});
    World &w = *gDecalWorld;
    const uint32_t n = w.created++;
    if (w.failEvery && n % w.failEvery == w.failEvery - 1) return nullptr;
    return &w.decals[n % 8];
}
void __fastcall RemoveOverlay(FakeManager *m, void *, FakeOverlay *overlay) { Ev({evRemove, m->id, overlay->id}); }

void __fastcall DecalSet(FakeDecal *d, void *, const uint32_t *p)
{
    Ev({evSet, d->id});
    gEv.insert(gEv.end(), p, p + 10);
    std::memcpy(d->params, p, 10 * sizeof(uint32_t));
    if (d->spareKnown) {
        gEv.push_back(p[10]);
        d->params[10] = p[10];
    }
    d->spareKnown = false;
}
void __fastcall DecalGet(FakeDecal *d, void *, uint32_t *p)
{
    Ev({evGet, d->id});
    std::memcpy(p, d->params, sizeof(d->params));
    d->spareKnown = true;
}
FakeOverlay *__fastcall DecalOverlay(FakeDecal *d, void *)
{
    Ev({evOverlay, d->id});
    return d->overlay;
}

void __fastcall OverlayVisible(FakeOverlay *o, void *, uint32_t value) { Ev({evVisible, o->id, value & 0xff}); }
void __fastcall OverlayAlpha(FakeOverlay *o, void *, float alpha) { Ev({evAlpha, o->id, Bits(alpha)}); }
void __fastcall OverlayColor(FakeOverlay *o, void *, const float *c) { Ev({evColor, o->id, Bits(c[0]), Bits(c[1]), Bits(c[2])}); }
void __fastcall OverlayDraw(FakeOverlay *o, void *, uint32_t draw) { Ev({evDraw, o->id, draw}); }

void *gOverlayVT[16], *gDecalVT[8], *gManagerVT[16], *gOwnerVT[32];
template <size_t... I> void FillOthers(void **table, std::index_sequence<I...>)
{
    ((table[I] = reinterpret_cast<void *>(&OtherSlot<unsigned(I)>)), ...);
}
template <size_t N> void Common(void *(&table)[N])
{
    FillOthers(table, std::make_index_sequence<N>());
    table[1] = reinterpret_cast<void *>(&AddRefOf);
    table[2] = reinterpret_cast<void *>(&ReleaseOf);
}
const bool gFakeVTablesFilled = [] {
    Common(gOverlayVT);
    Common(gDecalVT);
    Common(gManagerVT);
    Common(gOwnerVT);
    gOverlayVT[3] = reinterpret_cast<void *>(&OverlayVisible);
    gOverlayVT[4] = reinterpret_cast<void *>(&OverlayAlpha);
    gOverlayVT[5] = reinterpret_cast<void *>(&OverlayColor);
    gOverlayVT[6] = reinterpret_cast<void *>(&OverlayDraw);
    gDecalVT[4] = reinterpret_cast<void *>(&DecalSet);
    gDecalVT[5] = reinterpret_cast<void *>(&DecalGet);
    gDecalVT[6] = reinterpret_cast<void *>(&DecalOverlay);
    gManagerVT[5] = reinterpret_cast<void *>(&CreateDecal);
    gManagerVT[9] = reinterpret_cast<void *>(&RemoveOverlay);
    gOwnerVT[26] = reinterpret_cast<void *>(&OwnerManager);
    return true;
}();

void BuildWorld(World &w, std::mt19937 &r)
{
    (void)gFakeVTablesFilled;
    for (uint32_t i = 0; i < 8; ++i) {
        w.overlays[i] = {{gOverlayVT, 0x10 + i, 0}};
        w.decals[i] = {{gDecalVT, 0x20 + i, 0}, &w.overlays[i], {}};
        for (uint32_t &p : w.decals[i].params) p = r();
    }
    w.managers[0] = {{gManagerVT, 0x30, 0}};
    w.managers[1] = {{gManagerVT, 0x31, 0}};
    w.owners[0] = {{gOwnerVT, 0x40, 0}, {&w.managers[0], &w.managers[1]}};
    w.owners[1] = {{gOwnerVT, 0x41, 0}, {r() % 2 ? nullptr : &w.managers[1], r() % 2 ? nullptr : &w.managers[0]}};
    w.failEvery = r() % 3 ? 0 : 2 + r() % 4;
}

struct OurDecalTables {
    EffectVt live, destroyed;
};
const OurDecalTables &OurTables()
{
    static OurDecalTables t = [] {
        OurDecalTables v{};
        static cTSDecalDescription description;
        alignas(8) static uint8_t block[sizeof(cTSDecalEffect)];
        ::new (block) cTSDecalEffect(&description);
        v.live = EffectVt::Of(block);
        CallSlot<void *>(block + 4, 0, uint32_t(0));
        v.destroyed = EffectVt::Of(block);
        return v;
    }();
    return t;
}

uint32_t TableCode(uint32_t word)
{
    const OurDecalTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

uint32_t StringTableWord(uint32_t word) { return word - uint32_t(reinterpret_cast<uintptr_t>(GameAddress<void>(cRZString::kVTable))); }

void PushHead(Blob &b, const uint32_t *w, const void *description)
{
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    b.push_back(w[2]);
    b.push_back(uint32_t(w[3] - uint32_t(reinterpret_cast<uintptr_t>(description))));
}

// The whole object, on a block both sides filled alike: every byte the constructor or destructor did not store must stay.
void PushRaw(Blob &b, const void *block, const void *description)
{
    const auto *w = static_cast<const uint32_t *>(block);
    PushHead(b, w, description);
    for (uint32_t i = 4; i < kWords; ++i) {
        if (i == 0xc0 / 4 || i == 0xc4 / 4) b.push_back(PointerId(w[i]));
        else if (i == 0xc8 / 4) b.push_back(StringTableWord(w[i]));
        else b.push_back(w[i]);
    }
}

// A live effect on the heap: padding and the time are what the allocator left until a Start has set them.
void PushLive(Blob &b, const void *effect, const void *description, bool timeSet)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    PushHead(b, w, description);
    b.push_back(w[4] & 0xff);
    for (uint32_t i = 5; i < 0xb4 / 4; ++i) b.push_back((i == 0x14 / 4 || i == 0x4c / 4) ? w[i] & 0xffff00ffu : w[i]);
    b.push_back(w[0xb4 / 4] & 0xff);
    if (timeSet) b.push_back(w[0xb8 / 4]);
    b.push_back(w[0xbc / 4]);
    b.push_back(PointerId(w[0xc0 / 4]));
    b.push_back(PointerId(w[0xc4 / 4]));
    b.push_back(StringTableWord(w[0xc8 / 4]));
    const auto *text = reinterpret_cast<const cRZString *>(static_cast<const uint8_t *>(effect) + 0xc8);
    b.push_back(uint32_t(text->mString.size()));
    b.push_back(uint32_t(text->mString.capacity()));
    for (const char c : text->mString) b.push_back(uint32_t(uint8_t(c)));
    b.push_back(text->mRefCount);
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

float QuietNaN(std::mt19937 &r) { return std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu) | (r() << 31)); }
float SignallingNaN(std::mt19937 &r) { return std::bit_cast<float>(0x7f800001u | (r() & 0x3ffffeu) | (r() << 31)); }

// A number for a curve, scale or offset. Fractions, so a product's rounding shows at either precision.
float Number(std::mt19937 &r)
{
    switch (r() % 16) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return -1.0f;
    case 3: return 123456.7f;
    case 4: return QuietNaN(r);
    case 5: return SignallingNaN(r);
    case 6: return std::numeric_limits<float>::infinity();
    case 7: return 1e-40f;
    case 8: return 3e38f;
    default: return std::uniform_real_distribution<float>(-3.0f, 3.0f)(r);
    }
}

void RandomCurve(cTSEffectCurve &c, std::mt19937 &r)
{
    const uint32_t kind = r() % 8;
    if (kind == 0) {
        c.mKind = cTSEffectCurve::kWave;
        c.mValues.resize(4);
    } else if (kind == 1) {
        c.mKind = 0x80000000u; // any other kind reads as 0
        c.mValues.resize(2);
    } else {
        c.mKind = cTSEffectCurve::kList;
        c.mValues.resize(1 + r() % 4);
    }
    for (float &v : c.mValues) v = Number(r);
}

void RandomDescription(cTSDecalDescription &d, std::mt19937 &r, bool tame)
{
    d.mFlags = r() & 0x7f;
    d.mDraw = uint8_t(r());
    d.mLifeMode = uint8_t(r() % 4);
    static const char *const textures[] = {"", "decal", "a texture name of more than fifteen characters"};
    d.mTexture = textures[r() % 3];
    switch (r() % 8) {
    case 0: d.mLife = 0.0f; break;
    case 1: d.mLife = -2.0f; break;
    case 2: d.mLife = QuietNaN(r); break;
    case 3: d.mLife = std::numeric_limits<float>::infinity(); break;
    case 4: d.mLife = 1e-5f; break;
    case 5: d.mLife = 0.5f * float(1 + r() % 4); break; // the time lands on the end exactly
    default: d.mLife = std::uniform_real_distribution<float>(0.1f, 6.0f)(r);
    }
    RandomCurve(d.mRotate, r);
    RandomCurve(d.mSize, r);
    RandomCurve(d.mAlpha, r);
    RandomCurve(d.mAspect, r);
    d.mColors.resize(tame ? 1 + r() % 5 : 1);
    for (fxdesc::Color &c : d.mColors) c = fxdesc::Color(Number(r), Number(r), Number(r));
    d.mAlphaVary = Number(r);
    d.mSizeVary = Number(r);
    d.mRotateVary = Number(r);
    d.mRepeat = Number(r);
    d.mOffset.x = Number(r);
    d.mOffset.y = Number(r);
}

void SeedRandom(uint32_t seed)
{
    auto *g = GameAddress<cRZRandom>(0x10fd138);
    g->mIntegerSeed = seed * 2654435761u + 12345u;
    g->mRealSeed = int32_t(seed * 40503u + 77u);
}
void PushRandom(Blob &b)
{
    const auto *g = GameAddress<cRZRandom>(0x10fd138);
    b.push_back(g->mIntegerSeed);
    b.push_back(uint32_t(g->mRealSeed));
}

float TimeStep(std::mt19937 &r, bool tame, bool mayGoBack)
{
    if (!tame) {
        switch (r() % 6) {
        case 0: return QuietNaN(r);
        case 1: return SignallingNaN(r);
        case 2: return std::numeric_limits<float>::infinity();
        case 3: return -std::numeric_limits<float>::infinity();
        case 4: return 5e9f;
        }
    }
    switch (r() % 8) {
    case 0: return 0.0f;
    case 1: return mayGoBack ? -0.25f : 0.125f;
    case 2: return 0.01f;
    case 3: return 0.25f * float(1 + r() % 4);
    default: return std::uniform_real_distribution<float>(0.0f, 1.5f)(r);
    }
}

void RandomPlacement(fx::EffectsTransform &t, std::mt19937 &r, bool tidy)
{
    RandomTransform(t, r);
    if (tidy) { // a rotation about the vertical, so the angle is a fine number and not a NaN most of the time
        const float a = std::uniform_real_distribution<float>(-6.0f, 6.0f)(r);
        t.m[0][0] = std::cos(a);
        t.m[0][1] = -std::sin(a);
        t.m[0][2] = 0.0f;
        t.m[1][0] = std::sin(a);
        t.m[1][1] = std::cos(a);
        t.m[1][2] = 0.0f;
        t.m[2][0] = 0.0f;
        t.m[2][1] = 0.0f;
        t.m[2][2] = 1.0f;
    }
}

void PlaySeed(difftest::Context &ctx, CreateFn original, uint32_t seed, const char *name)
{
    Blob blobs[2];
    for (int mode = 0; mode < 2; ++mode) {
        Blob &b = blobs[mode];
        std::mt19937 r(seed * 7919u + 3);
        World w;
        gDecalWorld = &w;
        BuildWorld(w, r);
        gEv.clear();
        SeedRandom(seed);
        const bool tame = seed % 4 != 0;
        cTSDecalDescription description;
        RandomDescription(description, r, tame);
        // A NaN held on the x87 against a signalling size factor of a larger significand (the size chain's fmul from memory:
        // the held one wins). Rare by chance, and on its own generator so the worlds above stay the same.
        std::mt19937 nan(seed ^ 0x5eedu);
        if (nan() % 8 == 0)
            for (float &v : description.mSize.mValues) v = std::bit_cast<float>(0x7fc00000u | (nan() & 0xfffffu));
        void *e = mode ? static_cast<void *>(cTSDecalEffect::Create(&description)) : original(&description);
        auto *object = static_cast<cTSDecalEffect *>(static_cast<cITSComponentEffect *>(e));
        PushLive(b, e, &description, false);
        PushRandom(b);
        CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2); // the caller's reference
        uint32_t context[9] = {};
        context[7] = r();
        bool timeSet = false;
        CallSlot<void>(e, 0, static_cast<void *>(&w.owners[0]));
        for (uint32_t step = 0; step < 60; ++step) {
            b.push_back(0x0b000000u | step);
            uint32_t result = 0;
            const uint32_t pick = step == 1 ? 4 : r() % 16;
            switch (pick) {
            case 0: case 1: case 2: case 3: case 13: case 14: {
                const float dt = TimeStep(r, tame, description.mLifeMode == 0);
                CallSlot<void>(e, 5, reinterpret_cast<void *>(uintptr_t(Bits(dt))), static_cast<void *>(context));
                b.push_back(context[7]);
                break;
            }
            case 4: case 5:
                CallSlot<void>(e, 2, uint32_t(r() % 2 ? 0 : r() | 1));
                break;
            case 6:
                CallSlot<void>(e, 3, uint32_t(r()));
                break;
            case 7: case 8: {
                fx::EffectsTransform local, parent;
                RandomPlacement(local, r, r() % 4 != 0);
                RandomPlacement(parent, r, r() % 4 != 0);
                CallSlot<void>(e, 6, &local, &parent);
                break;
            }
            case 9: {
                float rectangle[4] = {Number(r), Number(r), Number(r), Number(r)};
                if (r() % 2) { // a plain one
                    rectangle[0] = std::uniform_real_distribution<float>(-3.0f, 0.0f)(r);
                    rectangle[1] = std::uniform_real_distribution<float>(-3.0f, 0.0f)(r);
                    rectangle[2] = std::uniform_real_distribution<float>(0.1f, 3.0f)(r);
                    rectangle[3] = std::uniform_real_distribution<float>(0.1f, 3.0f)(r);
                }
                const uint32_t which = r() % 8;
                const uint32_t kind = which <= 2 ? 3 : which == 3 ? 0xb : which <= 5 ? 0x101 : r() % 2 ? 2 + r() % 12 : 0x100 + r() % 4;
                if (kind == 0x101 && nan() % 2) rectangle[1] = std::bit_cast<float>(0x7fbfffffu | (nan() << 31));
                b.push_back(kind);
                result = CallSlot<uint8_t>(e, 7, kind, static_cast<void *>(rectangle), reinterpret_cast<void *>(uintptr_t(r())));
                break;
            }
            case 10:
                CallSlot<void>(e, 9, bool(r() % 2));
                break;
            case 11:
                if (r() % 4 == 0) {
                    // Nothing in the image writes the first placement either: Attach's reset shows on this one.
                    for (float &v : object->mPlacement.m[1]) v = Number(r);
                    object->mPlacement.flags = uint8_t(r());
                    object->mPlacement.pad[1] = uint8_t(r());
                    object->mPlacement.pad[2] = uint8_t(r());
                    object->mPlacement.scale = Number(r);
                    object->mPlacement.translation.y = Number(r);
                    CallSlot<void>(e, 0, static_cast<void *>(&w.owners[r() % 2]));
                }
                break;
            case 12:
                // Nothing in the image sets the override texture: this stands in for the writer.
                if (r() % 2) object->mTexture.mString.assign(r() % 2 ? "short" : "an override texture name that is rather long");
                break;
            case 15:
                switch (r() % 5) {
                case 0: result = CallSlot<uint8_t>(e, 4); break;
                case 1: CallSlot<void>(e, 1); break;
                case 2: result = CallSlot<uint8_t>(e, 8, reinterpret_cast<void *>(uintptr_t(r())), reinterpret_cast<void *>(uintptr_t(r()))); break;
                case 3:
                    CallSlot<void>(e, 10, uint32_t(r()));
                    CallSlot<void>(e, 11, uint32_t(r()));
                    break;
                default: // the count through both faces; the caller's reference stays
                    result = CallSlot<uint32_t>(e, 12);
                    b.push_back(CallSlot<uint32_t>(e, 13));
                    b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2));
                    b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 1));
                }
                break;
            }
            b.push_back(pick);
            b.push_back(result);
            Drain(b);
            timeSet = timeSet || object->mDecal.Get() != nullptr;
            PushLive(b, e, &description, timeSet);
        }
        b.push_back(0x0b0000ffu);
        // The end: the deleting destructor through the count's face.
        const bool free = seed % 2 != 0;
        alignas(8) uint8_t after[sizeof(cTSDecalEffect)];
        difftest::FreeRecorder rec;
        if (free) {
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
        } else {
            b.push_back(uint32_t(CallSlot<void *>(static_cast<uint8_t *>(e) + 4, 0, uint32_t(0)) == e));
            std::memcpy(after, e, sizeof(after));
        }
        rec.Stop();
        Drain(b);
        size_t sizes[4] = {};
        const size_t freed = rec.SortedSizes(sizes, 4);
        b.push_back(uint32_t(freed));
        b.push_back(uint32_t(rec.Freed(e)));
        for (size_t i = 0; i < freed && i < 4; ++i) b.push_back(uint32_t(sizes[i]));
        if (!free) {
            const auto *words = reinterpret_cast<const uint32_t *>(after);
            PushHead(b, words, &description);
            b.push_back(words[4] & 0xff);
            for (uint32_t i = 0x84 / 4; i < 0xb4 / 4; ++i) b.push_back(words[i]);
            b.push_back(PointerId(words[0xc0 / 4]));
            b.push_back(PointerId(words[0xc4 / 4]));
            b.push_back(StringTableWord(words[0xc8 / 4]));
            b.push_back(words[0xcc / 4] & 0xff);
            b.push_back(words[0xdc / 4]);
            b.push_back(words[0xe0 / 4]);
            b.push_back(words[0xe4 / 4]);
            ::operator delete(e);
        }
        Drain(b);
        // Every counted reference the effect took has gone back.
        for (const Fake &f : w.overlays) b.push_back(f.refs);
        for (const FakeDecal &f : w.decals) {
            b.push_back(f.refs);
            for (const uint32_t p : f.params) b.push_back(p);
        }
        for (const Fake &f : w.managers) b.push_back(f.refs);
        for (const FakeOwnerWithManager &f : w.owners) b.push_back(f.refs);
        gDecalWorld = nullptr;
    }
    Same(ctx, blobs[0], blobs[1], name, seed);
}

} // namespace

DIFF_TEST(DecalEffect_Construct, 0xbfc3f0)
{
    OurTables(); // builds an effect, which draws random numbers
    for (const difftest::X87Pc pc : {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24}) {
        difftest::X87Precision precision(pc);
        for (uint32_t seed = 0; seed < 300; ++seed) {
            Blob blobs[2];
            for (int mode = 0; mode < 2; ++mode) {
                Blob &b = blobs[mode];
                std::mt19937 r(seed + 1);
                cTSDecalDescription description;
                RandomDescription(description, r, false);
                alignas(8) uint8_t block[sizeof(cTSDecalEffect)];
                for (uint8_t &x : block) x = uint8_t(r());
                SeedRandom(seed);
                void *e = mode ? static_cast<void *>(::new (block) cTSDecalEffect(&description))
                               : ctx.Original<ConstructFn>(0xbfc3f0)(block, &description);
                b.push_back(uint32_t(e == block));
                PushRaw(b, block, &description);
                PushRandom(b);
                CallSlot<void *>(block + 4, 0, uint32_t(0));
                PushRaw(b, block, &description);
            }
            Same(ctx, blobs[0], blobs[1], "DecalEffect_Construct", seed);
        }
    }
}

DIFF_TEST(DecalEffect_Slots53, 0xbfd440)
{
    difftest::X87Precision precision(difftest::X87Pc::Bits53);
    OurTables(); // builds an effect, which draws random numbers
    const CreateFn original = ctx.Original<CreateFn>(0xbfd440);
    for (uint32_t seed = 0; seed < 1000; ++seed) PlaySeed(ctx, original, seed, "DecalEffect_Slots53");
}

DIFF_TEST(DecalEffect_Slots24, 0xbfd440)
{
    difftest::X87Precision precision(difftest::X87Pc::Bits24);
    OurTables(); // builds an effect, which draws random numbers
    const CreateFn original = ctx.Original<CreateFn>(0xbfd440);
    for (uint32_t seed = 0; seed < 1000; ++seed) PlaySeed(ctx, original, seed + 5000, "DecalEffect_Slots24");
}
