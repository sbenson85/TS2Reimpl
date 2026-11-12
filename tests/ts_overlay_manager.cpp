// cTSOverlayManager, cTSOverlay, cTSDecal, cTSOverlayTable and cTSTestMap (src/ts/cTSOverlayManager.h), lane RX: original vs
// ours.
//
// One random script runs twice, side 0 on a manager the original factory 0xbdaf50 made, side 1 on ours; every call goes
// through the object's vtable (the original's slots or ours), so the two share only the fakes. The pools 0x107d9a8 and
// 0x107d9b8 are reset before each side and their Malloc is an arena reset alike, so overlays land at the same addresses.
// After each step: the result, the log of the fakes' calls, the manager's words, its list and visible vector, every
// overlay's and decal's words but the vtables and the list node, and the table's records and free list.
//
// Fakes (globals of the image, the same cells on both sides): the framework 0x10c9378 with its common system (slot 7: a
// counter), the texture manager 0x10f8f94 (slot 17 a name's id; 18, 19 counted in the log; 23 the texture), a texture with
// its device texture (sizes per name), the allocator service 0x10c95d4, and a ground that snaps and answers normals.
// Floats are random from a list with zeros, integers, ones and NaNs at the corners. x87 sites run at 53 and 24 bits.
//
// A record's bytes the original leaves unset (the first 0xa0 bytes of a new one, +0xa9, +0xae, +0xaf, +0xb5..) are masked
// by filling them before compare: all records get their padding cleared, and the first 0xa0 are always written by Update
// (only records handed out by Acquire are read).
//
// Covered RVAs: see the list at DIFF_TEST below.
#include "code_patch.h"
#include "difftest.h"
#include "rz/RZMemory.h"
#include "shape.h"
#include "ts/cTSOverlayManager.h"
#include "ts_effects_collection.h"

#include <bit>
#include <climits>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <list>
#include <random>
#include <string>
#include <vector>

