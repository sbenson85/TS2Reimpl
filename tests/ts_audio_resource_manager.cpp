// Differential tests for cTSAudioResourceManager (lane IY, src/audio/cTSAudioResourceManager.h): the whole class in the
// standalone shape.
//
// AudioResourceManager_Run: a manager built by the original constructor (0x22b1d9, game vtables 0xdaab54 / 0xdaab8c /
// 0xdaaba0 / 0xdaabb4 / 0xdaabc8 / 0xdaac00) or factory (0x22c83b) against one built by ours (the Construct adapter /
// Create), each driven through its own vtables by the same random operations, then destroyed by the last Release (the
// +0x14 thunk 0x22b6fb, the deleting destructor 0x22b78b, the destructor 0x22b528, the filters' destructor 0x22b50b).
// Operations: Init (0x22cd5a, also through the service face's thunk 0x22ce7e), Shutdown (0x22e6e2, thunk 0x22e784),
// StartPreloading (0x22e78c), ClearInitialized (0x22e7c4), PostSegmentAdded (0x22ba3a), GetSoundDefinition (0x22ca32),
// GetHitList (0x22c8c4), OpenDecoder (0x22bd57), CreateDecoder (0x22c05a), GetPropertySet (0x22bb69), HandleEvent
// (0x22c64e: the memory threshold 0x17f / 0x180 / 0x181 MB, a segment's keys through AddSegmentKeys 0x22b97b, RegisterKeys
// 0x22b7fc, the filters' IsKeyIncluded 0x22df58 and GetKeyName 0x22e7d4), Update (0x22e8a3: every preload state, the passes
// 0x22bb4c / 0x22bcba / 0x22bb2f / 0x22bc1d / 0x22bb12 / 0x22bb70 with the stopwatch at 3990..4010 around the 4000 cap,
// PreloadSoundDefinition 0x22e38c, PreloadHitList 0x22e1ef, PreloadDataSource 0x22df9d, IsPreloadGroup 0x22e6ad), DoMessage
// (0x22c741), QueryInterface (0x22e529) / AddRef (0x22b7cc) / Release (0x22e5ca) on every face (the adjustor thunks
// 0x22e56b / 0x22e573 / 0x22e58b / 0x22e57b / 0x22e583, 0x22b7d4 / 0x22b7dc / 0x22b7f4 / 0x22b7e4 / 0x22b7ec, 0x22e5d2 /
// 0x22e5da / 0x22e5f2 / 0x22e5e2 / 0x22e5ea) and the service accessors.
//
// Every object the manager reaches is a recording fake (tests/ts_services_fixture.h); the callees that are other classes'
// code are stood in for on both sides: the message-id walks (0x1c0458 / 0x1c081e), the stopwatch's Start / Elapsed
// (0x1c982b / 0x1c9256), cRZVersion's constructor / GetMemory / destructor (0x1c98d6 / 0x1c9c01 / 0x1c9901), the log line
// (cRZStd's slot 2, vtable 0xda3e6c: logged with its file, line, channel, level, format and arguments by content) and
// cRZString's Sprintf (slot 31 of 0xd9b470); on the image (original code on both sides): the sound probe 0x9c5b7e,
// BuildResourceMaps 0x22cf55 (a stand-in that fills the key maps, lists, caches, filters, groups and segments the same way
// on both sides), the memory record 0x22b41e, the two decoders 0xb22549 / 0xb22c42, the memory buffer 0x23b4a8 and its fill
// 0x21b2e2. Compared after every operation: the result, the event log, every field (the never-written bytes too: both blocks
// start as 0xc5), the key maps and caches (list order, buckets as list indices, mask, size), the lists, the cursor, the
// groups, the segments, the filters and every fake's count.
//
// AudioKeyFilter_Run: the filter alone (constructor 0x22b17a, IsKeyIncluded 0x22df58, its QueryInterface / AddRef / Release,
// the deleting destructor 0x22b76c), built by the original constructor on the heap against ours.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/ReportFinish.h"
#include "audio/cTSAudioResourceManager.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocate
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersA.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace audiores_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{9, "d"}, {10, "d"}, {11, "d"}, {12, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cTSAudioResourceManager;

constexpr uint32_t kTag = 0x7e000000; // the manager under test

enum : unsigned {
    kService = 0, kComFw, kProbe, kPersist, kRM, kRecord, kNamer, kData, kStream, kDec0, kDecA, kDecB, kDec3, kMsg, kPayload,
    kStd, kEvent, kSegObj, kSegKeys, kRes0, kRes1, kMemRecord, kBuffer, kSegRef,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Call0(void *object, unsigned slot)
{
    return reinterpret_cast<uint32_t(__thiscall *)(void *)>((*static_cast<void ***>(object))[slot])(object);
}

// ---- the world for a seed ----
struct World {
    bool probe;
    bool persist;
    uint32_t memory; // GetMemory's total
};
World gWorld;

// The key pool: the three types the filters take and one they do not, three groups, instances colliding in the eight
// buckets, resources 0..2, duplicates.
constexpr uint32_t kTypes[4] = {0x2026960b, 0x7b1acfcd, 0x0b9eb87e, 0x12345678};
constexpr uint32_t kGroups[3] = {0x7f000001, 0x7f000002, 0x7f000003};
cGZPersistResourceKey gKeys[20];
void MakeKeys(std::mt19937 &rng)
{
    for (unsigned i = 0; i < 20; ++i) {
        gKeys[i] = {kTypes[rng() % 4], kGroups[rng() % 3], 0x100u + (rng() % 6) * 8 + rng() % 2, rng() % 3};
        if (i >= 16) gKeys[i] = gKeys[i - 16];
    }
}
void LogKey(const void *key)
{
    const auto *k = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(k[i]);
}
uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}

void __fastcall StandStart(cRZStopwatch *self, void *)
{
    Ev(0x1c982b);
    self->mStart = 1;
}
uint32_t gElapsed;
uint32_t __fastcall StandElapsed(const cRZStopwatch *, void *)
{
    Ev(0x1c9256);
    return gElapsed++;
}

appbase::cRZVersion *__fastcall StandVersion(appbase::cRZVersion *self, void *)
{
    Ev(0x1c98d6);
    std::memset(self, 0x5a, sizeof(*self));
    return self;
}
uint8_t __fastcall StandMemory(const appbase::cRZVersion *self, void *, uint32_t *total, uint32_t *available)
{
    Ev(0x1c9c01);
    Ev(*reinterpret_cast<const uint32_t *>(self));
    Ev(*total);
    Ev(*available);
    *total = gWorld.memory;
    *available = 0x40;
    return 1;
}
void __fastcall StandVersionEnd(appbase::cRZVersion *self, void *)
{
    Ev(0x1c9901);
    Ev(*reinterpret_cast<const uint32_t *>(self));
}

// The log line: cRZStd's slot 2 (cdecl: the object, channel, level, format, arguments). The arguments by the format: %s
// by content, the rest as words.
void LogText(const char *text)
{
    const size_t n = std::strlen(text);
    Ev(uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
}
void LogFormatted(const char *format, va_list args)
{
    for (const char *f = format; *f; ++f) {
        if (*f != '%') continue;
        ++f;
        unsigned longs = 0;
        for (; *f && std::strchr("-+ #0123456789.lhI", *f); ++f) longs += *f == 'l';
        if (!*f) break;
        if (*f == '%') continue;
        if (*f == 's') {
            LogText(va_arg(args, const char *));
        } else if (longs >= 2) { // %llx: both words of the id
            const uint64_t v = va_arg(args, uint64_t);
            Ev(uint32_t(v));
            Ev(uint32_t(v >> 32));
        } else {
            Ev(va_arg(args, uint32_t));
        }
    }
}
void __cdecl LogLine(cRZStd *self, uint32_t channel, uint32_t level, const char *format, ...)
{
    Ev(0x10600);
    Ev(self->mFirst - P(GameAddress<void>(0)));
    Ev(self->mSecond);
    Ev(channel);
    Ev(level);
    Ev(P(format) - P(GameAddress<void>(0)));
    va_list args;
    va_start(args, format);
    LogFormatted(format, args);
    va_end(args);
}
// cRZString's Sprintf (slot 31): the format and arguments logged, the text formatted into the string.
void __cdecl StandSprintf(cRZString *self, const char *format, ...)
{
    Ev(0x10631);
    Ev(P(format) - P(GameAddress<void>(0)));
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    LogFormatted(format, copy);
    va_end(copy);
    char text[128];
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    self->mString = text;
}
struct VtablePatch {
    void **slot;
    void *saved;
    VtablePatch(uint32_t vtable, unsigned index, void *stand) : slot(GameAddress<void *>(vtable + index * 4))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = stand;
    }
    ~VtablePatch() { *slot = saved; }
    VtablePatch(const VtablePatch &) = delete;
    VtablePatch &operator=(const VtablePatch &) = delete;
};

void *__cdecl StandProbe() { return gWorld.probe ? &F(kProbe) : nullptr; }

// The constructors of the game objects the manager makes: the memory is freed, a fake stands for the object.
void *__fastcall StandMemoryRecord(void *memory, void *, void *buffer)
{
    Ev(0x22b41e);
    Ev(svcfx::Norm(P(buffer)));
    ::operator delete(memory);
    return &F(kMemRecord);
}
void *__fastcall StandDecoderA(void *memory, void *)
{
    Ev(0xb22549);
    ::operator delete(memory);
    return &F(kDecA);
}
void *__fastcall StandDecoderB(void *memory, void *)
{
    Ev(0xb22c42);
    ::operator delete(memory);
    return &F(kDecB);
}
void *__fastcall StandBuffer(void *memory, void *)
{
    Ev(0x23b4a8);
    ::operator delete(memory);
    return &F(kBuffer);
}
void __fastcall StandFill(void *buffer, void *, void *stream)
{
    Ev(0x21b2e2);
    Ev(svcfx::Norm(P(buffer)));
    Ev(svcfx::Norm(P(stream)));
}

// BuildResourceMaps: fills the manager's containers (the same types on both sides, so the same layout) from the op's
// generator: filters, groups, keys into the three maps and their lists, cached resources, segments.
void __fastcall StandBuildMaps(uint8_t *self, void *)
{
    Ev(0x22cf55);
    Ev(svcfx::Norm(P(self)));
    auto &m = *reinterpret_cast<Mgr *>(self);
    cTSAudioKeyFilterByTypesAndGroups *filters[3] = {&m.mSoundDefinitionFilter, &m.mHitListFilter, &m.mDataSourceFilter};
    for (unsigned i = 0; i < 3; ++i) {
        filters[i]->mTypes.insert(kTypes[i]);
        if (Draw(4) == 0) filters[i]->mTypes.insert(kTypes[3]);
        for (uint32_t g : kGroups)
            if (Draw(3)) filters[i]->mGroups.insert(g);
    }
    for (uint32_t g : kGroups)
        if (Draw(2)) m.mPreloadGroups.push_back(g);
    AudioKeyMap *maps[3] = {&m.mSoundDefinitionKeys, &m.mHitListKeys, &m.mDataSourceKeys};
    AudioKeyList *lists[3] = {&m.mSoundDefinitionPreload, &m.mHitListPreload, &m.mDataSourcePreload};
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned n = Draw(12);
        for (unsigned k = 0; k < n; ++k) {
            const cGZPersistResourceKey key = gKeys[Draw(20)];
            const uint64_t id = uint64_t(key.resource) << 32 | key.instance;
            if (maps[i]->try_emplace(id, key).second || Draw(3) == 0) lists[i]->push_back(key);
        }
    }
    AudioCache *caches[3] = {&m.mSoundDefinitions, &m.mHitLists, &m.mDataSources};
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned n = Draw(4);
        for (unsigned k = 0; k < n; ++k) {
            const cGZPersistResourceKey &key = gKeys[Draw(20)];
            const uint64_t id = uint64_t(key.resource) << 32 | key.instance;
            (*caches[i])[id].Assign(reinterpret_cast<cIGZUnknown *>(&F(Draw(2) ? kRes0 : kRes1)));
        }
    }
    if (Draw(2)) m.mSegments[Draw(5)].Assign(reinterpret_cast<cIGZUnknown *>(&F(kSegRef)));
}

