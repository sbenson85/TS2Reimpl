// Differential tests for cTSEffectsResource (src/ts/cTSEffectsResource.*), lane IT.
//
// Standalone shape. Three ways to run the same random operations, compared word for word:
//   mode 0: the original constructor 0xbd2ba0 builds the resource (game vtables); the resource's own slots are called at
//           their RVAs, the other faces' slots through the game's vtables (the original thunks);
//   mode 1: our constructor builds it; every slot through our vtables (the compiler's thunks on the other faces);
//   mode 2: the original constructor builds it; the adapters (the hook entries) are called.
// After each operation: the result, the event log (every call into a fake stream, description or stand-in, every AddRef /
// Release with the fake's id) and the whole resource (the collection with buckets and capacities, the persist resource's
// count and key, the name, the string list). At the end the destructor: its log and the frees by size.
//
// Fakes: streams (an output that records what is written, an input that replays a tape: random, truncated, or what a
// Write produced), component descriptions (slot 4 reads one number, slot 5 writes its id). Stood in on both sides: the 13
// description factories (0xbe7ba0 ... 0xbf9370, ours and the image's: a description from a pool) and the effect serializers
// 0xbf9ee0 / 0xbf9da0 (not ours yet).
//
// Covered RVAs: 0xbd2ba0 (constructor), 0xbd2da0 (destructor), 0xbd2e50 (deleting destructor), 0xbd2f90 (Create),
// 0xbd3090 (QueryInterface), 0xbd2ea0 (AddRef), 0xbd4010 (Release), 0xbd4190 (SetName), 0xbd3070 (GetName), 0xbd2ec0
// (MergeInto), 0xbd41c0 (Write), 0xbd30f0 (Read), 0xbd3050 (GetClassId), 0xbd8dc0 (the trigger writer), 0xbd8a70 (the
// trigger reader); the thunks 0xbd30db / 0xbd2ea8 / 0xbd4018 / 0xbd2e3b (collection face) and 0xbd30e3 / 0xbd2eb0 / 0xbd4020
// / 0xbd2e43 (persist face) through the vtables.
//
// cTSEffectsResourceFactory (EffectsResourceFactory_Slots): 0xbd2c50 (constructor), 0xbd2e30 (destructor), 0xbd2e80
// (deleting destructor), 0xbd2ff0 (Create), 0xbd2ed0 (slot 9, a new resource), 0xbd4030 / 0xbd40e0 (slots 10 / 11, read /
// write on the record's stream); the base's slots 0-6 and the shared slot 8 (0x1f62f0) through each side's vtable.
#include "ts_effects_collection.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSEffectsResource.h"
#include "ts/cTSEffectsResourceFactory.h"
#include "ts/cTSParticlesDescription.h"

#include <cstddef>
#include <new>

namespace {

using namespace fxtest;
using Res = cTSEffectsResource;
using A = cTSEffectsResourceAdapters;

enum : uint32_t {
    evPut16 = 0xc1000000,
    evPut32 = 0xc2000000,
    evPutText = 0xc3000000,
    evGet16 = 0xc4000000,
    evGet32 = 0xc5000000,
    evGetText = 0xc6000000,
    evMeta = 0xc7000000,
    evCreate = 0xc8000000,
    evDescRead = 0xc9000000,
    evDescWrite = 0xca000000,
    evEffectWrite = 0xcb000000,
    evEffectRead = 0xcc000000,
    evStreamTrap = 0xcd000000,
};

uint8_t *gBase = nullptr; // the resource under test (pointers into it are logged as offsets)
uint32_t Off(const void *p)
{
    const auto *q = static_cast<const uint8_t *>(p);
    if (gBase && q >= gBase && q < gBase + sizeof(Res)) return 0x0f000000u | uint32_t(q - gBase);
    return p ? 0x0c000000u : 0;
}

void EvText(const char *text)
{
    const size_t n = std::strlen(text);
    Ev({uint32_t(n)});
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text + i, n - i < 4 ? n - i : 4);
        Ev({w});
    }
}

// ---- streams ----

using Get32Fn = bool(__thiscall *)(void *, uint32_t *);
using Put32Fn = bool(__thiscall *)(void *, uint32_t);

struct Item {
    uint32_t kind; // 0 a 16-bit number, 1 a 32-bit number, 2 a text
    uint32_t value;
    std::string text;
};
using Tape = std::vector<Item>;

struct FakeStream {
    void *const *vt;
    Tape tape;
    size_t pos = 0;
    bool endText = false; // the last text read came from past the end ("end"): the next number is -1

    const Item *Next()
    {
        return pos < tape.size() ? &tape[pos++] : nullptr;
    }
    uint32_t Number(bool wide)
    {
        const Item *it = Next();
        const bool end = endText;
        endText = false;
        if (!it) return wide && end ? 0xffffffffu : 0;
        return it->kind == 2 ? uint32_t(it->text.size()) : it->value;
    }
};