namespace {

using fxtest::Blob;

constexpr uint32_t kFramework = 0x10c9378, kTextureCache = 0x10f8f94, kAllocCache = 0x10c95d4, kAllocGuard = 0x10c95d8;
constexpr uint32_t kOverlayPool = 0x107d9a8, kDecalPool = 0x107d9b8;

using ManagerFactory = void *(__cdecl *)();

std::vector<uint32_t> gLog;
void Note(uint32_t a) { gLog.push_back(a); }
void Note(uint32_t a, uint32_t b)
{
    gLog.push_back(a);
    gLog.push_back(b);
}

// The arena behind the pools.
constexpr size_t kArena = 0x40000;
alignas(16) uint8_t gArena[kArena];
size_t gArenaUsed = 0;
void *__cdecl ArenaMalloc(uint32_t bytes)
{
    const size_t at = gArenaUsed;
    gArenaUsed += (size_t(bytes) + 15) & ~size_t(15);
    return gArenaUsed > kArena ? nullptr : gArena + at;
}

template <typename T> uint32_t Word(T *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }

// Fakes.
struct FakeCommon {
    virtual void *V0() { return nullptr; }
    virtual uint32_t V1() { return 0; }
    virtual uint32_t Release() { Note(0xa2); return 0; }
    virtual void V3() {}
    virtual void V4() {}
    virtual void V5() {}
    virtual void V6() {}
    virtual uint32_t NextId() { Note(0xa7); return ++ids; }
    uint32_t ids = 0;
} gCommon;

struct FakeFramework {
    virtual void V0() {}
    virtual void V1() {}
    virtual void V2() {}
    virtual void V3() {}
    virtual void V4() {}
    virtual uint8_t GetSystemService(uint32_t a, uint32_t b, void **out)
    {
        Note(0xb5, a);
        Note(b);
        *out = &gCommon;
        return 1;
    }
} gFramework;

struct FakeDevice {
    virtual void V0() {}
    virtual void V1() {}
    virtual void V2() {}
    virtual void V3() {}
    virtual void V4() {}
    virtual void V5() {}
    virtual bool Ready() { Note(0xd6); return ready; }
    virtual void V7() {}
    virtual void V8() {}
    virtual void V9() {}
    virtual void V10() {}
    virtual void V11() {}
    virtual void V12() {}
    virtual void V13() {}
    virtual void V14() {}
    virtual void V15() {}
    virtual void V16() {}
    virtual int32_t Width(uint32_t level) { Note(0xd11, level); return width; }
    virtual int32_t Height(uint32_t level) { Note(0xd12, level); return height; }
    bool ready = true;
    int32_t width = 64, height = 128;
} gDevice;

struct FakeTexture {
    virtual void V0() {}
    virtual void V1() {}
    virtual void V2() {}
    virtual void V3() {}
    virtual void V4() {}
    virtual void V5() {}
    virtual void V6() {}
    virtual void V7() {}
    virtual void *Device(uint32_t face) { Note(0xe8, face); return device; }
    void *device = &gDevice;
} gTexture;

bool gNoTexture = false;

struct FakeTextures {
    virtual void V0() {}
    virtual void V1() {}
    virtual void V2() {}
    virtual void V3() {}
    virtual void V4() {}
    virtual void V5() {}
    virtual void V6() {}
    virtual void V7() {}
    virtual void V8() {}
    virtual void V9() {}
    virtual void V10() {}
    virtual void V11() {}
    virtual void V12() {}
    virtual void V13() {}
    virtual void V14() {}
    virtual void V15() {}
    virtual void V16() {}
    virtual uint32_t Find(const char *name)
    {
        uint32_t id = 0x40;
        for (const char *c = name; *c; ++c) id = id * 31 + uint8_t(*c);
        Note(0xf17, id);
        return id & 0xffff;
    }
    virtual void Count(uint32_t id) { Note(0xf18, id); }
    virtual void Uncount(uint32_t id) { Note(0xf19, id); }
    virtual void V20() {}
    virtual void V21() {}
    virtual void V22() {}
    virtual void *Get(uint32_t id)
    {
        Note(0xf23, id);
        return gNoTexture ? nullptr : &gTexture;
    }
} gTextures;

float gNormal[3] = {0, 0, 1};
bool gHasNormal = true;
float gSnap = 0;

struct FakeGround {
    virtual void *V0() { return nullptr; }
    virtual uint32_t AddRef() { Note(0x91, id); return ++refs; }
    virtual uint32_t Release() { Note(0x92, id); return --refs; }
    virtual void Snap(float *p)
    {
        Note(0x93, std::bit_cast<uint32_t>(p[2]));
        p[2] += gSnap;
    }
    virtual bool Normal(const float *p, float *n)
    {
        Note(0x94, std::bit_cast<uint32_t>(p[2]));
        if (gHasNormal) std::memcpy(n, gNormal, 12);
        return gHasNormal;
    }
    uint32_t id = 0, refs = 0;
} gGrounds[2];

void *__fastcall ServiceAllocate(void *, void *, uint32_t size) { return ::operator new(size); }
void __fastcall ServiceFree(void *, void *, void *block) { ::operator delete(block); }
void *__fastcall ServiceOther(void *, void *) { return nullptr; }
void *gServiceVT[8];
struct {
    void **vtable = gServiceVT;
} gService;

// A status string: only slot 31 is called.
int gStatusA, gStatusB;
std::string gStatusFormat;
void __cdecl StringSprintf(void *, const char *format, int a, int b)
{
    gStatusFormat = format;
    gStatusA = a;
    gStatusB = b;
}
void *gStringVT[40];
struct {
    void **vtable = gStringVT;
} gString;

float Pick(std::mt19937 &r)
{
    static const float kList[] = {0.0f,  -0.0f, 1.0f,  -1.0f, 0.5f,   0.25f,    2.0f,   3.0f,    4.0f,  8.0f, 16.0f, 0.05f, 0.96f,
                                  1.5f,  63.0f, 64.0f, 65.0f, -3.25f, 1234.5f,  0.1f,   1e-3f,   1e6f};
    const uint32_t k = r() % 8;
    if (r() % 64 == 0) return r() % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    if (k < 3) return kList[r() % std::size(kList)];
    if (k < 6) return float(int32_t(r() % 4096) - 2048) / 64.0f;
    return float(int32_t(r() % 2000001) - 1000000) / 1000.0f;
}

// Positive and finite, for the sizes that divide.
float PickSize(std::mt19937 &r) { return float(1 + r() % 512) / 16.0f; }

struct Pointers {
    // The list and the vectors, as words.
    static void AddManager(Blob &out, const uint8_t *m)
    {
        for (uint32_t at = 0x10; at < 0x40; at += 4) out.push_back(*reinterpret_cast<const uint32_t *>(m + at));
        out.push_back(*reinterpret_cast<const uint32_t *>(m + 0x44));
        out.push_back(*reinterpret_cast<const uint32_t *>(m + 0x48));
        out.push_back(*reinterpret_cast<const uint32_t *>(m + 0x4c));
        const auto *head = *reinterpret_cast<uint32_t *const *>(m + 0x40);
        for (auto *node = reinterpret_cast<uint32_t *>(uintptr_t(head[0])); node != head;
             node = reinterpret_cast<uint32_t *>(uintptr_t(node[0]))) {
            out.push_back(node[2]);
            // The overlay's node word points back here.
            out.push_back(*reinterpret_cast<uint32_t *>(uintptr_t(node[2] + 0xd0)) == Word(node));
        }
        const auto *first = *reinterpret_cast<const uint32_t *const *>(m + 0x50);
        const auto *last = *reinterpret_cast<const uint32_t *const *>(m + 0x54);
        for (const uint32_t *p = first; p != last; ++p) out.push_back(*p);
    }
};

// An overlay's words but the vtables and the list node; the texture word stays (a fake's id).
void AddOverlay(Blob &out, const uint8_t *o)
{
    uint32_t word;
    for (uint32_t at = 0xc; at < 0xd0; at += 4) {
        std::memcpy(&word, o + at, 4);
        if (at == 0x14) word &= 0xffff00ffu; // the +0x15 pad
        out.push_back(word);
    }
    std::memcpy(&word, o + 8, 4); // the count
    out.push_back(word);
}

template <typename Fn> Fn Slot(void *object, uint32_t slot) { return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot]); }

