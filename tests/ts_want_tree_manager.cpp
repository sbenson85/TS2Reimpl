// Differential tests for cTSWantTreeManager (lane IV, src/ts/cTSWantTreeManager.h).
//
// WantTreeManager_Run: a manager built by the original constructor (0x551c40, game vtables 0xdd4210 / 0xdd423c / 0xdd425c)
// against one built by ours through the constructor hook's adapter, each driven through its own vtables by the same random
// sequence of operations: Init (0x5545f0), Shutdown (0x5562a0), Reload (0x556510), DoMessage (0x552430) with the reload
// message, a watcher's change message and others, GetDefinition (0x5525a0), QueryInterface / AddRef / Release on each face,
// and finally the last Release (0x556160, the destructor inline). The original side runs the original helpers AddPackage
// (0x496ef0), AddDirectory (0x496c00), WatchDirectories (0x497230) and LoadDefinition (0x555280); ours runs ours (the
// statics the hooks point at). Fakes (tests/ts_services_fixture.h): the resource manager (0x20deb5 patched), the COM
// framework (packed file segments, directory segments with their type table, directory watchers), the message server
// (0x9c5a89 patched; posted messages logged by content), the app singleton (want simulator, object table, persons), the
// definitions. Stood in on both sides: cTSAppCommonImpl slots 4 and 5 (0x1d0c00 / 0x1d0c60: folders and directory lists
// from the case's plan), cRZFile::PathExists (0x1c1242), DirectoryExists (0x1c4f94) and cTSWantTreeLoader::LoadTree
// (0x5550c0, verified by lane IL: the stand logs the key and puts a tree node fake in the manager's tree map). Compared
// after every operation: its result, the event log (every fake call, arguments normalised, texts by content), the
// manager's fields (refcount, both flags and the two padding bytes the constructor leaves, the four lists and two maps by
// content) and every fake's reference count.
//
// WantTreeManager_Factory: the persist factory face. CreateInstanceByType (0x552370) with the definition's constructor
// (0x5b10e0) patched to our adapter on the original side (both sides build our cTSWantDefinition), CreateInstance (the
// factories' shared 0x5522d0 against ours) with slots 4 / 5 of the face replaced by recording fakes in each side's own
// vtable copy, Read (0x555f30) and Write (0x557240) with the temporary property set stood in (the original's constructor
// 0x5519c0 and our NewPropertySet) by a fake set whose serializer and the record, stream and definition are fakes.
//
// WantTreeManager_Forward: slot 8 (LoadTreesFromFile 0x554a50, still the game's body) is forwarded: a recording stub over
// the original's entry is reached once from our vtable with `this` and the argument, and its result returned.
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocate
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSWantDefinition.h"
#include "ts/cTSWantTreeLoader.h"
#include "ts/cTSWantTreeManager.h"

#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace wanttreemgr_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {2, "dd"}, {4, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cTSWantTreeManager;

constexpr uint32_t kTag = 0x7a000000; // the manager under test (its range)
constexpr uint32_t kPattern = 0xc5c5c5c5;

