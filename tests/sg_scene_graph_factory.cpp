// Differential tests for nGZSceneGraph::cSceneGraphFactory (src/sg/cSceneGraphFactory.*, lane RC, scenegraph N1 rest): the
// scene graph's persist resource factory and the sixteen class-id statics Init registers with it; and for the O-adapter
// the factory's Write wraps a record's output stream in (src/gz/cOStreamStringBehaviourAdaptor.*).
//
// Functions the script test reaches through the object (in effect through callers): 0xa4d3e0 (destructor body), 0xa4d570
// (deleting destructor), 0xa4d590 / 0xa4d59b / 0xa4d6f5 / 0xa4d96f / 0xa4d99f / 0xa4d8c7 / 0xa4d8f5 / 0xa4dbe3 (adjustor
// thunks), 0xa4d5c0 (CreateResource), 0xa4d700 (QueryInterface), 0xa4d900 (RegisterClass), 0xa4d97a (SetOption14), 0xa4d9b0
// (SetBinaryReader), 0xa4d9df (SetOption18), 0xa4da47 (SetBinaryWriter), 0xa4da90 (Write), 0xa4dbf0 (GetBinaryWriter).
//
//   SG_SceneGraphFactory_Script     the whole object. Each side builds its factory as the singleton getter does (the game's
//       constructor 0xa4d310 with flag 1 in a 0x40 block; ours the compiler's constructor), the lock at +8 swapped for a
//       recording fake, then both run the same random script through the object's own vtables: the primary part (+0:
//       QueryInterface, AddRef / Release, CreateInstance 3 and 4, Read, Write, the slots 8..11), the virtual cIGZUnknown (+0x30,
//       through the vtordisp thunks) and the face (+0x38: the eight cISceneGraphFactory slots). Fakes: the framework
//       (GetServiceObject: slot 5), the resource manager (slot 20), the COM (GetClassObject: reader, writer and every other
//       class), a record (the streams, the reader's context, the key), a resource (the serial), the serial, binary reader
//       and writer pools, the streams and the context; every call logs its arguments. The adapters Read and Write make are
//       driven through the reader's and writer's fakes (their slots 3, 17, 18 reach the stream fake). After every step the
//       answer (the full eax), the call log, the object's words (reference count, options, the four counted fields, the
//       vtordisp words) and every fake's reference count are compared; the end is the deleting destructor (flags 0 or 1)
//       or the last Release, with the blocks freed and the singleton cleared.
//   SG_SceneGraphFactory_Layout     the constructor's bytes: 0x40, the vbtable entries, the vtordisp words, every other word
//       (the table words compared through what they point at: the vbtables' entries).
//   SG_SceneGraphFactory_GetCreate  the getter 0xa4d660 (the singleton, and again after the destructor cleared it) and the
//       creator 0xa4d600 (the virtual cIGZUnknown, relative to the object).
//   SG_ClassIds                     the seventeen `mov eax, imm32; ret` statics.
//   SG_OStreamAdaptor_Script        the O-adapter: Flush (0xa4d5f0), the raw write and GetError (folded 0xa4d650 / 0xa4d640)
//       and the deleting destructor (0xa4d510).
#include "difftest.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "game/vtable.h"

#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIStreamStringBehaviourAdaptor.h"
#include "gz/cOStreamStringBehaviourAdaptor.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "sg/cSceneGraphFactory.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using F = nGZSceneGraph::cSceneGraphFactory;
constexpr uint32_t kCtor = 0xa4d310, kGetter = 0xa4d660, kCreator = 0xa4d600;
constexpr uint32_t kServiceObject = 0x1427b5, kDirectorFramework = 0x1ae18d;
constexpr uint32_t kFaceOffset = 0x38, kUnknownOffset = 0x30;
constexpr uint32_t kReaderClass = 0x5b101e6b, kWriterClass = 0xbb100367;

// ---- the log ----
std::vector<uint32_t> gLog;
void Log(std::initializer_list<uint32_t> words)
{
    difftest::NotCounted bookkeeping;
    gLog.insert(gLog.end(), words);
}

uint8_t *gObject = nullptr;
uint8_t *gAdapter = nullptr;

struct Script {
    uint32_t comWord = 1;       // GetClassObject's answer for the classes that are not the reader or the writer
    bool comGives = true;       // ... and whether it hands out the resource
    bool recordHasStream = true;
    bool resourceHasSerial = true;
    bool contextFound = true;
    uint32_t groupWord = 0;
    uint32_t qiRule = 0;
    uint32_t nextReader = 0, nextWriter = 0;
};
Script gScript;
uint32_t gKey[4];

#define BAD(n, kind) \
    virtual uint32_t Slot##n() \
    { \
        Log({0x1ff, n, kind}); \
        return 0xdead0000u | n; \
    }