// ---- fake handlers ----

// The framework service's slot 5 GetSystemService(class, iid, out).
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x056b906e && a[1] == 0x9af5e924) return gWorld.persist ? HandOut(a[2], F(kPersist)) : 0;
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc) return HandOut(a[2], F(kRM));
    return 0;
}
// The COM framework's slot 3 CreateInstance(class, iid, out): the decoders and the renderer's messages, on a roll.
uint32_t CreateInstance(Obj *, uint32_t *a)
{
    if (Draw(3) == 0) return 0x300u;
    switch (a[0]) {
    case 0x6d4df5bf: return HandOut(a[2], F(kDec0));
    case 0x3cec2b47: return HandOut(a[2], F(kDec3));
    case 0xcbc79579: return HandOut(a[2], F(kMsg));
    default: return 0x301u;
    }
}
// Resource manager slot 3 GetResource(key, iid, out, 0, 0) and slot 34 (key, out record).
uint32_t GetResource(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    if (Draw(4) == 0) return 0x5500u;
    HandOut(a[2], F(Draw(2) ? kRes0 : kRes1));
    return 0x5501u;
}
uint32_t GetRecord(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    if (Draw(4) == 0) return 0x3400u;
    return HandOut(a[1], F(kRecord));
}
// QueryInterface on a roll: the record gives the namer (0x856c8cf7), the data the stream (0xc56b8f08), the resources and
// the buffer themselves; the memory record always the stream (the original uses it unchecked: 0x22be08).
uint32_t Query(Obj *self, uint32_t *a)
{
    if (self == &F(kMemRecord)) return HandOut(a[1], F(kStream));
    if (Draw(5) == 0) return 0x3300u;
    if (self == &F(kRecord)) return a[0] == 0x856c8cf7 ? HandOut(a[1], F(kNamer)) : 0x3302u;
    if (self == &F(kData)) return a[0] == 0xc56b8f08 ? HandOut(a[1], F(kStream)) : 0x3302u;
    if (self == &F(kSegObj)) return a[0] == 0x656b8f0c ? HandOut(a[1], F(kSegKeys)) : a[0] == 1 ? HandOut(a[1], *self) : 0x3302u;
    if (self == &F(kStd)) return a[0] == 0x65297976 ? HandOut(a[1], *self) : 0x3302u;
    HandOut(a[1], *self);
    return 0x3301u;
}
// The record's slot 22 (key, out stream, 1).
uint32_t GetData(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    if (Draw(4) == 0) return 0x2200u;
    return HandOut(a[1], F(kData));
}
// The namer's slot 17 (key, out string): a name from the key (equal for some different keys), into the cRZString.
uint32_t NameKey(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const cGZPersistResourceKey *>(uintptr_t(a[0]));
    LogKey(key);
    auto *out = reinterpret_cast<cRZString *>(uintptr_t(a[1]));
    char text[64];
    std::snprintf(text, sizeof(text), "%s-%u", key->instance % 3 ? "a sound name long enough for the heap" : "short",
                  (key->instance >> 3) % 2 + key->resource * Draw(2));
    out->mString = text;
    return 0x1700u | uint32_t(Draw(4) != 0);
}
// The segment keys' slot 19 (vector out, 0): 0..5 keys of the pool in an allocator-service block.
uint32_t SegmentKeys(Obj *, uint32_t *a)
{
    auto *keys = reinterpret_cast<cGZPersistResourceKey **>(uintptr_t(a[0]));
    const uint32_t n = Draw(6);
    if (!n && Draw(2)) return 0;
    auto *block = static_cast<cGZPersistResourceKey *>(GZServiceAllocate((n + 1) * 16));
    for (uint32_t i = 0; i < n; ++i) block[i] = gKeys[Draw(20)];
    keys[0] = block;
    keys[1] = block + n;
    keys[2] = block + n + 1;
    return 0;
}
uint32_t Roll(Obj *self, uint32_t *) { return 0x100u | uint32_t(Draw(self->value[0] + 2) == 0); }