// Fake indices.
enum : unsigned {
    kRes = 0, kFw = 1, kServer = 2, kGlobals = 3, kSim = 4, kObjects = 5, kPerson0 = 6, kPerson1 = 7,
    kPackage0 = 8, kDirSeg0 = 10, kTypes = 12, kOpened0 = 13, kWatcher0 = 15, kDef0 = 19, kNode = 22, kMessage = 23,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

// The case's plan (the same for both sides).
struct Plan {
    std::string folders[3];        // options 0x4dd1e035, 0x0d7eb18b, 0x0d7ebaf1
    uint32_t resourcesNull = 0;    // 1 in N calls of 0x20deb5 answer null (0: never)
    bool objectsNull = false;
    uint32_t guids[3];
    uint32_t simIds[2];
};
Plan gPlan;

void PushText(const char *text)
{
    Ev(0x7e000000u | uint32_t(std::strlen(text)));
    for (const char *c = text; *c; ++c) Ev(uint8_t(*c));
}
const char *TextOf(uint32_t rzString) { return reinterpret_cast<const cRZString *>(uintptr_t(rzString))->mString.c_str(); }

// ---- stand-ins ----

// Null only where the callers test it: the first five calls of an operation (Init's own test and the four folder helpers'; the
// key lists and LoadDefinition use the manager unchecked, as Shutdown's single call is checked).
unsigned gResourceCalls = 0;
void *__cdecl StandResources()
{
    Ev(0x20deb5);
    if (++gResourceCalls <= 5 && gPlan.resourcesNull && Draw(gPlan.resourcesNull) == 0) return nullptr;
    return &F(kRes);
}
void *__cdecl StandServer() { return &F(kServer); }

uint32_t __cdecl StandSlot4(uint32_t option, uint32_t string, uint32_t c, uint32_t d, uint32_t e)
{
    Ev(0x1d0c00);
    Ev(option);
    Ev(c);
    Ev(d);
    Ev(e);
    const unsigned which = option == Mgr::kPackageFolder ? 0 : option == Mgr::kWantsXmlFolder ? 1 : 2;
    reinterpret_cast<cRZString *>(uintptr_t(string))->mString.assign(gPlan.folders[which]);
    return 1;
}

struct DirectoryNode {
    DirectoryNode *mNext;
    DirectoryNode *mPrev;
    std::string mText;
};
uint32_t __cdecl StandSlot5(uint32_t option, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    Ev(0x1d0c60);
    Ev(option);
    Ev(c);
    Ev(d);
    Ev(e);
    auto *header = reinterpret_cast<appdebugger::ListHeader *>(uintptr_t(list));
    auto *sentinel = static_cast<DirectoryNode *>(header->mHead);
    const uint32_t count = Draw(4);
    for (uint32_t i = 0; i < count; ++i) {
        auto *node = static_cast<DirectoryNode *>(appdebugger::AllocateListNodes(1, 0));
        char text[16];
        std::snprintf(text, sizeof(text), "w%u\\%u", unsigned(option & 0xff), unsigned(Draw(100)));
        ::new (&node->mText) std::string(text);
        node->mNext = sentinel;
        node->mPrev = sentinel->mPrev;
        sentinel->mPrev->mNext = node;
        sentinel->mPrev = node;
        ++header->mSize;
    }
    return 1;
}

bool __cdecl StandPathExists(const cIGZString &path)
{
    Ev(0x1c1242);
    PushText(reinterpret_cast<const cRZString &>(path).mString.c_str());
    return Draw(3) != 0;
}

bool __cdecl StandDirectoryExists(cRZString *path)
{
    Ev(0x1c4f94);
    PushText(path->mString.c_str());
    return Draw(3) != 0;
}

void __cdecl StandLoadTree(const void *key, void *manager)
{
    const auto *k = static_cast<const uint32_t *>(key);
    Ev(0x5550c0);
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    Ev(svcfx::Norm(P(manager)));
    auto &trees = *reinterpret_cast<std::map<uint32_t, void *> *>(static_cast<uint8_t *>(manager) + 0x34);
    if (Draw(4) != 0 && trees.find(k[2]) == trees.end()) {
        ++F(kNode).refs;
        trees[k[2]] = &F(kNode);
    }
}

// ---- fake handlers ----

uint32_t HandOut(uint32_t out, unsigned index)
{
    ++F(index).refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &F(index);
    return 1;
}

bool gWatchersFail = false;

// COM framework slot 3: GetClassObject(class, iid, out).
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    unsigned first = 0, count = 0;
    if (a[0] == 0x086d4a89) first = kPackage0, count = 2;
    else if (a[0] == 0xa079ce1b) first = kDirSeg0, count = 2;
    else if (a[0] == 0xe953da68) first = kWatcher0, count = 4;
    if (!count) return 0;
    // A watcher always comes in WantTreeManager_Run: a null one goes into the list too (0x497230), and Shutdown then calls it
    // (0x5564d6, no test). WantTreeManager_Helpers calls WatchDirectories alone and lets it fail.
    if (first == kWatcher0) {
        if (gWatchersFail && Draw(3) == 0) return 0;
        return HandOut(a[2], first + Draw(count));
    }
    const uint32_t roll = Draw(8);
    if (roll < 6) return HandOut(a[2], first + Draw(count));
    if (roll == 6) HandOut(a[2], first + Draw(count)); // fails but hands one out anyway
    return 0;
}