struct FakeReader {
    virtual bool QI(uint32_t id, void **) { Log({0x1100, id}); return false; }
    virtual uint32_t AddRef() { Log({0x1101}); return ++refs; }
    virtual uint32_t Release() { Log({0x1102}); return --refs; }
    virtual void ReadRoot(void *serial, void *stream, void *context);
    int refs = 5;
};
struct FakeWriter {
    virtual bool QI(uint32_t id, void **) { Log({0x1200, id}); return false; }
    virtual uint32_t AddRef() { Log({0x1201}); return ++refs; }
    virtual uint32_t Release() { Log({0x1202}); return --refs; }
    virtual void WriteRoot(void *serial, void *stream, void *context);
    int refs = 5;
};
struct FakeStreamI { // cIGZIStream
    virtual bool QI(uint32_t id, void **) { Log({0x100, id}); return false; }
    virtual uint32_t AddRef() { Log({0x101}); return ++refs; }
    virtual uint32_t Release() { Log({0x102}); return --refs; }
    virtual uint32_t Skip(uint32_t n) { Log({0x103, n}); return 0x11; }
    BAD(4, 1) BAD(5, 1) BAD(6, 1) BAD(7, 1) BAD(8, 1) BAD(9, 1) BAD(10, 1) BAD(11, 1) BAD(12, 1) BAD(13, 1) BAD(14, 1)
    BAD(15, 1) BAD(16, 1)
    virtual uint32_t GetVoid(void *dst, uint32_t n)
    {
        Log({0x111, n});
        difftest::NotCounted bookkeeping;
        std::memset(dst, 0x5a, n);
        return 0x7701;
    }
    virtual uint32_t GetError() { Log({0x112}); return 0x33; }
    BAD(19, 1) BAD(20, 1) BAD(21, 1) BAD(22, 1) BAD(23, 1)
    int refs = 5;
};
struct FakeStreamO { // cIGZOStream
    virtual bool QI(uint32_t id, void **) { Log({0x200, id}); return false; }
    virtual uint32_t AddRef() { Log({0x201}); return ++refs; }
    virtual uint32_t Release() { Log({0x202}); return --refs; }
    virtual uint32_t Flush() { Log({0x203}); return 0x21; }
    BAD(4, 2) BAD(5, 2) BAD(6, 2) BAD(7, 2) BAD(8, 2) BAD(9, 2) BAD(10, 2) BAD(11, 2) BAD(12, 2) BAD(13, 2) BAD(14, 2)
    BAD(15, 2) BAD(16, 2)
    virtual uint32_t SetVoid(const void *src, uint32_t n)
    {
        Log({0x211, n, n ? *static_cast<const uint8_t *>(src) : 0u});
        return 0x7702;
    }
    virtual uint32_t GetError() { Log({0x212}); return 0x34; }
    BAD(19, 2) BAD(20, 2) BAD(21, 2) BAD(22, 2) BAD(23, 2)
    int refs = 5;
};
struct FakeSerial {
    virtual bool QI(uint32_t id, void **) { Log({0x300, id}); return false; }
    virtual uint32_t AddRef() { Log({0x301}); return ++refs; }
    virtual uint32_t Release() { Log({0x302}); return --refs; }
    BAD(3, 3) BAD(4, 3) BAD(5, 3) BAD(6, 3) BAD(7, 3) BAD(8, 3) BAD(9, 3) BAD(10, 3) BAD(11, 3) BAD(12, 3) BAD(13, 3)
    BAD(14, 3) BAD(15, 3) BAD(16, 3) BAD(17, 3) BAD(18, 3) BAD(19, 3)
    virtual uint32_t Group() { Log({0x314}); return gScript.groupWord; }
    int refs = 5;
};
struct FakePlain { // the context, and the counted field at +0x1c
    virtual bool QI(uint32_t id, void **) { Log({0x400, id}); return false; }
    virtual uint32_t AddRef() { Log({0x401}); return ++refs; }
    virtual uint32_t Release() { Log({0x402}); return --refs; }
    int refs = 5;
};
struct FakeResMgr {
    virtual bool QI(uint32_t id, void **) { Log({0x500, id}); return false; }
    virtual uint32_t AddRef() { Log({0x501}); return ++refs; }
    virtual uint32_t Release() { Log({0x502}); return --refs; }
    BAD(3, 5) BAD(4, 5) BAD(5, 5) BAD(6, 5) BAD(7, 5) BAD(8, 5) BAD(9, 5) BAD(10, 5) BAD(11, 5) BAD(12, 5) BAD(13, 5)
    BAD(14, 5) BAD(15, 5) BAD(16, 5) BAD(17, 5) BAD(18, 5) BAD(19, 5)
    virtual uint32_t Register(uint32_t type, uint32_t classId, uint32_t third)
    {
        Log({0x514, type, classId, third});
        return 1;
    }
    int refs = 5;
};
struct FakeFramework {
    virtual bool QI(uint32_t id, void **) { Log({0x600, id}); return false; }
    virtual uint32_t AddRef() { Log({0x601}); return ++refs; }
    virtual uint32_t Release() { Log({0x602}); return --refs; }
    BAD(3, 6) BAD(4, 6)
    virtual bool GetSystemService(uint32_t service, uint32_t iid, void **out);
    int refs = 5;
};
struct FakeCom {
    virtual bool QI(uint32_t id, void **) { Log({0x700, id}); return false; }
    virtual uint32_t AddRef() { Log({0x701}); return ++refs; }
    virtual uint32_t Release() { Log({0x702}); return --refs; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **ppv);
    int refs = 5;
};
struct FakeResource { // what the base class's CreateInstance gets back from the COM
    virtual bool QI(uint32_t id, void **out)
    {
        Log({0x800, id});
        if (id == 0x9b11bf70 || id == 0x9b4bbca3) return SerialInto(out);
        const bool yes = ((id ^ gScript.qiRule) & 3) != 0;
        if (yes) *out = reinterpret_cast<void *>(uintptr_t(0x5e000000u | (id & 0xffff)));
        return yes;
    }
    virtual uint32_t AddRef() { Log({0x801}); return ++refs; }
    virtual uint32_t Release() { Log({0x802}); return --refs; }
    BAD(3, 8)
    virtual void SetKey(const uint32_t *key) { Log({0x804, key ? key[0] : 0xffffffffu}); }
    bool SerialInto(void **out);
    int refs = 5;
};
struct FakeRecord {
    virtual bool QI(uint32_t id, void **out);
    virtual uint32_t AddRef() { Log({0x901}); return ++refs; }
    virtual uint32_t Release() { Log({0x902}); return --refs; }
    virtual const uint32_t *GetKey() { Log({0x903}); return gKey; }
    BAD(4, 9) BAD(5, 9) BAD(6, 9) BAD(7, 9) BAD(8, 9) BAD(9, 9) BAD(10, 9) BAD(11, 9) BAD(12, 9) BAD(13, 9) BAD(14, 9)
    virtual bool Context(uint32_t id, void **out);
    int refs = 5;
};
#undef BAD