// ---- building the fakes ----

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11);
    svcfx::ResetAll();
    MakeKeys(rng);
    gWorld.probe = rng() % 6 != 0;
    gWorld.persist = rng() % 6 != 0;

    Obj &service = F(kService);
    service.Set(3, "p", 0x303);
    service.Set(4, "p", 0x304);
    service.Set(5, "pdp", 0, &GetSystemService);
    F(kComFw).Set(3, "ddp", 0, &CreateInstance);
    Obj &probe = F(kProbe);
    for (unsigned s : {7u, 11u}) probe.Set(s, "d----", 0x700 + s);
    for (unsigned s : {8u, 15u, 16u, 31u}) probe.Set(s, "p", 0x700 + s);
    probe.Set(30, "pd", 0x730);
    F(kPersist).Set(3, 0, &F(kRM));
    Obj &rm = F(kRM);
    rm.Set(3, "pdp--", 0, &GetResource);
    rm.Set(20, "dd-", 0x2000);
    rm.Set(21, "d", 0x2100);
    rm.Set(34, "pp", 0, &GetRecord);
    for (unsigned q : {kRecord, kData, kRes0, kRes1, kMemRecord, kBuffer, kSegObj, kStd}) F(q).Set(0, "dp", 0, &Query);
    F(kRecord).Set(22, "ppd", 0, &GetData);
    F(kNamer).Set(17, "pp", 0, &NameKey);
    F(kData).Set(6, 0, 0x606);
    F(kStream).Set(55, 1, 0x5555);
    for (unsigned d : {kDec0, kDecA, kDecB, kDec3}) {
        F(d).Set(3, "p", 0x333);
        F(d).Set(13, 0, 0, &Roll);
        F(d).value[0] = rng() % 3;
    }
    F(kMsg).Set(5, "d", 0x505);
    F(kMsg).Set(6, 0, &F(kPayload));
    F(kPayload).Set(33, "p", 0x3333);
    F(kPayload).Set(32, 0, rng() % 4 ? &F(kSegObj) : nullptr);
    F(kStd).Set(4, 0, 0u);
    F(kStd).Set(32, 0, &F(kSegObj));
    F(kEvent).Set(4, 0, 0u);
    F(kEvent).Set(6, 0, &F(kPayload));
    F(kSegKeys).Set(19, "p-", 0, &SegmentKeys);

    // The cache cells, as the seed has them (the getters fill them during a run: set again for each side).
    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? &F(kService) : nullptr;
    *GameAddress<void *>(0x10f51a8) = rng() % 2 ? &F(kRM) : nullptr;
    svcfx::gFrameworkIndex = kComFw;
}

