#include "difftest.h"
#include "free_scope.h"
#include "ts/cTSTreeProperties.h"
#include "misc/UtilitiesC.h"
#include "game/vtable.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <random>
#include <intrin.h>

namespace {
struct Fake { void **vt; uint32_t id; };
void *tableVT[21]{}, *streamVT[21]{}, *ownerVT[18]{}, *selfVT[34]{}, *directorVT[13]{}, *frameworkVT[4]{};
Fake parameter{tableVT, 1}, local{tableVT, 2}, owner{ownerVT, 3}, oldOwner{ownerVT, 4};
Fake stream{streamVT, 5}, legacy{streamVT, 6}, framework{frameworkVT, 7};
struct Guarded { uint8_t before[16]; alignas(cTSTreeProperties) uint8_t bytes[0x60]; uint8_t after[16]; } work;
cTSTreeProperties *Object() { return reinterpret_cast<cTSTreeProperties *>(work.bytes); }
std::vector<uint32_t> trace;
std::vector<std::string> texts;
std::array<std::string, 12> names;
uint32_t version, failures, transfers, errorByte, createIndex, stringIndex;
int32_t parameterCount, localCount;
bool querySuccess, queryOutput;
int throwAt = -1, calls;
uint8_t *initialFlags;
size_t initialFlagCapacity;
bool forceZeroFlags;
alignas(cTSTreeProperties) uint8_t sourceBytes[0x60];
cTSTreeProperties *Source() { return reinterpret_cast<cTSTreeProperties *>(sourceBytes); }
void *sourceVT[34]{}, *otherVT[1]{}, *textVT[18]{}, *storageVT[74]{};
Fake other{otherVT, 9}, textObject{textVT, 10}, storage{storageVT, 11};
Fake sourceParameter{tableVT, 12}, sourceLocal{tableVT, 13};
bool querySelf;
std::string inputText;
uint32_t nameDifference, countDifference, resourceCalls;
bool resourcePresent;
int32_t storageResult;
void *lastTemporaryBlock;
void *resourceVT[12]{};
Fake resourceA{resourceVT, 14}, resourceB{resourceVT, 15};

void Tick() { if (calls++ == throwAt) throw 17; }
uint32_t Destination(const void *pointer)
{
    uintptr_t p = reinterpret_cast<uintptr_t>(pointer), base = reinterpret_cast<uintptr_t>(Object());
    if (p >= base && p < base + 0x60) return uint32_t(p - base) + 0x100;
    auto &v = Object()->mParameterFlags;
    if (p >= reinterpret_cast<uintptr_t>(v.data()) && p < reinterpret_cast<uintptr_t>(v.data()) + v.size())
        return 0x200 + uint32_t(p - reinterpret_cast<uintptr_t>(v.data()));
    return 0xffffffff;
}
bool Transfer(unsigned slot, uint32_t value, void *out = nullptr, unsigned width = 4, bool alwaysFill = false)
{
    Tick();
    bool ok = !(failures & (1u << (transfers++ % 32)));
    trace.insert(trace.end(), {slot, value, out ? Destination(out) : 0, ok});
    if (out && (ok || alwaysFill)) std::memcpy(out, &value, width);
    return ok;
}
uint32_t __fastcall AddRef(Fake *f, void *) { Tick(); trace.insert(trace.end(), {f->id, 1}); return 0xabcdef01; }
uint32_t __fastcall ReleaseFake(Fake *f, void *) { Tick(); trace.insert(trace.end(), {f->id, 2}); return 0xabcdef02; }
uint32_t __fastcall SelfAdd(cTSTreeProperties *f, void *) { Tick(); trace.push_back(101); return ++f->mRefCount; }
void __fastcall DeleteFake(cTSTreeProperties *, void *, uint8_t flags) { Tick(); trace.insert(trace.end(), {133, flags}); }
uint32_t __fastcall ClassId(cTSTreeProperties *, void *) { Tick(); trace.push_back(105); return 0x12345678; }
void *__fastcall SelectTable(cTSTreeProperties *, void *, int32_t scope)
{
    Tick(); trace.insert(trace.end(), {131, uint32_t(scope)});
    return scope == 0 ? &parameter : scope == 1 ? &local : nullptr;
}
uint32_t __fastcall Tree(Fake *f, void *, uint32_t id) { Tick(); trace.insert(trace.end(), {f->id, 17, id}); return id ^ 0xa56b3912; }
void *__fastcall TableService(Fake *f, void *) { Tick(); trace.insert(trace.end(), {f->id, 3}); return f; }
void __fastcall Group(Fake *f, void *, uint32_t group) { Tick(); trace.insert(trace.end(), {f->id, 9, group}); }
int32_t __fastcall Count(Fake *f, void *, int32_t language)
{
    Tick(); trace.insert(trace.end(), {f->id, 5, uint32_t(language)});
    int32_t value = f == &parameter || f == &sourceParameter ? parameterCount : localCount;
    if ((f == &sourceParameter && countDifference == 1) || (f == &sourceLocal && countDifference == 2)) ++value;
    return value;
}
void __fastcall GetName(Fake *f, void *, cRZString *out, int32_t index, int32_t language)
{
    Tick(); trace.insert(trace.end(), {f->id, 7, uint32_t(index), uint32_t(language)});
    out->mString = names.at((index + (f == &parameter || f == &sourceParameter ? 1 : 2)) % names.size());
    if ((f == &sourceParameter && nameDifference == 1) || (f == &sourceLocal && nameDifference == 2)) out->mString += "different";
    lastTemporaryBlock = out->mString.capacity() > 15 ? out->mString.data() : nullptr;
    if (lastTemporaryBlock && out->mString.capacity() + 1 >= 4096) lastTemporaryBlock = reinterpret_cast<void **>(lastTemporaryBlock)[-1];
    if (lastTemporaryBlock) difftest::BeginFrees(); // from here to the end of the call: is the temporary freed
}
void __fastcall PutName(Fake *f, void *, cRZString *text, int32_t index, int32_t language)
{
    Tick(); trace.insert(trace.end(), {f->id, 10, uint32_t(index), uint32_t(language)});
    texts.push_back(text->mString);
}
uint32_t __fastcall Create(Fake *, void *, uint32_t cls, uint32_t iid, void **out)
{
    Tick(); trace.insert(trace.end(), {7, 3, cls, iid, Destination(out)});
    *out = createIndex++ == 0 ? &local : &parameter;
    return 1;
}
void *__fastcall Framework(void *, void *) { Tick(); trace.push_back(112); return &framework; }
bool __fastcall Attach(Fake *, void *, UtilObjectB *meta)
{
    // Compare every byte of the allocated metadata before transferring ownership.
    const auto *words = reinterpret_cast<uint32_t *>(meta);
    trace.push_back(19);
    trace.insert(trace.end(), words, words + 5);
    ::operator delete(meta, sizeof(UtilObjectB));
    Tick();
    return true;
}
bool __fastcall Put9(Fake *, void *, uint32_t value) { return Transfer(9, value); }
bool __fastcall Put8(Fake *, void *, uint32_t value) { return Transfer(8, value); }
bool __fastcall Put5(Fake *, void *, uint32_t value) { return Transfer(5, value); }
bool __fastcall PutString(Fake *, void *, cRZString *text) { texts.push_back(text->mString); return Transfer(15, uint32_t(text->mString.size())); }
bool __fastcall Get9(Fake *, void *, void *out)
{
    const uint32_t value = transfers == 1 ? version : 0xdeadc0de;
    // Header/version/count locals are genuinely uninitialized in the original. On a
    // failed getter fill these locals deterministically; do not mask failed field reads.
    return Transfer(9, value, out, 4, true);
}
bool __fastcall Get8(Fake *, void *, void *out)
{
    uint32_t value = transfers == 3 ? parameterCount : transfers == 4 ? localCount :
        transfers == 5 ? errorByte * 0x01010101 : 0xc0ffe000 + transfers;
    const bool uninitializedCount = transfers < 5;
    return Transfer(8, value, out, 4, uninitializedCount);
}
bool __fastcall Get5(Fake *, void *, void *out) { return Transfer(5, 0x80 + transfers, out, 1); }
bool __fastcall GetString(Fake *f, void *, cRZString *text)
{
    const bool ok = Transfer(f == &legacy ? 20 : 15, stringIndex);
    if (ok) text->mString = names.at(stringIndex % names.size());
    lastTemporaryBlock = text->mString.capacity() > 15 ? text->mString.data() : nullptr;
    if (lastTemporaryBlock && text->mString.capacity() + 1 >= 4096) lastTemporaryBlock = reinterpret_cast<void **>(lastTemporaryBlock)[-1];
    if (lastTemporaryBlock) difftest::BeginFrees(); // from here to the end of the call: is the temporary freed
    ++stringIndex;
    return ok;
}
uint8_t __fastcall Query(Fake *, void *, uint32_t iid, void **out)
{
    Tick(); trace.insert(trace.end(), {0, iid, querySuccess, queryOutput});
    if (queryOutput) *out = &legacy;
    return querySuccess ? 0x80 : 0;
}
uint8_t __fastcall Error(Fake *, void *) { Tick(); trace.insert(trace.end(), {18, errorByte}); return uint8_t(errorByte); }
void __fastcall CopyDescription(cRZString *, void *, const cIGZString *text)
{
    Tick(); trace.insert(trace.end(), {17, uint32_t(reinterpret_cast<uintptr_t>(text))});
}
void __fastcall ScopeText(Fake *, void *, const char *text) { Tick(); trace.push_back(4); texts.emplace_back(text); }
uint8_t __fastcall QuerySource(Fake *, void *, uint32_t iid, void **out)
{
    Tick(); trace.insert(trace.end(), {9, 0, iid, querySuccess, queryOutput});
    if (queryOutput) *out = querySelf ? Object() : Source();
    return querySuccess ? 0xff : 0;
}
uint32_t __fastcall SourceRelease(cTSTreeProperties *, void *) { Tick(); trace.push_back(102); return 3; }
void __fastcall CopyTable(Fake *f, void *, Fake *from) { Tick(); trace.insert(trace.end(), {f->id, 4, from->id}); }
void __fastcall RemoveTable(Fake *f, void *, int32_t index, uint32_t language) { Tick(); trace.insert(trace.end(), {f->id, 14, uint32_t(index), language}); }
uint32_t __fastcall TextLength(Fake *, void *) { Tick(); trace.push_back(107); return uint32_t(inputText.size()); }
const char *__fastcall TextData(Fake *, void *) { Tick(); trace.push_back(106); return inputText.data(); }
void __fastcall TextCopy(Fake *, void *, const cRZString *value)
{
    Tick(); trace.insert(trace.end(), {117, value->mRefCount, uint32_t(value->mString.size()), uint32_t(value->mString.capacity())});
    texts.push_back(value->mString);
}
int32_t __fastcall CountSlot(cTSTreeProperties *, void *, int32_t scope) { Tick(); trace.insert(trace.end(), {116, uint32_t(scope)}); return scope == 0 ? parameterCount : localCount; }
void *__fastcall ResourceSlot(cTSTreeProperties *, void *)
{
    Tick(); trace.insert(trace.end(), {114, resourceCalls});
    ++resourceCalls;
    return !resourcePresent && resourceCalls == 1 ? nullptr : resourceCalls % 2 == 1 ? &resourceA : &resourceB;
}
uint16_t __fastcall TreeIdSlot(cTSTreeProperties *o, void *) { Tick(); trace.push_back(113); return o->mTreeId; }
uint32_t __fastcall ResourceGroup(Fake *f, void *) { Tick(); trace.insert(trace.end(), {f->id, 11}); return 0xaf013957; }
int32_t __fastcall Store(Fake *, void *, cTSTreeProperties *o, Fake *resource, uint32_t tree)
{
    Tick(); trace.insert(trace.end(), {72, uint32_t(reinterpret_cast<uintptr_t>(o)), resource->id, tree}); return storageResult;
}
int32_t __fastcall Load(Fake *, void *, cTSTreeProperties *o, Fake *resource, uint32_t tree, uint32_t zero)
{
    Tick(); trace.insert(trace.end(), {73, uint32_t(reinterpret_cast<uintptr_t>(o)), resource->id, tree, zero}); return storageResult;
}
void *__fastcall KeyService(Fake *, void *) { Tick(); trace.push_back(303); return &storage; }
void __fastcall Key(Fake *, void *, const uint32_t *key) { Tick(); trace.push_back(310); trace.insert(trace.end(), key, key + 4); }
void __fastcall Defaults(cTSTreeProperties *, void *) { Tick(); trace.push_back(123); }

struct DirectorFixture {
    void **object;
    void *saved;
    int *guard, savedGuard;
    explicit DirectorFixture(difftest::Context &ctx)
    {
        object = ctx.image.At<void *>(0x10c9390); saved = *object;
        guard = ctx.image.At<int>(0x10c93f4); savedGuard = *guard;
        *object = directorVT; *guard = INT_MIN; // bypass static initialization on both sides
    }
    ~DirectorFixture() { *object = saved; *guard = savedGuard; }
};
void Setup(bool reading)
{
    trace.reserve(10000); texts.reserve(1000);
    ownerVT[1] = reinterpret_cast<void *>(&AddRef); ownerVT[2] = reinterpret_cast<void *>(&ReleaseFake);
    ownerVT[17] = reinterpret_cast<void *>(&Tree);
    tableVT[2] = reinterpret_cast<void *>(&ReleaseFake); tableVT[3] = reinterpret_cast<void *>(&TableService);
    tableVT[5] = reinterpret_cast<void *>(&Count); tableVT[7] = reinterpret_cast<void *>(&GetName);
    tableVT[9] = reinterpret_cast<void *>(&Group); tableVT[10] = reinterpret_cast<void *>(&PutName);
    tableVT[4] = reinterpret_cast<void *>(&CopyTable); tableVT[14] = reinterpret_cast<void *>(&RemoveTable);
    selfVT[1] = reinterpret_cast<void *>(&SelfAdd); selfVT[5] = reinterpret_cast<void *>(&ClassId);
    selfVT[31] = reinterpret_cast<void *>(&SelectTable); selfVT[33] = reinterpret_cast<void *>(&DeleteFake);
    streamVT[0] = reinterpret_cast<void *>(&Query); streamVT[2] = reinterpret_cast<void *>(&ReleaseFake);
    streamVT[5] = reading ? reinterpret_cast<void *>(&Get5) : reinterpret_cast<void *>(&Put5);
    streamVT[8] = reading ? reinterpret_cast<void *>(&Get8) : reinterpret_cast<void *>(&Put8);
    streamVT[9] = reading ? reinterpret_cast<void *>(&Get9) : reinterpret_cast<void *>(&Put9);
    streamVT[15] = reading ? reinterpret_cast<void *>(&GetString) : reinterpret_cast<void *>(&PutString);
    streamVT[18] = reinterpret_cast<void *>(&Error); streamVT[19] = reinterpret_cast<void *>(&Attach);
    streamVT[20] = reinterpret_cast<void *>(&GetString);
    directorVT[12] = reinterpret_cast<void *>(&Framework); frameworkVT[3] = reinterpret_cast<void *>(&Create);
    selfVT[2] = sourceVT[2] = reinterpret_cast<void *>(&SourceRelease);
    selfVT[16] = reinterpret_cast<void *>(&CountSlot);
    otherVT[0] = reinterpret_cast<void *>(&QuerySource);
    textVT[6] = reinterpret_cast<void *>(&TextData); textVT[7] = reinterpret_cast<void *>(&TextLength); textVT[17] = reinterpret_cast<void *>(&TextCopy);
    selfVT[13] = reinterpret_cast<void *>(&TreeIdSlot); selfVT[14] = reinterpret_cast<void *>(&ResourceSlot); selfVT[23] = reinterpret_cast<void *>(&Defaults);
    storageVT[72] = reinterpret_cast<void *>(&Store); storageVT[73] = reinterpret_cast<void *>(&Load);
    storageVT[3] = reinterpret_cast<void *>(&KeyService); storageVT[10] = reinterpret_cast<void *>(&Key);
    resourceVT[11] = reinterpret_cast<void *>(&ResourceGroup);
}
void ResetTrace() { trace.clear(); texts.clear(); lastTemporaryBlock = nullptr; transfers = createIndex = stringIndex = calls = resourceCalls = 0; }
void Seed(unsigned seed, size_t length = 8, size_t capacity = 16, size_t description = 0)
{
    std::mt19937 rng(seed);
    for (auto &byte : reinterpret_cast<std::array<uint8_t, sizeof(work)> &>(work)) byte = uint8_t(rng());
    auto *o = Object();
    o->mVTable = selfVT; o->mOwner = &owner; o->mLocalNames = &local; o->mParameterNames = &parameter;
    // Keep the initial string's unused bytes identical, including its SSO storage.
    std::memset(&o->mDescription.mString, 0, sizeof(std::string)); o->mDescription.Construct();
    o->mDescription.mString.assign(description, 'x');
    new (&o->mParameterFlags) TreeParameterFlags;
    o->mParameterFlags.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) o->mParameterFlags.data()[i] = uint8_t(i * 17 + seed);
    for (size_t i = 0; i < length; ++i) o->mParameterFlags.push_back(forceZeroFlags && (i + seed / 20) % 2 == 0 ? 0 : uint8_t(rng()));
    initialFlags = o->mParameterFlags.data();
    initialFlagCapacity = o->mParameterFlags.capacity();
}
struct Snapshot {
    Guarded object;
    std::vector<uint8_t> flags;
    std::string description;
    std::vector<uint32_t> calls;
    std::vector<std::string> strings;
    bool operator==(const Snapshot &sample) const {
        return std::memcmp(&object, &sample.object, sizeof(object)) == 0 && flags == sample.flags &&
            description == sample.description && calls == sample.calls && strings == sample.strings;
    }
};
Snapshot Snap(bool destroyed = false)
{
    Snapshot s{work, {}, {}, trace, texts};
    auto *o = Object(); auto *words = reinterpret_cast<uint32_t *>(s.object.bytes);
    if (!destroyed) {
        // Retained allocation tails have deterministic seeded bytes. Newly allocated
        // spare capacity is uninitialized in both libraries; only its size is compared.
        // A free-then-allocate operation can reuse the old address; capacity must
        // also match before treating spare bytes as the seeded old allocation.
        const bool retained = o->mParameterFlags.data() == initialFlags && o->mParameterFlags.capacity() == initialFlagCapacity;
        const size_t observable = retained ? o->mParameterFlags.capacity() : o->mParameterFlags.size();
        if (observable) s.flags.assign(o->mParameterFlags.data(), o->mParameterFlags.data() + observable);
        s.description = o->mDescription.mString;
        // Normalize ONLY heap addresses, retaining null/non-null, size and capacity.
        const auto begin = words[0x50 / 4];
        words[0x54 / 4] -= begin; words[0x58 / 4] -= begin; words[0x50 / 4] = begin ? 0xfeed0000 : 0;
        if (o->mDescription.mString.capacity() > 15) words[0x34 / 4] = 0xfeed1000;
    }
    return s;
}
void Cleanup() { Object()->mParameterFlags.~TreeParameterFlags(); Object()->mDescription.Destruct(); }
template<class Action> uint32_t Pair(difftest::Context &ctx, unsigned seed, Action action,
    size_t length = 8, size_t capacity = 16, size_t description = 0)
{
    Seed(seed, length, capacity, description); ResetTrace();
    auto start = Snap();
    uint32_t a = action(true);
    if (lastTemporaryBlock && !difftest::FreedSinceBegin(lastTemporaryBlock)) ctx.Fail("original leaked temporary string seed %u", seed);
    auto left = Snap(); Cleanup();
    Seed(seed, length, capacity, description); ResetTrace();
    if (!(start == Snap())) ctx.Fail("different initial state seed %u", seed);
    uint32_t b = action(false);
    if (lastTemporaryBlock && !difftest::FreedSinceBegin(lastTemporaryBlock)) ctx.Fail("replacement leaked temporary string seed %u", seed);
    auto right = Snap(); Cleanup();
    ++ctx.cases;
    if (a != b || !(left == right)) ctx.Fail("seed %u differs return %x/%x, bytes/heap/trace", seed, a, b);
    return b;
}
}

