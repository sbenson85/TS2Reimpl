// Differential tests for cTSEffectsCollection (src/ts/cTSEffectsCollection.*), lane IT.
//
// Standalone shape: the original side's collection is built by the original constructor 0xbd7210 (game vtable) and its code
// called at the RVAs; ours by our constructor and called through its own vtable (slots 3, 5-11). A world is one collection
// plus 16 fake descriptions (tests/ts_effects_collection.h: logged GetType / AddRef / Release). A run applies the same random
// operations to both sides and compares, after each, the result, out-parameters, the event log and the whole state
// (PushCollection: every container in list order with its buckets and capacities). Covered RVAs: 0xbd7210 (constructor),
// 0xbd8680 (Clear), 0xbd7c00 (AddEffect), 0xbd8970 (GetEffect), 0xbd7840 (AddDescription), 0xbd8890 (GetDescription),
// 0xbd7b40 (AddMessageAlias), 0xbd7ef0 (AddMessageTrigger), 0xbd8b50 (SetEffectVariant), 0xbd87b0 (FindEffectIndex),
// 0xbd7670 (destructor), 0xbd7820 (deleting destructor), 0xbd8820 (Create), 0xbd8a40 (QueryInterface), 0xbd7f30
// (MergeInto), 0xbd8080 (AddAllTo), 0xbd8160 (AppendTo); the folded slots
// 1 (0x148fb0), 2 (0x244060) and 4 (0x140fe0) through our vtable in EffectsCollection_Slots.
#include "ts_effects_collection.h"

#include "free_recorder.h"

#include <new>

namespace {

using namespace fxtest;
using Coll = cTSEffectsCollection;
using A = cTSEffectsCollectionAdapters;

struct World {
    alignas(8) uint8_t buffer[sizeof(Coll)];
    Descriptions descs;
    Coll *obj = nullptr;
    int side = 0;

    void Build(difftest::Context &ctx, int s, uint32_t seed)
    {
        side = s;
        std::mt19937 r(seed);
        for (uint8_t &b : buffer) b = uint8_t(r());
        descs.Build(seed);
        if (side == 0)
            obj = ctx.Original<Coll *(__thiscall *)(void *)>(0xbd7210)(buffer);
        else
            obj = ::new (buffer) Coll;
    }
    void Destroy(difftest::Context &ctx)
    {
        if (side == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbd7670)(obj);
        else
            obj->~Coll();
        obj = nullptr;
    }
};

// One call of the operation `op` on a side, its result and out-parameters pushed. The inputs come from `r` (the same seed on
// both sides).
void Operate(difftest::Context &ctx, World &w, Blob &b, uint32_t op, std::mt19937 &r)
{
    Coll *c = w.obj;
    const bool orig = w.side == 0;
    switch (op) {
    case 0: { // AddEffect
        const char *name = Name(r);
        const EffectEntry e = RandomEffect(r);
        if (orig)
            ctx.Original<void(__thiscall *)(void *, const char *, const EffectEntry *)>(0xbd7c00)(c, name, &e);
        else
            CallSlot<void>(c, 5, name, &e);
        break;
    }
    case 1: { // GetEffect
        const char *name = Name(r);
        const bool withOut = r() % 4 != 0;
        EffectEntry out = RandomEffect(r);
        const uint8_t res = orig ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, EffectEntry *)>(0xbd8970)(
                                       c, name, withOut ? &out : nullptr)
                                 : CallSlot<uint8_t>(c, 6, name, withOut ? &out : nullptr);
        b.push_back(res);
        PushEffect(b, out);
        break;
    }
    case 2: { // AddDescription
        const char *name = r() % 5 ? Name(r) : nullptr;
        cITSComponentDescription *d = w.descs.At(r());
        b.push_back(orig ? ctx.Original<uint32_t(__thiscall *)(void *, const char *, void *)>(0xbd7840)(c, name, d)
                         : CallSlot<uint32_t>(c, 7, name, d));
        break;
    }
    case 3: { // GetDescription
        const char *name = Name(r);
        const int32_t type = int32_t(1 + r() % 13);
        const bool withOut = r() % 4 != 0;
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint8_t res = orig ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, int32_t, void **)>(0xbd8890)(
                                       c, name, type, withOut ? &out : nullptr)
                                 : CallSlot<uint8_t>(c, 8, name, type, withOut ? &out : nullptr);
        b.insert(b.end(), {res, DescId(out)});
        break;
    }
    case 4: { // AddMessageAlias
        const char *name = Name(r);
        const uint32_t first = r(), second = r();
        if (orig)
            ctx.Original<void(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(0xbd7b40)(c, name, first, second);
        else
            CallSlot<void>(c, 9, name, first, second);
        break;
    }
    case 5: { // AddMessageTrigger
        const EffectMessageTrigger t{r(), Name(r)};
        if (orig)
            ctx.Original<void(__thiscall *)(void *, const EffectMessageTrigger *)>(0xbd7ef0)(c, &t);
        else
            CallSlot<void>(c, 10, &t);
        break;
    }
    case 6: { // SetEffectVariant
        const char *name = Name(r);
        const uint32_t variant = r() % 5 ? r() % 3 : r();
        const char *newName = r() % 3 ? Name(r) : nullptr;
        b.push_back(orig ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, uint32_t, const char *)>(0xbd8b50)(
                               c, name, variant, newName)
                         : CallSlot<uint8_t>(c, 11, name, variant, newName));
        break;
    }
    case 7: { // FindEffectIndex (non-virtual): the key as the callers build it (a lower-case std::string)
        std::string key = Name(r);
        for (char &ch : key) ch = char(tolower(ch));
        const uint32_t variant = r() % 5 ? r() % 3 : r();
        b.push_back(orig ? ctx.Original<int32_t(__thiscall *)(void *, const std::string *, uint32_t)>(0xbd87b0)(c, &key, variant)
                         : uint32_t(c->FindEffectIndex(key, variant)));
        break;
    }
    case 8: // Clear
        if (orig)
            ctx.Original<void(__thiscall *)(void *)>(0xbd8680)(c);
        else
            CallSlot<void>(c, 3);
        break;
    case 9: { // a named description, then an effect whose components name it (any case) with its type or a neighbour's
        const char *descName = Name(r);
        const uint32_t which = r();
        cITSComponentDescription *d = w.descs.At(which);
        const int32_t type = w.descs.d[which % 16].type;
        b.push_back(orig ? ctx.Original<uint32_t(__thiscall *)(void *, const char *, void *)>(0xbd7840)(c, descName, d)
                         : CallSlot<uint32_t>(c, 7, descName, d));
        const char *name = Name(r);
        EffectEntry e = RandomEffect(r);
        for (EffectComponent &comp : e.mComponents) {
            comp.mName = descName;
            if (r() % 2) for (char &ch : comp.mName) ch = char(toupper(ch));
            const uint32_t k = r() % 4;
            comp.mType = k == 0 ? 1 : k == 1 ? type : k == 2 ? (type % 13) + 1 : 0;
            comp.mIndex = r() % 5 ? -1 : int32_t(r() % 3);
        }
        if (orig)
            ctx.Original<void(__thiscall *)(void *, const char *, const EffectEntry *)>(0xbd7c00)(c, name, &e);
        else
            CallSlot<void>(c, 5, name, &e);
        break;
    }
    }
}