bool __fastcall OPut16(FakeStream *s, void *, uint16_t v)
{
    Ev({evPut16, v});
    s->tape.push_back({0, v, {}});
    return true;
}
bool __fastcall OPut32(FakeStream *s, void *, uint32_t v)
{
    Ev({evPut32, v});
    s->tape.push_back({1, v, {}});
    return true;
}
bool __fastcall OPutText(FakeStream *s, void *, const char *text)
{
    Ev({evPutText});
    EvText(text);
    s->tape.push_back({2, 0, text});
    return true;
}
bool __fastcall IGet16(FakeStream *s, void *, uint16_t *out)
{
    *out = uint16_t(s->Number(false));
    Ev({evGet16, *out});
    return true;
}
bool __fastcall IGet32(FakeStream *s, void *, uint32_t *out)
{
    *out = s->Number(true);
    Ev({evGet32, *out});
    return true;
}
// The text goes into the string through its own slot 3 (FromChar with a length), as a stream does.
bool __fastcall IGetText(FakeStream *s, void *, cRZString *out)
{
    const Item *it = s->Next();
    std::string text = !it ? "end" : it->kind == 2 ? it->text : std::to_string(it->value);
    s->endText = !it;
    Ev({evGetText});
    EvText(text.c_str());
    using FromChar = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<const FromChar *>(out->mVTable)[3](out, text.data(), uint32_t(text.size()));
    return true;
}
// The metadata: a cRZVariant (the stream takes it; logged and freed here).
void __fastcall ISetMeta(FakeStream *, void *, cRZVariant *v)
{
    if (!v) {
        Ev({evMeta, 0});
        return;
    }
    uint16_t value;
    std::memcpy(&value, v->mPayload, 2);
    uint32_t high;
    std::memcpy(&high, v->mPayload + 4, 4);
    // A cRZVariant's vtable: the image's when the original Read built it (0x9c7aea), ours when our Read did (since lane
    // LQ it builds our class); anything else is logged as 0.
    const void *vtable = *reinterpret_cast<void *const *>(v);
    static const void *const ourVTable = [] {
        cRZVariant probe;
        return *reinterpret_cast<const void *const *>(&probe);
    }();
    const bool variantVTable = vtable == GameAddress<void>(cRZVariant::kVTable) || vtable == ourVTable;
    Ev({evMeta, 1, uint32_t(variantVTable), value, high, v->mRefCount, v->mType,
        v->mCount});
    ::operator delete(v);
}
void __fastcall STrap(FakeStream *, void *)
{
    Ev({evStreamTrap});
}

struct StreamVTables {
    void *out[20];
    void *in[20];
    StreamVTables()
    {
        for (void *&p : out) p = reinterpret_cast<void *>(&STrap);
        for (void *&p : in) p = reinterpret_cast<void *>(&STrap);
        out[7] = reinterpret_cast<void *>(&OPut16);
        out[9] = reinterpret_cast<void *>(&OPut32);
        out[14] = reinterpret_cast<void *>(&OPutText);
        in[7] = reinterpret_cast<void *>(&IGet16);
        in[9] = reinterpret_cast<void *>(&IGet32);
        in[15] = reinterpret_cast<void *>(&IGetText);
        in[19] = reinterpret_cast<void *>(&ISetMeta);
    }
};
const StreamVTables kStreamVT;

// ---- descriptions: the collection's fakes, with slot 4 reading one number and slot 5 writing the id ----

bool __fastcall DescRead(FakeDescription *d, void *, FakeStream *s)
{
    Ev({evDescRead | d->id});
    uint32_t value = 0;
    reinterpret_cast<Get32Fn const *>(s->vt)[9](s, &value);
    return true;
}
bool __fastcall DescWrite(FakeDescription *d, void *, FakeStream *s)
{
    Ev({evDescWrite | d->id});
    reinterpret_cast<Put32Fn const *>(s->vt)[9](s, d->id);
    return true;
}
struct DescVTable {
    void *slots[9];
    DescVTable()
    {
        for (int i = 0; i < 9; ++i) slots[i] = kFakeDescriptionVT[i];
        slots[4] = reinterpret_cast<void *>(&DescRead);
        slots[5] = reinterpret_cast<void *>(&DescWrite);
    }
};
const DescVTable kDescVT;

// The created descriptions come from a pool (ids 0x41-0x50), typed by the factory.
Descriptions *gPool = nullptr;
uint32_t gPoolNext = 0;
template <int Type> cITSComponentDescription *__cdecl StandCreate()
{
    FakeDescription &d = gPool->d[gPoolNext++ % 16];
    d.type = Type;
    Ev({evCreate | uint32_t(Type), d.id});
    return reinterpret_cast<cITSComponentDescription *>(&d);
}

// ---- the effect serializers (not ours yet) ----

void PushEffectSummary(const EffectEntry &e)
{
    Ev({e.mFlags, e.mVariant, e.mParams[0], uint32_t(e.mComponents.size())});
    for (const EffectComponent &c : e.mComponents) {
        Ev({uint32_t(c.mType), uint32_t(c.mIndex)});
        EvText(c.mName.c_str());
    }
}
void __cdecl StandEffectWrite(FakeStream *s, const EffectEntry *e)
{
    Ev({evEffectWrite});
    PushEffectSummary(*e);
    reinterpret_cast<Put32Fn const *>(s->vt)[9](s, e->mFlags);
}
void __cdecl StandEffectRead(FakeStream *s, uint32_t version, EffectEntry *e)
{
    uint32_t v = 0;
    reinterpret_cast<Get32Fn const *>(s->vt)[9](s, &v);
    Ev({evEffectRead, version, v});
    e->mFlags = v;
    e->mVariant = uint16_t(v % 3);
    e->mParams[0] = v * 3;
    e->mComponents.resize(v % 3);
    for (uint32_t k = 0; k < e->mComponents.size(); ++k) {
        EffectComponent &c = e->mComponents[k];
        c.mName = kNames[((v >> 4) + k) % kNameCount];
        c.mType = int32_t(((v >> 8) + k) % 14);
        c.mIndex = int32_t((v >> 12) % 5) - 1;
    }
}