// ---- driving a manager ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) return ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x22c83b)();
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) cTSAudioResourceManagerAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x22b1d9)(memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

void DumpString(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0) - P(GameAddress<void>(0)));
    const auto *s = reinterpret_cast<const std::string *>(at + 4);
    b.push_back(uint32_t(s->size()));
    b.push_back(uint32_t(s->capacity()));
    for (char c : *s) b.push_back(uint8_t(c));
    b.push_back(Word(at, 0x1c));
}
void DumpKeyList(Blob &b, const AudioKeyList &l)
{
    b.push_back(uint32_t(l.size()));
    for (const auto &k : l) b.push_back(k.type), b.push_back(k.group), b.push_back(k.instance), b.push_back(k.resource);
    for (auto it = l.rbegin(); it != l.rend(); ++it) b.push_back(it->instance);
}
// An unordered map: its words, its list (each node's words from +8, `refAt` normalised), the buckets as list indices.
void DumpHash(Blob &b, const uint8_t *at, unsigned words, int refAt)
{
    b.push_back(Word(at, 0)); // max load factor
    b.push_back(Word(at, 8)); // size
    b.push_back(Word(at, 0x18)); // mask
    b.push_back(Word(at, 0x1c)); // max index
    std::vector<uint32_t> order;
    const uint32_t head = Word(at, 4);
    for (uint32_t n = *reinterpret_cast<const uint32_t *>(uintptr_t(head)); n != head;
         n = *reinterpret_cast<const uint32_t *>(uintptr_t(n))) {
        order.push_back(n);
        const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(n));
        for (unsigned i = 2; i < 2 + words; ++i) b.push_back(int(i) == refAt ? svcfx::Norm(w[i]) : w[i]);
    }
    const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0xc)));
    const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0x10)));
    b.push_back(uint32_t(last - first));
    for (const uint32_t *p = first; p != last; ++p) {
        uint32_t index = 0xffffffff;
        for (uint32_t i = 0; i < order.size(); ++i)
            if (order[i] == *p) index = i;
        if (*p != head && index == 0xffffffff) index = 0xeeeeeeee;
        b.push_back(index);
    }
}
void DumpFilter(Blob &b, const uint8_t *at, bool ours)
{
    const bool game = Word(at, 0) == P(GameAddress<void>(0xdaab3c));
    b.push_back(ours ? !game : game);
    b.push_back(Word(at, 4));
    for (uint32_t off : {8u, 0x10u}) {
        const auto &s = *reinterpret_cast<const std::set<uint32_t> *>(at + off);
        b.push_back(uint32_t(s.size()));
        for (uint32_t v : s) b.push_back(v);
    }
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    const auto &mgr = *static_cast<const Mgr *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[6][2] = {{0, 0xdaab54}, {4, 0xdaab8c}, {8, 0xdaaba0},
                                          {0xc, 0xdaabb4}, {0x10, 0xdaabc8}, {0x14, 0xdaac00}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 0x18; off < 0x28; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? m[0x28] : Word(m, 0x28)); // the running flag; its padding is the allocator's from a factory
    b.push_back(svcfx::Norm(Word(m, 0x2c)));
    b.push_back(gFactory ? Word(m, 0x30) & 0xffff : Word(m, 0x30));
    for (uint32_t id : mgr.mMessageIds) b.push_back(id);
    DumpString(b, m + 0x3c);
    b.push_back(gFactory ? m[0x5c] : Word(m, 0x5c));
    for (uint32_t off : {0x60u, 0x80u, 0xa0u}) DumpHash(b, m + off, 6, -1);
    DumpKeyList(b, mgr.mSoundDefinitionPreload);
    DumpKeyList(b, mgr.mHitListPreload);
    DumpKeyList(b, mgr.mDataSourcePreload);
    b.push_back(mgr.mPreloadState);
    // The cursor: null, or its list and index (the end as the list's size).
    const uint32_t cursor = Word(m, 0xdc);
    uint32_t where = cursor ? 0xdddddddd : 0;
    const AudioKeyList *lists[3] = {&mgr.mSoundDefinitionPreload, &mgr.mHitListPreload, &mgr.mDataSourcePreload};
    for (unsigned i = 0; i < 3 && cursor; ++i) {
        uint32_t index = 0;
        for (auto it = lists[i]->begin();; ++it, ++index) {
            if (P(*reinterpret_cast<void *const *>(&it)) == cursor) {
                where = (i + 1) << 16 | index;
                break;
            }
            if (it == lists[i]->end()) break;
        }
    }
    b.push_back(where);
    b.push_back(uint32_t(mgr.mPreloadGroups.size()));
    for (uint32_t g : mgr.mPreloadGroups) b.push_back(g);
    for (uint32_t off : {0xecu, 0x10cu, 0x12cu}) DumpHash(b, m + off, 3, 4);
    b.push_back(uint32_t(mgr.mSegments.size()));
    for (const auto &s : mgr.mSegments) b.push_back(s.first), b.push_back(svcfx::Norm(P(s.second.Get())));
    for (uint32_t off : {0x154u, 0x16cu, 0x184u}) DumpFilter(b, m + off, ours);
    for (uint32_t off = 0x19c; off < 0x1a8; off += 4) b.push_back(Word(m, off));
    b.push_back(Word(m, 0x1a8) != 0);
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 30;
    if (rng() % 5) ops.push_back({0, rng(), 0});
    if (rng() % 3) ops.push_back({10, 2, rng()}); // build the maps
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 17, rng(), rng()});
    return ops;
}