DIFF_TEST(TreeProperties_Construct, 0x41bcd0)
{
    Setup(false); DirectorFixture fixture(ctx);
    for (unsigned i = 0; i < 64; ++i) {
        Guarded initial; std::mt19937 rng(i);
        for (auto &b : reinterpret_cast<std::array<uint8_t, sizeof(initial)> &>(initial)) b = uint8_t(rng());
        void *inputOwner = i % 2 ? &owner : nullptr;
        work = initial; ResetTrace();
        using Fn = cTSTreeProperties *(__thiscall *)(cTSTreeProperties *, void *, uint16_t);
        auto *a = ctx.Original<Fn>(0x41bcd0)(Object(), inputOwner, uint16_t(i * 997)); auto left = Snap(); Cleanup();
        work = initial; ResetTrace(); auto *b = Object()->Construct(inputOwner, uint16_t(i * 997)); auto right = Snap(); Cleanup();
        ++ctx.cases;
        if (a != Object() || b != Object() || !(left == right)) ctx.Fail("constructor seed %u", i);
    }
    std::printf("  Construct: 32 null, 32 nonnull owners, randomized padding/flag23\n");
}
DIFF_TEST(TreeProperties_Initialize, 0x41c620)
{
    Setup(false); DirectorFixture fixture(ctx);
    for (unsigned i = 0; i < 96; ++i) {
        void *input = i % 3 == 0 ? nullptr : i % 3 == 1 ? &owner : &oldOwner;
        Pair(ctx, i, [&](bool original) {
            if (i % 4 == 0) Object()->mOwner = nullptr;
            if (i % 5 == 0) Object()->mLocalNames = nullptr;
            if (i % 7 == 0) Object()->mParameterNames = nullptr;
            using Fn = uint8_t(__thiscall *)(cTSTreeProperties *, void *, uint16_t);
            return original ? ctx.Original<Fn>(0x41c620)(Object(), input, uint16_t(i * 1001)) : Object()->Initialize(input, uint16_t(i * 1001));
        });
    }
    std::printf("  Initialize: 96 owner same/different/null and table release cases\n");
}
DIFF_TEST(TreeProperties_DeletingDestructor, 0x41be70)
{
    Setup(false);
    constexpr size_t lengths[] = {0, 1, 15, 16, 100, 4095, 4096, 5000};
    unsigned masks[8]{};
    for (unsigned i = 0; i < 128; ++i) {
        const unsigned mask = i % 8; ++masks[mask];
        Snapshot left, right;
        for (unsigned side = 0; side < 2; ++side) {
            Seed(i, lengths[i % 8], lengths[i % 8], lengths[(i / 8) % 8]); ResetTrace();
            auto *o = Object();
            if (!(mask & 1)) o->mOwner = nullptr;
            if (!(mask & 2)) o->mLocalNames = nullptr;
            if (!(mask & 4)) o->mParameterNames = nullptr;
            void *vectorBlock = o->mParameterFlags.data();
            if (vectorBlock && o->mParameterFlags.capacity() >= 4096) vectorBlock = reinterpret_cast<void **>(vectorBlock)[-1];
            void *stringBlock = o->mDescription.mString.capacity() > 15 ? o->mDescription.mString.data() : nullptr;
            if (stringBlock && o->mDescription.mString.capacity() + 1 >= 4096) stringBlock = reinterpret_cast<void **>(stringBlock)[-1];
            using Fn = cTSTreeProperties *(__thiscall *)(cTSTreeProperties *, uint8_t);
            difftest::BeginFrees();
            auto *result = side == 0 ? ctx.Original<Fn>(0x41be70)(o, uint8_t(i & 0xfe)) : o->DeletingDestructor(uint8_t(i & 0xfe));
            const bool vectorFreed = vectorBlock && difftest::FreedSinceBegin(vectorBlock), stringFreed = stringBlock && difftest::FreedSinceBegin(stringBlock);
            if (result != o) ctx.Fail("destructor return");
            if (vectorBlock && !vectorFreed) ctx.Fail("vector not freed");
            if (stringBlock && !stringFreed) ctx.Fail("string not freed");
            auto state = Snap(true);
            // Destructor resets only first SSO byte; retained pointer bytes are the old allocation.
            if (stringBlock) {
                auto *bytes = state.object.bytes + 0x34;
                bytes[1] = bytes[2] = bytes[3] = 0;
            }
            (side == 0 ? left : right) = state;
        }
        ++ctx.cases; if (!(left == right)) ctx.Fail("destructor state %u", i);
    }
    // Deleting bit, observed without reading a freed object.
    for (unsigned side = 0; side < 2; ++side) {
        Seed(7, 0, 0); auto *heap = static_cast<cTSTreeProperties *>(::operator new(0x60));
        std::memcpy(heap, Object(), 0x60); ResetTrace();
        using Fn = cTSTreeProperties *(__thiscall *)(cTSTreeProperties *, uint8_t);
        difftest::BeginFrees();
        auto *result = side == 0 ? ctx.Original<Fn>(0x41be70)(heap, 0xff) : heap->DeletingDestructor(0xff);
        if (result != heap || !difftest::FreedSinceBegin(heap)) ctx.Fail("object not deleted");
        // Empty members were transferred to heap and destroyed there; don't destroy them twice.
        ++ctx.cases;
    }
    std::printf("  Destructor: 128 cases, 16 per reference mask; SSO/heap/aligned allocations; 2 deleting cases\n");
}
DIFF_TEST(TreeProperties_Write, 0x41d0b0)
{
    Setup(false);
    unsigned empty = 0, nonempty = 0, injected = 0;
    for (unsigned i = 0; i < 256; ++i) {
        parameterCount = i % 7 - 1; localCount = (i / 7) % 5 - 1;
        if (parameterCount <= 0) ++empty; else ++nonempty;
        failures = i < 32 ? 1u << i : i * 0x91a37;
        errorByte = std::array<uint32_t, 4>{0, 1, 0x80, 0xff}[i % 4];
        injected += failures != 0;
        Pair(ctx, i, [&](bool original) {
            using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
            return uint32_t(original ? ctx.Original<Fn>(0x41d0b0)(Object(), &stream) : Object()->Write(&stream));
        });
    }
    std::printf("  Write: empty %u/nonempty %u, injected failures %u, four error bytes\n", empty, nonempty, injected);
}
DIFF_TEST(TreeProperties_Read, 0x41c7a0)
{
    Setup(true);
    for (auto &n : names) n = "";
    constexpr uint32_t versions[] = {0, 1, 2, 3, 4, 62, 63, 64, 85, 0x80000000, 0xffffffff};
    for (uint32_t v : versions) {
        version = v; unsigned legacyOK = 0, legacyFail = 0;
        for (unsigned i = 0; i < 128; ++i) {
            parameterCount = i % 7; localCount = (i / 7) % 5 - 1;
            querySuccess = (i % 2) != 0; queryOutput = (i % 4) != 0;
            if (querySuccess && !queryOutput) queryOutput = true; // valid successful query
            if (querySuccess) ++legacyOK; else ++legacyFail;
            failures = i < 32 ? 1u << i : i * 0x371;
            errorByte = std::array<uint32_t, 4>{0, 1, 0x80, 0xff}[i % 4];
            Pair(ctx, i, [&](bool original) {
                using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
                return uint32_t(original ? ctx.Original<Fn>(0x41c7a0)(Object(), &stream) : Object()->Read(&stream));
            }, i % 9, i % 3 == 0 ? 0 : i % 9 + 4);
        }
        parameterCount = 6; localCount = 3; querySuccess = queryOutput = true;
        const unsigned expectedTransfers = 14 + (v > 0) + 6 * (v > 1) + (v > 2) + (v > 3);
        uint32_t failedPositions = 0;
        for (unsigned mode = 0; mode < expectedTransfers + 2; ++mode) {
            failures = mode == 0 ? 0 : mode <= expectedTransfers ? 1u << (mode - 1) : 0xffffffff;
            errorByte = std::array<uint32_t, 4>{0, 1, 0x80, 0xff}[mode % 4];
            Pair(ctx, 1000 + mode, [&](bool original) {
                using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
                return uint32_t(original ? ctx.Original<Fn>(0x41c7a0)(Object(), &stream) : Object()->Read(&stream));
            });
            if (transfers != expectedTransfers) ctx.Fail("version %u fixed-schema transfer count %u/%u", v, transfers, expectedTransfers);
            failedPositions |= failures & ((1u << transfers) - 1);
        }
        if (failedPositions != (1u << expectedTransfers) - 1) ctx.Fail("version %u missing transfer failure coverage", v);
        std::printf("  Read version %08x: 128 variable cases + %u fixed cases, legacy query true %u/false %u; %u/%u failed transfer positions\n",
            v, expectedTransfers + 2, legacyOK, legacyFail, expectedTransfers, expectedTransfers);
    }
    // Strings span empty, SSO, heap, embedded NUL, and aligned heap paths.
    for (size_t i = 0; i < names.size(); ++i) names[i] = std::string(i * 500, char('a' + i));
    names[1] = std::string("ab\0cd", 5); version = 85;
    for (unsigned i = 0; i < 32; ++i) {
        parameterCount = 5; localCount = 4; failures = 0; errorByte = i % 2;
        Pair(ctx, i, [&](bool original) {
            using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
            return uint32_t(original ? ctx.Original<Fn>(0x41c7a0)(Object(), &stream) : Object()->Read(&stream));
        });
    }
}
DIFF_TEST(TreeProperties_InitializeParameters, 0x41cc30)
{
    Setup(false);
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        using Fn = void(__thiscall *)(cTSTreeProperties *);
        if (original) ctx.Original<Fn>(0x41cc30)(Object()); else Object()->InitializeParameters();
        return 0u;
    }, i % 12, i % 2 ? i % 12 : 32);
    std::printf("  Defaults: 128 cases, insertion with/without allocation, empty/nonempty vector\n");
}