// The game's atexit (its CRT is not initialised here): the original Read's function-local cRZString registers its
// destructor on first use.
int __cdecl StubAtexit(void (*)())
{
    return 0;
}

struct StandIns {
    shape::CodePatch atexit{0x824cbeu, shape::CodeOf(&StubAtexit)};
    shape::StandIn c1{0xbe7ba0u, &cTSParticlesComponentDescription::Create, &StandCreate<1>};
    shape::StandIn c2{0xbf1a70u, &cTSMetaParticlesComponentDescription::Create, &StandCreate<2>};
    shape::StandIn c3{0xbecb10u, &cTSDecalComponentDescription::Create, &StandCreate<3>};
    shape::StandIn c4{0xbf8af0u, &cTSBrushComponentDescription::Create, &StandCreate<4>};
    shape::StandIn c5{0xbfa140u, &cTSScrubberComponentDescription::Create, &StandCreate<5>};
    shape::StandIn c6{0xbf4370u, &cTSSequenceComponentDescription::Create, &StandCreate<6>};
    shape::StandIn c7{0xbf25d0u, &cTSSoundComponentDescription::Create, &StandCreate<7>};
    shape::StandIn c8{0xbf6530u, &cTSCameraComponentDescription::Create, &StandCreate<8>};
    shape::StandIn c9{0xbfa3a0u, &cTSGameComponentDescription::Create, &StandCreate<9>};
    shape::StandIn c10{0xbf3050u, &cTSModelComponentDescription::Create, &StandCreate<10>};
    shape::StandIn c11{0xbf8360u, &cTSScreenComponentDescription::Create, &StandCreate<11>};
    shape::StandIn c12{0xbf6f90u, &cTSWaterComponentDescription::Create, &StandCreate<12>};
    shape::StandIn c13{0xbf9370u, &cTSLightComponentDescription::Create, &StandCreate<13>};
    shape::CodePatch effectWrite{0xbf9ee0u, shape::CodeOf(&StandEffectWrite)};
    shape::CodePatch effectRead{0xbf9da0u, shape::CodeOf(&StandEffectRead)};
};

// ---- a side ----

void *Face4(Res *r) { return static_cast<cTSEffectsCollection *>(r); }
void *Face78(Res *r) { return static_cast<cGZPersistResource *>(r); }

struct World {
    alignas(8) uint8_t buffer[sizeof(Res)];
    Descriptions descs; // ids 1-16: the collection operations'
    Descriptions pool;  // ids 0x41-0x50: what the factories hand out
    Res *obj = nullptr;
    int mode = 0;

    void MakeFakes(uint32_t seed)
    {
        descs.Build(seed);
        pool.Build(seed + 77);
        for (FakeDescription &d : descs.d) d.vt = kDescVT.slots;
        for (uint32_t i = 0; i < 16; ++i) pool.d[i].id = 0x41 + i, pool.d[i].vt = kDescVT.slots;
        gPool = &pool;
        gPoolNext = 0;
    }
    void Build(difftest::Context &ctx, int m, uint32_t seed)
    {
        mode = m;
        MakeFakes(seed);
        std::mt19937 r(seed * 13u + 5);
        for (uint8_t &b : buffer) b = uint8_t(r());
        gBase = buffer;
        if (mode == 1)
            obj = ::new (buffer) Res;
        else
            obj = ctx.Original<Res *(__thiscall *)(void *)>(0xbd2ba0)(buffer);
        static_cast<cGZPersistResource *>(obj)->mRefCount = 10;
    }
    void Destroy(difftest::Context &ctx)
    {
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbd2da0)(obj);
        else if (mode == 1)
            obj->Res::~Res();
        else
            A::Destruct(obj, nullptr);
        obj = nullptr;
    }
};

void PushResource(Blob &b, Res *r)
{
    PushCollection(b, static_cast<const cTSEffectsCollection *>(r));
    const cGZPersistResource *p = r;
    b.insert(b.end(), {uint32_t(p->mRefCount), p->mField8, p->mKey.type, p->mKey.group, p->mKey.instance});
    PushString(b, r->mName);
    b.insert(b.end(), {uint32_t(r->mStrings.size()), uint32_t(r->mStrings.capacity())});
}

void PushTape(Blob &b, const Tape &tape)
{
    b.push_back(uint32_t(tape.size()));
    for (const Item &it : tape) {
        b.insert(b.end(), {it.kind, it.value});
        PushString(b, it.text);
    }
}