// An id from the pool (the resource word zero on a roll), or an unknown one.
void PickId(uint32_t a, uint32_t &lo, uint32_t &hi)
{
    const cGZPersistResourceKey &k = gKeys[a % 20];
    lo = (a >> 5) % 9 == 0 ? 0x999u : k.instance;
    hi = (a >> 9) % 3 == 0 ? 0u : (a >> 11) % 7 == 0 ? 7u : k.resource;
}

static const uint32_t kEvents[] = {0xdfd2843, 0x4e078a57, 0xace5f9ea, 0xace5f9f1, 0x1234};
static const uint32_t kMemory[] = {0x17c, 0x17f, 0x180, 0x181, 0x400, 0};
static const uint32_t kIids[] = {1, Mgr::kIid, Mgr::kMessageTargetIid, 0x287fb697, 0x6d92870d, 0x12345678};

// The lookups (slots 8-10) call the persist manager at +0x2c unchecked
// once a key is found (0x22bec2 in OpenDecoder): without one (before Init, after Shutdown, or with no persist service)
// both sides would crash, so the run skips them then.
bool HasPersist(void *object) { return Word(static_cast<const uint8_t *>(object), 0x2c) != 0; }

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const events = Face(object, 4);
    void *const update = Face(object, 8);
    void *const target = Face(object, 0xc);
    void *const service = Face(object, 0x10);
    const uint32_t a = op.arg, c = op.arg2;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    if (op.kind >= 5 && op.kind <= 7 && !HasPersist(object)) return; // see HasPersist
    switch (op.kind) {
    case 0: b.push_back((a % 3 ? Slot<uint32_t>(primary, 3) : Slot<uint32_t>(service, 8)) & 0xff); break;
    case 1: b.push_back((a % 3 ? Slot<uint32_t>(primary, 4) : Slot<uint32_t>(service, 9)) & 0xff); break;
    case 2: b.push_back(Slot<uint32_t>(primary, 5) & 0xff); break;
    case 3: b.push_back(Slot<uint32_t>(primary, 6) & 0xff); break;
    case 4: Slot<uint32_t>(primary, 7, a % 4 ? static_cast<void *>(&F(kSegObj)) : nullptr); break;
    case 5: case 6: { // GetSoundDefinition / GetHitList
        uint32_t lo, hi;
        PickId(a, lo, hi);
        b.push_back(Slot<uint32_t>(primary, op.kind == 5 ? 8 : 9, lo, hi, c % 3 ? 1u : 0x4321u, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    }
    case 7: { // OpenDecoder
        uint32_t lo, hi;
        PickId(a, lo, hi);
        b.push_back(Slot<uint32_t>(primary, 10, lo, hi, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    }
    case 8: // CreateDecoder
        b.push_back(Slot<uint32_t>(primary, 11, static_cast<void *>(&F(kStream)), &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    case 9: b.push_back(Slot<uint32_t>(primary, 12) == Word(static_cast<uint8_t *>(object), 0x1a8)); break;
    case 10: { // HandleEvent
        gWorld.memory = kMemory[c % 6];
        F(kEvent).ret[4] = kEvents[a % 5];
        b.push_back(Slot<uint32_t>(events, 3, (c >> 8) % 11 ? static_cast<void *>(&F(kEvent)) : nullptr) & 0xff);
        break;
    }
    case 11: case 12: { // Update, a few times (the stopwatch around the cap), sometimes from a chosen state
        if (a % 4 == 0) { // a chosen state that sets its own cursor (4, 6 and 8 need one from 3, 5 or 7)
            static const uint32_t kStates[] = {0, 1, 2, 3, 5, 7, 9};
            static_cast<Mgr *>(object)->mPreloadState = kStates[(a >> 2) % 7];
        }
        const unsigned times = 1 + (a >> 6) % 4;
        for (unsigned t = 0; t < times; ++t) {
            const uint32_t r = gRng();
            gElapsed = r % 3 == 0 ? 0 : 3990 + (r >> 2) % 20;
            Slot<uint32_t>(update, 3, c);
        }
        break;
    }
    case 13: { // DoMessage
        static const uint32_t kMessageTypes[] = {0x2df90c05, 0x2df90c05, 0x1234};
        F(kStd).ret[4] = kMessageTypes[a % 3];
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kStd))) & 0xff);
        break;
    }
    case 14: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, events, update, target, service, Face(object, 0x14)};
        void *q = out;
        const uint32_t ok = Slot<uint32_t>(faces[a % 6], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 6], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 6], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 15: // the cIGZSystemService accessors
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<uint32_t>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(Face(object, 0x14), 5));
        break;
    default: // the probe comes and goes
        gWorld.probe = a % 4 != 0;
        break;
    }
}

} // namespace audiores_test

