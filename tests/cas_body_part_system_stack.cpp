// Differential tests for the modifier-stack functions of nTSCASCore::cTSCASBodyPartSystem's interface subobject
// (vftable 0xeba198). See cas_body_part_system.h for the fixture: a whole object buffer, a modifier stack and a redo stack
// built on the heap from specs with the original record constructors, and a fake vtable copy whose slots record calls.
#include "cas_body_part_system.h"

#include <cmath>
#include <limits>

namespace bpt {

std::vector<Event> gEvents;
const uint8_t *gBase = nullptr;

namespace {

using Ctor5 = void(__thiscall *)(BodyPartEntry *, const char *, int32_t, int32_t, float, float);
using CopyCtor = void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *);
using Dtor = void(__thiscall *)(BodyPartEntry *);

// Builds the items of a record: n items of two (empty or short) strings and a dword, laid out as the game's 0x44-byte items.
// The record's own vector storage comes from the game's allocator service (the record's destructor frees it there), so the
// items are built in a scratch vector of ours and copied into the record by the game's vector copy (0xcddae0, which the
// record's copy constructor uses too). The scratch vector and its strings are left to leak.
void BuildItems(difftest::Context &ctx, BodyPartEntry &e, int n, std::mt19937 &rng)
{
    e.mItemsFirst = e.mItemsLast = e.mItemsEnd = nullptr;
    if (n == 0) return;
    uint8_t *buf = static_cast<uint8_t *>(operator new(static_cast<size_t>(n) * 0x44));
    std::memset(buf, 0, static_cast<size_t>(n) * 0x44);
    for (int i = 0; i < n; ++i) {
        uint8_t *item = buf + i * 0x44;
        auto *s1 = reinterpret_cast<cRZString *>(item);
        auto *s2 = reinterpret_cast<cRZString *>(item + 0x20);
        s1->Construct(RandomName(rng));
        s2->Construct(RandomName(rng));
        *reinterpret_cast<uint32_t *>(item + 0x40) = static_cast<uint32_t>(rng());
    }
    uint8_t *scratch[3] = {buf, buf + static_cast<size_t>(n) * 0x44, buf + static_cast<size_t>(n) * 0x44};
    ctx.Original<void(__thiscall *)(void *, const void *)>(0xcddae0)(&e.mItemsFirst, scratch);
}

const char *const kNames[] = {"alpha", "ALPHA", "Alpha", "beta", "gamma", "", "a_rather_long_name_over_fifteen",
                              "A_RATHER_LONG_NAME_OVER_FIFTEEN", "x", "fifteen_chars_ab", "0123456789abcde"};

} // namespace

const char *RandomName(std::mt19937 &rng)
{
    return kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))];
}

Spec RandomSpec(std::mt19937 &rng)
{
    static const int kKinds[] = {0, 1, 1, 1, 2, 3, 3, 5, 6, 7, 7, 8, 9, -1, 4};
    static const float kWeights[] = {0.0f, 1.0f, 0.5f, -1.0f, 0.25f, 0.75f, std::numeric_limits<float>::quiet_NaN(), 2.0f};
    Spec s;
    s.kind = kKinds[rng() % (sizeof(kKinds) / sizeof(kKinds[0]))];
    s.a = static_cast<int16_t>(rng() % 4 == 0 ? static_cast<int16_t>(rng()) : rng() % 4);
    s.b = static_cast<int16_t>(rng() % 3);
    s.value = static_cast<int32_t>(rng() % 3 == 0 ? rng() : rng() % 4);
    s.weight = rng() % 5 == 0 ? static_cast<float>(static_cast<int>(rng() % 200) - 100) / 17.0f : kWeights[rng() % 8];
    s.extra = rng() % 2 ? 0.0f : static_cast<float>(rng() % 100) / 7.0f;
    s.name = RandomName(rng);
    s.items = rng() % 6 == 0 ? static_cast<int>(rng() % 3) : 0;
    return s;
}

std::vector<Spec> RandomSpecs(std::mt19937 &rng, int maxCount)
{
    std::vector<Spec> v(rng() % (maxCount + 1));
    for (auto &s : v) s = RandomSpec(rng);
    return v;
}

Scenario RandomScenario(std::mt19937 &rng)
{
    Scenario sc;
    for (auto &b : sc.bytes) b = static_cast<uint8_t>(rng());
    sc.stack = RandomSpecs(rng, 6);
    sc.redo = RandomSpecs(rng, 4);
    sc.stackSpare = rng() % 3 == 0 ? 0 : static_cast<int>(rng() % 4) + 1;
    sc.redoSpare = rng() % 3 == 0 ? 0 : static_cast<int>(rng() % 4) + 1;
    return sc;
}