// A random input: the format with random counts and entries, at times a wrong version, odd name entries ("end" with
// another index, "END"), or cut short.
Tape RandomTape(std::mt19937 &r)
{
    Tape t;
    t.push_back({0, r() % 10 == 0 ? 1 + r() % 3 : 0, {}});
    for (int table = 0; table < 13; ++table) {
        const uint32_t n = r() % 4 == 0 ? r() % 4 : 0;
        t.push_back({1, n, {}});
        for (uint32_t i = 0; i < n; ++i) t.push_back({1, r() % 1000, {}});
    }
    t.push_back({0, r() % 6, {}});
    const uint32_t effects = r() % 5;
    t.push_back({1, effects, {}});
    for (uint32_t i = 0; i < effects; ++i) t.push_back({1, r() % 0x10000, {}});
    const uint32_t names = r() % 7;
    for (uint32_t i = 0; i < names; ++i) {
        const uint32_t k = r() % 8;
        std::string name = k == 0 ? "end" : k == 1 ? "END" : Name(r);
        if (k > 2)
            for (char &c : name) c = char(tolower(c));
        t.push_back({2, 0, name});
        t.push_back({1, k == 1 ? 0xffffffffu : r() % 6 == 0 ? r() % 1000 : r() % (effects + 1), {}});
    }
    t.push_back({2, 0, "end"});
    t.push_back({1, 0xffffffffu, {}});
    const uint32_t aliases = r() % 4;
    t.push_back({1, aliases, {}});
    for (uint32_t i = 0; i < aliases; ++i) {
        t.push_back({2, 0, Name(r)});
        t.push_back({1, r(), {}});
        t.push_back({1, r(), {}});
    }
    t.push_back({0, r() % 4, {}});
    const uint32_t triggers = r() % 4;
    t.push_back({1, triggers, {}});
    for (uint32_t i = 0; i < triggers; ++i) {
        t.push_back({1, r(), {}});
        t.push_back({2, 0, Name(r)});
    }
    if (r() % 8 == 0) t.resize(r() % t.size());
    return t;
}

// Slot `slot` of the resource's +0 face: the original at `rva` (mode 0), our vtable (1), the adapter (2).
template <class R, class... Args>
R Invoke(difftest::Context &ctx, World &w, uint32_t rva, int slot, R(__fastcall *adapter)(Res *, void *, Args...), Args... args)
{
    if (w.mode == 0) return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(w.obj, args...);
    if (w.mode == 2) return adapter(w.obj, nullptr, args...);
    return CallSlot<R>(w.obj, slot, args...);
}

// A face of the resource (0, 4 or 0x78) at random.
void *Face(Res *r, uint32_t k)
{
    return k == 0 ? static_cast<void *>(r) : k == 1 ? Face4(r) : Face78(r);
}

// A target collection for MergeInto, built and filled the side's way (the original's code in modes 0 and 2).
cTSEffectsCollection *NewTarget(difftest::Context &ctx, World &w, std::mt19937 &r)
{
    cTSEffectsCollection *t = w.mode == 1 ? cTSEffectsCollection::Create()
                                          : ctx.Original<cTSEffectsCollection *(__cdecl *)()>(0xbd8820)();
    const uint32_t steps = r() % 3 == 0 ? 0 : r() % 8;
    for (uint32_t i = 0; i < steps; ++i) {
        if (r() % 2) {
            const EffectEntry e = RandomEffect(r);
            CallSlot<void>(t, 5, Name(r), &e);
        } else {
            CallSlot<uint32_t>(t, 7, r() % 4 ? Name(r) : nullptr, w.descs.At(r()));
        }
    }
    return t;
}

constexpr uint32_t kWeights[] = {6, 5, 2, 2, 1, 2, 2, 3, 4, 3, 3, 2, 2, 1};

uint32_t PickOp(std::mt19937 &r)
{
    uint32_t total = 0;
    for (uint32_t x : kWeights) total += x;
    uint32_t x = r() % total;
    for (uint32_t op = 0;; ++op) {
        if (x < kWeights[op]) return op;
        x -= kWeights[op];
    }
}