using namespace audiores_test;

DIFF_TEST(AudioResourceManager_Run, 0x22b1d9)
{
    // Also reached (see the file comment): 0x22c83b (factory), 0x22b528, 0x22b78b, 0x22b6fb, 0x22b17a, 0x22b50b, 0x22e529,
    // 0x22b7cc, 0x22e5ca, 0x22cd5a, 0x22ce7e, 0x22e6e2, 0x22e784, 0x22e78c, 0x22e7c4, 0x22ba3a, 0x22ca32, 0x22c8c4,
    // 0x22bd57, 0x22c05a, 0x22bb69, 0x22c64e, 0x22e8a3, 0x22c741, 0x22b97b, 0x22b7fc, 0x22df58, 0x22e7d4, 0x22bb4c,
    // 0x22bcba, 0x22bb2f, 0x22bc1d, 0x22bb12, 0x22bb70, 0x22e38c, 0x22e1ef, 0x22df9d, 0x22e6ad, the adjustor thunks
    // 0x22e56b, 0x22e573, 0x22e58b, 0x22e57b, 0x22e583, 0x22b7d4, 0x22b7dc, 0x22b7f4, 0x22b7e4, 0x22b7ec, 0x22e5d2,
    // 0x22e5da, 0x22e5f2, 0x22e5e2, 0x22e5ea.
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10f5194u, 0x10f51a8u}) globals.SetWord(cell, 0);
    globals.SetWord(0xff5f10, 0x10f5);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch probe(0x9c5b7e, shape::CodeOf(&StandProbe));
    shape::CodePatch buildMaps(0x22cf55, shape::CodeOf(&StandBuildMaps));
    shape::CodePatch memoryRecord(0x22b41e, shape::CodeOf(&StandMemoryRecord));
    shape::CodePatch decoderA(0xb22549, shape::CodeOf(&StandDecoderA));
    shape::CodePatch decoderB(0xb22c42, shape::CodeOf(&StandDecoderB));
    shape::CodePatch buffer(0x23b4a8, shape::CodeOf(&StandBuffer));
    shape::CodePatch fill(0x21b2e2, shape::CodeOf(&StandFill));
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::StandIn start(0x1c982b, &cRZStopwatch::Start, &StandStart);
    shape::StandIn elapsed(0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed);
    shape::StandIn version(0x1c98d6, &appbase::cRZVersion::Construct, &StandVersion);
    shape::StandIn memory(0x1c9c01, &appbase::cRZVersion::GetMemory, &StandMemory);
    shape::StandIn versionEnd(0x1c9901, &appbase::cRZVersion::Destruct, &StandVersionEnd);
    VtablePatch logLine(cRZStd::kVTable, 2, reinterpret_cast<void *>(&LogLine));
    VtablePatch sprintf(cRZString::kVTable, 31, reinterpret_cast<void *>(&StandSprintf));

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioResourceManager_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                return;
            }
        }
    }
}