DIFF_TEST(TreeProperties_QueryInterface, 0x41c740)
{
    Setup(false); constexpr uint32_t ids[] = {1, 0x1b36c800, 0x1b36c827, 0x456b8f1d, 0, 2, 0x1b36c828, 0xffffffff};
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        struct Output { uint32_t before; void *value; uint32_t after; } out{0x98ab76cd, reinterpret_cast<void *>(0x1234abcd), 0xcdef4321};
        // The original writes AL only; upper EAX contains the IID arithmetic or AddRef result.
        using Fn = uint8_t(__thiscall *)(cTSTreeProperties *, uint32_t, void **);
        uint32_t result = original ? ctx.Original<Fn>(0x41c740)(Object(), ids[i % 8], &out.value) : Object()->QueryInterface(ids[i % 8], &out.value);
        trace.insert(trace.end(), {out.before, uint32_t(reinterpret_cast<uintptr_t>(out.value)), out.after}); return result;
    });
    std::printf("  QueryInterface: 48 primary/16 secondary/64 unsupported IIDs, guarded out pointer and AddRef\n");
}
DIFF_TEST(TreeProperties_Release, 0x41ca70)
{
    Setup(false); constexpr uint32_t counts[] = {0, 1, 2, 3, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < 96; ++i) Pair(ctx, i, [&](bool original) {
        Object()->mRefCount = counts[i % 6];
        using Fn = uint32_t(__thiscall *)(cTSTreeProperties *);
        return original ? ctx.Original<Fn>(0x41ca70)(Object()) : Object()->Release();
    });
    std::printf("  Release: 32 deleting/64 decrement cases, zero/one/two and unsigned extremes\n");
}
DIFF_TEST(TreeProperties_GetGZCLSID, 0x41c3d0)
{
    Setup(false);
    for (unsigned i = 0; i < 32; ++i) Pair(ctx, i, [&](bool original) {
        using Fn = uint32_t(__thiscall *)(cTSTreeProperties *);
        return original ? ctx.Original<Fn>(0x41c3d0)(Object()) : Object()->GetGZCLSID();
    });
}
DIFF_TEST(TreeProperties_GetTree, 0x41c3b0)
{
    Setup(false);
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        using Fn = uint32_t(__thiscall *)(cTSTreeProperties *);
        return original ? ctx.Original<Fn>(0x41c3b0)(Object()) : uint32_t(reinterpret_cast<uintptr_t>(Object()->GetTree()));
    });
}
DIFF_TEST(TreeProperties_GetScopeName, 0x41c570)
{
    Setup(false); void *vt[5]{}; vt[4] = reinterpret_cast<void *>(&ScopeText); Fake sink{vt, 8};
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        int32_t scope = i % 5 - 1;
        using Fn = void(__thiscall *)(cTSTreeProperties *, int32_t, cIGZString *);
        if (original) ctx.Original<Fn>(0x41c570)(Object(), scope, reinterpret_cast<cIGZString *>(&sink));
        else Object()->GetScopeName(scope, reinterpret_cast<cIGZString *>(&sink));
        return 0u;
    });
    std::printf("  Scope name: 26 parameter/26 local/76 fallback cases\n");
}
DIFF_TEST(TreeProperties_GetNameCount, 0x41c0b0)
{
    Setup(false);
    for (unsigned i = 0; i < 128; ++i) {
        parameterCount = i * 0x91837; localCount = ~parameterCount;
        Pair(ctx, i, [&](bool original) {
            using Fn = int32_t(__thiscall *)(cTSTreeProperties *, int32_t);
            return uint32_t(original ? ctx.Original<Fn>(0x41c0b0)(Object(), i % 5 - 1) : Object()->GetNameCount(i % 5 - 1));
        });
    }
    std::printf("  Count: 52 table/76 null-table dispatches\n");
}
DIFF_TEST(TreeProperties_GetParameterFlag, 0x41c5b0)
{
    Setup(false); unsigned negative = 0, inside = 0, outside = 0, zero = 0, nonzero = 0;
    forceZeroFlags = true;
    for (unsigned i = 0; i < 256; ++i) {
        int16_t index = int16_t(i % 20 - 5);
        if (index < 0) ++negative; else if (index < 8) ++inside; else ++outside;
        const uint32_t value = Pair(ctx, i, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(cTSTreeProperties *, int16_t);
            return uint32_t(original ? ctx.Original<Fn>(0x41c5b0)(Object(), index) : Object()->GetParameterFlag(index));
        });
        if (index >= 0 && index < 8) value ? ++nonzero : ++zero;
    }
    std::printf("  Flag getter: negative %u/in-range %u/out-of-range %u, compare AL (upper EAX is unspecified)\n", negative, inside, outside);
    if (!zero || !nonzero) ctx.Fail("missing flag value branch");
    std::printf("  Flag values: zero %u/nonzero %u\n", zero, nonzero);
    forceZeroFlags = false;
}
DIFF_TEST(TreeProperties_SetParameterFlag, 0x41d070)
{
    Setup(false);
    for (unsigned i = 0; i < 256; ++i) Pair(ctx, i, [&](bool original) {
        using Fn = void(__thiscall *)(cTSTreeProperties *, int16_t, uint8_t);
        if (original) ctx.Original<Fn>(0x41d070)(Object(), int16_t(i % 20 - 5), uint8_t(i));
        else Object()->SetParameterFlag(int16_t(i % 20 - 5), uint8_t(i));
        return 0u;
    });
    std::printf("  Flag setter: 65 negative/104 in-range/87 outside cases\n");
}
DIFF_TEST(TreeProperties_ClearFlag23, 0x41d0a0)
{
    Setup(false);
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        using Fn = void(__thiscall *)(cTSTreeProperties *);
        if (original) ctx.Original<Fn>(0x41d0a0)(Object()); else Object()->ClearFlag23(); return 0u;
    });
}
DIFF_TEST(TreeProperties_SetDescription, 0x41cf00)
{
    Setup(false); auto stringVT = std::array<void *, 34>{}; stringVT[17] = reinterpret_cast<void *>(&CopyDescription);
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        Object()->mDescription.mVTable = stringVT.data();
        auto *text = reinterpret_cast<const cIGZString *>(&stream);
        using Fn = void(__thiscall *)(cTSTreeProperties *, const cIGZString *);
        if (original) ctx.Original<Fn>(0x41cf00)(Object(), text); else Object()->SetDescription(text); return 0u;
    });
}
DIFF_TEST(TreeProperties_GetNameTable, 0x41c5e0)
{
    Setup(false);
    for (unsigned i = 0; i < 128; ++i) Pair(ctx, i, [&](bool original) {
        using Fn = void *(__thiscall *)(cTSTreeProperties *, int32_t);
        return uint32_t(reinterpret_cast<uintptr_t>(original ? ctx.Original<Fn>(0x41c5e0)(Object(), i % 5 - 1) : Object()->GetNameTable(i % 5 - 1)));
    });
    std::printf("  Table selection: 26 parameter/26 local/76 null cases\n");
}