void Operate(difftest::Context &ctx, World &w, Blob &b, uint32_t op, std::mt19937 &r)
{
    Res *res = w.obj;
    switch (op) {
    case 0: { // AddEffect (the collection face's slot 5), components naming the descriptions
        const char *name = Name(r);
        EffectEntry e = RandomEffect(r);
        for (EffectComponent &c : e.mComponents)
            if (r() % 2) c.mName = Name(r), c.mType = int32_t(1 + r() % 13), c.mIndex = -1;
        CallSlot<void>(Face4(res), 5, name, &e);
        break;
    }
    case 1: // AddDescription (slot 7)
        b.push_back(CallSlot<uint32_t>(Face4(res), 7, r() % 5 ? Name(r) : nullptr, w.descs.At(r())));
        break;
    case 2: { // AddMessageAlias (slot 9)
        const char *name = Name(r);
        const uint32_t x = r(), y = r();
        CallSlot<void>(Face4(res), 9, name, x, y);
        break;
    }
    case 3: { // AddMessageTrigger (slot 10)
        const EffectMessageTrigger t{r(), Name(r)};
        CallSlot<void>(Face4(res), 10, &t);
        break;
    }
    case 4: // Clear (slot 3)
        CallSlot<void>(Face4(res), 3);
        break;
    case 5: { // SetName
        const char *name = Name(r);
        Invoke<void, const char *>(ctx, w, 0xbd4190, 3, &A::SetName, name);
        break;
    }
    case 6: { // GetName
        const char *name = Invoke<const char *>(ctx, w, 0xbd3070, 4, &A::GetName);
        b.push_back(Off(name));
        EvText(name);
        break;
    }
    case 7: { // Write
        FakeStream s{kStreamVT.out};
        b.push_back(Invoke<bool, void *>(ctx, w, 0xbd41c0, 6, &A::Write, &s));
        PushTape(b, s.tape);
        break;
    }
    case 8: { // Read a random tape
        FakeStream s{kStreamVT.in, RandomTape(r)};
        b.push_back(Invoke<bool, void *>(ctx, w, 0xbd30f0, 7, &A::Read, &s));
        b.push_back(uint32_t(s.pos));
        // A random tape can leave name indices past the end of the effects; the collection's later operations (AddEffect,
        // the merges) index them unchecked, in the original and ours alike. Record what was read, then clear such a
        // state (it is the same in every mode, so every mode clears together).
        const cTSEffectsCollection *c = res;
        bool valid = true;
        for (const auto &n : c->mEffectsByName) valid = valid && n.second < c->mEffects.size();
        if (!valid) {
            PushResource(b, res);
            CallSlot<void>(Face4(res), 3);
            b.push_back(0x0b0000aa);
        }
        break;
    }
    case 9: { // Write, then Read what was written (onto the same resource)
        FakeStream out{kStreamVT.out};
        b.push_back(Invoke<bool, void *>(ctx, w, 0xbd41c0, 6, &A::Write, &out));
        FakeStream in{kStreamVT.in, out.tape};
        b.push_back(Invoke<bool, void *>(ctx, w, 0xbd30f0, 7, &A::Read, &in));
        b.push_back(uint32_t(in.pos));
        break;
    }
    case 10: { // MergeInto a new collection (the source's flag at random)
        cTSEffectsCollection *t = NewTarget(ctx, w, r);
        static_cast<cTSEffectsCollection *>(res)->mFlag58 = r() % 3 == 0;
        b.insert(b.end(), gEv.begin(), gEv.end());
        gEv.clear();
        Invoke<void, cTSEffectsCollection *>(ctx, w, 0xbd2ec0, 5, &A::MergeInto, t);
        b.insert(b.end(), gEv.begin(), gEv.end());
        gEv.clear();
        PushCollection(b, t);
        t->mRefCount = 1;
        CallSlot<uint32_t>(t, 2); // deleted
        break;
    }
    case 11: { // QueryInterface on a face
        const uint32_t ids[] = {1, 0x456b8f1d, cTSEffectsCollection::kInterfaceId, Res::kInterfaceId, 0xc97cf5cd, 0x12345678};
        const uint32_t id = ids[r() % 6];
        void *out = reinterpret_cast<void *>(0x1234);
        void *face = Face(res, r() % 3);
        const bool ok = w.mode == 2 ? A::QueryInterface(res, nullptr, id, &out) : CallSlot<bool>(face, 0, id, &out);
        b.insert(b.end(), {uint32_t(ok), out == reinterpret_cast<void *>(0x1234) ? 0x1234u : Off(out)});
        break;
    }
    case 12: { // AddRef / Release on faces (the count stays above 1)
        const uint32_t k1 = r() % 3, k2 = r() % 3;
        if (w.mode == 2) {
            b.push_back(A::AddRef(res, nullptr));
            b.push_back(A::Release(res, nullptr));
        } else {
            b.push_back(CallSlot<uint32_t>(Face(res, k1), 1));
            b.push_back(CallSlot<uint32_t>(Face(res, k2), 2));
        }
        break;
    }
    case 13: // GetClassId
        b.push_back(Invoke<uint32_t>(ctx, w, 0xbd3050, 8, &A::GetClassId));
        break;
    }
}

void PushDescs(Blob &b, World &w)
{
    for (const FakeDescription &d : w.descs.d) b.push_back(d.refs);
    for (const FakeDescription &d : w.pool.d) b.push_back(d.refs);
}

} // namespace

// The constructor, random operations over every slot of the three faces, the destructor; three modes compared.
DIFF_TEST(EffectsResource_Operations, 0xbd30f0)
{
    StandIns stands;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            World w;
            gEv.clear();
            w.Build(ctx, mode, seed);
            Blob &b = blobs[mode];
            PushResource(b, w.obj);
            std::mt19937 r(seed * 1000003u + 3);
            const uint32_t steps = 20 + seed * 2;
            for (uint32_t step = 0; step < steps; ++step) {
                const uint32_t op = PickOp(r);
                b.push_back(0x0b000000u | op);
                Operate(ctx, w, b, op, r);
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushResource(b, w.obj);
            }
            difftest::FreeRecorder rec;
            w.Destroy(ctx);
            rec.Stop();
            b.insert(b.end(), gEv.begin(), gEv.end());
            size_t sizes[512];
            const size_t n = rec.SortedSizes(sizes, 512);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 512; ++i) b.push_back(uint32_t(sizes[i]));
            PushDescs(b, w);
            gBase = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Operations (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "Operations (adapters)", seed);
    }
}