FakeReader gReaders[4];
FakeWriter gWriters[4];
FakeStreamI gStreamI;
FakeStreamO gStreamO;
FakeSerial gSerial;
FakePlain gContext, gUnused;
FakeResMgr gResMgr;
FakeFramework gFramework;
FakeCom gCom;
FakeResource gResource;
FakeRecord gRecord;

uint32_t Tag(const void *p)
{
    if (p == nullptr) return 0;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (gObject && b >= gObject && b < gObject + 0x40) return 0x2000 + uint32_t(b - gObject);
    if (b == gAdapter) return 0x3001;
    for (int i = 0; i < 4; ++i) {
        if (p == &gReaders[i]) return 0x1100 + i;
        if (p == &gWriters[i]) return 0x1200 + i;
    }
    if (p == &gStreamI) return 0x1301;
    if (p == &gStreamO) return 0x1302;
    if (p == &gSerial) return 0x1303;
    if (p == &gContext) return 0x1304;
    if (p == &gResMgr) return 0x1305;
    if (p == &gFramework) return 0x1306;
    if (p == &gCom) return 0x1307;
    if (p == &gResource) return 0x1308;
    if (p == &gUnused) return 0x1309;
    if (p == &gRecord) return 0x130a;
    const uint32_t v = uint32_t(uintptr_t(p));
    return v >= 0x5e000000u && v < 0x5f000000u ? v : 0x1fff;
}

bool FakeFramework::GetSystemService(uint32_t service, uint32_t iid, void **out)
{
    Log({0x605, service, iid});
    *out = &gResMgr;
    ++gResMgr.refs;
    return true;
}

uint32_t FakeCom::GetClassObject(uint32_t classId, uint32_t iid, void **ppv)
{
    Log({0x703, classId, iid, ppv ? 1u : 0u});
    if (classId == kReaderClass) {
        FakeReader *r = &gReaders[gScript.nextReader++ & 3];
        ++r->refs;
        *ppv = r;
        return 1;
    }
    if (classId == kWriterClass) {
        FakeWriter *w = &gWriters[gScript.nextWriter++ & 3];
        ++w->refs;
        *ppv = w;
        return 1;
    }
    if (gScript.comGives) {
        ++gResource.refs;
        *ppv = &gResource;
    }
    return gScript.comWord;
}

bool FakeResource::SerialInto(void **out)
{
    if (!gScript.resourceHasSerial) return false;
    ++gSerial.refs;
    *out = &gSerial;
    return true;
}

bool FakeRecord::QI(uint32_t id, void **out)
{
    Log({0x900, id});
    if (id == 0x051b11bd && gScript.recordHasStream) {
        ++gStreamI.refs;
        *out = &gStreamI;
        return true;
    }
    if (id == 0xc51b11cb && gScript.recordHasStream) {
        ++gStreamO.refs;
        *out = &gStreamO;
        return true;
    }
    return false;
}

bool FakeRecord::Context(uint32_t id, void **out)
{
    Log({0x90f, id});
    if (!gScript.contextFound) return false;
    ++gContext.refs;
    *out = &gContext;
    return true;
}

template <typename Fn, typename... Args> auto SlotAt(uint32_t offset, int slot, Args... args)
{
    void *self = gObject + offset;
    return VirtualSlot<Fn>(self, slot)(self, args...);
}
using U32 = uint32_t(__thiscall *)(void *);

// The adapter Read and Write made, driven from inside the reader's and the writer's fake: its slots reach the stream fake.
void DriveAdapter(void *adapter, bool input)
{
    gAdapter = static_cast<uint8_t *>(adapter);
    if (!adapter) {
        Log({0x7f});
        return;
    }
    // The adapter's own count (+4): Read and Write AddRef it before handing it on (its Release frees at 1 or 0, so the
    // count is only seen here).
    Log({0x71, Tag(*reinterpret_cast<void **>(gAdapter + 0x10)), uint32_t(gStreamI.refs), uint32_t(gStreamO.refs),
         *reinterpret_cast<uint32_t *>(gAdapter + 4)});
    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    if (input) {
        Log({VirtualSlot<uint32_t(__thiscall *)(void *, void *, uint32_t)>(adapter, 17)(adapter, buf, 3)});
        Log({VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(adapter, 3)(adapter, 2)});
    } else {
        Log({VirtualSlot<U32>(adapter, 3)(adapter)});
        Log({VirtualSlot<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(adapter, 17)(adapter, buf, 2)});
    }
    Log({VirtualSlot<U32>(adapter, 18)(adapter)});
}