DIFF_TEST(TreeProperties_CopyFrom, 0x41bfb0)
{
    Setup(false); querySelf = false;
    unsigned success = 0, failed = 0, self = 0;
    for (unsigned i = 0; i < 128; ++i) {
        std::memset(sourceBytes, 0, sizeof(sourceBytes)); auto *s = Source();
        s->mVTable = sourceVT; s->mLocalNames = &sourceLocal; s->mParameterNames = &sourceParameter;
        s->mDescription.Construct(); s->mDescription.mString.assign(i % 5 == 0 ? 5000 : i % 40, 'a');
        new (&s->mParameterFlags) TreeParameterFlags; s->mParameterFlags.assign(i % 20, uint8_t(i));
        s->mFlag23 = uint8_t(i); s->mField24 = i * 0x391781; s->mField5C = i * 0x718391;
        querySuccess = i % 3 != 0; queryOutput = i % 3 != 0 || i % 2 != 0;
        querySelf = i % 7 == 0;
        if (querySuccess) ++success; else ++failed;
        if (querySelf && querySuccess) ++self;
        Pair(ctx, i, [&](bool original) {
            std::array<uint8_t, sizeof(sourceBytes)> before;
            std::memcpy(before.data(), sourceBytes, before.size());
            const auto beforeFlags = s->mParameterFlags;
            const auto beforeText = s->mDescription.mString;
            using Fn = void(__thiscall *)(cTSTreeProperties *, void *);
            if (original) ctx.Original<Fn>(0x41bfb0)(Object(), &other); else Object()->CopyFrom(&other);
            if (std::memcmp(before.data(), sourceBytes, before.size()) != 0 || beforeFlags != s->mParameterFlags || beforeText != s->mDescription.mString)
                ctx.Fail("CopyFrom changed source %u", i);
            return 0u;
        }, i % 12, i % 12 + 4, i % 16);
        s->mParameterFlags.~TreeParameterFlags(); s->mDescription.Destruct();
    }
    querySelf = false;
    std::printf("  Copy: success %u/failure %u/self-copy %u; SSO/heap/aligned descriptions\n", success, failed, self);
}