using U1 = uint32_t(__thiscall *)(void *);

void AddTable(Blob &out, const cTSOverlayTable &table)
{
    for (const uint32_t index : table.mFree) out.push_back(index);
    out.push_back(0xfeedu);
    out.push_back(uint32_t(table.mRecords.size()));
    for (const cTSOverlayRecord &rec : table.mRecords) {
        out.push_back(rec.mActive);
        out.push_back(rec.mKey);
        if (!rec.mActive) continue; // an inactive one was written in an earlier pass; both sides hold the same anyway
        const auto *w = reinterpret_cast<const uint32_t *>(&rec);
        for (uint32_t i = 0; i < 0xa0 / 4; ++i) out.push_back(w[i]);
        out.push_back(rec.mColor);
        out.push_back(rec.mTexture);
        out.push_back(rec.mDraw);
        out.push_back(rec.mFlags);
        out.push_back(rec.mLayer);
    }
}

void ResetWorld(difftest::Context &ctx)
{
    *ctx.image.At<void *>(kFramework) = &gFramework;
    *ctx.image.At<void *>(kTextureCache) = &gTextures;
    *ctx.image.At<void *>(kAllocCache) = &gService;
    *ctx.image.At<int>(kAllocGuard) = INT_MIN;
    uint32_t *pools[2] = {ctx.image.At<uint32_t>(kOverlayPool), ctx.image.At<uint32_t>(kDecalPool)};
    for (uint32_t *pool : pools) pool[2] = pool[3] = 0;
    gArenaUsed = 0;
    std::memset(gArena, 0xcd, kArena);
    gCommon.ids = 0;
    gGrounds[0].refs = gGrounds[1].refs = 1;
    gGrounds[0].id = 1;
    gGrounds[1].id = 2;
    gLog.clear();
    gDevice.ready = true;
    gTexture.device = &gDevice;
    gNoTexture = false;
    gHasNormal = true;
    gSnap = 0;
}

// What the script overwrites of the image, put back at the end.
struct SavedGlobals {
    explicit SavedGlobals(difftest::Context &c) : ctx(c)
    {
        for (uint32_t i = 0; i < 4; ++i) words[i] = *ctx.image.At<uint32_t>(kCells[i]);
        std::memcpy(pools, ctx.image.At<uint32_t>(kOverlayPool), sizeof(pools));
    }
    ~SavedGlobals()
    {
        for (uint32_t i = 0; i < 4; ++i) *ctx.image.At<uint32_t>(kCells[i]) = words[i];
        std::memcpy(ctx.image.At<uint32_t>(kOverlayPool), pools, sizeof(pools));
    }
    static constexpr uint32_t kCells[4] = {kFramework, kTextureCache, kAllocCache, kAllocGuard};
    difftest::Context &ctx;
    uint32_t words[4];
    uint32_t pools[8];
};