void FakeReader::ReadRoot(void *serial, void *stream, void *context)
{
    gAdapter = static_cast<uint8_t *>(stream);
    Log({0x70, Tag(this), Tag(serial), Tag(context), Tag(stream)});
    DriveAdapter(stream, true);
}
void FakeWriter::WriteRoot(void *serial, void *stream, void *context)
{
    gAdapter = static_cast<uint8_t *>(stream);
    Log({0x72, Tag(this), Tag(serial), Tag(context), Tag(stream)});
    DriveAdapter(stream, false);
}

// The lock at +8 (a recording fake in place of cRZCriticalSection's table).
uint32_t __fastcall LockEnter(void *, void *)
{
    Log({0x51});
    return 1;
}
uint32_t __fastcall LockLeave(void *, void *)
{
    Log({0x52});
    return 0;
}
void *gLockVT[3] = {nullptr, reinterpret_cast<void *>(&LockEnter), reinterpret_cast<void *>(&LockLeave)};

void *__cdecl StandService() { return &gFramework; }
void *__cdecl StandCom() { return &gCom; }

void ResetFakes()
{
    difftest::NotCounted bookkeeping;
    gLog.clear();
    gScript = Script{};
    for (auto &r : gReaders) r.refs = 5;
    for (auto &w : gWriters) w.refs = 5;
    gStreamI.refs = gStreamO.refs = gSerial.refs = gContext.refs = gUnused.refs = 5;
    gResMgr.refs = gFramework.refs = gCom.refs = gResource.refs = gRecord.refs = 5;
    gAdapter = nullptr;
    gKey[0] = 0xe5259c1b;
    gKey[1] = gKey[2] = gKey[3] = 0;
}

// Both sides' stand-ins: the framework (GetServiceObject) and the COM (GetDirectorFrameworkObject).
struct Stands {
    shape::StandIn service{kServiceObject, &GetServiceObject, &StandService};
    shape::StandIn com{kDirectorFramework, &GetDirectorFrameworkObject, &StandCom};
};

void LogRefs()
{
    Log({0x41, uint32_t(gStreamI.refs), uint32_t(gStreamO.refs), uint32_t(gSerial.refs), uint32_t(gContext.refs),
         uint32_t(gUnused.refs), uint32_t(gResMgr.refs), uint32_t(gResource.refs)});
    for (int i = 0; i < 4; ++i) Log({uint32_t(gReaders[i].refs), uint32_t(gWriters[i].refs)});
}

void LogObject()
{
    uint32_t w[16];
    std::memcpy(w, gObject, 0x40);
    Log({0x40, w[1], w[5], w[6], Tag(reinterpret_cast<void *>(uintptr_t(w[7]))), Tag(reinterpret_cast<void *>(uintptr_t(w[8]))),
         Tag(reinterpret_cast<void *>(uintptr_t(w[9]))), Tag(reinterpret_cast<void *>(uintptr_t(w[10]))), w[11], w[13],
         ts2_rva_10f61b0 ? 1u : 0u});
    LogRefs();
}

// Builds a factory as the getter does: a 0x40 block, the constructor (the game's with flag 1, ours the compiler's), the
// singleton set, the lock's table replaced.
void Build(bool original, difftest::Context &ctx)
{
    gObject = static_cast<uint8_t *>(::operator new(0x40));
    std::memset(gObject, 0xcd, 0x40);
    if (original) {
        ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtor)(gObject, 1);
    } else {
        new (gObject) F;
    }
    ts2_rva_10f61b0 = reinterpret_cast<F *>(gObject);
    *reinterpret_cast<void **>(gObject + 8) = gLockVT;
}

constexpr uint32_t kFaceIdQ = 0x85259c23;