// The factory 0xbd2f90 and construction in place (0xbd2ba0 / the Construct adapter); deletion through the last Release
// (0xbd4010 -> the persist face's slot 12, the thunk 0xbd2e43), through the collection face's slot 12 (the thunk 0xbd2e3b)
// and the deleting destructor 0xbd2e50 itself, with flags 1 and 0.
DIFF_TEST(EffectsResource_CreateDelete, 0xbd2f90)
{
    StandIns stands;
    for (uint32_t seed = 0; seed < 30; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Blob &b = blobs[mode];
            World w;
            w.MakeFakes(seed);
            gEv.clear();
            std::mt19937 r(seed);
            const uint32_t how = seed % 4;
            void *block = nullptr;
            Res *res;
            if (how == 3) {
                block = ::operator new(sizeof(Res));
                std::memset(block, 0x5c, sizeof(Res));
            }
            if (mode == 0)
                res = how == 3 ? ctx.Original<Res *(__thiscall *)(void *)>(0xbd2ba0)(block) : ctx.Original<Res *(__cdecl *)()>(0xbd2f90)();
            else if (mode == 1)
                res = how == 3 ? ::new (block) Res : Res::Create();
            else
                res = how == 3 ? A::Construct(static_cast<Res *>(block), nullptr) : Res::Create();
            w.obj = res;
            w.mode = mode;
            gBase = reinterpret_cast<uint8_t *>(res);
            PushResource(b, res);
            for (int i = 0; i < 5; ++i) Operate(ctx, w, b, r() % 2 ? 1 : 0, r);
            {
                FakeStream s{kStreamVT.in, RandomTape(r)};
                Invoke<bool, void *>(ctx, w, 0xbd30f0, 7, &A::Read, &s);
            }
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            difftest::FreeRecorder rec;
            if (how == 0) {
                // AddRef to 1, then the last Release (on a face at random) deletes.
                const uint32_t k = r() % 3;
                if (mode == 2) {
                    b.push_back(A::AddRef(res, nullptr));
                    b.push_back(A::Release(res, nullptr));
                } else if (mode == 0 && k == 0) {
                    b.push_back(ctx.Original<uint32_t(__thiscall *)(void *)>(0xbd2ea0)(res));
                    b.push_back(ctx.Original<uint32_t(__thiscall *)(void *)>(0xbd4010)(res));
                } else {
                    b.push_back(CallSlot<uint32_t>(Face(res, k), 1));
                    b.push_back(CallSlot<uint32_t>(Face(res, k), 2));
                }
            } else {
                const uint32_t flags = how == 3 ? 0u : 1u;
                void *face = how == 1 ? Face4(res) : Face78(res);
                void *got;
                if (mode == 0 && how == 2)
                    got = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbd2e50)(res, flags);
                else if (mode == 2)
                    got = A::DeletingDestructor(res, nullptr, flags);
                else
                    got = CallSlot<void *>(face, 12, flags);
                b.push_back(uint32_t(got == res));
            }
            rec.Stop();
            b.push_back(uint32_t(rec.Freed(res)));
            if (how == 3 && !rec.Freed(res)) ::operator delete(block);
            b.insert(b.end(), gEv.begin(), gEv.end());
            size_t sizes[512];
            const size_t n = rec.SortedSizes(sizes, 512);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 512; ++i) b.push_back(uint32_t(sizes[i]));
            PushDescs(b, w);
            gBase = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "CreateDelete (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "CreateDelete (adapters)", seed);
    }
}

// ---- cTSEffectsResourceFactory ----