constexpr uint32_t kOpWeights[] = {6, 3, 5, 3, 1, 1, 3, 2, 0, 3};

uint32_t PickOp(std::mt19937 &r, uint32_t step)
{
    if (step % 37 == 36) return 8; // an occasional Clear
    uint32_t total = 0;
    for (uint32_t w : kOpWeights) total += w;
    uint32_t x = r() % total;
    for (uint32_t op = 0;; ++op) {
        if (x < kOpWeights[op]) return op;
        x -= kOpWeights[op];
    }
}

} // namespace

// The constructor, then random operations (every slot 3, 5-11 and FindEffectIndex), the state compared after each, then the
// destructor (its releases logged, the frees counted by size).
DIFF_TEST(EffectsCollection_Operations, 0xbd7c00)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            World w;
            gEv.clear();
            w.Build(ctx, side, seed);
            Blob &b = blobs[side];
            PushCollection(b, w.obj);
            std::mt19937 r(seed * 1000003u);
            const uint32_t steps = 20 + seed * 3;
            for (uint32_t step = 0; step < steps; ++step) {
                const uint32_t op = PickOp(r, step);
                b.push_back(0x0b000000u | op);
                Operate(ctx, w, b, op, r);
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushCollection(b, w.obj);
            }
            difftest::FreeRecorder rec;
            w.Destroy(ctx);
            rec.Stop();
            b.insert(b.end(), gEv.begin(), gEv.end());
            size_t sizes[512];
            const size_t n = rec.SortedSizes(sizes, 512);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 512; ++i) b.push_back(uint32_t(sizes[i]));
            for (auto &d : w.descs.d) b.push_back(d.refs);
        }
        Same(ctx, blobs[0], blobs[1], "Operations", seed);
    }
}