DIFF_TEST(AudioKeyFilter_Run, 0x22df58)
{
    // Also reached: 0x22b17a (constructor), 0x22b50b (destructor), 0x22b76c (the scalar deleting destructor, slot 4), and
    // the folded slots 0x99b760 (QueryInterface), 0x148fb0 (AddRef), 0x723ae0 (Release).
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            Blob &b = results[side];
            void *filter;
            if (side) {
                filter = new cTSAudioKeyFilterByTypesAndGroups;
            } else {
                filter = ::operator new(0x18);
                std::memset(filter, 0xc5, 0x18);
                ctx.Original<void *(__thiscall *)(void *)>(0x22b17a)(filter);
            }
            auto *sets = reinterpret_cast<std::set<uint32_t> *>(static_cast<uint8_t *>(filter) + 8);
            for (unsigned i = 0; i < 4; ++i) sets[0].insert(rng() % 6), sets[1].insert(10 + rng() % 6);
            b.push_back(static_cast<uint32_t *>(filter)[1]);
            for (unsigned i = 0; i < 40; ++i) {
                const uint32_t r = rng();
                switch (r % 4) {
                case 0: {
                    const uint32_t key[4] = {(r >> 2) % 7, 10 + (r >> 5) % 7, r, 0};
                    b.push_back(Slot<uint32_t>(filter, 3, key) & 0xff);
                    break;
                }
                case 1: {
                    static const uint32_t kAsk[] = {1, 0x8865cd89, 0x8865cd8a, 0, 0x12345678};
                    void *q = reinterpret_cast<void *>(uintptr_t(0x77));
                    b.push_back(Slot<uint32_t>(filter, 0, kAsk[(r >> 2) % 5], &q) & 0xff);
                    b.push_back(q == filter ? 1 : P(q));
                    break;
                }
                default: b.push_back(Slot<uint32_t>(filter, 1)); break;
                }
            }
            // Released to the end: the last Release deletes it (slot 4) and answers 0.
            for (uint32_t n = static_cast<uint32_t *>(filter)[1];; --n) {
                const uint32_t left = Slot<uint32_t>(filter, 2);
                b.push_back(left);
                if (n <= 1) break;
            }
        }
        if (!svcfx::Same(ctx, results[0], results[1], "AudioKeyFilter_Run", seed)) return;
    }
}