const char *const kNames[] = {"", "overlay", "decal_shadow", "water-ring", "x"};

Blob Run(difftest::Context &ctx, bool ours, uint32_t seed)
{
    std::mt19937 r(seed);
    ResetWorld(ctx);
    Blob out;
    void *manager = ours ? static_cast<void *>(static_cast<cITSOverlayManager *>(cTSOverlayManager::Create()))
                         : ctx.Original<ManagerFactory>(0xbdaf50)();
    auto *m = static_cast<uint8_t *>(manager);
    Slot<U1>(manager, 1)(manager);
    for (uint32_t at = 0x10; at < 0x2c; at += 4) out.push_back(*reinterpret_cast<uint32_t *>(m + at));
    out.push_back(*reinterpret_cast<uint32_t *>(m + 0x3c));
    out.push_back(*reinterpret_cast<uint32_t *>(m + 0x48));
    out.push_back(*reinterpret_cast<uint32_t *>(m + 0x4c));
    std::vector<uint8_t *> overlays; // cITSOverlay faces, counted by us
    std::vector<uint8_t *> decals;   // cITSDecal faces, counted by us
    cTSOverlayTable table;

    out.push_back(Slot<bool(__thiscall *)(void *)>(manager, 3)(manager));
    if (r() % 4 == 0) out.push_back(Slot<bool(__thiscall *)(void *)>(manager, 3)(manager)); // up already

    const auto rect = [&](float *q) {
        q[0] = Pick(r);
        q[1] = q[0] + std::fabs(Pick(r));
        q[2] = Pick(r);
        q[3] = q[2] + std::fabs(Pick(r));
    };
    const auto matrix = [&](float *q) {
        for (int i = 0; i < 16; ++i) q[i] = Pick(r);
    };

    const uint32_t steps = 10 + r() % 50;
    for (uint32_t step = 0; step < steps; ++step) {
        uint32_t result = 0;
        const uint32_t op = r() % 24;
        float q[4], a[16], lo[2], hi[2];
        rect(q);
        matrix(a);
        lo[0] = Pick(r);
        lo[1] = Pick(r);
        hi[0] = Pick(r);
        hi[1] = Pick(r);
        uint8_t *o = overlays.empty() ? nullptr : overlays[r() % overlays.size()];
        uint8_t *d = decals.empty() ? nullptr : decals[r() % decals.size()];
        switch (op) {
        case 0: case 1: {
            const float position[3] = {Pick(r), Pick(r), Pick(r)};
            gDevice.ready = r() % 4 != 0;
            gDevice.width = 8 << (r() % 4);
            gDevice.height = 8 << (r() % 4);
            gTexture.device = r() % 5 ? &gDevice : nullptr;
            gNoTexture = r() % 6 == 0;
            auto *made = Slot<uint8_t *(__thiscall *)(void *, const char *, const float *, float, float)>(manager, 5)(
                manager, kNames[r() % 5], position, PickSize(r), r() % 3 ? 0.0f : Pick(r));
            decals.push_back(made);
            overlays.push_back(Slot<uint8_t *(__thiscall *)(void *)>(made, 6)(made));
            Slot<U1>(made, 1)(made); // the script's reference: a removed overlay stays readable
            result = uint32_t(made - gArena);
            break;
        }
        case 2: {
            auto *made = Slot<uint8_t *(__thiscall *)(void *, const char *, const float *, const float *)>(manager, 8)(
                manager, kNames[r() % 5], q, a);
            overlays.push_back(made);
            Slot<U1>(made, 1)(made); // the script's reference
            result = uint32_t(made - gArena);
            break;
        }
        case 3: {
            auto *made = Slot<uint8_t *(__thiscall *)(void *, const char *, const float *, const float *, const float *,
                                                      const float *)>(manager, 7)(manager, kNames[r() % 5], q, a, lo, hi);
            overlays.push_back(made);
            Slot<U1>(made, 1)(made); // the script's reference
            result = uint32_t(made - gArena);
            break;
        }
        case 4:
            if (o) Slot<void(__thiscall *)(void *, void *)>(manager, r() % 2 ? 9 : 6)(manager, o);
            break;
        case 5: case 6: case 7: case 8: {
            // Records or none; the table and the visible list both come back.
            const bool withTable = r() % 5 != 0;
            result = Slot<bool(__thiscall *)(void *, void *)>(manager, 11)(manager, withTable ? &table : nullptr);
            break;
        }
        case 9:
            Slot<void(__thiscall *)(void *, float, float, float, int32_t, int32_t)>(manager, 12)(
                manager, Pick(r), Pick(r), PickSize(r), int32_t(r() % 128), int32_t(r() % 128));
            break;
        case 10:
            Slot<void(__thiscall *)(void *, float, int32_t, int32_t)>(manager, 13)(manager, PickSize(r), int32_t(r() % 128),
                                                                                   int32_t(r() % 128));
            break;
        case 11: {
            const uint32_t pick = r() % 3;
            Slot<void(__thiscall *)(void *, void *)>(manager, 14)(manager, pick == 2 ? nullptr : &gGrounds[pick]);
            gSnap = r() % 2 ? 0.0f : Pick(r);
            gHasNormal = r() % 4 != 0;
            float n[3] = {Pick(r), Pick(r), Pick(r)};
            if (r() % 3 == 0) n[0] = n[1] = 0.0f, n[2] = 1.0f;
            std::memcpy(gNormal, n, 12);
            break;
        }
        case 12:
            if (o) Slot<void(__thiscall *)(void *, bool)>(o, 3)(o, r() % 2);
            break;
        case 13:
            if (o) {
                const float current = *reinterpret_cast<float *>(o + 0xa8);
                const uint32_t kind = r() % 6;
                float alpha = kind < 2 ? Pick(r) : current + (kind == 2 ? 0.01f : 0.055f); // either side of the 0.05 step
                if (kind >= 4) { // the ends always land, however small the step to them
                    alpha = float(r() % 2);
                    if (kind == 5) Slot<void(__thiscall *)(void *, float)>(o, 4)(o, alpha != 0.0f ? 0.97f : 0.03f);
                }
                Slot<void(__thiscall *)(void *, float)>(o, 4)(o, alpha);
            }
            break;
        case 14:
            if (o) {
                const float color[3] = {Pick(r), Pick(r), Pick(r)};
                Slot<void(__thiscall *)(void *, const float *)>(o, 5)(o, color);
            }
            break;
        case 15:
            if (o) Slot<void(__thiscall *)(void *, uint32_t)>(o, 6)(o, r() % 2 ? r() % 4 : r());
            break;
        case 16:
            if (o) Slot<void(__thiscall *)(void *, const char *)>(o, 7)(o, kNames[r() % 5]);
            break;
        case 17:
            if (o) Slot<void(__thiscall *)(void *, uint16_t)>(o, 8)(o, uint16_t(r()));
            break;
        case 18:
            if (o)
                Slot<void(__thiscall *)(void *, const float *, const float *, const float *, const float *, bool)>(o, 9)(
                    o, q, a, lo, hi, r() % 2);
            break;
        case 19:
            if (o) Slot<void(__thiscall *)(void *, const float *, const float *, bool)>(o, 10)(o, q, a, r() % 2);
            break;
        case 20:
            if (o) {
                float color[3] = {};
                out.push_back(Slot<bool(__thiscall *)(void *)>(o, 11)(o));
                out.push_back(std::bit_cast<uint32_t>(Slot<float(__thiscall *)(void *)>(o, 12)(o)));
                out.push_back(Slot<float *(__thiscall *)(void *, float *)>(o, 13)(o, color) == color);
                out.push_back(std::bit_cast<uint32_t>(color[0]));
                out.push_back(std::bit_cast<uint32_t>(color[1]));
                out.push_back(std::bit_cast<uint32_t>(color[2]));
                out.push_back(Slot<uint8_t(__thiscall *)(void *)>(o, 14)(o));
                out.push_back(Slot<uint16_t(__thiscall *)(void *)>(o, 15)(o));
            }
            break;
        case 21:
            if (d) {
                // Around the stored value by a few float steps, or anywhere.
                float position[3];
                std::memcpy(position, d + 0xd8, 12);
                for (float &v : position) {
                    uint32_t bits = std::bit_cast<uint32_t>(v);
                    bits += int32_t(r() % 81) - 40;
                    v = r() % 4 ? std::bit_cast<float>(bits) : Pick(r);
                }
                Slot<void(__thiscall *)(void *, const float *)>(d, 3)(d, position);
            }
            break;
        case 22:
            if (d) {
                cTSDecalParams p{};
                p.mPosition = {Pick(r), Pick(r), Pick(r)};
                p.mSize = PickSize(r);
                p.mRotation = r() % 2 ? 0.0f : Pick(r);
                p.mAspect = PickSize(r);
                p.mRepeatU = r() % 2 ? 1.0f : PickSize(r);
                p.mRepeatV = r() % 2 ? 1.0f : PickSize(r);
                p.mOffsetU = Pick(r);
                p.mOffsetV = Pick(r);
                p.mSpare = r();
                Slot<void(__thiscall *)(void *, const cTSDecalParams *)>(d, 4)(d, &p);
            }
            break;
        default:
            if (d) {
                cTSDecalParams p;
                Slot<void(__thiscall *)(void *, cTSDecalParams *)>(d, 5)(d, &p);
                out.insert(out.end(), reinterpret_cast<uint32_t *>(&p), reinterpret_cast<uint32_t *>(&p) + 11);
            }
            break;
        }
        out.push_back(result);
        Pointers::AddManager(out, m);
        for (uint8_t *face : overlays) AddOverlay(out, face);
        for (uint8_t *face : decals) out.insert(out.end(), reinterpret_cast<uint32_t *>(face + 0xd8), reinterpret_cast<uint32_t *>(face + 0x108));
        AddTable(out, table);
        out.push_back(0xabcd0000u + step);
    }

    gStatusFormat.clear();
    Slot<void(__thiscall *)(void *, void *)>(manager, 10)(manager, &gString);
    out.push_back(uint32_t(gStatusFormat.size()));
    out.push_back(gStatusA);
    out.push_back(gStatusB);
    for (const char c : gStatusFormat) out.push_back(uint8_t(c));

    // Every face's QueryInterface, for the interface, the base and something else.
    const auto ask = [&](uint8_t *object, uint32_t faces) {
        for (const uint32_t riid : {0xac22fa89u, 1u, 0x2c2a7106u, 0x12345u}) {
            for (uint32_t face = 0; face < faces; ++face) {
                void *subject = object + face * 4, *got = nullptr;
                out.push_back(Slot<bool(__thiscall *)(void *, uint32_t, void **)>(subject, 0)(subject, riid, &got));
                out.push_back(got ? uint32_t(static_cast<uint8_t *>(got) - object) : 0xffffu);
                if (got) Slot<U1>(got, 2)(got);
            }
        }
    };
    ask(m, 2);
    ask(m + 8, 1);
    for (uint8_t *face : overlays) ask(face, 2);
    for (uint8_t *face : decals) ask(face, 3);

    // The manager's face +4 (DoMessage), then down: our counts go, the manager's list goes.
    void *target = m + 4;
    out.push_back(Slot<bool(__thiscall *)(void *, void *)>(target, 3)(target, nullptr));
    out.push_back(Slot<bool(__thiscall *)(void *)>(manager, 4)(manager));
    out.push_back(Slot<bool(__thiscall *)(void *)>(manager, 4)(manager));
    for (uint8_t *face : overlays) AddOverlay(out, face);
    for (uint8_t *face : overlays) out.push_back(Slot<U1>(face, 2)(face));
    out.push_back(Slot<U1>(manager, 2)(manager));
    out.push_back(*ctx.image.At<uint32_t>(kOverlayPool + 0xc));
    out.push_back(*ctx.image.At<uint32_t>(kDecalPool + 0xc));
    out.insert(out.end(), gLog.begin(), gLog.end());
    return out;
}

} // namespace