// Resource manager slot 19: the keys of a type into the caller's vector.
uint32_t KeysOfType(Obj *, uint32_t *a)
{
    auto *v = reinterpret_cast<uint32_t **>(uintptr_t(a[0]));
    const uint32_t count = Draw(4);
    if (!count) return 0;
    auto *keys = static_cast<uint32_t *>(GZServiceAllocate(count * 16));
    for (uint32_t i = 0; i < count; ++i) {
        keys[i * 4 + 0] = a[1];
        keys[i * 4 + 1] = Draw(3);
        keys[i * 4 + 2] = Draw(5);
        keys[i * 4 + 3] = 0;
    }
    v[0] = keys;
    v[1] = keys + count * 4;
    v[2] = keys + count * 4;
    return count;
}

// Resource manager slot 3: GetResource(key, iid, out, 0, 0).
uint32_t GetResource(Obj *, uint32_t *a)
{
    if (Draw(5) == 0) return 0;
    return HandOut(a[2], kDef0 + Draw(3));
}

uint32_t LogText0(Obj *, uint32_t *a)
{
    PushText(TextOf(a[0]));
    return 1;
}
uint32_t LogText1(Obj *, uint32_t *a)
{
    PushText(TextOf(a[1]));
    return 1;
}

// Message server slot 4: post(message, flag), the message logged by content.
uint32_t __fastcall PostShim(Obj *self, void *, uint32_t message, uint32_t flag)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(4);
    Ev(flag);
    svcfx::LogMessage(message);
    return 1;
}

void *gPersonList[3];
void *gPersonVector[2] = {gPersonList, gPersonList + 3};

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    std::mt19937 rng(seed);
    const char *folders[] = {"", "d:\\x\\", "data\\w\\", "c:\\abcdefghij\\"};
    for (auto &f : gPlan.folders) f = folders[rng() % 4];
    gPlan.resourcesNull = rng() % 3 == 0 ? 6 : 0;
    gPlan.objectsNull = rng() % 6 == 0;
    for (auto &g : gPlan.guids) g = 1 + rng() % 3;
    gPlan.simIds[0] = 0xabcd0000u | (rng() & 0xff);
    gPlan.simIds[1] = rng() & 0xffff;

    Obj &res = F(kRes);
    res.Set(3, "pdp--", 0, &GetResource);
    res.Set(19, "pd", 0, &KeysOfType);
    res.Set(20, "ddp", 1);
    res.Set(21, "d", 1);
    res.Set(27, "p", 1);
    res.Set(30, "p", 1);
    F(kFw).Set(3, "ddp", 0, &GetClassObject);
    Obj &server = F(kServer);
    server.Set(4, 2, 1);
    server.table[4] = reinterpret_cast<void *>(&PostShim);
    server.Set(5, "pd", 1);
    server.Set(6, "pd", 1);
    F(kGlobals).Set(4, 0, &F(kSim));
    F(kGlobals).Set(26, 0, gPlan.objectsNull ? nullptr : &F(kObjects));
    F(kSim).Set(55, 0);
    F(kSim).Set(35, "d-");
    F(kObjects).Set(46, 0, gPersonVector);
    gPersonList[0] = &F(kPerson0);
    gPersonList[1] = nullptr;
    gPersonList[2] = &F(kPerson1);
    F(kPerson0).Set(57, 0, gPlan.simIds[0]);
    F(kPerson1).Set(57, 0, gPlan.simIds[1]);
    for (unsigned i = 0; i < 2; ++i) {
        Obj &p = F(kPackage0 + i);
        p.Set(3, 0, 1);
        p.Set(4, 0, 1);
        p.Set(12, "p", 1, &LogText0);
        Obj &d = F(kDirSeg0 + i);
        d.Set(15, "dd");
        d.Set(16, "d");
        d.Set(10, "d");
        d.Set(12, 0, &F(kTypes));
        d.Set(4, 0, 1);
        d.Set(9, "p", 1, &LogText0);
        d.Set(6, "dd", 1);
        d.Set(3, 0, &F(kOpened0 + i));
        F(kOpened0 + i).Set(4, 0, 1);
    }
    F(kTypes).Set(3, "dp", 1, &LogText1);
    for (unsigned i = 0; i < 4; ++i) {
        Obj &w = F(kWatcher0 + i);
        w.Set(4, 0, 1);
        w.Set(5, 0, 1);
        w.Set(6, 0, 1);
        w.Set(8, "p");
        w.Set(10, "pd-", 0, &LogText0);
    }
    for (unsigned i = 0; i < 3; ++i) F(kDef0 + i).Set(3, 0, gPlan.guids[i]);
    F(kNode).Set(3, 0);
    F(kMessage).Set(4, 0);
}