DIFF_TEST(TreeProperties_Equals, 0x41c0e0)
{
    Setup(false); querySelf = false;
    unsigned branches[11]{};
    for (unsigned i = 0; i < 220; ++i) {
        const unsigned branch = i % 11; ++branches[branch];
        parameterCount = 3; localCount = 4;
        countDifference = branch == 6 ? 1 : branch == 7 ? 2 : 0;
        nameDifference = branch == 8 ? 1 : branch == 9 ? 2 : 0;
        querySuccess = branch != 0; queryOutput = branch != 0 || i % 2 != 0;
        querySelf = false;
        Pair(ctx, i, [&](bool original) {
            auto *o = Object(); auto *s = Source(); std::memcpy(sourceBytes, work.bytes, 0x60);
            s->mVTable = sourceVT; s->mLocalNames = &sourceLocal; s->mParameterNames = &sourceParameter;
            new (&s->mDescription.mString) std::string(o->mDescription.mString);
            new (&s->mParameterFlags) TreeParameterFlags; s->mParameterFlags.assign(o->mParameterFlags.begin(), o->mParameterFlags.end());
            if (branch == 1) ++s->mFlag23;
            if (branch == 2) ++s->mField24;
            if (branch == 3) s->mDescription.mString += "x";
            if (branch == 4) s->mParameterFlags.push_back(1);
            if (branch == 5) s->mParameterFlags[3] ^= 0x80;
            if (branch == 10) s->mField5C ^= 0xffffffffu; // serialized, but intentionally ignored by equality
            std::array<uint8_t, sizeof(sourceBytes)> before;
            std::memcpy(before.data(), sourceBytes, before.size());
            const auto beforeFlags = s->mParameterFlags;
            const auto beforeText = s->mDescription.mString;
            using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
            const uint32_t result = original ? ctx.Original<Fn>(0x41c0e0)(o, &other) : o->Equals(&other);
            if (std::memcmp(before.data(), sourceBytes, before.size()) != 0 || beforeFlags != s->mParameterFlags || beforeText != s->mDescription.mString)
                ctx.Fail("Equals changed source %u", i);
            s->mParameterFlags.~TreeParameterFlags(); s->mDescription.Destruct(); return result;
        });
    }
    countDifference = nameDifference = 0;
    querySuccess = queryOutput = true; querySelf = true;
    for (unsigned i = 0; i < 32; ++i) Pair(ctx, i, [&](bool original) {
        parameterCount = i % 5; localCount = i % 4;
        using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
        return uint32_t(original ? ctx.Original<Fn>(0x41c0e0)(Object(), &other) : Object()->Equals(&other));
    });
    querySelf = false;
    std::printf("  Equals: 20 cases per each of 10 early-outs, 20 ignored-field5C/32 self-equal cases\n");
}