// One side only on purpose: the game's own operator new and delete (CRT, library code; ours is the toolchain's and needs no
// patch), whose imports are traps in the harness. Neither target is a function this file tests.
struct GameHeapOnOurs {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(+[](size_t n) -> void * { return ::operator new(n); })};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(+[](void *p) { ::operator delete(p); })};
};

// 0xbdaf50 factory, 0xbd9fd0 constructor, 0xbda200 destructor, 0xbda420 deleting; slots 0xbdb190, 0xbdb6d0, 0xbda440, 0xbda820,
// 0xbda830, 0xbda710, 0xbda610, 0xbdb390, 0xbdb170, 0xbdc0b0, 0xbdb500, 0xbdb4d0.
// Reached by the script through the vtables and helpers: 0xbda346, 0xbdb350, 0xbdb377, 0xbdb37f, 0xbdabd0,
// 0xbdbef0, 0xbdb5d0, 0xbdb750, 0xbdafc0, 0xbda920, 0xbda960, 0xbda990, 0xbdaa10, 0xbdaaf0, 0xbd9e70, 0xbda3d0,
// 0xbda33e, 0xbdb5a0, 0xbdb420, 0xbdb490, 0xbdb570, 0xbdb650, 0xbdb6c0, 0xbdbf60, 0xbdc010, 0xbdb1d0, 0xbdabc0,
// 0xbdad70, 0xbdadd0, 0xbdc470, 0xbda380, 0xbda336, 0xbdb330, 0xbdb338, 0xbdb340, 0xbdb1e0, 0xbdbea0, 0xbdad90.
DIFF_TEST(OverlayManager_Script, 0xbdc0b0)
{
    GameHeapOnOurs heap;
    shape::StandIn malloc{0x1be3b0, &RZMemory::Malloc, &ArenaMalloc};
    for (auto &slot : gServiceVT) slot = reinterpret_cast<void *>(&ServiceOther);
    gServiceVT[3] = reinterpret_cast<void *>(&ServiceAllocate);
    gServiceVT[4] = reinterpret_cast<void *>(&ServiceFree);
    gStringVT[31] = reinterpret_cast<void *>(&StringSprintf);
    SavedGlobals saved(ctx);
    for (const difftest::X87Pc pc : {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24}) {
        difftest::X87Precision precision(pc);
        for (uint32_t seed = 1; seed <= 3000; ++seed) {
            ++ctx.cases;
            const Blob a = Run(ctx, false, seed * 2 + uint32_t(pc == difftest::X87Pc::Bits24));
            const Blob b = Run(ctx, true, seed * 2 + uint32_t(pc == difftest::X87Pc::Bits24));
            fxtest::Same(ctx, a, b, "overlay script", seed);
        }
    }
}