// ---- the two sides ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}

uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

void *Build(difftest::Context &ctx, bool ours)
{
    void *memory;
    if (ours) {
        memory = ::operator new(Mgr::kSize);
        std::memset(memory, 0xc5, Mgr::kSize);
        cTSWantTreeManagerAdapters::Construct(memory, nullptr);
    } else {
        memory = ctx.Original<void *(__cdecl *)(uint32_t)>(0x824632)(Mgr::kSize);
        std::memset(memory, 0xc5, Mgr::kSize);
        ctx.Original<void *(__thiscall *)(void *)>(0x551c40)(memory);
    }
    return memory;
}

void DumpList(Blob &b, const uint8_t *at)
{
    const auto *head = *reinterpret_cast<void *const *>(at);
    b.push_back(*reinterpret_cast<const uint32_t *>(at + 4));
    for (auto *n = *static_cast<void *const *>(head); n != head; n = *static_cast<void *const *>(n))
        b.push_back(svcfx::Norm(static_cast<const uint32_t *>(n)[2]));
    // backwards too (the prev links)
    for (auto *n = static_cast<void *const *>(head)[1]; n != head; n = static_cast<void *const *>(n)[1])
        b.push_back(svcfx::Norm(static_cast<const uint32_t *>(n)[2]));
}
void DumpMap(Blob &b, const uint8_t *at)
{
    const auto &m = *reinterpret_cast<const std::map<uint32_t, void *> *>(at);
    b.push_back(uint32_t(m.size()));
    for (const auto &e : m) {
        b.push_back(e.first);
        b.push_back(svcfx::Norm(P(e.second)));
    }
}
void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    // The vtable words: the side's own (the game's on the original side, ours on ours).
    static const uint32_t kGame[3] = {Mgr::kVTable, Mgr::kFactoryVTable, Mgr::kMessageVTable};
    for (int i = 0; i < 3; ++i) {
        const uint32_t word = reinterpret_cast<const uint32_t *>(m)[i];
        b.push_back(ours ? uint32_t(word != P(GameAddress<void>(kGame[i]))) : uint32_t(word == P(GameAddress<void>(kGame[i]))));
    }
    for (uint32_t off = 0xc; off < 0x14; off += 4) b.push_back(*reinterpret_cast<const uint32_t *>(m + off));
    for (uint32_t off : {0x14u, 0x1cu, 0x24u, 0x2cu}) DumpList(b, m + off);
    DumpMap(b, m + 0x34);
    DumpMap(b, m + 0x3c);
}