namespace {

using Fac = cTSEffectsResourceFactory;
using FA = cTSEffectsResourceFactoryAdapters;

enum : uint32_t {
    evRecQI = 0xd1000000,
    evRecRelease = 0xd2000000,
    evRecKey = 0xd3000000,
    evResType = 0xd4000000,
    evResIo = 0xd5000000,
    evFakeTrap = 0xd6000000,
};

// A record (and its own serial record): slot 0 answers the serial record id with itself, the stream ids with `stream`
// (null at times), 2 is logged, 3 hands out the key.
struct FakeRecord {
    void *const *vt;
    uint32_t key[3];
    bool serialOk;
    void *stream;
};
bool __fastcall RecQI(FakeRecord *r, void *, uint32_t riid, void **out)
{
    void *answer = riid == 0xc56b8f08u ? (r->serialOk ? r : nullptr) : riid == 0x051b11bdu || riid == 0xc51b11cbu ? r->stream : nullptr;
    Ev({evRecQI, riid, uint32_t(answer != nullptr)});
    if (answer) *out = answer;
    return answer != nullptr;
}
uint32_t __fastcall RecRelease(FakeRecord *, void *)
{
    Ev({evRecRelease});
    return 1;
}
const uint32_t *__fastcall RecKey(FakeRecord *r, void *)
{
    Ev({evRecKey});
    return r->key;
}
void __fastcall FakeTrap(void *, void *)
{
    Ev({evFakeTrap});
}
struct RecordVTable {
    void *slots[8];
    RecordVTable()
    {
        for (void *&p : slots) p = reinterpret_cast<void *>(&FakeTrap);
        slots[0] = reinterpret_cast<void *>(&RecQI);
        slots[2] = reinterpret_cast<void *>(&RecRelease);
        slots[3] = reinterpret_cast<void *>(&RecKey);
    }
};
const RecordVTable kRecordVT;
FakeRecord gStreamFake{kRecordVT.slots, {}, false, nullptr}; // the stream slots 10 / 11 hand on (released: slot 2 logged)

// An effects resource as slots 10 / 11 see it: the persist face at +0x78 (slot 5 the type), the +0 face's slots 6 / 7
// (Write / Read) logged with the stream, answering `ret`.
struct FakeEffects {
    void *const *vt0;
    uint8_t pad[0x74];
    void *const *vt78;
    uint32_t type;
    uint8_t ret;
};
static_assert(offsetof(FakeEffects, vt78) == 0x78);
uint32_t __fastcall FxType(void *face, void *)
{
    const auto *f = reinterpret_cast<const FakeEffects *>(static_cast<uint8_t *>(face) - 0x78);
    Ev({evResType});
    return f->type;
}
bool __fastcall FxWrite(FakeEffects *f, void *, void *stream)
{
    Ev({evResIo | 6, uint32_t(stream == &gStreamFake)});
    return f->ret;
}
bool __fastcall FxRead(FakeEffects *f, void *, void *stream)
{
    Ev({evResIo | 7, uint32_t(stream == &gStreamFake)});
    return f->ret;
}
struct EffectsVTables {
    void *face0[9];
    void *face78[13];
    EffectsVTables()
    {
        for (void *&p : face0) p = reinterpret_cast<void *>(&FakeTrap);
        for (void *&p : face78) p = reinterpret_cast<void *>(&FakeTrap);
        face0[6] = reinterpret_cast<void *>(&FxWrite);
        face0[7] = reinterpret_cast<void *>(&FxRead);
        face78[5] = reinterpret_cast<void *>(&FxType);
    }
};
const EffectsVTables kEffectsVT;

void PushFactory(Blob &b, const Fac *f)
{
    b.insert(b.end(), {f->mRefCount, uint32_t(f->mLock.mVTable == GameAddress<void>(cRZCriticalSection::kVTable)),
                       f->mLock.mBlock ? uint32_t(f->mLock.mBlock->lockCount) : 0xdeadu});
}

// What a created resource is: the face handed out (its offset from the +0 face), the class id, then released.
void PushCreated(Blob &b, void *out)
{
    void *res = nullptr;
    const bool ok = CallSlot<bool>(out, 0, Res::kInterfaceId, &res);
    b.insert(b.end(), {uint32_t(ok), ok ? uint32_t(static_cast<uint8_t *>(out) - static_cast<uint8_t *>(res)) : 0u});
    if (!ok) return;
    b.push_back(CallSlot<uint32_t>(res, 8));
    b.push_back(CallSlot<uint32_t>(res, 2));
    b.push_back(CallSlot<uint32_t>(out, 2)); // the last: deleted
}

uint32_t PickRiid(std::mt19937 &r)
{
    const uint32_t ids[] = {Res::kInterfaceId, cTSEffectsCollection::kInterfaceId, 0x456b8f1du, 1u, 0x12345678u};
    return ids[r() % 5];
}

// Slot `slot` of the factory: the original at `rva` (mode 0), our vtable (1), the adapter (2).
template <class R, class... Args>
R InvokeFac(difftest::Context &ctx, int mode, Fac *f, uint32_t rva, int slot, R(__fastcall *adapter)(Fac *, void *, Args...),
            Args... args)
{
    if (mode == 0) return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(f, args...);
    if (mode == 2) return adapter(f, nullptr, args...);
    return CallSlot<R>(f, slot, args...);
}

void OperateFactory(difftest::Context &ctx, int mode, Fac *f, Blob &b, uint32_t op, std::mt19937 &r)
{
    switch (op) {
    case 0: { // slot 9: a new resource by type
        const uint32_t type = r() % 4 ? Res::kResourceType : r() % 2 ? Res::kResourceType + 1 : r();
        const uint32_t riid = PickRiid(r);
        void *out = reinterpret_cast<void *>(0x1234);
        const bool ok = InvokeFac<bool, uint32_t, uint32_t, void **>(ctx, mode, f, 0xbd2ed0, 9, &FA::CreateResource, type, riid, &out);
        b.insert(b.end(), {uint32_t(ok), out == nullptr ? 0u : out == reinterpret_cast<void *>(0x1234) ? 0x1234u : 1u});
        if (ok) PushCreated(b, out);
        break;
    }
    case 1:   // slot 10: read
    case 2: { // slot 11: write
        FakeEffects fx{};
        fx.vt0 = kEffectsVT.face0;
        fx.vt78 = kEffectsVT.face78;
        fx.type = r() % 4 ? Res::kResourceType : r();
        fx.ret = uint8_t(r() % 3 == 0 ? r() : r() % 2);
        FakeRecord rec{kRecordVT.slots, {r(), r(), r()}, true, r() % 4 ? static_cast<void *>(&gStreamFake) : nullptr};
        auto *resource = reinterpret_cast<cIGZPersistResource *>(&fx.vt78);
        auto *record = reinterpret_cast<cIGZUnknown *>(&rec);
        const bool ok = op == 1 ? InvokeFac<bool, cIGZPersistResource *, cIGZUnknown *>(ctx, mode, f, 0xbd4030, 10, &FA::ReadRecord, resource, record)
                                : InvokeFac<bool, cIGZPersistResource *, cIGZUnknown *>(ctx, mode, f, 0xbd40e0, 11, &FA::WriteRecord, resource, record);
        b.push_back(uint32_t(ok) & 0xff);
        break;
    }
    case 3: { // slots 3 / 8 (the base's CreateInstance): create by the record's type, read from its stream, ask for riid
        FakeStream s{kStreamVT.in, RandomTape(r)};
        FakeRecord rec{kRecordVT.slots, {r() % 4 ? Res::kResourceType : r(), r(), r()}, r() % 5 != 0, r() % 5 ? &s : nullptr};
        const uint32_t riid = PickRiid(r);
        void *out = reinterpret_cast<void *>(0x1234);
        const uint32_t a4 = r() % 3 == 0 ? r() % 2 : 0u, a5 = r() % 3 == 0 ? r() % 2 : 0u; // non-zero: the slot 4 route
        const bool ok = r() % 2 ? CallSlot<bool>(f, 3, &rec, riid, &out, a4, a5) : CallSlot<bool>(f, 8, &rec, riid, &out);
        b.insert(b.end(), {uint32_t(ok), out == nullptr ? 0u : out == reinterpret_cast<void *>(0x1234) ? 0x1234u : 1u,
                           uint32_t(s.pos)});
        if (ok) PushCreated(b, out);
        break;
    }
    default: { // slots 0-2 and 5 / 6 of the base, the count kept above 1
        const uint32_t k = r() % 4;
        if (k == 0) {
            const uint32_t ids[] = {1u, 0xa56b8f17u, 0x456b8f1du, Res::kInterfaceId};
            void *out = reinterpret_cast<void *>(0x1234);
            const bool ok = CallSlot<bool>(f, 0, ids[r() % 4], &out);
            b.insert(b.end(), {uint32_t(ok), out == f ? 1u : out == reinterpret_cast<void *>(0x1234) ? 0x1234u : 2u});
            if (ok) CallSlot<uint32_t>(f, 2);
        } else if (k == 1) {
            b.push_back(CallSlot<uint32_t>(f, 1));
            b.push_back(CallSlot<uint32_t>(f, 2));
        } else {
            FakeEffects fx{};
            fx.vt0 = kEffectsVT.face0;
            fx.vt78 = kEffectsVT.face78;
            fx.type = r() % 4 ? Res::kResourceType : r();
            fx.ret = uint8_t(r() % 2);
            FakeRecord rec{kRecordVT.slots, {r(), r(), r()}, r() % 4 != 0, r() % 4 ? static_cast<void *>(&gStreamFake) : nullptr};
            b.push_back(CallSlot<bool>(f, k == 2 ? 5 : 6, &fx.vt78, &rec));
        }
        break;
    }
    }
}

} // namespace