std::vector<uint32_t> RunSide(bool original, difftest::Context &ctx, uint32_t seed)
{
    ResetFakes();
    ts2_rva_10f61b0 = nullptr;
    Build(original, ctx);
    std::mt19937 rng(seed);
    auto pick = [&](uint32_t n) { return uint32_t(rng() % n); };
    if (pick(2)) { // the counted field at +0x1c nothing sets: the destructor releases it
        *reinterpret_cast<void **>(gObject + 0x1c) = &gUnused;
    }
    Log({0x10, SlotAt<U32>(0, 1)}); // the holder's reference
    LogObject();

    static const uint32_t kClassIds[] = {0xe5259c1b, 0xfc6eb1f7, 0xc9c81b9b, 0xbca76ddc, 0x7ba3838c, 0xed534136};
    const unsigned steps = 10 + pick(50);
    for (unsigned step = 0; step < steps; ++step) {
        const uint32_t op = pick(16);
        Log({0x20, op});
        switch (op) {
        case 0:
        case 1: { // QueryInterface through the primary part (0) or the virtual cIGZUnknown (1)
            static const uint32_t ids[] = {kFaceIdQ, kFaceIdQ, 0xa56b8f17, 1, 0, 0x85259c22, 0x456b8f1d};
            const uint32_t id = pick(4) == 0 ? uint32_t(rng()) : ids[pick(7)];
            void *out = reinterpret_cast<void *>(uintptr_t(0x5eeeeeee));
            void **target = pick(8) == 0 ? nullptr : &out;
            const uint32_t offset = op == 0 ? 0 : kUnknownOffset;
            const uint32_t r = SlotAt<bool(__thiscall *)(void *, uint32_t, void **)>(offset, 0, id, target) & 0xff;
            Log({r, Tag(out)});
            if (target && r && out != reinterpret_cast<void *>(uintptr_t(0x5eeeeeee))) SlotAt<U32>(0, 2);
            break;
        }
        case 2: { // AddRef / Release through the primary part and the virtual cIGZUnknown
            const uint32_t a = SlotAt<U32>(0, 1);
            const uint32_t b = SlotAt<U32>(kUnknownOffset, 1);
            const uint32_t c = SlotAt<U32>(kUnknownOffset, 2);
            const uint32_t d = SlotAt<U32>(0, 2);
            Log({a, b, c, d});
            break;
        }
        case 3: case 4: { // CreateResource (slot 9) and the 5-argument CreateInstance by type (slot 4)
            static const uint32_t words[] = {0, 1, 0x100, 0xff00, 0x1234, 0xffffff00u};
            gScript.comWord = words[pick(6)];
            gScript.comGives = pick(3) != 0;
            gScript.qiRule = rng();
            void *out = reinterpret_cast<void *>(uintptr_t(0x5eeeeeee));
            void **target = pick(6) == 0 ? nullptr : &out;
            const uint32_t type = pick(3) == 0 ? kReaderClass : uint32_t(rng());
            const uint32_t riid = pick(2) ? 0x456b8f1d : uint32_t(rng());
            uint32_t r;
            if (op == 3)
                r = SlotAt<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(0, 9, type, riid, target) & 0xff;
            else
                r = SlotAt<bool(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(0, 4, type, riid, target,
                                                                                                     rng(), rng()) & 0xff;
            Log({r, Tag(out)});
            if (out == &gReaders[0] || out == &gReaders[1] || out == &gReaders[2] || out == &gReaders[3]) {
                // the reader the COM handed out: the caller's reference (the test releases it)
                static_cast<FakeReader *>(out)->Release();
            } else if (out == &gResource) {
                gResource.Release();
            }
            break;
        }
        case 5: { // the base's CreateInstance (slot 3) over a record: CreateResource, Read, the interface
            gScript.comWord = pick(5) ? 1 : 0;
            gScript.comGives = gScript.comWord != 0 || pick(5) != 0; // the original calls the resource unchecked
            gScript.recordHasStream = pick(5) != 0;
            gScript.resourceHasSerial = pick(4) != 0;
            gScript.contextFound = pick(4) != 0;
            gScript.qiRule = rng();
            gKey[0] = 0x100 + pick(50); // not the reader or writer class: the COM hands those out as themselves
            void *out = reinterpret_cast<void *>(uintptr_t(0x5eeeeeee));
            const uint32_t riid = pick(2) ? 0x456b8f1d : uint32_t(rng());
            const bool five = pick(3) == 0;
            const uint32_t r = SlotAt<bool(__thiscall *)(void *, void *, uint32_t, void **, uint32_t, uint32_t)>(
                                   0, 3, &gRecord, riid, &out, five ? 1u : 0u, 0u) & 0xff;
            Log({r, Tag(out)});
            break;
        }
        case 6: { // Read (slot 5)
            gScript.recordHasStream = pick(5) != 0;
            gScript.resourceHasSerial = pick(4) != 0;
            gScript.contextFound = pick(4) != 0;
            gScript.groupWord = rng();
            const uint32_t r = SlotAt<bool(__thiscall *)(void *, void *, void *)>(0, 5, &gResource, &gRecord) & 0xff;
            Log({r});
            break;
        }
        case 7: { // Write (slot 6): needs a writer (the original calls through it unchecked) and a serial
            gScript.recordHasStream = pick(5) != 0;
            gScript.resourceHasSerial = true;
            gScript.groupWord = rng();
            void *writer = reinterpret_cast<void *>(uintptr_t(SlotAt<U32>(kFaceOffset, 7)));
            Log({Tag(writer)});
            if (!writer) break;
            const uint32_t r = SlotAt<bool(__thiscall *)(void *, void *, void *)>(0, 6, &gResource, &gRecord) & 0xff;
            Log({r});
            break;
        }
        case 8: // RegisterClass
            SlotAt<void(__thiscall *)(void *, uint32_t)>(kFaceOffset, 0, pick(2) ? kClassIds[pick(6)] : uint32_t(rng()));
            break;
        case 9: { // SetBinaryReader
            const uint32_t k = pick(6);
            SlotAt<void(__thiscall *)(void *, void *)>(kFaceOffset, 1, k >= 4 ? nullptr : &gReaders[k]);
            break;
        }
        case 10: { // SetBinaryWriter
            const uint32_t k = pick(6);
            SlotAt<void(__thiscall *)(void *, void *)>(kFaceOffset, 2, k >= 4 ? nullptr : &gWriters[k]);
            break;
        }
        case 11: { // SetOption14 / SetOption18
            static const uint32_t values[] = {0, 1, 2, 0x80000000u};
            const uint32_t v = pick(4) == 0 ? uint32_t(rng()) : values[pick(4)];
            if (pick(2))
                SlotAt<void(__thiscall *)(void *, uint32_t)>(kFaceOffset, 3, v);
            else
                SlotAt<void(__thiscall *)(void *, uint32_t)>(kFaceOffset, 4, v);
            break;
        }
        case 12: { // GetResourceFactory / GetBinaryReader / GetBinaryWriter
            const uint32_t which = 5 + pick(3);
            const uint32_t r = SlotAt<U32>(kFaceOffset, int(which));
            Log({which, Tag(reinterpret_cast<void *>(uintptr_t(r)))});
            break;
        }
        case 13: { // the slots shared with other classes: 8 (CreateInstance with 0, 0), 10, 11 (false)
            gScript.comWord = 1;
            gScript.comGives = true; // the original calls the resource unchecked
            gKey[0] = 0x100 + pick(50);
            gScript.recordHasStream = pick(5) != 0;
            gScript.resourceHasSerial = pick(4) != 0;
            gScript.contextFound = true;
            const uint32_t pickSlot = pick(3);
            if (pickSlot == 0) {
                void *out = reinterpret_cast<void *>(uintptr_t(0x5eeeeeee));
                const uint32_t r = SlotAt<bool(__thiscall *)(void *, void *, uint32_t, void **)>(0, 8, &gRecord, 0x456b8f1d, &out) & 0xff;
                Log({r, Tag(out)});
            } else {
                const uint32_t r = SlotAt<bool(__thiscall *)(void *, void *, void *)>(0, pickSlot == 1 ? 10 : 11, &gResource, &gRecord) & 0xff;
                Log({r});
            }
            break;
        }
        default: { // script only: the group word and the QI rule change between calls
            gScript.groupWord = rng();
            gScript.qiRule = rng();
            break;
        }
        }
        LogObject();
    }

    // The end: the deleting destructor (flags 0: the block stays; 1: freed) or the last Release.
    const uint32_t end = pick(3);
    Log({0x30, end});
    difftest::FreeRecorder frees;
    uint8_t *const block = gObject;
    if (end == 0) {
        SlotAt<uint32_t(__thiscall *)(void *, uint32_t)>(0, 7, 0u);
        Log({ts2_rva_10f61b0 ? 1u : 0u});
        frees.Stop();
        Log({0x32, uint32_t(frees.Count()), frees.Freed(block) ? 1u : 0u});
        ::operator delete(block);
    } else if (end == 1) {
        SlotAt<uint32_t(__thiscall *)(void *, uint32_t)>(0, 7, 1u);
        frees.Stop();
        Log({0x33, uint32_t(frees.Count()), frees.Freed(block) ? 1u : 0u});
    } else {
        // the reference taken after construction is the first of the count; release down to the last one
        while (true) {
            uint32_t count;
            std::memcpy(&count, block + 4, 4);
            if (count == 0) break;
            const uint32_t r = SlotAt<U32>(0, 2);
            Log({r});
            if (r == 0) break;
        }
        frees.Stop();
        Log({0x34, uint32_t(frees.Count())});
    }
    Log({ts2_rva_10f61b0 == nullptr ? 0u : 1u});
    gObject = nullptr;
    LogRefs();
    difftest::NotCounted bookkeeping;
    return gLog;
}

} // namespace