// MergeInto 0xbd7f30, AddAllTo 0xbd8080 and AppendTo 0xbd8160: a source and a target collection (each with its own fake
// descriptions, the target's ids 0x21-0x30) filled by random operations, then the source merged into the target (MergeInto,
// or one of its two ways called directly); both compared after, then both destroyed (releases and frees). The source's flag
// picks MergeInto's way; a target without effects takes the swap. Modes: 0 the originals at the RVAs on collections the
// original constructor built; 1 ours (our constructor, slots through our vtable, the members called directly); 2 the
// adapters on collections the original constructor built.
DIFF_TEST(EffectsCollection_Merge, 0xbd7f30)
{
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Blob &b = blobs[mode];
            gEv.clear();
            World source, target;
            source.Build(ctx, mode == 1 ? 1 : 0, seed);
            target.Build(ctx, mode == 1 ? 1 : 0, seed + 1000);
            for (FakeDescription &d : target.descs.d) d.id += 0x20;
            std::mt19937 r(seed * 7919u + 1);
            const uint32_t sourceSteps = r() % 16, targetSteps = seed % 4 == 0 ? r() % 4 : r() % 16;
            for (uint32_t i = 0; i < sourceSteps; ++i) Operate(ctx, source, b, PickOp(r, i), r);
            for (uint32_t i = 0; i < targetSteps; ++i) Operate(ctx, target, b, PickOp(r, i), r);
            source.obj->mFlag58 = r() % 3 == 0;
            const uint32_t how = seed % 5 == 1 ? 1 : seed % 5 == 2 ? 2 : 0;
            b.push_back(0x0b000000u | how);
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            if (mode == 0) {
                const uint32_t rva = how == 1 ? 0xbd8080 : how == 2 ? 0xbd8160 : 0xbd7f30;
                ctx.Original<void(__thiscall *)(void *, void *)>(rva)(source.obj, target.obj);
            } else if (mode == 1) {
                if (how == 1)
                    source.obj->AddAllTo(target.obj);
                else if (how == 2)
                    source.obj->AppendTo(target.obj);
                else
                    source.obj->MergeInto(target.obj);
            } else {
                if (how == 1)
                    A::AddAllTo(source.obj, nullptr, target.obj);
                else if (how == 2)
                    A::AppendTo(source.obj, nullptr, target.obj);
                else
                    A::MergeInto(source.obj, nullptr, target.obj);
            }
            b.push_back(0x0b0000ffu);
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            PushCollection(b, source.obj);
            PushCollection(b, target.obj);
            difftest::FreeRecorder rec;
            source.Destroy(ctx);
            target.Destroy(ctx);
            rec.Stop();
            b.insert(b.end(), gEv.begin(), gEv.end());
            size_t sizes[512];
            const size_t n = rec.SortedSizes(sizes, 512);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 512; ++i) b.push_back(uint32_t(sizes[i]));
            for (auto &d : source.descs.d) b.push_back(d.refs);
            for (auto &d : target.descs.d) b.push_back(d.refs);
        }
        Same(ctx, blobs[0], blobs[1], "Merge (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "Merge (adapters)", seed);
    }
}


// The factory 0xbd8820 and the scalar deleting destructor 0xbd7820 (slot 12): a new collection's state, then filled a little
// and deleted through slot 12 with flags 1 (freed) or 0 (destroyed only; the block is freed by the test).
DIFF_TEST(EffectsCollection_CreateDelete, 0xbd8820)
{
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = blobs[side];
            gEv.clear();
            Descriptions descs;
            descs.Build(seed);
            Coll *c = side == 0 ? ctx.Original<Coll *(__cdecl *)()>(0xbd8820)() : Coll::Create();
            PushCollection(b, c);
            std::mt19937 r(seed);
            for (int i = 0; i < 6; ++i) {
                cITSComponentDescription *d = descs.At(r());
                if (side == 0)
                    ctx.Original<uint32_t(__thiscall *)(void *, const char *, void *)>(0xbd7840)(c, Name(r), d);
                else
                    CallSlot<uint32_t>(c, 7, Name(r), d);
            }
            const uint32_t flags = seed % 2;
            difftest::FreeRecorder rec;
            Coll *res = side == 0 ? ctx.Original<Coll *(__thiscall *)(void *, uint32_t)>(0xbd7820)(c, flags)
                                  : CallSlot<Coll *>(c, 12, flags);
            rec.Stop();
            b.insert(b.end(), {uint32_t(res == c), uint32_t(rec.Freed(c))});
            b.insert(b.end(), gEv.begin(), gEv.end());
            if (!flags) ::operator delete(c);
            for (auto &d : descs.d) b.push_back(d.refs);
        }
        Same(ctx, blobs[0], blobs[1], "CreateDelete", seed);
    }
}