DIFF_TEST(TreeProperties_GetName, 0x41c3e0)
{
    Setup(false); nameDifference = 0;
    for (unsigned i = 0; i < 128; ++i) {
        names[i % names.size()] = i % 4 == 0 ? "" : std::string(i * 7, 'n');
        inputText.assign(i % 7 == 0 ? 5000 : i % 36, 'b');
        Pair(ctx, i, [&](bool original) {
            using Fn = void(__thiscall *)(cTSTreeProperties *, int32_t, int16_t, cIGZString *);
            if (original) ctx.Original<Fn>(0x41c3e0)(Object(), i % 5 - 1, int16_t(i % 6 - 1), reinterpret_cast<cIGZString *>(&textObject));
            else Object()->GetName(i % 5 - 1, int16_t(i % 6 - 1), reinterpret_cast<cIGZString *>(&textObject));
            return 0u;
        });
    }
    std::printf("  GetName: 52 table/76 null-table cases; empty, SSO, heap and 5000-byte caller text\n");
}
DIFF_TEST(TreeProperties_SetName, 0x41cf20)
{
    Setup(false);
    unsigned missing = 0, appended = 0, grow = 0, retained = 0, formatted = 0;
    for (unsigned i = 0; i < 256; ++i) {
        const int32_t scope = i % 4 - 1;
        const int16_t index = int16_t((i / 4) % 15 - 2);
        const size_t capacity = (i / 60) % 2 ? 8 : 16;
        inputText = (i / 4) % 4 == 0 ? "" : std::string(i % 5 == 0 ? 5000 : i % 40, 's');
        missing += scope != 0 && scope != 1;
        formatted += inputText.empty();
        if (scope == 0 && index == 8) { ++appended; capacity == 8 ? ++grow : ++retained; }
        Pair(ctx, i, [&](bool original) {
            using Fn = void(__thiscall *)(cTSTreeProperties *, int32_t, int16_t, cIGZString *);
            if (original) ctx.Original<Fn>(0x41cf20)(Object(), scope, index, reinterpret_cast<cIGZString *>(&textObject));
            else Object()->SetName(scope, index, reinterpret_cast<cIGZString *>(&textObject));
            return 0u;
        }, 8, capacity);
    }
    if (!appended || !grow || !retained) ctx.Fail("missing append coverage");
    std::printf("  SetName: absent table %u/append %u (grow %u, reuse %u)/formatted %u\n", missing, appended, grow, retained, formatted);
}
DIFF_TEST(TreeProperties_RemoveName, 0x41caa0)
{
    Setup(false);
    unsigned negative = 0, outside = 0, noTable = 0, trailing = 0, interior = 0;
    for (unsigned i = 0; i < 192; ++i) {
        parameterCount = 8; localCount = 5;
        const int32_t scope = i % 4;
        const int16_t index = int16_t((i / 4) % 12 - 2);
        const int32_t count = scope == 0 ? parameterCount : localCount;
        if (index < 0) ++negative;
        else if (index >= count) ++outside;
        else if (scope > 1) ++noTable;
        else if (index == count - 1) ++trailing;
        else ++interior;
        Pair(ctx, i, [&](bool original) {
            using Fn = void(__thiscall *)(cTSTreeProperties *, int32_t, int16_t);
            if (original) ctx.Original<Fn>(0x41caa0)(Object(), scope, index);
            else Object()->RemoveName(scope, index);
            return 0u;
        });
    }
    // Counts are truncated to signed 16 bits for the initial range check.
    for (unsigned i = 0; i < 16; ++i) {
        parameterCount = 0x8000 + i; localCount = 0x10002;
        Pair(ctx, i, [&](bool original) {
            using Fn = void(__thiscall *)(cTSTreeProperties *, int32_t, int16_t);
            if (original) ctx.Original<Fn>(0x41caa0)(Object(), i % 2, 0); else Object()->RemoveName(i % 2, 0);
            return 0u;
        });
    }
    if (!trailing || !interior || !noTable) ctx.Fail("missing RemoveName branch");
    std::printf("  RemoveName: negative %u/range %u/no table %u/trailing %u/interior %u, 16 count truncations\n", negative, outside, noTable, trailing, interior);
}
DIFF_TEST(TreeProperties_LoadFromService, 0x41c6f0)
{
    Setup(false); void **cache = ctx.image.At<void *>(0x10faf6c); void *saved = *cache; *cache = &storage;
    for (unsigned i = 0; i < 96; ++i) {
        resourcePresent = i % 3 != 0; storageResult = i % 3 == 1 ? -98 : -1;
        Pair(ctx, i, [&](bool original) {
            using Fn = void(__thiscall *)(cTSTreeProperties *);
            if (original) ctx.Original<Fn>(0x41c6f0)(Object()); else Object()->LoadFromService(); return 0u;
        });
    }
    *cache = saved;
    std::printf("  Load: 32 absent/32 missing/32 other result cases\n");
}
DIFF_TEST(TreeProperties_SaveToService, 0x41cb90)
{
    Setup(false); void **cache = ctx.image.At<void *>(0x10faf6c); void *saved = *cache; *cache = &storage;
    for (unsigned i = 0; i < 96; ++i) {
        resourcePresent = i % 3 != 0; storageResult = i % 3 == 1 ? 0 : -1;
        Pair(ctx, i, [&](bool original) {
            using Fn = void(__thiscall *)(cTSTreeProperties *);
            if (original) ctx.Original<Fn>(0x41cb90)(Object()); else Object()->SaveToService(); return 0u;
        });
    }
    *cache = saved;
    std::printf("  Save: 32 absent/32 success with full 16-byte key/32 failure cases\n");
}