DIFF_TEST(SG_SceneGraphFactory_Script, 0xa4d750)
{
    Stands stands;
    for (uint32_t seed = 0; seed < 500; ++seed) {
        ++ctx.cases;
        const std::vector<uint32_t> a = RunSide(true, ctx, seed);
        const std::vector<uint32_t> b = RunSide(false, ctx, seed);
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        if (i == a.size() && i == b.size()) continue;
        ctx.Fail("seed %u: log word %zu of %zu / %zu: original 0x%x, ours 0x%x", seed, i, a.size(), b.size(),
                 i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
        return;
    }
}

// The constructor's bytes: the vtable words are each side's own, so those compare through what they point at: the
// vbtables' entries (the virtual bases' offsets), and every other byte, vtordisp words included, compares as is.
DIFF_TEST(SG_SceneGraphFactory_Layout, kCtor)
{
    static_assert(sizeof(F) == 0x40);
    uint8_t images[2][0x40];
    int32_t vbtables[2][2][3];
    for (int side = 0; side < 2; ++side) {
        gObject = static_cast<uint8_t *>(::operator new(0x40));
        std::memset(gObject, 0xcd, 0x40);
        if (side == 0)
            ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtor)(gObject, 1);
        else
            new (gObject) F;
        std::memcpy(images[side], gObject, 0x40);
        const int32_t *own = *reinterpret_cast<const int32_t *const *>(gObject + 0x10);
        const int32_t *face = *reinterpret_cast<const int32_t *const *>(gObject + 0x3c);
        for (int k = 0; k < 3; ++k) vbtables[side][0][k] = own[k];
        for (int k = 0; k < 2; ++k) vbtables[side][1][k] = face[k];
        // The vtable words (+0, +0x10 and +0x3c are tables, +0x30 and +0x38 the two virtual bases') are non-null.
        for (uint32_t off : {0x0u, 0x10u, 0x30u, 0x38u, 0x3cu}) {
            ++ctx.cases;
            if (*reinterpret_cast<void **>(gObject + off) == nullptr) ctx.Fail("side %d: word +0x%x is null", side, off);
        }
        // Destroy through the constructor-made object's own table without the singleton: flags 0, then free.
        ts2_rva_10f61b0 = nullptr;
        *reinterpret_cast<void **>(gObject + 8) = gLockVT;
        VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(gObject, 7)(gObject, 0u);
        ::operator delete(gObject);
        gObject = nullptr;
    }
    for (int t = 0; t < 2; ++t) {
        for (int k = 0; k < (t == 0 ? 3 : 2); ++k) {
            ++ctx.cases;
            if (vbtables[0][t][k] != vbtables[1][t][k])
                ctx.Fail("vbtable %d entry %d: original %d, ours %d", t, k, vbtables[0][t][k], vbtables[1][t][k]);
        }
    }
    for (uint32_t off = 0; off < 0x40; ++off) {
        if (off == 0 || off == 0x10 || off == 0x30 || off == 0x38 || off == 0x3c) {
            off += 3;
            continue;
        }
        if (off >= 8 && off < 0x10) { // the lock: the block pointer is each side's own
            continue;
        }
        ++ctx.cases;
        if (images[0][off] != images[1][off])
            ctx.Fail("byte +0x%x: original 0x%02x, ours 0x%02x", off, images[0][off], images[1][off]);
    }
}