// The factory on a block (the original constructor 0xbd2c50 against ours), random operations over its slots (the base's
// through each side's vtable), then the destructor 0xbd2e30 or the deleting destructor 0xbd2e80 with flags 0; and the
// heap path: Create 0xbd2ff0, counted, the last Release deleting (slot 7, flags 1).
DIFF_TEST(EffectsResourceFactory_Slots, 0xbd2ed0)
{
    StandIns stands;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            World w;
            w.MakeFakes(seed);
            gEv.clear();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 1);
            alignas(8) uint8_t block[sizeof(Fac)];
            for (uint8_t &x : block) x = uint8_t(r());
            Fac *f = mode == 1 ? ::new (block) Fac : ctx.Original<Fac *(__thiscall *)(void *)>(0xbd2c50)(block);
            if (mode == 2 && seed % 2) f = FA::Construct(block, nullptr);
            b.push_back(uint32_t(f == reinterpret_cast<Fac *>(block)));
            PushFactory(b, f);
            f->mRefCount = 5;
            const uint32_t steps = 10 + seed % 20;
            for (uint32_t step = 0; step < steps; ++step) {
                const uint32_t op = r() % 5;
                b.push_back(0x0b000000u | op);
                OperateFactory(ctx, mode, f, b, op, r);
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushFactory(b, f);
            }
            difftest::FreeRecorder rec;
            const bool deleting = seed % 3 == 0;
            if (mode == 0)
                deleting ? (void)ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbd2e80)(f, 0)
                         : ctx.Original<void(__thiscall *)(void *)>(0xbd2e30)(f);
            else if (mode == 1)
                deleting ? (void)CallSlot<void *>(f, 7, 0u) : f->~Fac();
            else
                deleting ? (void)FA::DeletingDestructor(f, nullptr, 0) : FA::Destruct(f, nullptr);
            rec.Stop();
            b.push_back(uint32_t(rec.Freed(block)));
            size_t sizes[64];
            const size_t n = rec.SortedSizes(sizes, 64);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 64; ++i) b.push_back(uint32_t(sizes[i]));

            // The heap path.
            Fac *h = mode == 0 ? ctx.Original<Fac *(__cdecl *)()>(0xbd2ff0)() : Fac::Create();
            PushFactory(b, h);
            b.push_back(CallSlot<uint32_t>(h, 1));
            difftest::FreeRecorder rec2;
            b.push_back(mode == 2 && seed % 2 ? (FA::DeletingDestructor(h, nullptr, 1), 0u) : CallSlot<uint32_t>(h, 2));
            rec2.Stop();
            b.push_back(uint32_t(rec2.Freed(h)));
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
        }
        Same(ctx, blobs[0], blobs[1], "Factory (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "Factory (adapters)", seed);
    }
}