struct Op {
    unsigned kind;
    uint32_t arg;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 7 + 1);
    std::vector<Op> ops;
    if (rng() % 4) ops.push_back({0, 0});
    const unsigned n = 2 + rng() % 7;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned kind = rng() % 12;
        uint32_t arg = rng();
        if (kind == 5) arg = (arg & 1) ? 0x6ae76406u : (arg & 2) ? 0x0daba8b9u : arg;
        ops.push_back({kind, arg});
    }
    return ops;
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const factory = Face(object, 4);
    void *const target = Face(object, 8);
    static const uint32_t kIids[] = {1, Mgr::kIid, Mgr::kFactoryIid, Mgr::kMessageTargetIid, 0x456b8f1d, 0};
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(primary, 3) & 0xff); break; // Init
    case 1: b.push_back(Slot<uint32_t>(primary, 4) & 0xff); break; // Shutdown
    case 2: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Reload
    case 3:
    case 4:
    case 5: {
        const uint32_t type = op.kind == 3 ? 0x6ae76406u : op.kind == 4 ? 0x0daba8b9u : op.arg;
        F(kMessage).ret[4] = type;
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMessage))) & 0xff);
        break;
    }
    case 6:
        b.push_back(svcfx::Norm(Slot<uint32_t>(primary, 5, uint32_t(op.arg % 4))));
        b.push_back(svcfx::Norm(Slot<uint32_t>(primary, 6)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(primary, 7)));
        break;
    case 7: { // QueryInterface on a face, then the reference dropped through another face
        void *faces[] = {primary, factory, target};
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t iid = kIids[op.arg % 6];
        const uint32_t ok = Slot<uint32_t>(faces[(op.arg >> 8) % 3], 0, iid, &out) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(out)));
        b.push_back(Slot<uint32_t>(faces[(op.arg >> 12) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(op.arg >> 16) % 3], 2));
        if (ok) b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    default: b.push_back(Slot<uint32_t>(primary, 4) & 0xff); b.push_back(Slot<uint32_t>(primary, 3) & 0xff); break;
    }
}

// ---- the persist factory face ----

enum : unsigned { kRecord = 0, kStream = 1, kSet = 2, kSerializer = 3, kResource = 4, kDefinition = 5, kResult = 6 };
constexpr uint32_t kIStreamIid = 0x051b11bd;
constexpr uint32_t kOStreamIid = 0xc51b11cb;
constexpr uint32_t kSerializerIid = 0xebe701f3;
uint32_t gKey[4];

// The game's operator new (0x824632) and sized delete (0x824624) to ours: the original's definitions carry our vtables
// (constructor patched), so our Release frees what the original allocated.
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// A QueryInterface answering iid value[0] with fake value[1]; sometimes it fails, with or without handing one out anyway.
uint32_t AnswerIid(Obj *self, uint32_t *a)
{
    if (a[0] != self->value[0]) return 0;
    const uint32_t roll = Draw(8);
    if (roll < 6) return HandOut(a[1], self->value[1]);
    if (roll == 6) HandOut(a[1], self->value[1]);
    return 0;
}

// The temporary property set: the original's constructor (0x5519c0, on the 0x50 bytes from operator new) and our
// NewPropertySet both hand out the set fake (the original takes its set face at +0x1c of what the constructor returns).
void *__fastcall StandSetConstruct(void *memory, void *)
{
    Ev(0x5519c0);
    ::operator delete(memory); // GameNew's
    return reinterpret_cast<uint8_t *>(&F(kSet)) - 0x1c;
}
cGZPropertySetString *__cdecl StandNewSet()
{
    Ev(0x5519c0);
    return reinterpret_cast<cGZPropertySetString *>(&F(kSet));
}