// The getter, the singleton, the destructor clearing it, the creator.
DIFF_TEST(SG_SceneGraphFactory_GetCreate, kGetter)
{
    for (int round = 0; round < 2; ++round) {
        uint32_t result[2][10];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            ts2_rva_10f61b0 = nullptr;
            ++ctx.cases;
            uint32_t *r = result[side];
            std::memset(r, 0, sizeof(result[side]));
            uint8_t *first, *second;
            if (side == 0) {
                first = reinterpret_cast<uint8_t *>(ctx.Original<F *(__cdecl *)()>(kGetter)());
                second = reinterpret_cast<uint8_t *>(ctx.Original<F *(__cdecl *)()>(kGetter)());
            } else {
                first = reinterpret_cast<uint8_t *>(F::Get());
                second = reinterpret_cast<uint8_t *>(F::Get());
            }
            gObject = first;
            r[0] = first ? 1 : 0;
            r[1] = first == second;
            r[2] = reinterpret_cast<uint8_t *>(ts2_rva_10f61b0) == first;
            uint8_t *unk;
            if (side == 0)
                unk = reinterpret_cast<uint8_t *>(ctx.Original<void *(__cdecl *)()>(kCreator)());
            else
                unk = reinterpret_cast<uint8_t *>(F::Create());
            r[3] = Tag(unk);
            // The first reference count, the face's cIGZUnknown, and the destructor.
            std::memcpy(&r[4], first + 4, 4);
            *reinterpret_cast<void **>(first + 8) = gLockVT;
            difftest::FreeRecorder frees;
            VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(first, 7)(first, 1u);
            frees.Stop();
            r[5] = ts2_rva_10f61b0 == nullptr;
            r[6] = uint32_t(frees.Count());
            r[7] = frees.Freed(first);
            gObject = nullptr;
        }
        for (int k = 0; k < 8; ++k) {
            ++ctx.cases;
            if (result[0][k] != result[1][k]) ctx.Fail("round %d word %d: original 0x%x, ours 0x%x", round, k, result[0][k], result[1][k]);
        }
    }
}

// The seventeen class-id statics.
DIFF_TEST(SG_ClassIds, 0xa4d630)
{
    using nGZSceneGraph::cSceneGraphClassIds;
    struct Row {
        uint32_t rva;
        uint32_t(__cdecl *ours)();
    };
    static const Row rows[] = {
        {0xa4d630, &cSceneGraphClassIds::Factory},          {0xa5d410, &cSceneGraphClassIds::Shape},
        {0xa47d50, &cSceneGraphClassIds::AmbientLight},     {0xa47d60, &cSceneGraphClassIds::DirectionalLight},
        {0xa47da0, &cSceneGraphClassIds::PointLight},       {0xa47db0, &cSceneGraphClassIds::SpotLight},
        {0xa66080, &cSceneGraphClassIds::View},             {0xa425f0, &cSceneGraphClassIds::GeometryNode},
        {0xa80020, &cSceneGraphClassIds::IndexedMeshBuilder}, {0xa839b0, &cSceneGraphClassIds::CompactorBuilder},
        {0xa85110, &cSceneGraphClassIds::BoundingVolumeBuilder}, {0xa86c70, &cSceneGraphClassIds::TangentSpaceBuilder},
        {0xa8f2f0, &cSceneGraphClassIds::OldPart},          {0xa925d0, &cSceneGraphClassIds::Unknown925d0},
        {0xaadf80, &cSceneGraphClassIds::MaterialDefinition}, {0xab1660, &cSceneGraphClassIds::Texture},
        {0xab1670, &cSceneGraphClassIds::LevelOfDetail},
    };
    for (const Row &row : rows) {
        ++ctx.cases;
        const uint32_t original = ctx.Original<uint32_t(__cdecl *)()>(row.rva)();
        const uint32_t ours = row.ours();
        if (original != ours) ctx.Fail("0x%x: original 0x%x, ours 0x%x", row.rva, original, ours);
    }
}