BodyPartEntryVector *BuildStack(difftest::Context &ctx, const std::vector<Spec> &specs, int spare)
{
    auto ctor = ctx.Original<Ctor5>(0xcddd40);
    auto copy = ctx.Original<CopyCtor>(0xcddbc0);
    auto dtor = ctx.Original<Dtor>(0xcdf820);
    auto *hdr = new BodyPartEntryVector{nullptr, nullptr, nullptr};
    const size_t n = specs.size(), cap = n + static_cast<size_t>(spare);
    if (cap == 0) return hdr;
    auto *buf = static_cast<BodyPartEntry *>(ServiceAlloc(cap * sizeof(BodyPartEntry)));
    std::mt19937 itemRng(static_cast<uint32_t>(n * 977 + spare));
    for (size_t i = 0; i < n; ++i) {
        BodyPartEntry tmp;
        ctor(&tmp, specs[i].name.c_str(), specs[i].kind, specs[i].value, specs[i].weight, specs[i].extra);
        tmp.mShortA = specs[i].a;
        tmp.mShortB = specs[i].b;
        BuildItems(ctx, tmp, specs[i].items, itemRng);
        copy(&buf[i], &tmp);
        dtor(&tmp);
    }
    hdr->mFirst = buf;
    hdr->mLast = buf + n;
    hdr->mEnd = buf + cap;
    return hdr;
}

void Setup(difftest::Context &ctx, World &w, const Scenario &sc, const void *vtable)
{
    std::memset(w.front, 0xA5, sizeof(w.front));
    std::memset(w.back, 0x5A, sizeof(w.back));
    std::memcpy(w.full, sc.bytes, sizeof(w.full));
    w.stack = BuildStack(ctx, sc.stack, sc.stackSpare);
    w.redo = BuildStack(ctx, sc.redo, sc.redoSpare);
    Iface *s = w.S();
    s->mVTable = vtable;
    s->mStack = w.stack;
    s->mRedoStack = w.redo;
}

std::vector<uint8_t> Snapshot(const BodyPartEntryVector *v)
{
    std::vector<uint8_t> out;
    auto put = [&](const void *p, size_t n) {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        out.insert(out.end(), b, b + n);
    };
    for (const BodyPartEntry *e = v->mFirst; e != v->mLast; ++e) {
        put(&e->mKind, 4);
        put(&e->mShortA, 2);
        put(&e->mShortB, 2);
        put(&e->mValue, 4);
        put(&e->mWeight, 4);
        const uint32_t itemBytes = static_cast<uint32_t>(e->mItemsLast - e->mItemsFirst);
        put(&itemBytes, 4);
        for (const uint8_t *it = e->mItemsFirst; it != e->mItemsLast; it += 0x44) {
            const auto *n1 = reinterpret_cast<const cRZString *>(it);
            const auto *n2 = reinterpret_cast<const cRZString *>(it + 0x20);
            put(n1->mString.c_str(), n1->mString.size() + 1);
            put(n2->mString.c_str(), n2->mString.size() + 1);
            put(it + 0x40, 4);
        }
        put(&e->mExtra, 4);
        put(&e->mNameVTable, 4);
        const char *name = e->Name();
        put(name, e->mNameSize + 1);
        put(&e->mNameSize, 4);
        put(&e->mNameCap, 4);
        put(&e->mNameRef, 4);
    }
    const uint32_t count = static_cast<uint32_t>(v->mLast - v->mFirst);
    const uint32_t spare = static_cast<uint32_t>(v->mEnd - v->mLast);
    put(&count, 4);
    put(&spare, 4);
    return out;
}

std::vector<uint8_t> Normalised(World &w)
{
    std::vector<uint8_t> v(w.full, w.full + kObject);
    std::memset(v.data() + kIfaceOffset + 0x18, 0, 8); // the two stack pointers
    return v;
}

bool CanariesIntact(const World &w)
{
    for (uint8_t b : w.front)
        if (b != 0xA5) return false;
    for (uint8_t b : w.back)
        if (b != 0x5A) return false;
    return true;
}

VTable CopyVTable(const difftest::Context &ctx)
{
    VTable v;
    std::memcpy(v.slots, ctx.image.At(Iface::kVTable), sizeof(v.slots));
    return v;
}

} // namespace bpt