// CreateInstance's callees, slots 4 and 5 of the factory face, recorded in each side's own vtable copy. The upper bytes of
// the results are noise: the callers test al.
uint32_t __fastcall RecordByType(void *self, void *, uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    Ev(0x552370);
    Ev(svcfx::Norm(P(self)));
    Ev(type);
    Ev(riid);
    Ev(svcfx::Norm(P(out)));
    Ev(a4);
    Ev(a5);
    const uint32_t roll = Draw(4);
    if (roll) HandOut(P(out), kResource);
    return 0xabcd0000u | (roll >= 2 ? 1u : 0u);
}
uint32_t __fastcall RecordRead(void *self, void *, void *resource, void *record)
{
    Ev(0x555f30);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(P(resource)));
    Ev(svcfx::Norm(P(record)));
    return 0xabcd0000u | (Draw(3) != 0 ? 1u : 0u);
}

void BuildFactoryFakes(std::mt19937 &rng, unsigned kind, uint32_t riid)
{
    svcfx::ResetAll();
    static const uint32_t kTypes[] = {Mgr::kDefinitionType, Mgr::kDefinitionType, Mgr::kDefinitionType + 1, Mgr::kTreeType};
    gKey[0] = kTypes[rng() % 4];
    gKey[1] = rng();
    gKey[2] = rng();
    gKey[3] = 0;
    Obj &record = F(kRecord);
    record.Set(0, "dp", 0, &AnswerIid);
    record.value[0] = rng() % 5 == 0 ? 0x12345678u : kind == 3 ? kOStreamIid : kIStreamIid;
    record.value[1] = kStream;
    record.Set(3, 0, gKey);
    Obj &set = F(kSet);
    set.Set(0, "dp", 0, &AnswerIid);
    set.value[0] = rng() % 6 == 0 ? 0x12345678u : kSerializerIid;
    set.value[1] = kSerializer;
    F(kSerializer).Set(3, "pp", 0xabcd0000u | uint32_t(rng() % 4 != 0));
    F(kSerializer).Set(4, "pp-", 0xabcd0000u | uint32_t(rng() % 4 != 0));
    Obj &resource = F(kResource);
    resource.Set(0, "dp", 0, &AnswerIid);
    resource.value[0] = kind == 1 ? (rng() % 4 ? riid : riid + 1) : (rng() % 6 ? Mgr::kDefinitionIid : 0x12345678u);
    resource.value[1] = kind == 1 ? kResult : kDefinition;
    F(kDefinition).Set(20, "p");
    F(kDefinition).Set(21, "p");
}

void RunFactory(Blob &b, void *object, unsigned kind, uint32_t riid, uint32_t type, uint32_t a4, uint32_t a5)
{
    void *const factory = Face(object, 4);
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    switch (kind) {
    case 0: { // CreateInstanceByType: our definition on both sides (the original's constructor call patched to our adapter)
        const uint32_t ok = Slot<uint32_t>(factory, 4, type, riid, &out, a4, a5) & 0xff;
        b.push_back(ok);
        b.push_back(P(out) == 0x5e5e5e5e ? 1u : out ? 2u : 0u);
        if (out && P(out) != 0x5e5e5e5e) {
            // The face handed out and the definition behind it: vtables (ours on both sides), count, key, id.
            // The first eight words of the face, the one at the definition's +0x20 masked to its low half: the
            // constructor's `mov word ptr [esi + 0x20], 0` (0x5b1131) leaves +0x22 / +0x23 as the memory was.
            const auto *words = static_cast<const uint32_t *>(out);
            // index of +0x20 from the persist face (+4) the iid asks for, or none (past the eight from the primary face)
            const uint32_t word20 = riid == cTSWantDefinition::kPersistResourceIid ? 7 : 8;
            for (uint32_t i = 0; i < 8; ++i) b.push_back(i == word20 ? words[i] & 0xffff : words[i]);
            b.push_back(Slot<uint32_t>(out, 2));
        }
        break;
    }
    case 1: { // CreateInstance through the side's own vtable, slots 4 and 5 recorded
        void **&vptr = *reinterpret_cast<void ***>(factory);
        void **const saved = vptr;
        void *copy[7];
        std::memcpy(copy, saved, sizeof(copy));
        copy[4] = reinterpret_cast<void *>(&RecordByType);
        copy[5] = reinterpret_cast<void *>(&RecordRead);
        vptr = copy;
        b.push_back(Slot<uint32_t>(factory, 3, static_cast<void *>(&F(kRecord)), riid, &out, a4, a5) & 0xff);
        vptr = saved;
        b.push_back(svcfx::NormOut(P(out)));
        break;
    }
    case 2: // Read
        b.push_back(Slot<uint32_t>(factory, 5, static_cast<void *>(&F(kResource)), static_cast<void *>(&F(kRecord))) & 0xff);
        break;
    default: // Write
        b.push_back(Slot<uint32_t>(factory, 6, static_cast<void *>(&F(kResource)), static_cast<void *>(&F(kRecord))) & 0xff);
        break;
    }
}