// ---- the O-adapter ----
namespace {

constexpr uint32_t kOBaseCtor = 0x1b4f60;
using A = cOStreamStringBehaviourAdaptor;

struct OInner { // a cIGZOStream: every slot used logs
    virtual bool QI(uint32_t id, void **) { Log({0x1000, id}); return false; }
    virtual uint32_t AddRef() { Log({0x1001}); return ++refs; }
    virtual uint32_t Release() { Log({0x1002}); return --refs; }
    virtual uint32_t Flush() { Log({0x1003}); return answer; }
#define BAD(n) virtual uint32_t Slot##n() { Log({0x10ff, n}); return 0xdead0000u | n; }
    BAD(4) BAD(5) BAD(6) BAD(7) BAD(8) BAD(9) BAD(10) BAD(11) BAD(12) BAD(13) BAD(14) BAD(15) BAD(16)
    virtual uint32_t SetVoid(const void *src, uint32_t n)
    {
        Log({0x1011, n, n ? *static_cast<const uint8_t *>(src) : 0u});
        return answer + 1;
    }
    virtual uint32_t GetError() { Log({0x1012}); return answer + 2; }
    BAD(19) BAD(20) BAD(21) BAD(22) BAD(23)
#undef BAD
    int refs = 3;
    uint32_t answer = 0;
};
OInner gOInner;

std::vector<uint32_t> RunO(bool original, difftest::Context &ctx, uint32_t seed)
{
    {
        difftest::NotCounted bookkeeping;
        gLog.clear();
        gOInner.refs = 3;
    }
    std::mt19937 rng(seed);
    gOInner.answer = rng();
    const bool noInner = (rng() % 5) == 0;
    void *const inner = noInner ? nullptr : &gOInner;
    auto *obj = static_cast<uint8_t *>(::operator new(sizeof(A)));
    std::memset(obj, 0xcd, sizeof(A));
    A *a = reinterpret_cast<A *>(obj);
    const void *own = nullptr;
    if (original) {
        ctx.Original<void *(__thiscall *)(void *)>(kOBaseCtor)(obj);
        *reinterpret_cast<const void **>(obj) = ctx.image.At(A::kVTable);
        a->mInner = inner;
        if (inner) VirtualSlot<U32>(inner, 1)(inner);
        own = ctx.image.At(A::kVTable);
    } else {
        a->Construct(inner);
        Log({a->mVTable == ctx.image.At(A::kVTable) ? 1u : 0u});
        own = A::OwnVTable();
        a->mVTable = own;
    }
    if (original) Log({1u});
    Log({0x40, a->mRefCount, a->mNativeOrder, uint32_t(uintptr_t(a->mOwner)), inner ? 1u : 0u, uint32_t(gOInner.refs)});
    uint8_t src[16];
    for (auto &b : src) b = uint8_t(rng());
    if (!noInner) {
        for (unsigned step = 0; step < 6; ++step) {
            gOInner.answer = rng();
            switch (rng() % 3) {
            case 0: Log({VirtualSlot<U32>(obj, 3)(obj)}); break;
            case 1:
                Log({VirtualSlot<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(obj, 17)(obj, src, rng() % 9)});
                break;
            default: Log({VirtualSlot<U32>(obj, 18)(obj)}); break;
            }
        }
    }
    difftest::FreeRecorder frees;
    const uint32_t flags = rng() % 3;
    if (flags == 2) {
        // the first reference and the release: cRZOStream's Release runs the deleting destructor with flags 1
        VirtualSlot<U32>(obj, 1)(obj);
        const uint32_t r = VirtualSlot<U32>(obj, 2)(obj);
        Log({r});
        frees.Stop();
        Log({0x31, uint32_t(frees.Count()), frees.Freed(obj) ? 1u : 0u, uint32_t(gOInner.refs)});
    } else {
        const uint32_t ret = uint32_t(VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(obj, 21)(obj, flags));
        frees.Stop();
        Log({ret == uint32_t(uintptr_t(obj)) ? 1u : 0u, 0x32, uint32_t(frees.Count()), frees.Freed(obj) ? 1u : 0u,
             uint32_t(gOInner.refs)});
        if (flags == 0) ::operator delete(obj);
    }
    difftest::NotCounted bookkeeping;
    return gLog;
}

} // namespace

DIFF_TEST(SG_OStreamAdaptor_Script, 0xa4d510)
{
    for (uint32_t seed = 0; seed < 300; ++seed) {
        ++ctx.cases;
        const std::vector<uint32_t> a = RunO(true, ctx, seed);
        const std::vector<uint32_t> b = RunO(false, ctx, seed);
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        if (i == a.size() && i == b.size()) continue;
        ctx.Fail("seed %u: log word %zu of %zu / %zu: original 0x%x, ours 0x%x", seed, i, a.size(), b.size(),
                 i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
        return;
    }
}

// The image's table holds, slot by slot, what ours replaces (3, 17, 18, 21); the rest are cRZOStream's.
DIFF_TEST(SG_OStreamAdaptor_VTable, 0xe6c5e4)
{
    static const uint32_t kSlots[][2] = {{3, 0xa4d5f0}, {17, 0xa4d650}, {18, 0xa4d640}, {21, 0xa4d510}};
    void *const *table = static_cast<void *const *>(ctx.image.At(A::kVTable));
    for (const auto &s : kSlots) {
        ++ctx.cases;
        if (table[s[0]] != ctx.image.At(s[1])) ctx.Fail("vtable slot %u is not 0x%x", s[0], s[1]);
    }
}