namespace {

using namespace bpt;

constexpr int kAgeOffset = kIfaceOffset + 0x118; // the selector read by the gated pushes

// A name that sometimes is not in the pool.
const char *QueryName(std::mt19937 &rng)
{
    return rng() % 8 == 0 ? "nothing_like_it" : RandomName(rng);
}

// Makes the newest record a kind-1 record with a pool name about half of the time, so the top-of-stack paths are reached.
void BiasTop(Scenario &sc, std::mt19937 &rng, int32_t kind)
{
    if (sc.stack.empty() || rng() % 2) return;
    sc.stack.back().kind = kind;
    sc.stack.back().name = RandomName(rng);
}

void SetAge(Scenario &sc, std::mt19937 &rng)
{
    const uint32_t age = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng() % 5);
    std::memcpy(sc.bytes + kAgeOffset, &age, 4);
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_HasActiveMorph, 0xcf3220)
{
    std::mt19937 rng(0xcf3220);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const char *)>(0xcf3220);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 6000; ++i) {
        Scenario sc = RandomScenario(rng);
        const char *name = QueryName(rng);
        Compare(ctx, "HasActiveMorph", sc, vt, [&](World &w, bool original) -> uint64_t {
            // The original returns al only (the upper bits are leftovers).
            return (original ? orig(w.S(), name) : static_cast<uint32_t>(w.S()->HasActiveMorph(name))) & 0xff;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_TopMorphWeight, 0xce1690)
{
    std::mt19937 rng(0xce1690);
    auto orig = ctx.Original<float(__thiscall *)(void *, const char *)>(0xce1690);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 6000; ++i) {
        Scenario sc = RandomScenario(rng);
        BiasTop(sc, rng, 1);
        const char *name = QueryName(rng);
        Compare(ctx, "TopMorphWeight", sc, vt, [&](World &w, bool original) -> uint64_t {
            return FloatBits(original ? orig(w.S(), name) : w.S()->TopMorphWeight(name));
        });
    }
}

// Slot 50 takes a second argument it ignores (ret 8).
DIFF_TEST(cTSCASBodyPartSystem_SumNamedWeights, 0xcf4310)
{
    std::mt19937 rng(0xcf4310);
    auto orig = ctx.Original<float(__thiscall *)(void *, const char *, uint32_t)>(0xcf4310);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 8000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 8);
        const char *name = QueryName(rng);
        const uint32_t unused = static_cast<uint32_t>(rng());
        Compare(ctx, "SumNamedWeights", sc, vt, [&](World &w, bool original) -> uint64_t {
            return FloatBits(original ? orig(w.S(), name, unused) : w.S()->SumNamedWeights(name, unused));
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_ClearPartWeight, 0xcf7370)
{
    std::mt19937 rng(0xcf7370);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, int32_t, int32_t)>(0xcf7370);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 8000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 8);
        const int32_t value = static_cast<int32_t>(rng() % 5);
        const int32_t category = rng() % 3 == 0 ? -1 : static_cast<int32_t>(rng() % 5);
        Compare(ctx, "ClearPartWeight", sc, vt, [&](World &w, bool original) -> uint64_t {
            // Only al is meaningful.
            return (original ? orig(w.S(), value, category) : static_cast<uint32_t>(w.S()->ClearPartWeight(value, category))) &
                   0xff;
        });
    }
}

// Slot 39: some kind-3 records are named by the text form of the GUID (read from the game's format string).
DIFF_TEST(cTSCASBodyPartSystem_ClearGuidPartWeights, 0xcf7220)
{
    std::mt19937 rng(0xcf7220);
    auto orig = ctx.Original<void(__thiscall *)(void *, const uint32_t *)>(0xcf7220);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    const char *format = ctx.image.At<const char>(0xdce3cc);
    for (int i = 0; i < 4000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 6);
        uint32_t guid[4];
        for (auto &g : guid) g = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng());
        char text[64];
        std::snprintf(text, sizeof(text), format, guid[0], guid[1] >> 16, guid[1] & 0xffff, guid[2] >> 16, guid[2] & 0xffff,
                      guid[3]);
        for (auto &s : sc.stack) {
            if (rng() % 3 == 0) s.name = text;
            if (rng() % 8 == 0) s.name = std::string(text) + "x"; // a prefix match must not count
        }
        Compare(ctx, "ClearGuidPartWeights", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), guid);
            else w.S()->ClearGuidPartWeights(guid);
            return 0;
        });
    }
}

// ---- Pushes --------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_PushMarker, 0xcf59e0)
{
    std::mt19937 rng(0xcf59e0);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf59e0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        Compare(ctx, "PushMarker", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S());
            else w.S()->PushMarker();
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_PushKind0, 0xcf5e50)
{
    std::mt19937 rng(0xcf5e50);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *)>(0xcf5e50);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        const char *name = RandomName(rng);
        Compare(ctx, "PushKind0", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), name);
            else w.S()->PushKind0(name);
            return 0;
        });
    }
}

// Slot 33: only when the +0x118 selector is non-zero; the item vector (0x44-byte items) is copied.
DIFF_TEST(cTSCASBodyPartSystem_PushKind5, 0xcf5ce0)
{
    std::mt19937 rng(0xcf5ce0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0xcf5ce0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetAge(sc, rng);
        // The source vector is shared by both sides (the original copies from it and does not change it).
        Spec holder;
        holder.items = static_cast<int>(rng() % 4);
        std::vector<Spec> one{holder};
        BodyPartEntryVector *src = BuildStack(ctx, one, 0);
        const BodyPartEntry &record = src->mFirst[0];
        Compare(ctx, "PushKind5", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &record.mItemsFirst);
            else w.S()->PushKind5(&record.mItemsFirst);
            return 0;
        });
    }
}

// Slot 35: a named weight (a cRZString and a float at +0x20); only when the selector is non-zero, and then it sets +0x12a.
DIFF_TEST(cTSCASBodyPartSystem_PushKind6, 0xcf5da0)
{
    std::mt19937 rng(0xcf5da0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0xcf5da0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetAge(sc, rng);
        nTSCASCore::NamedWeight source;
        source.mName.Construct(RandomName(rng));
        source.mWeight = static_cast<float>(rng() % 100) / 8.0f;
        Compare(ctx, "PushKind6", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &source);
            else w.S()->PushKind6(&source);
            return 0;
        });
    }
}