// QueryInterface 0xbd8a40 and the folded slots 1 (AddRef 0x148fb0), 2 (Release 0x244060: at zero the deleting destructor)
// and 4 (Link 0x140fe0, empty) through our vtable; the original side calls the game's vtable's slots.
DIFF_TEST(EffectsCollection_Slots, 0xbd8a40)
{
    const uint32_t ids[] = {1, Coll::kInterfaceId, 0, 0xc97cf5cd, 0x452294aa};
    for (uint32_t seed = 0; seed < 30; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = blobs[side];
            World w;
            w.Build(ctx, side, seed);
            std::mt19937 r(seed);
            w.obj->mRefCount = r() % 3;
            for (int i = 0; i < 6; ++i) {
                void *out = reinterpret_cast<void *>(0x1234);
                const uint32_t id = ids[r() % 5];
                const bool withOut = r() % 5 != 0;
                b.push_back(CallSlot<uint8_t>(w.obj, 0, id, withOut ? &out : nullptr));
                b.push_back(out == w.obj ? 1u : uint32_t(uintptr_t(out)));
                b.push_back(CallSlot<uint32_t>(w.obj, 1));
                CallSlot<void>(w.obj, 4);
                b.push_back(w.obj->mRefCount);
            }
            // Release down to one (counting), then to zero: the deleting destructor with 1 frees the block. The buffer is
            // on the stack, so the last Release runs on a heap copy built the side's way.
            for (uint32_t n = w.obj->mRefCount; n > 1; --n) b.push_back(CallSlot<uint32_t>(w.obj, 2));
            w.Destroy(ctx);
            Coll *heap = side == 0 ? ctx.Original<Coll *(__cdecl *)()>(0xbd8820)() : Coll::Create();
            heap->mRefCount = 1;
            difftest::FreeRecorder rec;
            b.push_back(CallSlot<uint32_t>(heap, 2));
            rec.Stop();
            b.push_back(uint32_t(rec.Freed(heap)));
        }
        Same(ctx, blobs[0], blobs[1], "Slots", seed);
    }
}

// The adapters on a collection built by the original constructor with a copy of the game's vtable whose every slot but 1 is a
// trap (an adapter that dispatched virtually would land there): each adapter against the RVA on an identical world.
namespace {
uint32_t gTrapHits;
uint32_t __fastcall Trap0(void *, void *) { return ++gTrapHits; }
uint32_t __fastcall Trap1(void *, void *, uint32_t) { return ++gTrapHits; }
uint32_t __fastcall Trap2(void *, void *, uint32_t, uint32_t) { return ++gTrapHits; }
uint32_t __fastcall Trap3(void *, void *, uint32_t, uint32_t, uint32_t) { return ++gTrapHits; }
void *gTrapVT[13];
} // namespace