// A recording stub over the original LoadTreesFromFile.
void *gStubSelf;
uint32_t gStubArg;
unsigned gStubHits;
uint32_t __fastcall StubLoadTrees(void *self, void *, uint32_t arg)
{
    gStubSelf = self;
    gStubArg = arg;
    ++gStubHits;
    return 0xc0de0001u;
}

} // namespace wanttreemgr_test

using namespace wanttreemgr_test;

DIFF_TEST(WantTreeManager_Run, 0x5545f0)
{
    // Also reached: 0x551c40 (constructor), 0x5562a0 (Shutdown), 0x556510 (Reload), 0x552430 (DoMessage), 0x5525a0
    // (GetDefinition), 0x555490 (QueryInterface), 0x556160 (Release), 0x496ef0 (AddPackage), 0x496c00 (AddDirectory),
    // 0x497230 (WatchDirectories), 0x555280 (LoadDefinition); through the other faces the QueryInterface / Release
    // adjustor thunks 0x5554fd, 0x555505, 0x556223, 0x55622b; the folded slots 6 / 7 0x49d5c0 (GetTrees), 0x44c520
    // (GetDefinitions).
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    svcfx::gFrameworkIndex = kFw;
    globals.Set(svcfx::kAppCell, &F(kGlobals));
    shape::CodePatch resources(0x20deb5, reinterpret_cast<const void *>(&StandResources));
    shape::StandIn server(0x9c5a89, &GetCachedMessageServer, &StandServer);
    shape::StandIn slot4(0x1d0c00, &appinit::CommonImplSlot4, &StandSlot4);
    shape::StandIn slot5(0x1d0c60, &appinit::CommonImplSlot5, &StandSlot5);
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn dirExists(0x1c4f94, &as2::DirectoryExists, &StandDirectoryExists);
    shape::StandIn loadTree(0x5550c0, &cTSWantTreeLoader::LoadTree, &StandLoadTree);

    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i));
                gResourceCalls = 0;
                if (i < ops.size()) {
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "WantTreeManager_Run", seed)) {
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()),
                         i < ops.size() ? ops[i].kind : 99u);
                return;
            }
        }
    }
}

DIFF_TEST(WantTreeManager_Factory, 0x552370)
{
    // Also reached: 0x5522d0 (CreateInstance, the persist factories' shared body), 0x555f30 (Read), 0x557240 (Write).
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch definition(0x5b10e0, shape::CodeOf(&cTSWantDefinitionAdapters::Construct));
    shape::CodePatch setImage(0x5519c0, shape::CodeOf(&StandSetConstruct));
    shape::CodePatch setOurs(shape::CodeOf(&Mgr::NewPropertySet), shape::CodeOf(&StandNewSet));
    static const uint32_t kRiids[] = {1, cTSWantDefinition::kIid, cTSWantDefinition::kPersistResourceIid, 0x12345678};

    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        ++ctx.cases;
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            const unsigned kind = rng() % 4;
            const uint32_t riid = kRiids[rng() % 4];
            const uint32_t type = rng() % 3 ? Mgr::kDefinitionType : rng() % 2 ? Mgr::kTreeType : 0;
            const uint32_t a4 = rng(), a5 = rng();
            BuildFactoryFakes(rng, kind, riid);
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1);
            Blob &b = results[side];
            gRng.seed(seed * 7919u + 13); // apart from the case's own stream (seeded with the seed)
            RunFactory(b, object, kind, riid, type, a4, a5);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            b.push_back(Slot<uint32_t>(object, 2));
            svcfx::CaptureFakes(b, kValueKinds);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "WantTreeManager_Factory", seed)) return;
    }
}