// 0xbd9700 factory; 0xbd99d0 QueryInterface with its thunk; slots 0xbd97a0, 0xbd9960, 0xbd97d0, 0xbd9860, 0xbd9b70.
// Also: 0xbd99f7.
DIFF_TEST(TestMap_Slots, 0xbd97d0)
{
    GameHeapOnOurs heap;
    const auto run = [&](bool ours, uint32_t seed) {
        std::mt19937 r(seed);
        Blob out;
        void *map = ours ? static_cast<void *>(static_cast<cITSEffectMap *>(cTSTestMap::Create()))
                         : ctx.Original<ManagerFactory>(0xbd9700)();
        Slot<U1>(map, 1)(map);
        float bounds[4] = {};
        Slot<void(__thiscall *)(void *, float *, float *, float *, float *)>(map, 3)(map, &bounds[0], &bounds[1], &bounds[2], &bounds[3]);
        for (const float b : bounds) out.push_back(std::bit_cast<uint32_t>(b));
        for (uint32_t i = 0; i < 400; ++i) {
            const float x = Pick(r), y = Pick(r);
            out.push_back(Slot<bool(__thiscall *)(void *, float, float)>(map, 4)(map, x, y));
            out.push_back(std::bit_cast<uint32_t>(Slot<float(__thiscall *)(void *, float, float)>(map, 5)(map, x, y)));
            float n[3] = {};
            out.push_back(Slot<float *(__thiscall *)(void *, float *, float, float)>(map, 6)(map, n, x, y) == n);
            for (const float v : n) out.push_back(std::bit_cast<uint32_t>(v));
        }
        out.push_back(std::bit_cast<uint32_t>(Slot<float(__thiscall *)(void *)>(map, 7)(map)));
        for (const uint32_t riid : {0x2c2a7106u, 1u, 0xac22fa89u}) {
            for (uint32_t face = 0; face < 2; ++face) {
                void *subject = static_cast<uint8_t *>(map) + face * 4, *got = nullptr;
                out.push_back(Slot<bool(__thiscall *)(void *, uint32_t, void **)>(subject, 0)(subject, riid, &got));
                out.push_back(got ? uint32_t(static_cast<uint8_t *>(got) - static_cast<uint8_t *>(map)) : 0xffffu);
                if (got) Slot<U1>(got, 2)(got);
            }
        }
        out.push_back(Slot<U1>(map, 2)(map));
        return out;
    };
    for (const difftest::X87Pc pc : {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24}) {
        difftest::X87Precision precision(pc);
        for (uint32_t seed = 1; seed <= 50; ++seed) {
            ++ctx.cases;
            fxtest::Same(ctx, run(false, seed), run(true, seed), "test map", seed);
        }
    }
}