DIFF_TEST(EffectsCollection_Adapters, 0xbd7210)
{
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = blobs[side];
            gEv.clear();
            gTrapHits = 0;
            alignas(8) uint8_t buffer[sizeof(Coll)];
            std::mt19937 fill(seed);
            for (uint8_t &x : buffer) x = uint8_t(fill());
            Coll *c = side == 0 ? ctx.Original<Coll *(__thiscall *)(void *)>(0xbd7210)(buffer) : A::Construct(reinterpret_cast<Coll *>(buffer), nullptr);
            b.push_back(uint32_t(c == reinterpret_cast<Coll *>(buffer)));
            // Our Construct leaves our vtable; give both the game's with traps (slot 1 stays the game's AddRef).
            std::memcpy(gTrapVT, ctx.image.At<void>(Coll::kOriginalVTable), sizeof(gTrapVT));
            gTrapVT[0] = reinterpret_cast<void *>(&Trap2);
            gTrapVT[2] = reinterpret_cast<void *>(&Trap0);
            gTrapVT[3] = reinterpret_cast<void *>(&Trap0);
            gTrapVT[4] = reinterpret_cast<void *>(&Trap0);
            for (int s : {5, 6, 7}) gTrapVT[s] = reinterpret_cast<void *>(&Trap2);
            gTrapVT[8] = reinterpret_cast<void *>(&Trap3);
            gTrapVT[9] = reinterpret_cast<void *>(&Trap3);
            gTrapVT[10] = reinterpret_cast<void *>(&Trap1);
            gTrapVT[11] = reinterpret_cast<void *>(&Trap3);
            gTrapVT[12] = reinterpret_cast<void *>(&Trap1);
            *reinterpret_cast<void **>(c) = gTrapVT;
            Descriptions descs;
            descs.Build(seed);
            std::mt19937 r(seed * 7u);
            const bool orig = side == 0;
            for (int i = 0; i < 25; ++i) {
                const uint32_t op = r() % 10;
                b.push_back(0x0b000000u | op);
                switch (op) {
                case 0: {
                    const char *name = Name(r);
                    const EffectEntry e = RandomEffect(r);
                    if (orig)
                        ctx.Original<void(__thiscall *)(void *, const char *, const EffectEntry *)>(0xbd7c00)(c, name, &e);
                    else
                        A::AddEffect(c, nullptr, name, &e);
                    break;
                }
                case 1: {
                    const char *name = Name(r);
                    EffectEntry out;
                    b.push_back(orig ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, EffectEntry *)>(0xbd8970)(c, name, &out)
                                     : A::GetEffect(c, nullptr, name, &out));
                    PushEffect(b, out);
                    break;
                }
                case 2: {
                    const char *name = r() % 4 ? Name(r) : nullptr;
                    cITSComponentDescription *d = descs.At(r());
                    b.push_back(orig ? ctx.Original<uint32_t(__thiscall *)(void *, const char *, void *)>(0xbd7840)(c, name, d)
                                     : A::AddDescription(c, nullptr, name, d));
                    break;
                }
                case 3: {
                    const char *name = Name(r);
                    const int32_t type = int32_t(1 + r() % 13);
                    cITSComponentDescription *out = nullptr;
                    b.push_back(orig ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, int32_t, void *)>(0xbd8890)(c, name, type, &out)
                                     : A::GetDescription(c, nullptr, name, type, &out));
                    b.push_back(DescId(out));
                    break;
                }
                case 4: {
                    const char *name = Name(r);
                    const uint32_t x = r(), y = r();
                    if (orig)
                        ctx.Original<void(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(0xbd7b40)(c, name, x, y);
                    else
                        A::AddMessageAlias(c, nullptr, name, x, y);
                    break;
                }
                case 5: {
                    const EffectMessageTrigger t{r(), Name(r)};
                    if (orig)
                        ctx.Original<void(__thiscall *)(void *, const EffectMessageTrigger *)>(0xbd7ef0)(c, &t);
                    else
                        A::AddMessageTrigger(c, nullptr, t);
                    break;
                }
                case 6: {
                    const char *name = Name(r);
                    const uint32_t v = r() % 3;
                    const char *nn = r() % 2 ? Name(r) : nullptr;
                    b.push_back(orig ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, uint32_t, const char *)>(0xbd8b50)(c, name, v, nn)
                                     : A::SetEffectVariant(c, nullptr, name, v, nn));
                    break;
                }
                case 7: {
                    std::string key = Name(r);
                    for (char &ch : key) ch = char(tolower(ch));
                    const uint32_t v = r() % 3;
                    b.push_back(orig ? ctx.Original<int32_t(__thiscall *)(void *, const std::string *, uint32_t)>(0xbd87b0)(c, &key, v)
                                     : uint32_t(A::FindEffectIndex(c, nullptr, key, v)));
                    break;
                }
                case 8: {
                    void *out = nullptr;
                    const uint32_t id = r() % 2 ? Coll::kInterfaceId : r();
                    b.push_back(orig ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xbd8a40)(c, id, &out)
                                     : A::QueryInterface(c, nullptr, id, &out));
                    b.push_back(out == c);
                    break;
                }
                case 9:
                    if (r() % 4 == 0) {
                        if (orig)
                            ctx.Original<void(__thiscall *)(void *)>(0xbd8680)(c);
                        else
                            A::Clear(c, nullptr);
                    }
                    break;
                }
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushCollection(b, c);
            }
            b.push_back(gTrapHits);
            difftest::FreeRecorder rec;
            if (orig)
                ctx.Original<void(__thiscall *)(void *)>(0xbd7670)(c);
            else
                A::Destruct(c, nullptr);
            rec.Stop();
            b.push_back(uint32_t(rec.Count()));
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            // The deleting destructor's adapter on a heap collection (flags 1: freed).
            Coll *heap = ctx.Original<Coll *(__cdecl *)()>(0xbd8820)();
            *reinterpret_cast<void **>(heap) = gTrapVT;
            difftest::FreeRecorder rec2;
            Coll *res = orig ? ctx.Original<Coll *(__thiscall *)(void *, uint32_t)>(0xbd7820)(heap, 1) : A::DeletingDestructor(heap, nullptr, 1);
            rec2.Stop();
            b.insert(b.end(), {uint32_t(res == heap), uint32_t(rec2.Freed(heap)), gTrapHits});
        }
        Same(ctx, blobs[0], blobs[1], "Adapters", seed);
    }
}