DIFF_TEST(TreeProperties_ConstructExceptions, 0x41bcd0)
{
    Setup(false); DirectorFixture fixture(ctx); unsigned thrown = 0, completed = 0;
    for (int point = 0; point < 12; ++point) {
        Guarded initial; std::memset(&initial, 0x7a, sizeof(initial));
        Snapshot left, right; bool result[2]{};
        for (unsigned side = 0; side < 2; ++side) {
            work = initial; ResetTrace(); throwAt = point;
            try {
                using Fn = cTSTreeProperties *(__thiscall *)(cTSTreeProperties *, void *, uint16_t);
                if (side == 0) ctx.Original<Fn>(0x41bcd0)(Object(), &owner, 0x8123);
                else Object()->Construct(&owner, 0x8123);
            } catch (int) { result[side] = true; }
            throwAt = -1;
            (side == 0 ? left : right) = Snap(true);
            if (!result[side]) Cleanup();
        }
        ++ctx.cases; result[0] ? ++thrown : ++completed;
        if (result[0] != result[1] || !(left == right)) ctx.Fail("constructor exception at call %d", point);
    }
    std::printf("  Constructor exception injection: %u threw/%u completed, full partial object and unwind trace\n", thrown, completed);
}

DIFF_TEST(TreeProperties_ReadExceptions, 0x41c7a0)
{
    Setup(true); failures = 0; parameterCount = localCount = 3; querySuccess = queryOutput = true; errorByte = 0;
    for (auto &name : names) name.assign(80, 'x');
    for (uint32_t v : {1u, 85u}) {
        version = v; unsigned thrown = 0;
        for (int point = 0; point < 25; ++point) {
            // The original terminates (0xc0000409, observed in an isolated run) if
            // the legacy reference's Release throws. This destructor is noexcept;
            // fatal-process behavior cannot be compared by the in-process harness.
            if (v == 1 && point == 19) continue;
            throwAt = point;
            Pair(ctx, point, [&](bool original) {
                try {
                    using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
                    return uint32_t(original ? ctx.Original<Fn>(0x41c7a0)(Object(), &stream) : Object()->Read(&stream));
                } catch (int) { if (original) ++thrown; return 0x100u; }
            });
        }
        std::printf("  Read exceptions version %u: %u/%u throws (legacy noexcept Release excluded)\n", v, thrown, v == 1 ? 24 : 25);
    }
    throwAt = -1;
}
DIFF_TEST(TreeProperties_WriteExceptions, 0x41d0b0)
{
    Setup(false); failures = 0; parameterCount = localCount = 3; errorByte = 0;
    for (auto &name : names) name.assign(80, 'x');
    unsigned thrown = 0;
    for (int point = 0; point < 25; ++point) {
        throwAt = point;
        Pair(ctx, point, [&](bool original) {
            try {
                using Fn = bool(__thiscall *)(cTSTreeProperties *, void *);
                return uint32_t(original ? ctx.Original<Fn>(0x41d0b0)(Object(), &stream) : Object()->Write(&stream));
            } catch (int) { if (original) ++thrown; return 0x100u; }
        });
    }
    throwAt = -1;
    std::printf("  Write exceptions: %u/25 throws, temporary heap releases observed\n", thrown);
}
DIFF_TEST(TreeProperties_DefaultExceptions, 0x41cc30)
{
    Setup(false); unsigned thrown = 0;
    for (int point = 0; point < 6; ++point) {
        throwAt = point;
        Pair(ctx, point, [&](bool original) {
            try {
                using Fn = void(__thiscall *)(cTSTreeProperties *);
                if (original) ctx.Original<Fn>(0x41cc30)(Object()); else Object()->InitializeParameters();
                return 0u;
            } catch (int) { if (original) ++thrown; return 0x100u; }
        });
    }
    throwAt = -1;
    std::printf("  Default exceptions: %u/6 throws\n", thrown);
}