DIFF_TEST(WantTreeManager_Forward, 0x554a50)
{
    // Slot 8 (LoadTreesFromFile) is still the game's body: our slot must reach the original's entry once, with the manager
    // as `this` and the argument, and return its result.
    shape::CodePatch stub(0x554a50, shape::CodeOf(&StubLoadTrees));
    void *object = Build(ctx, true);
    Slot<uint32_t>(object, 1);
    for (uint32_t arg : {0u, 1u, 0x12345678u}) {
        ++ctx.cases;
        gStubHits = 0;
        gStubSelf = nullptr;
        const uint32_t r = Slot<uint32_t>(object, 8, arg);
        if (gStubHits != 1 || gStubSelf != object || gStubArg != arg || (r & 0xff) != 1)
            ctx.Fail("slot 8: %u hits, this %p (manager %p), argument %x (%x), result %x", gStubHits, gStubSelf, object, gStubArg,
                     arg, r);
    }
    Slot<uint32_t>(object, 2);
}

DIFF_TEST(WantTreeManager_Helpers, 0x497230)
{
    // Also reached: 0x496ef0 (AddPackage). Init (WantTreeManager_Run) ignores AddPackage's result and never lets a watcher
    // fail (a null one crashes the original's Shutdown); the hooked helpers are called here on their own: the original's
    // entry against ours, on a list of our own, with the stand-ins of WantTreeManager_Run.
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    svcfx::gFrameworkIndex = kFw;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch resources(0x20deb5, reinterpret_cast<const void *>(&StandResources));
    shape::StandIn slot4(0x1d0c00, &appinit::CommonImplSlot4, &StandSlot4);
    shape::StandIn slot5(0x1d0c60, &appinit::CommonImplSlot5, &StandSlot5);
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    gWatchersFail = true;
    static const uint32_t kFolders[] = {Mgr::kPackageFolder, Mgr::kWantsXmlFolder, Mgr::kTreesXmlFolder};
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        Blob out[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gRng.seed(seed * 7919u + 13); // apart from the case's own stream (seeded with the seed)
            gResourceCalls = 0;
            std::mt19937 rng(seed * 3 + 1);
            const uint32_t folder = kFolders[rng() % 3];
            Blob &b = out[side];
            {
                WantTreeRefList list;
                if (rng() % 2) {
                    void *const target = rng() % 4 ? static_cast<void *>(&F(kMessage)) : nullptr;
                    WantTreeRefList *const into = rng() % 5 ? &list : nullptr;
                    if (side == 0)
                        ctx.Original<void(__cdecl *)(uint32_t, void *, WantTreeRefList *)>(0x497230)(folder, target, into);
                    else
                        Mgr::WatchDirectories(folder, static_cast<cIGZMessageTarget2 *>(target), into);
                } else {
                    union Name {
                        cRZString s;
                        Name() {}
                        ~Name() {}
                    } name;
                    name.s.Construct(rng() % 2 ? "Wants.package" : "x.package");
                    const bool r = side == 0
                                       ? ctx.Original<bool(__cdecl *)(uint32_t, cRZString *, WantTreeRefList *)>(0x496ef0)(folder, &name.s, &list)
                                       : Mgr::AddPackage(folder, &name.s, &list);
                    b.push_back(r);
                    name.s.Destruct();
                }
                DumpList(b, reinterpret_cast<const uint8_t *>(&list));
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b);
            }
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end()); // the list's releases
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, out[0], out[1], "WantTreeManager_Helpers", seed)) break;
    }
    gWatchersFail = false;
}
