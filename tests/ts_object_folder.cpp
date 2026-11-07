// Differential tests for cTSObjectFolder (src/ts/cTSObjectFolder.*), the lot's and the neighbourhood's object folder. Lane IK.
//
// Standalone shape (docs/standalone-shape.md): the original side's folder is built by the original factory 0x4d3280 (game
// vtables) and every slot is called through the game's vtables; ours is built by cTSObjectFolder::Create (the factory hook's
// entry) and called through our vtables, the same face and slot on both. Calls between slots go through the object's own
// vtables, so a run on the original side is the original code end to end and ours is ours end to end.
//
// The world (built from a seed) is a set of fakes, each with one vtable whose slots log their calls (fake id, slot, arguments
// with pointers mapped to ids, the 16-byte keys a slot takes) and answer from the seed: game objects (lot objects, loose
// objects, the lot object, portals) with their data, behaviour, framework parts, link and resource objects; the framework
// objects the director makes (GetDirectorFrameworkObject through the COM director at 0x10c9390); the resource catalogue (cache
// 0x10faf6c) and its persist database, records, packages and segments; the service object 0x10c9378 with its settings and
// options; the cached singletons 0x10cc288 / 0x10cc290; lot scripts; message fakes (incoming messages and their sources, the
// standard message and its server); streams; the tag list. AddRef and Release are logged like every other call.
//
// Stand-ins on the image (both sides reach them: ours calls them through OriginalFunction): the folder's own loaders the lane
// leaves original (0x4d76a0 LoadObject, which here files the object in the folder's containers through the shared layout;
// 0x4d5d00, 0x4cfc20, 0x4d8850, 0x4d3980), the GUID translation reader 0xc0b669, 0xb39360, the directory functions of the
// neighbourhood scan (0xc0b150, 0x1c48da, 0x1c63d4, 0x1c5047, 0x1c509f, 0x1c49eb, 0x1c5031, 0x1c1931, 0x1c5fa0, 0x1c2e25),
// the message server 0x9c5a89 and constructor 0x1c033d. Stand-ins on both sides: the message allocation (the image's RZMalloc
// 0x1be3b0 behind 0x1c03b7, our RZMemory::Malloc), cTSAppCommonImpl slot 4 (0x1d0c00), the message-id walks 0x1c0458 /
// 0x1c081e, cRZVersion's constructor, destructor and GetMemory (0x1c98d6, 0x1c9901, 0x1c9c01).
//
// A run applies one random sequence of operations (face 4 slots 3-57, face 0 slots 3-9, DoMessage on face 8 with every message
// type, QueryInterface on each face, AddRef/Release down to the deleting destructor, and direct changes through the shared
// layout: portals, lot scripts, cached resource ids, package descriptions, the import flags) and compares after every one: the
// result, the call log, the folder's whole state (flags, every container with sizes, capacities, keys and elements in
// iteration order, the current neighbourhood's entries) and the reference count of every fake and name string.
//
// Operations whose preconditions the original does not test are skipped on both sides when the state would make them fault
// (the decision reads the shared state, so a divergence shows in the comparison first): GetSelectedGlobalValue (0x4d3690),
// ReadSaveTable / WriteSaveTable (0x4d46d0 / 0x4d49a0), ClearSaveIndices (0x4d4670) and RemoveObject (0x4d5680) read the
// current neighbourhood's objects without a null test; message 0xc5 looks its name up without an end test (0x4d2a4f /
// 0x4d2a6b); message 0xc4 on the current neighbourhood would leave the folder pointing at a freed entry; an object whose
// portal index (slot 73) is past the portal list is never filed (DetachObjectFully 0x4d2210 indexes the list unchecked).
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "rz/PathFileOps.h"
#include "game_allocator.h"
#include "shape.h"
#include "ts_persist_world.h"

#include "app/AppInit.h"
#include "app/ReportFinish.h"
#include "app/SpecialFolders.h"
#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/DirectoryScan.h"
#include "rz/GeneralAllocator.h"
#include "rz/MiscHelpersB.h"
#include "rz/PathAttributes.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSObjectFolder.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace ik {

using Blob = std::vector<uint32_t>;
using shape::P;
difftest::Context *gCtx = nullptr;

uint32_t TextHash(const char *s)
{
    if (!s) return 0xdead0000u;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h & 0x0fffffffu;
}
uint32_t TextHash(const std::string &s) { return TextHash(s.c_str()); }

constexpr uint32_t kObjd = 0x4f424a44, kObjf = 0x4f424a66, kGlob = 0x474c4f42, kTree = 0x54524545, kFern = 0x4e524546;

// ---- fakes ----

enum Kind {
    kObject, kData, kBehavior, kPart, kLink, kFramework, kScript, kDirFw, kSingletonC, kService, kSettings, kOptions, kCached,
    kCatalog, kPersist, kResource, kRecord, kInMsg, kSource, kOutMsg, kServer, kWStream, kRStream, kVersion, kSrcPkg, kNewPkg,
    kEdit, kSegment, kList, kTag, kKinds
};
struct SlotSpec {
    unsigned slot, words;
    int keyArg; // the argument that points at a 16-byte key (logged), -1 none
};
const std::vector<SlotSpec> kSpecs[kKinds] = {
    /* kObject */ {{1, 0, -1}, {2, 0, -1}, {4, 0, -1}, {5, 1, -1}, {8, 0, -1}, {11, 1, -1}, {18, 0, -1}, {19, 1, -1}, {20, 1, -1},
                   {21, 3, -1}, {23, 1, -1}, {28, 0, -1}, {31, 1, -1}, {37, 0, -1}, {38, 0, -1}, {40, 0, -1}, {41, 1, -1},
                   {42, 0, -1}, {43, 1, -1}, {45, 0, -1}, {55, 1, -1}, {57, 0, -1}, {58, 1, -1}, {62, 1, -1}, {63, 0, -1},
                   {66, 0, -1}, {67, 0, -1}, {68, 2, -1}, {70, 0, -1}, {73, 0, -1}, {74, 0, -1}, {75, 0, -1}, {76, 0, -1},
                   {77, 1, -1}},
    /* kData */ {{3, 1, -1}, {8, 0, -1}, {9, 0, -1}, {11, 2, -1}},
    /* kBehavior */ {{1, 0, -1}, {2, 0, -1}, {3, 2, -1}, {5, 0, -1}, {8, 1, -1}, {11, 1, -1}, {14, 0, -1}, {15, 0, -1}, {16, 0, -1},
                     {28, 0, -1}},
    /* kPart */ {{7, 0, -1}, {8, 1, -1}, {10, 0, -1}},
    /* kLink */ {{10, 0, -1}},
    /* kFramework */ {{1, 0, -1}, {2, 0, -1}, {5, 2, -1}, {7, 0, -1}, {8, 1, -1}, {11, 0, -1}, {15, 1, -1}},
    /* kScript */ {{1, 0, -1}, {2, 0, -1}, {6, 0, -1}, {7, 0, -1}},
    /* kDirFw */ {{3, 3, -1}, {7, 1, -1}},
    /* kSingletonC */ {{58, 1, -1}, {59, 0, -1}, {73, 2, -1}},
    /* kService */ {{5, 3, -1}, {21, 0, -1}},
    /* kSettings */ {{1, 0, -1}, {2, 0, -1}, {9, 2, -1}},
    /* kOptions */ {{9, 1, -1}},
    /* kCached */ {{31, 0, -1}},
    /* kCatalog */ {{3, 0, -1}, {5, 5, 0}, {11, 1, -1}, {16, 2, -1}, {21, 7, -1}, {22, 7, -1}, {26, 1, -1}, {36, 3, -1},
                    {37, 4, 0}, {39, 0, -1}, {43, 1, -1}, {48, 2, -1}, {72, 3, -1}, {73, 4, -1}},
    /* kPersist */ {{5, 5, 0}, {10, 1, -1}, {19, 2, -1}, {30, 1, -1}, {32, 0, -1}, {34, 2, 0}},
    /* kResource */ {{1, 0, -1}, {2, 0, -1}, {3, 1, -1}, {14, 1, -1}},
    /* kRecord */ {{0, 2, -1}, {1, 0, -1}, {2, 0, -1}, {3, 1, -1}, {4, 1, 0}, {6, 2, -1}, {14, 1, -1}, {15, 1, -1}},
    /* kInMsg */ {{0, 2, -1}, {1, 0, -1}, {2, 0, -1}, {4, 0, -1}, {8, 0, -1}, {9, 0, -1}, {29, 2, -1}, {32, 0, -1}},
    /* kSource */ {{0, 2, -1}},
    /* kOutMsg */ {{1, 0, -1}, {2, 0, -1}, {5, 1, -1}, {11, 1, -1}},
    /* kServer */ {{3, 1, -1}},
    /* kWStream */ {{6, 1, -1}},
    /* kRStream */ {{6, 1, -1}, {9, 1, -1}, {20, 0, -1}},
    /* kVersion */ {{78, 0, -1}},
    /* kSrcPkg */ {{0, 2, -1}, {1, 0, -1}, {2, 0, -1}, {4, 0, -1}, {9, 0, -1}, {11, 1, -1}, {19, 2, -1}},
    /* kNewPkg */ {{0, 2, -1}, {1, 0, -1}, {2, 0, -1}, {3, 0, -1}, {11, 2, 0}},
    /* kEdit */ {{1, 0, -1}, {2, 0, -1}, {4, 0, -1}, {9, 1, -1}},
    /* kSegment */ {{1, 0, -1}, {2, 0, -1}, {10, 0, -1}},
    /* kList */ {{8, 1, -1}},
    /* kTag */ {{1, 0, -1}, {2, 0, -1}, {3, 2, -1}},
};
constexpr unsigned kSlots = 80;

struct Fake {
    void **vt;
    uint32_t id;
    Kind kind;
    int32_t refs;
    uint32_t ret[kSlots];
};

// Objects: 0..19 can be filed (LoadObject picks among them), 20..23 loose (20 and 21 without data, behaviour or parts), 24..25
// the lot objects the director makes, 26..27 the portals.
constexpr unsigned kLoadable = 20, kObjects = 28, kNewBehaviors = 6, kRecords = 6, kSrcPkgs = 5, kNewPkgs = 3, kTags = 3;
constexpr unsigned kNames = 6; // neighbourhood names
struct World {
    Fake obj[kObjects], data[kObjects], beh[kObjects], partA[kObjects], partB[kObjects], link[kObjects], res[kObjects];
    Fake newBeh[kNewBehaviors], fw[2], script[4], dirFw, singC, service, settings, options, cached, catalog, persist,
        rec[kRecords], inMsg, source, outMsg, server, wstream, rstream, version, srcPkg[kSrcPkgs], newPkg[kNewPkgs], edit,
        segment, list, tag[kTags];
    // what the world says
    uint32_t dataVal[kObjects][8]; // data indices 9, 0xcb, 10, 0xb, 0x38, 0x3f, 0x5a, 0xca
    uint32_t recVal[kRecords][5];  // record data 9, 10, 0xb, 0x3f, 0xca
    const char *objName[kObjects];
    uint32_t groupPkgs[4]; // catalogue slot 48: bit per source package, by group % 4
    uint32_t versionWord;
    uint32_t nextFw, nextBeh, nextTag, nextLot, nextNew;
    uint32_t owner;
    unsigned zeroId; // the object with id 0 (kObjects: none)
    uint32_t memTotal, memAvail;
    uint32_t skipEvery, importAnswer, readNameMask, dirCount;
    uint32_t sourceTarget; // the incoming message's source: what its QueryInterface gives (index into a small list)
    int32_t msgString;     // the name the incoming message's slot 29 gives (-1 fails)
    bool msgQi, sourceQi;
    uint32_t msgType, msg8, msg9;
    int16_t streamIndex;
    uint32_t streamId;
};
World gW;
std::vector<std::array<uint32_t, 4>> gGlobKeys, gObjdKeys, gPkgKeys[kSrcPkgs];
std::string gSrcPkgName[kSrcPkgs];
void *gVTables[kKinds][kSlots];
union NameString {
    cRZString s;
    NameString() {}
    ~NameString() {}
};
NameString gNames[kNames]; // neighbourhood names (messages 0xc3/0xc4/0xc5), import names and directories
const char *const kNameText[kNames] = {"N001", "n001", "N002", "Downtown", "lot", "dir/lots"};
Blob gLog;
uintptr_t gStackTop;
const void *gFolder; // the side's folder (its base)
uint32_t gEntries[8];
alignas(8) uint8_t gMessageBlock[0x2c];
bool gFailedSlot;

Fake *AllFakes(unsigned &count)
{
    count = unsigned(offsetof(World, dataVal) / sizeof(Fake));
    return reinterpret_cast<Fake *>(&gW);
}

uint32_t Map(uint32_t v)
{
    unsigned count;
    Fake *all = AllFakes(count);
    if (v >= P(&all[0]) && v < P(&all[count])) {
        const uint32_t off = v - P(&all[0]);
        return off % sizeof(Fake) == 0 ? 0xF0000000u | (off / sizeof(Fake)) : 0xF8000000u | off;
    }
    if (gFolder && v >= P(gFolder) && v < P(gFolder) + 0xa4) return 0xB0000000u | (v - P(gFolder));
    for (unsigned i = 0; i < kNames; ++i)
        if (v == P(&gNames[i].s)) return 0x57000000u | i;
    if (v >= P(&gEntries[0]) && v < P(&gEntries[8])) return 0x5E000000u | ((v - P(&gEntries[0])) / 4);
    if (v == P(gMessageBlock)) return 0x5B000000u;
    for (unsigned i = 0; i < kObjects; ++i)
        if (gW.objName[i] && v == P(gW.objName[i])) return 0x5C000000u | i;
    uint32_t here = 0;
    if (v >= P(&here) - 0x10000 && v < gStackTop) return 0x5AC0u;
    return v;
}

template <class T> T *AddRefd(T *p)
{
    if (p) ++p->refs;
    return p;
}
void Give(uint32_t outAddress, Fake *f)
{
    *reinterpret_cast<Fake **>(uintptr_t(outAddress)) = AddRefd(f);
}
cRZString *Str(uint32_t a) { return reinterpret_cast<cRZString *>(uintptr_t(a)); }
void LogText(const std::string &s) { gLog.push_back(TextHash(s)); }

using KeyVector = std::vector<std::array<uint32_t, 4>, GZServiceAllocator<std::array<uint32_t, 4>>>;

unsigned IndexIn(const Fake *base, unsigned n, const void *p)
{
    for (unsigned i = 0; i < n; ++i)
        if (p == &base[i]) return i;
    return ~0u;
}

uint32_t DataValue(const uint32_t *values, uint32_t index, uint32_t fallback)
{
    static const uint32_t kIndices[8] = {9, 0xcb, 10, 0xb, 0x38, 0x3f, 0x5a, 0xca};
    for (unsigned i = 0; i < 8; ++i)
        if (kIndices[i] == index) return values[i];
    return fallback + index;
}

uint32_t Handle(Fake *self, unsigned slot, const uint32_t *a, unsigned n)
{
    gLog.insert(gLog.end(), {0xCA11u, self->id, slot});
    int keyArg = -1;
    for (const SlotSpec &s : kSpecs[self->kind])
        if (s.slot == slot) keyArg = s.keyArg;
    for (unsigned i = 0; i < n; ++i) {
        // The heap blocks some slots take (key filters) are logged by presence: their addresses differ between the sides.
        if (self->kind == kSrcPkg && slot == 19 && i == 1) {
            gLog.push_back(a[i] != 0);
            continue;
        }
        // A key in a key vector (a heap block) is logged by its words only.
        gLog.push_back(int(i) == keyArg && Map(a[i]) == a[i] ? 0x4EA90000u : Map(a[i]));
        if (int(i) == keyArg) {
            const uint32_t *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[i]));
            gLog.insert(gLog.end(), {key[0], key[1], key[2], key[3]});
        }
    }
    const uint32_t r = self->ret[slot];
    if (slot == 1) {
        for (const SlotSpec &s : kSpecs[self->kind])
            if (s.slot == 1) return uint32_t(++self->refs);
    }
    if (slot == 2) {
        for (const SlotSpec &s : kSpecs[self->kind])
            if (s.slot == 2) {
                if (--self->refs < 0) gLog.push_back(0xBAD0000u | self->id);
                return uint32_t(self->refs);
            }
    }
    switch (self->kind) {
    case kObject: {
        const unsigned i = IndexIn(gW.obj, kObjects, self);
        if (slot == 8) return P(gW.objName[i]);
        if (slot == 11) { // contains(object)
            const unsigned j = IndexIn(gW.obj, kObjects, reinterpret_cast<void *>(uintptr_t(a[0])));
            return ((i * 7 + j * 3 + r) % 3 == 0) ? 0x101u : 0x100u;
        }
        return r;
    }
    case kData: {
        const unsigned i = IndexIn(gW.data, kObjects, self);
        if (slot == 3) return DataValue(gW.dataVal[i], a[0], r);
        if (slot == 11) return 0xab0000u | ((a[0] + a[1] + i) % 3);
        return r;
    }
    case kRecord: {
        const unsigned i = IndexIn(gW.rec, kRecords, self);
        if (slot == 0) { // QueryInterface: the tree (0xeda4decd) by the world, the object and persistent faces: itself
            const bool ok = a[0] == 0xeda4decdu ? (r & 1) != 0 : a[0] == 0x456b8f1du ? r != 0 : true;
            if (ok) Give(a[1], self);
            return ok ? 1u : 0x100u;
        }
        if (slot == 3) {
            const uint32_t values[8] = {gW.recVal[i][0], 0, gW.recVal[i][1], gW.recVal[i][2], 0, gW.recVal[i][3], 0, gW.recVal[i][4]};
            return DataValue(values, a[0], r);
        }
        if (slot == 14) {
            static const char *const kTexts[] = {"lot", "Chair", "lot - 0, 1", "lot - Lead"};
            Str(a[0])->mString = kTexts[(i + r) % 4];
        }
        if (slot == 15) LogText(Str(a[0])->mString);
        return r;
    }
    case kDirFw:
        if (slot == 3) {
            Fake *made = nullptr;
            switch (a[0]) {
            case 0x0b54e37d: made = &gW.fw[gW.nextFw++ % 2]; break;
            case 0x5b12bf90: made = &gW.obj[24 + gW.nextLot++ % 2]; break;
            case 0x1b269450: made = &gW.newBeh[gW.nextBeh++ % kNewBehaviors]; break;
            case 0x5a69d325: made = &gW.tag[gW.nextTag++ % kTags]; break;
            default: break;
            }
            Give(a[2], made);
            return made != nullptr;
        }
        *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gW.owner;
        return r;
    case kSingletonC:
        if (slot == 58) LogText(Str(a[0])->mString);
        return r;
    case kService:
        if (slot == 5) {
            Give(a[2], &gW.settings); // the settings service is always there (Init uses it untested, 0x4d3d97)
            return 1;
        }
        return P(&gW.options);
    case kSettings: {
        const auto *name = reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]));
        gLog.push_back(TextHash(name->c_str()));
        const uint32_t answer = (r >> (TextHash(name->c_str()) % 8)) & 3;
        if ((answer & 1) || (r & 0x10000)) reinterpret_cast<uint8_t *>(uintptr_t(a[1]))[4] = uint8_t(answer >> 1 | 1); // written even when not found, sometimes
        return (answer & 1) | 0x200u;
    }
    case kOptions:
        LogText(Str(a[0])->mString);
        return r;
    case kCatalog:
        switch (slot) {
        case 3: return P(&gW.persist);
        case 5: { // (key, iid, &out, 0, 0): an object's resource
            const uint32_t *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
            if (key[2] % 5 == 3) return 0x100;
            Give(a[2], &gW.res[(key[1] * 3 + key[2]) % kObjects]);
            return 1;
        }
        case 11: return TextHash(Str(a[0])->mString) % 7;
        case 21: { // (type, &package, 1, path, name, 1, 1)
            gLog.push_back(TextHash(reinterpret_cast<const char *>(uintptr_t(a[3]))));
            gLog.push_back(TextHash(reinterpret_cast<const char *>(uintptr_t(a[4]))));
            Give(a[1], &gW.newPkg[gW.nextNew++ % kNewPkgs]);
            return 1;
        }
        case 22: { // (type, &package, arg, path, name, 1, 1)
            LogText(Str(a[3])->mString);
            LogText(Str(a[4])->mString);
            if ((r >> (gW.nextNew % 8)) % 7 == 0) {
                ++gW.nextNew;
                return 0x100;
            }
            Give(a[1], &gW.newPkg[gW.nextNew++ % kNewPkgs]);
            return 1;
        }
        case 36: { // (&keys, group, type)
            auto *keys = reinterpret_cast<KeyVector *>(uintptr_t(a[0]));
            for (const auto &key : gObjdKeys)
                if (key[1] % 4 == a[1] % 4) keys->push_back(key);
            return uint32_t(keys->size());
        }
        case 48: { // (group, &vector): the group's packages, each referenced
            auto *v = reinterpret_cast<ServiceRefVector *>(uintptr_t(a[1]));
            const uint32_t mask = gW.groupPkgs[a[0] % 4];
            for (unsigned i = 0; i < kSrcPkgs; ++i)
                if (mask & (1u << i)) v->PushBack(&gW.srcPkg[i]);
            return 0;
        }
        case 72:
        case 73: { // the save table: finder, lot scripts, translation (heap: presence), flag
            const uint32_t *table = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
            gLog.insert(gLog.end(), {Map(table[1]), Map(table[2]), table[3] != 0, table[4]});
            return r;
        }
        default: return r;
        }
    case kPersist:
        switch (slot) {
        case 5: { // (key, iid, &out, 0, 0): a record
            const uint32_t *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
            const uint32_t pick = (key[0] + key[1] * 5 + key[2] * 3 + r) % (kRecords + 2);
            if (pick >= kRecords) return 0x100;
            Give(a[2], &gW.rec[pick]);
            return 1;
        }
        case 19: { // (&keys, type)
            auto *keys = reinterpret_cast<KeyVector *>(uintptr_t(a[0]));
            for (const auto &key : a[1] == kGlob ? gGlobKeys : gObjdKeys) keys->push_back(key);
            return uint32_t(keys->size());
        }
        case 34: { // (&key, &package)
            const uint32_t *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
            const unsigned pick = (key[1] + key[2] + r) % (kSrcPkgs + 1);
            if (pick < kSrcPkgs) Give(a[1], &gW.srcPkg[pick]);
            return pick < kSrcPkgs;
        }
        default: return r;
        }
    case kInMsg:
        switch (slot) {
        case 0:
            if (!gW.msgQi) return 0x100;
            Give(a[1], self);
            return 1;
        case 4: return gW.msgType;
        case 8: return gW.msg8;
        case 9: return gW.msg9;
        case 29:
            if (gW.msgString < 0) return 0x100;
            *reinterpret_cast<void **>(uintptr_t(a[1])) = &gNames[gW.msgString].s;
            gNames[gW.msgString].s.AsInterface()->AddRef();
            return 1;
        case 32: return P(&gW.source);
        default: return r;
        }
    case kSource: {
        if (!gW.sourceQi) return 0x100;
        // a behaviour (0x7b269499, message 0xa2) or a framework object (0xeb54e473, message 0x7c26430f; compared by address)
        const uint32_t k = gW.sourceTarget;
        if (a[0] == 0x7b269499u) Give(a[1], k % 3 == 0 ? &gW.newBeh[k % kNewBehaviors] : &gW.beh[k % kLoadable]);
        else Give(a[1], k % 3 == 2 ? &gW.newBeh[0] : &gW.fw[k % 2]);
        return 1;
    }
    case kResource:
        if (slot == 14) { // the record's name (AddLotObjectCopy compares it with the new name)
            static const char *const kTexts[] = {"lot", "N001", "x"};
            Str(a[0])->mString = kTexts[(IndexIn(gW.res, kObjects, self) + r) % 3];
        }
        return r;
    case kOutMsg:
        return r;
    case kRStream:
        if (slot == 20) return P(&gW.version);
        if (slot == 6) *reinterpret_cast<int16_t *>(uintptr_t(a[0])) = gW.streamIndex;
        if (slot == 9) *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gW.streamId;
        return r;
    case kVersion:
        return P(&gW.versionWord);
    case kSrcPkg: {
        const unsigned i = IndexIn(gW.srcPkg, kSrcPkgs, self);
        if (slot == 0) {
            if (a[0] == 0x856c8cf7u) { // a lot script package: skipped by the import
                if ((r & 3) != 0) return 0x100;
                Give(a[1], &gW.script[i % 4]);
                return 1;
            }
            Give(a[1], &gW.segment);
            return 1;
        }
        if (slot == 11) Str(a[0])->mString = gSrcPkgName[i];
        if (slot == 19) {
            auto *keys = reinterpret_cast<KeyVector *>(uintptr_t(a[0]));
            void *filter = reinterpret_cast<void *>(uintptr_t(a[1]));
            for (const auto &key : gPkgKeys[i]) {
                bool in = true;
                if (filter) {
                    using Included = uint32_t(__thiscall *)(void *, const uint32_t *);
                    in = uint8_t((*static_cast<Included **>(filter))[3](filter, key.data())) != 0;
                    gLog.insert(gLog.end(), {0xF117u, key[0], key[1], in});
                }
                if (in) keys->push_back(key);
            }
        }
        return r;
    }
    case kNewPkg:
        if (slot == 0) {
            if (a[0] == 0x8ceec300u) {
                if (!(r & 1)) return 0x100;
                Give(a[1], &gW.edit);
                return 1;
            }
            if (r & 2) return 0x100;
            Give(a[1], self);
            return 1;
        }
        if (slot == 3) return P(&gW.segment);
        if (slot == 11) {
            const uint32_t *source = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
            uint32_t *target = reinterpret_cast<uint32_t *>(uintptr_t(a[1]));
            target[0] = source[0];
            target[1] = 0x77;
            target[2] = source[2] + 1;
            target[3] = 0;
        }
        return r;
    default: return r;
    }
}

template <unsigned S, unsigned... I> uint32_t __fastcall SlotThunk(Fake *self, void *, shape::StackWord<I>... w)
{
    const uint32_t args[] = {0u, static_cast<uint32_t>(w)...};
    return Handle(self, S, args + 1, sizeof...(I));
}
template <unsigned S, unsigned... I> void *ThunkAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&SlotThunk<S, I...>);
}
template <unsigned S> void *ThunkFor(unsigned words)
{
    switch (words) {
    case 0: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 0>{});
    case 1: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 1>{});
    case 2: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 2>{});
    case 3: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 3>{});
    case 4: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 4>{});
    case 5: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 5>{});
    default: return ThunkAddress<S>(std::make_integer_sequence<unsigned, 7>{});
    }
}
template <unsigned... S> void *ThunkOf(unsigned slot, unsigned words, std::integer_sequence<unsigned, S...>)
{
    void *result = nullptr;
    ((slot == S ? (result = ThunkFor<S>(words), 0) : 0), ...);
    return result;
}
uint32_t BadSlot(void *self, unsigned words)
{
    if (!gFailedSlot && gCtx) gCtx->Fail("unexpected slot call on %08x (%u words)", Map(P(self)), words);
    gFailedSlot = true;
    return 0;
}

void *__fastcall DirectorFramework(void *, void *) { return &gW.dirFw; }
void *gDirectorVt[16];

void BuildVTables()
{
    static bool built;
    if (built) return;
    built = true;
    for (unsigned k = 0; k < kKinds; ++k) {
        for (unsigned s = 0; s < kSlots; ++s) gVTables[k][s] = shape::TrapFor<BadSlot>(0);
        for (const SlotSpec &spec : kSpecs[k])
            gVTables[k][spec.slot] = ThunkOf(spec.slot, spec.words, std::make_integer_sequence<unsigned, kSlots>{});
    }
    for (void *&slot : gDirectorVt) slot = shape::TrapFor<BadSlot>(0);
    gDirectorVt[12] = reinterpret_cast<void *>(&DirectorFramework);
}

void Init(Fake &f, Kind kind, uint32_t id, std::mt19937 &r)
{
    f.vt = gVTables[kind];
    f.id = id;
    f.kind = kind;
    f.refs = 1 + int32_t(r() % 3);
    for (unsigned s = 0; s < kSlots; ++s) {
        // Byte-valued answers include 0x100 (al 0 with the upper bits set): a caller reading eax shows.
        const uint32_t pick = r() % 8;
        f.ret[s] = pick < 3 ? 0u : pick < 6 ? 1u : pick == 6 ? 0x100u : (r() | 1u);
    }
}

const uint32_t kSpecialIds[] = {0x3c08965f, 0x7fd96b54, 0x4c859106, 0xacc53dff, 0xec85f0f3, 0x7c4,
                                0xecfedb37, 0xb21ada68, 0x532fb5a1, 0x6cd4a8bc};
const char *const kObjNames[] = {"Chair", "chair", "Lamp", "LAMP", "Sofa", "Bed", "bed", "Table"};

void BuildWorld(uint32_t seed)
{
    BuildVTables();
    std::mt19937 r(seed * 2654435761u + 7);
    uint32_t id = 1;
    for (unsigned i = 0; i < kObjects; ++i) {
        Init(gW.obj[i], kObject, id++, r);
        Init(gW.data[i], kData, id++, r);
        Init(gW.beh[i], kBehavior, id++, r);
        Init(gW.partA[i], kPart, id++, r);
        Init(gW.partB[i], kPart, id++, r);
        Init(gW.link[i], kLink, id++, r);
        Init(gW.res[i], kResource, id++, r);
    }
    for (Fake &f : gW.newBeh) Init(f, kBehavior, id++, r);
    for (Fake &f : gW.fw) Init(f, kFramework, id++, r);
    for (Fake &f : gW.script) {
        Init(f, kScript, id++, r);
        f.ret[7] = 0xabcd0000u | (r() % 5); // a 16-bit id with upper bits
    }
    Init(gW.dirFw, kDirFw, id++, r);
    Init(gW.singC, kSingletonC, id++, r);
    Init(gW.service, kService, id++, r);
    Init(gW.settings, kSettings, id++, r);
    Init(gW.options, kOptions, id++, r);
    Init(gW.cached, kCached, id++, r);
    Init(gW.catalog, kCatalog, id++, r);
    Init(gW.persist, kPersist, id++, r);
    for (Fake &f : gW.rec) Init(f, kRecord, id++, r);
    Init(gW.inMsg, kInMsg, id++, r);
    Init(gW.source, kSource, id++, r);
    Init(gW.outMsg, kOutMsg, id++, r);
    Init(gW.server, kServer, id++, r);
    Init(gW.wstream, kWStream, id++, r);
    Init(gW.rstream, kRStream, id++, r);
    Init(gW.version, kVersion, id++, r);
    for (Fake &f : gW.srcPkg) Init(f, kSrcPkg, id++, r);
    for (Fake &f : gW.newPkg) Init(f, kNewPkg, id++, r);
    Init(gW.edit, kEdit, id++, r);
    Init(gW.segment, kSegment, id++, r);
    Init(gW.list, kList, id++, r);
    for (Fake &f : gW.tag) Init(f, kTag, id++, r);
    gW.rstream.ret[20] = P(&gW.version);

    // The special ids go to a few filed objects (shuffled); the rest are plain and unique.
    std::vector<uint32_t> ids(kSpecialIds, kSpecialIds + 10);
    for (unsigned i = 0; i < 10; ++i) std::swap(ids[i], ids[r() % 10]);
    const unsigned zeroId = gW.zeroId = r() % 4 ? r() % kLoadable : kObjects;
    for (unsigned i = 0; i < kObjects; ++i) {
        Fake &o = gW.obj[i];
        const bool empty = i == 20 || i == 21;
        o.ret[4] = empty ? 0 : P(&gW.data[i]);
        o.ret[18] = empty ? 0 : P(&gW.beh[i]);
        o.ret[37] = empty || r() % 4 == 0 ? 0 : P(&gW.link[i]);
        o.ret[67] = empty ? 0 : P(&gW.partA[i]);
        o.ret[70] = empty || r() % 3 == 0 ? 0 : P(&gW.partB[i]);
        o.ret[75] = P(&gW.res[i]);
        o.ret[28] = i < kLoadable && i < 8 && r() % 2 ? ids[i] : (i == 22 ? 0x5000u + r() % 4 : 0x5000u + i);
        o.ret[45] = 0xabcd0000u | uint16_t(int16_t(int(r() % 9) - 2));
        o.ret[57] = 0x7770000u | (r() % 6 == 0 ? 0xffffu : r() % 5);
        o.ret[63] = (r() % 2) | (r() % 2 ? 0x100u : 0u);
        o.ret[66] = r() % 4;
        o.ret[73] = (i < kLoadable && r() % 5 == 0) ? r() % 2 : 0xffffffffu;
        o.ret[76] = r() % 4;
        if (i == zeroId) o.ret[28] = 0; // an object with id 0 (save index 0 then finds it unless excluded)
        if (i == 24) o.ret[66] = 1;            // the import's usual lot object: the group most package keys use
        o.ret[38] = r() % 4 == 0;
        gW.objName[i] = kObjNames[r() % 8];
        gW.link[i].ret[10] = P(&gW.obj[r() % kObjects]);
        gW.partA[i].ret[10] = r() % 3;
        for (int k = 0; k < 3; ++k) {
            // behaviour ids: pointers to the framework objects (message 0x7c26430f compares them) or small numbers
            const uint32_t choices[] = {P(&gW.fw[0]), P(&gW.fw[1]), 1, 2, 0, 3};
            gW.beh[i].ret[14 + k] = choices[r() % 6];
        }
        uint32_t *d = gW.dataVal[i];
        const uint32_t kinds[] = {2, 0x10, 0x11, 5};
        d[0] = kinds[r() % 4];
        d[1] = r() % 3 ? gW.obj[r() % kLoadable].ret[28] : r();
        d[2] = r() % 8 == 0 ? 0xffffu : r() % 3;
        d[3] = r() % 3;
        d[4] = r() % 3;
        d[5] = r() % 3 == 0;
        d[6] = r() % 2 ? 0 : r() % 50 + 1;
        d[7] = r() % 4;
        gW.data[i].ret[8] = r() % 6 == 0;
    }
    for (Fake &b : gW.newBeh)
        for (int k = 0; k < 3; ++k) b.ret[14 + k] = k;
    // the filed objects' ids again (the shuffle above assigned before the data looked them up)
    for (unsigned i = 0; i < kObjects; ++i)
        if (gW.dataVal[i][1] < 0x10000 || r() % 3 == 0) gW.dataVal[i][1] = gW.obj[r() % kLoadable].ret[28];
    for (unsigned i = 0; i < kRecords; ++i) {
        const uint32_t kinds[] = {2, 0x10, 0x11, 5};
        gW.recVal[i][0] = kinds[r() % 4];
        gW.recVal[i][1] = r() % 3 == 0 ? 0 : 1;
        gW.recVal[i][2] = r() % 3 == 0 ? 0xffffffffu : r() % 4 == 0 ? 0u - (r() % 0x300 + 2) : r() % 0x300;
        gW.recVal[i][3] = r() % 2;
        gW.recVal[i][4] = r() % 4;
    }
    gGlobKeys.clear();
    gObjdKeys.clear();
    for (int i = int(r() % 5); i > 0; --i) gGlobKeys.push_back({kGlob, r() % 4, r() % 3, 0});
    for (int i = int(r() % 9); i > 0; --i) gObjdKeys.push_back({kObjd, r() % 4, r() % 12, r() % 2});
    const uint32_t types[] = {kObjd, kTree, kFern, 0x12345678, kObjd, kTree};
    const char *sep = GameAddress<const char>(0xd9f880);
    for (unsigned p = 0; p < kSrcPkgs; ++p) {
        gPkgKeys[p].clear();
        for (int i = int(r() % 9); i > 0; --i) gPkgKeys[p].push_back({types[r() % 6], r() % 4 ? 1u : r() % 4, r() % 9, 0});
        char name[64];
        if (r() % 3 == 0)
            std::snprintf(name, sizeof name, "C:\\lots\\lot%u%s%u.package", p, sep, unsigned(r() % 3));
        else
            std::snprintf(name, sizeof name, "C:\\lots\\Lot%u.package", p);
        gSrcPkgName[p] = name;
    }
    for (auto &m : gW.groupPkgs) m = r() % 3 ? 0x1fu & ~(1u << r() % 8) : r() % 32; // mostly 4-5 source packages
    gW.versionWord = 0x2e + r() % 3; // around 0x2f, where ReadObjectRef switches formats
    gW.nextFw = gW.nextBeh = gW.nextTag = gW.nextLot = gW.nextNew = 0;
    gW.owner = 0x9900u + r() % 3;
    const uint32_t totals[] = {0x100, 0x1ff, 0x200, 0x400};
    gW.memTotal = totals[r() % 4];
    gW.memAvail = r() % 0x100;
    gW.skipEvery = r() % 3 == 0 ? r() % 3 + 1 : 0;
    gW.importAnswer = r() % 2;
    gW.readNameMask = r();
    gW.dirCount = r() % 4;
}

// ---- stand-ins ----

cTSObjectFolder *FolderOf(void *face0) { return static_cast<cTSObjectFolder *>(static_cast<cITSBehaviorFinder *>(face0)); }

bool Contains(const FolderObjects &v, void *o)
{
    for (const auto &ref : v)
        if (ref.Get() == o) return true;
    return false;
}

// 0x4d76a0 (folder, group, instance, &out, load, name): files the object (group, instance) picks in the lot list or, when a
// neighbourhood is current and the instance is odd, in its list (with the id maps), unless an object with its id is there.
uint8_t __fastcall StandLoadObject(void *face0, void *, uint32_t group, uint32_t instance, void **out, uint32_t load, const char *name)
{
    gLog.insert(gLog.end(), {0x4d76a0u, group, instance, out != nullptr, load, TextHash(name)});
    cTSObjectFolder *f = FolderOf(face0);
    const unsigned i = (group * 5 + instance) % kLoadable;
    Fake *o = &gW.obj[i];
    const uint32_t id = o->ret[28];
    const int32_t portal = int32_t(o->ret[73]);
    if (portal >= 0 && uint32_t(portal) >= f->mPortals.size()) return 0;
    if (Contains(f->mObjects, o) || f->mObjectsById.count(id)) return 0;
    if (f->mGlobals && (Contains(*f->mGlobals, o) || f->mGlobalIds->count(id))) return 0;
    if (f->mGlobals && (instance & 1)) {
        f->mGlobals->push_back(FolderRef(o));
        (*f->mGlobalIds)[id].Assign(o);
    } else {
        f->mObjects.push_back(FolderRef(o));
        f->mObjectsById[id].Assign(o);
    }
    if (out && (instance % 7 != 5 || gW.owner % 2)) *out = AddRefd(o); // sometimes filed without handing it out
    return 1;
}
uint32_t __fastcall StandImport(void *face0, void *)
{
    gLog.push_back(0x4d5d00u);
    (void)face0;
    return gW.importAnswer;
}
void __fastcall StandLoadCustom(void *, void *, uint32_t flag, const std::unordered_map<uint32_t, FolderName> *names)
{
    gLog.insert(gLog.end(), {0x4cfc20u, flag & 0xff, uint32_t(names->size())});
    for (const auto &e : *names) gLog.insert(gLog.end(), {e.first, TextHash(e.second.s.mString)});
}
uint8_t __fastcall StandReadName(void *, void *, uint32_t group, uint32_t instance, cRZString *out)
{
    gLog.insert(gLog.end(), {0x4d8850u, group, instance});
    if ((gW.readNameMask >> ((group * 3 + instance) % 32)) & 1) return 0;
    static const char *const kTexts[] = {"N001", "n001", "N002", "Downtown"};
    out->mString += kTexts[(group + instance) % 4]; // appended: what the string held before shows
    return 1;
}
uint32_t __cdecl StandSkipEvery()
{
    gLog.push_back(0x4d3980u);
    return gW.skipEvery;
}
void __fastcall StandReadTranslation(void *table, void *, void *package)
{
    gLog.insert(gLog.end(), {0xc0b669u, table != nullptr, Map(P(package))});
}
uint32_t __cdecl StandKindValue()
{
    gLog.push_back(0xb39360u);
    return 0x20000u + gW.owner;
}
uint8_t __cdecl StandEnsureDirectory(cRZString *path)
{
    gLog.insert(gLog.end(), {0x1c5031u, TextHash(path->mString)});
    return 1;
}
void __cdecl StandDirectoryOf(const cRZString *name, cRZString *out, uint32_t a, uint32_t b)
{
    gLog.insert(gLog.end(), {0xc0b150u, TextHash(name->mString), a, b});
    out->mString = "C:\\hood\\" + name->mString;
}
void *__fastcall StandScanCtor(uint8_t *self, void *, const cRZString *dir, uint32_t a, uint32_t b, uint32_t c)
{
    gLog.insert(gLog.end(), {0x1c48dau, TextHash(dir->mString), a, b, c});
    std::memset(self, 0, 0x5c);
    *reinterpret_cast<uint32_t *>(self + 8) = gW.dirCount;
    return self;
}
void __fastcall StandScanFind(void *, void *, cRZString *pattern, uint32_t flag)
{
    gLog.insert(gLog.end(), {0x1c63d4u, TextHash(pattern->mString), flag});
}
void *__fastcall StandScanEntry(void *, void *, uint32_t i)
{
    gLog.insert(gLog.end(), {0x1c5047u, i});
    return &gEntries[i % 8];
}
void __fastcall StandEntryPath(uint32_t *entry, void *, cRZString *out)
{
    const uint32_t i = uint32_t(entry - gEntries);
    gLog.insert(gLog.end(), {0x1c509fu, i});
    char text[48];
    std::snprintf(text, sizeof text, "C:\\hood\\Pkg%u.package", unsigned((i * 7 + gW.owner) % 3));
    out->mString = text;
}
void __fastcall StandScanDtor(void *, void *) { gLog.push_back(0x1c49ebu); }
uint8_t __cdecl StandIsFile(cRZString *path, uint32_t flag)
{
    gLog.insert(gLog.end(), {0x1c1931u, TextHash(path->mString), flag});
    return uint8_t(TextHash(path->mString) & 1);
}
uint8_t __cdecl StandDeleteA(cRZString *path, uint32_t flag)
{
    gLog.insert(gLog.end(), {0x1c5fa0u, TextHash(path->mString), flag});
    return 1;
}
uint8_t __cdecl StandDeleteB(cRZString *path)
{
    gLog.insert(gLog.end(), {0x1c2e25u, TextHash(path->mString)});
    return 1;
}
// The message's 0x2c bytes come from the allocator (the image's RZMalloc 0x1be3b0 behind 0x1c03b7, our RZMemory::Malloc);
// the constructor stand-in hands out the message fake instead and frees the block (logged by presence: heap addresses differ).
void *__fastcall StandMessageCtor(void *memory, void *)
{
    gLog.insert(gLog.end(), {0x1c033du, memory != nullptr});
    if (memory) RZMemory::Free(memory);
    return &gW.outMsg;
}
void *__cdecl StandMessageServer() { return &gW.server; }
uint32_t __cdecl StandCommonImpl4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    gLog.insert(gLog.end(), {0x1d0c00u, a, Map(b), c, d, e});
    Str(b)->mString = "Option" + std::to_string(gW.owner % 3);
    return 1;
}
void __fastcall StandWalk5(std::set<uint32_t> *ids, void *, uint32_t arg, void *object)
{
    gLog.insert(gLog.end(), {0x1c0458u, Map(arg), P(object), uint32_t(ids->size())});
    for (uint32_t id : *ids) gLog.push_back(id);
}
void __fastcall StandWalk6(std::set<uint32_t> *ids, void *, uint32_t arg, void *object)
{
    gLog.insert(gLog.end(), {0x1c081eu, Map(arg), P(object), uint32_t(ids->size())});
    for (uint32_t id : *ids) gLog.push_back(id);
}
void *__fastcall StandVersionConstruct(uint32_t *self, void *)
{
    gLog.push_back(0x1c98d6u);
    std::memset(self, 0, 0x30);
    return self;
}
void __fastcall StandVersionDestruct(void *, void *) { gLog.push_back(0x1c9901u); }
uint8_t __fastcall StandGetMemory(void *, void *, uint32_t *total, uint32_t *available)
{
    gLog.push_back(0x1c9c01u);
    *total = gW.memTotal;
    *available = gW.memAvail;
    return 1;
}

// ---- the folder's state ----

void PushRefs(Blob &b, const FolderObjects &v)
{
    b.insert(b.end(), {uint32_t(v.size()), uint32_t(v.capacity())});
    for (const auto &ref : v) b.push_back(Map(P(ref.Get())));
}
void PushIds(Blob &b, const FolderIdMap &m)
{
    b.insert(b.end(), {uint32_t(m.size()), uint32_t(m.bucket_count())});
    for (const auto &e : m) b.insert(b.end(), {e.first, Map(P(e.second.Get()))});
}
void Dump(Blob &b, const cTSObjectFolder *f)
{
    b.insert(b.end(), {0xD0D0u, f->mRefCount, f->mLoaded, f->mRegistered, f->mImporting, f->mLoadingGlobals,
                       Map(P(f->mFramework.Get())), Map(P(f->mLotObject.Get()))});
    PushRefs(b, f->mLotScripts);
    b.push_back(uint32_t(f->mGroupObjects.size()));
    uint32_t current = 0xffffffff, currentIds = 0xffffffff, at = 0;
    for (const auto &e : f->mGroupObjects) {
        b.push_back(TextHash(e.first.s.mString));
        PushRefs(b, e.second);
        if (&e.second == f->mGlobals) current = at;
        ++at;
    }
    b.push_back(uint32_t(f->mGroupIds.size()));
    at = 0;
    for (const auto &e : f->mGroupIds) {
        b.push_back(TextHash(e.first.s.mString));
        PushIds(b, e.second);
        if (&e.second == f->mGlobalIds) currentIds = at;
        ++at;
    }
    b.insert(b.end(), {current, currentIds, f->mGlobals != nullptr, f->mGlobalIds != nullptr});
    PushIds(b, f->mObjectsById);
    PushRefs(b, f->mObjects);
    PushRefs(b, f->mPortals);
    b.push_back(uint32_t(f->mPackageInfos.size()));
    for (const auto &info : f->mPackageInfos) b.insert(b.end(), {TextHash(info.mName.c_str()), info.mFlag, uint32_t(info.mEntries.size())});
    b.push_back(uint32_t(f->mCachedResources.size()));
    for (const FolderRef &v : f->mCachedResources) b.push_back(Map(P(v.Get())));
    b.insert(b.end(), {uint32_t(f->mSaveIndex.size()), uint32_t(f->mSaveIndex.capacity())});
    for (uint32_t v : f->mSaveIndex) b.push_back(v);
    b.push_back(uint32_t(f->mMessageIds.size()));
    for (uint32_t v : f->mMessageIds) b.push_back(v);
    b.push_back(f->mMessageCount);
}
void DumpCounts(Blob &b)
{
    unsigned count;
    Fake *all = AllFakes(count);
    b.push_back(0xC0C0u);
    for (unsigned i = 0; i < count; ++i) b.push_back(uint32_t(all[i].refs));
    for (auto &n : gNames) b.push_back(n.s.mRefCount);
    b.insert(b.end(), {gW.nextFw, gW.nextBeh, gW.nextTag, gW.nextLot, gW.nextNew});
}

// ---- globals and patches ----

struct Fixture {
    difftest::Context &ctx;
    static constexpr unsigned kGlobalCount = 8;
    static constexpr uint32_t kGlobals[kGlobalCount] = {0x10c95d4, 0x10c95d8, 0x10c9378, 0x10faf6c,
                                                        0x10cc288, 0x10cc290, 0x10c9390, 0x10c93f4};
    uint32_t saved[kGlobalCount];
    bool builtAllocator = false;
    pworld::Patches patches;

    struct AllocService {
        void **vt;
    } alloc;
    void *allocVt[8] = {};
    static void *__fastcall Alloc(AllocService *, void *, uint32_t bytes) { return RZMemory::Malloc(bytes); }
    static void __fastcall Free(AllocService *, void *, void *block)
    {
        if (block) RZMemory::Free(block);
    }

    explicit Fixture(difftest::Context &c) : ctx(c)
    {
        gCtx = &ctx;
        BuildVTables();
        for (unsigned i = 0; i < kGlobalCount; ++i) saved[i] = *ctx.image.At<uint32_t>(kGlobals[i]);
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
        allocVt[3] = reinterpret_cast<void *>(&Alloc);
        allocVt[4] = reinterpret_cast<void *>(&Free);
        alloc.vt = allocVt;
        *ctx.image.At<void *>(0x10c95d4) = &alloc;
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        *ctx.image.At<void *>(0x10c9378) = &gW.service;
        *ctx.image.At<void *>(0x10faf6c) = &gW.catalog;
        *ctx.image.At<void *>(0x10cc288) = &gW.cached;
        *ctx.image.At<void *>(0x10cc290) = &gW.singC;
        *ctx.image.At<void *>(0x10c9390) = gDirectorVt;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;

        patches.Heap();
        patches.Image(0x4d76a0, &StandLoadObject);
        patches.Image(0x4d5d00, &StandImport);
        patches.Image(0x4cfc20, &StandLoadCustom);
        patches.Image(0x4d8850, &StandReadName);
        patches.Image(0x4d3980, &StandSkipEvery);
        patches.Image(0xc0b669, &StandReadTranslation);
        patches.Image(0xb39360, &StandKindValue);
        patches.Both(0x1c5031, &folders::EnsureDirectory, &StandEnsureDirectory); // ours too (lane LY)
        patches.Image(0xc0b150, &StandDirectoryOf);
        patches.Both(0x1c48da, &DirectoryScanAdapters::Construct, &StandScanCtor);
        patches.Both(0x1c63d4, &DirectoryScan::SetPattern, &StandScanFind);
        patches.Both(0x1c5047, &DirectoryScan::EntryAt, &StandScanEntry);
        patches.Both(0x1c509f, &dscan::Entry::FullPath, &StandEntryPath);
        patches.Both(0x1c49eb, &DirectoryScanAdapters::Destruct, &StandScanDtor);
        patches.Both(0x1c1931, &PathAttributesMatch, &StandIsFile); // ours too (lane MA)
        patches.Image(0x1c5fa0, &StandDeleteA);
        patches.Both(0x1c2e25, &PathDelete, &StandDeleteB);
        patches.Both(0x1c033d, &cRZMessage2Standard::Construct, &StandMessageCtor);
        patches.Both(0x9c5a89, &GetCachedMessageServer, &StandMessageServer);
        patches.Both(0x1d0c00, &appinit::CommonImplSlot4, &StandCommonImpl4);
        patches.Both(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
        patches.Both(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
        patches.Both(0x1c98d6, &appbase::cRZVersion::Construct, &StandVersionConstruct);
        patches.Both(0x1c9901, &appbase::cRZVersion::Destruct, &StandVersionDestruct);
        patches.Both(0x1c9c01, &appbase::cRZVersion::GetMemory, &StandGetMemory);

        NT_TIB *tib = reinterpret_cast<NT_TIB *>(NtCurrentTeb());
        gStackTop = reinterpret_cast<uintptr_t>(tib->StackBase);
        for (unsigned i = 0; i < kNames; ++i) {
            gNames[i].s.Construct(kNameText[i]);
            gNames[i].s.AsInterface()->AddRef();
        }
    }
    ~Fixture()
    {
        for (unsigned i = 0; i < kNames; ++i) gNames[i].s.Destruct();
        for (unsigned i = 0; i < kGlobalCount; ++i) *ctx.image.At<uint32_t>(kGlobals[i]) = saved[i];
        if (builtAllocator) difftest_alloc::ShutdownAndRelease(GameAddress<GeneralAllocator>(0x10cb098));
        gCtx = nullptr;
    }
};

template <class R = uint32_t, class... A> R CallSlot(void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return (*static_cast<Fn *const *>(object))[slot](object, args...);
}

// ---- operations ----

struct Op {
    int code;
    uint32_t a = 0, b = 0, c = 0, d = 0;
};

enum : int {
    oFace4 = 0,       // + slot
    oFace0 = 100,     // + slot
    oQi = 200, oAddRef = 201, oRelease = 202, oMessage = 300,
    oPortals = 400, oScripts = 401, oCached = 402, oInfos = 403, oFlags = 404, oSaveIdx = 405,
};

std::vector<Op> MakeOps(uint32_t seed, int count)
{
    std::mt19937 r(seed * 1103515245u + 12345);
    std::vector<Op> ops;
    ops.push_back({oPortals, 2});
    // Before Init: queries on the empty folder, sometimes neighbourhood messages first.
    for (int s : {10, 11, 12, 13, 14, 18, 19, 35, 56}) ops.push_back({oFace4 + s, r() % 4});
    for (int i = int(r() % 3); i > 0; --i) ops.push_back({oMessage, 0xc3, r() % 4, 1});
    if (r() % 2) ops.push_back({oMessage, 0xc5, r() % 4, 1});
    if (r() % 2) { // FindObject while only the globals are loading (not loaded, not importing)
        ops.push_back({oFlags, 2});
        for (int i = 0; i < 3; ++i) ops.push_back({oFace4 + 14, gW.obj[r() % kLoadable].ret[28]});
        ops.push_back({oFlags, 0});
    }
    ops.push_back({oFace4 + 4});
    for (int i = 0; i < count; ++i) {
        Op op{};
        const uint32_t obj = r() % 6 == 0 ? kLoadable + r() % 4 : r() % kLoadable;
        switch (r() % 30) {
        case 0: op = {oFace4 + int(10 + r() % 3)}; break;
        case 1: op = {oFace4 + 13, r() % 30}; break;
        case 2: op = {oFace4 + int(14 + r() % 4), r() % 3 ? gW.obj[r() % kObjects].ret[28] : r() % 8, r() % 8};
            if (op.code == oFace4 + 17 && r() % 2) { // by save index: a small one, sometimes after a save table
                op.a = r() % 4;
                if (r() % 2) ops.push_back({oSaveIdx, r(), r()});
            }
            break;
        case 3: op = {oFace4 + int(r() % 2 ? 18 : 19), r() % 3}; break;
        case 4: op = {oFace4 + int(20 + r() % 4), r() % kLoadable, r() % 3, r() % 3, r() % 3}; break;
        case 5: op = {oFace4 + 24}; break;
        case 6: op = {oFace4 + 25, r() % 5, r() % 2, r() % 2}; break;
        case 7: op = {oFace4 + 26, r() % 6, r() % 2}; break;
        case 8: op = {oFace4 + 27, obj}; break;
        case 9: op = {oFace4 + int(28 + r() % 4), r() % 5 == 0 ? 99u : obj}; break;
        case 10: op = {oFace4 + int(32 + r() % 3), r() % kSrcPkgs}; break;
        case 11: op = {oFace4 + int(35 + r() % 2), r() % 5}; break;
        case 12: op = {oFace4 + int(37 + r() % 15), obj}; break;
        case 13: op = {oFace4 + 52, r(), r(), r(), r()}; break;
        case 14: { // copy an object's record; again under another name half the time (a name already used stops it)
            const uint32_t which = r() % 5 == 0 ? 99u : obj, name = r() % kNames;
            if (r() % 2) ops.push_back({oFace4 + 53, which, (name + 1 + r() % (kNames - 1)) % kNames});
            op = {oFace4 + 53, which, name};
            break;
        }
        case 15: op = {oFace4 + int(54 + r() % 4), r() % 4, r() % 2}; break;
        case 16: op = {oFace4 + int(r() % 3 ? 7 : 6)}; break;
        case 17:
            if (r() % 3 == 0) ops.push_back(r() % 2 ? Op{oFace4 + 33, r() % kSrcPkgs} : Op{oSaveIdx, r(), r()}); // numbers the objects: save indices to read back
            op = {oFace4 + int(r() % 2 ? 8 : 9), r() % 3 ? obj : 99u, r(), r()};
            break;
        case 18: op = {oFace0 + int(6 + r() % 4), obj}; break;
        case 19:
            if (r() % 3 == 0) ops.push_back(r() % 2 ? Op{oFace4 + 33, r() % kSrcPkgs} : Op{oSaveIdx, r(), r()});
            op = {oFace0 + int(3 + r() % 2), r() % 3 ? obj : 99u, r(), r()};
            break;
        case 20: case 21: case 22: {
            const uint32_t types[] = {0xc3, 0xc4, 0xc5, 0xa2, 0x7c26430f, 0xb5, 0x8ecc1b6d, 0x1234, 0xc3, 0xc5};
            op = {oMessage, types[r() % 10], r() % 5, r(), r()};
            break;
        }
        case 23: op = {oQi, r() % 3, r() % 4}; break;
        case 24: op = {int(r() % 2 ? oAddRef : oRelease)}; break;
        case 25: op = r() % 4 == 0 ? Op{oFace4 + 5} : Op{oFace4 + 4}; break;
        case 26: op = {oScripts, r() % 5}; break;
        case 27: op = {oCached, r() % 4, r()}; break;
        case 28: op = {oInfos, r() % 3}; break;
        default: op = {oFlags, r() % 4}; break;
        }
        ops.push_back(op);
    }
    return ops;
}

Fake *Obj(uint32_t i) { return i < kObjects ? &gW.obj[i] : nullptr; }

template <class M> bool NameIsIn(const M &m, const char *text)
{
    for (const auto &e : m)
        if (_stricmp(e.first.s.Data(), text) == 0) return true;
    return false;
}
bool NameIsCurrent(const cTSObjectFolder *f, const char *text)
{
    for (const auto &e : f->mGroupObjects)
        if (&e.second == f->mGlobals && _stricmp(e.first.s.Data(), text) == 0) return true;
    return false;
}
bool PortalsCover(const cTSObjectFolder *f, const FolderObjects &v)
{
    for (const auto &ref : v) {
        const int32_t portal = int32_t(static_cast<Fake *>(ref.Get())->ret[73]);
        if (portal >= 0 && uint32_t(portal) >= f->mPortals.size()) return false;
    }
    return true;
}

constexpr uint32_t kSkipped = 0x5C1Bu;

uint32_t SetToWord(const FolderObjectSet &s, Blob &step)
{
    step.push_back(uint32_t(s.size()));
    for (void *p : s) step.push_back(Map(P(p)));
    return uint32_t(s.size());
}

uint32_t RunOp(void *face4, const Op &op, int &refs, Blob &step)
{
    cTSObjectFolder *f = static_cast<cTSObjectFolder *>(static_cast<cITSObjectFolder *>(face4));
    void *face0 = static_cast<cITSBehaviorFinder *>(f);
    void *face8 = static_cast<cIGZMessageTarget2 *>(f);
    const bool anyFlag = f->mLoaded || f->mImporting || f->mLoadingGlobals;
    switch (op.code) {
    case oQi: { // QueryInterface 0x4d4bd0 and its adjustor thunks: folder face 0x4d4c19, message face 0x4d4c21
        void *const faces[] = {face0, face4, face8};
        const uint32_t iids[] = {1, 0x9b14358e, 0x1b26949e, 0x12345};
        void *out = reinterpret_cast<void *>(0x1234);
        const uint8_t ok = CallSlot<uint8_t>(faces[op.a % 3], 0, iids[op.b % 4], &out);
        if (ok) ++refs;
        return ok | (Map(P(out)) << 8);
    }
    case oAddRef: ++refs; return CallSlot(face4, 1);
    case oRelease: // Release through the folder face's thunk 0x4d55f5 or the message face's 0x4d55fd
        if (refs <= 1) return kSkipped;
        --refs;
        return CallSlot(refs % 2 ? face8 : face4, 2);
    case oPortals:
        for (uint32_t i = 0; i < op.a; ++i) f->mPortals.push_back(FolderRef(&gW.obj[26 + i % 2]));
        return 0;
    case oScripts:
        for (uint32_t i = 0; i < op.a; ++i) f->mLotScripts.push_back(FolderRef(i % 5 == 4 ? nullptr : &gW.script[i % 4]));
        return 0;
    case oSaveIdx: {
        // A save table as ReadSaveTable leaves it (index 0 always 0, see 0x4d46d0), after filing the id-0 object through
        // the LoadObject stand-in (so a save index of 0 has something to wrongly find).
        if (!f->mLoaded) return kSkipped;
        if (gW.zeroId < kLoadable) {
            const uint32_t group = gW.zeroId % 2 ? 1 : 0, instance = gW.zeroId % 2 ? (gW.zeroId + 15) % 20 : gW.zeroId;
            StandLoadObject(face0, nullptr, group, instance, nullptr, 1, gW.objName[0]);
        }
        f->mSaveIndex.assign(1, 0u);
        for (uint32_t i = 0; i < 1 + op.a % 5; ++i) f->mSaveIndex.push_back(gW.obj[(op.b >> (i * 3)) % kLoadable].ret[28]);
        return 0;
    }
    case oCached:
        for (uint32_t i = 0; i < op.a; ++i)
            f->mCachedResources.insert(FolderRef(i == 0 && op.b % 3 == 0 ? nullptr : &gW.res[(op.b >> (i * 4)) % kObjects]));
        return 0;
    case oInfos:
        for (uint32_t i = 0; i < op.a; ++i) {
            FolderPackageInfo info;
            info.mName = i % 2 ? "Second" : "First";
            info.mFlag = i % 2 == 0;
            info.mLists[i % 4].push_back(FolderString("x"));
            info.mEntries.resize(i);
            f->mPackageInfos.push_back(info);
        }
        return 0;
    case oFlags:
        f->mImporting = (op.a & 1) != 0;
        f->mLoadingGlobals = (op.a & 2) != 0;
        return 0;
    case oMessage: {
        const uint32_t type = op.a;
        gW.msgType = type;
        gW.msgQi = op.c % 7 != 0;
        gW.msgString = op.c % 11 == 0 ? -1 : int32_t(op.b % 4);
        gW.msg8 = (op.d & 3) ? 1 : 0x100;
        gW.msg9 = (op.d >> 2) % 3 == 0 ? 1 : 0;
        gW.sourceQi = (op.d >> 4) % 4 != 0;
        gW.sourceTarget = (op.d >> 6) % 7;
        if (gW.msgQi && gW.msgString >= 0) {
            const char *text = kNameText[gW.msgString];
            if (type == 0xc5 && (!NameIsIn(f->mGroupObjects, text) || !NameIsIn(f->mGroupIds, text))) return kSkipped;
            if (type == 0xc4 && NameIsCurrent(f, text)) return kSkipped;
        }
        if (type == 0xb5 && f->mGlobals && !PortalsCover(f, *f->mGlobals)) return kSkipped;
        const bool nullMessage = op.c % 13 == 0;
        return CallSlot<uint8_t>(face8, 3, nullMessage ? nullptr : static_cast<void *>(&gW.inMsg));
    }
    default: break;
    }
    if (op.code >= oFace0 && op.code < oQi) {
        const int slot = op.code - oFace0;
        Fake *o = Obj(op.a);
        switch (slot) {
        case 3: {
            void *object = o;
            return CallSlot(face0, 3, static_cast<void *>(&gW.wstream), &object), 0;
        }
        case 4: {
            gW.streamIndex = int16_t(op.b % 10 == 9 ? -1 : op.b % 10);
            gW.streamId = op.c % 2 ? gW.obj[op.c % kObjects].ret[28] : op.c;
            void *out = reinterpret_cast<void *>(0x4321);
            CallSlot(face0, 4, static_cast<void *>(&gW.rstream), &out);
            return Map(P(out));
        }
        case 7:
            if (!o || o->ret[67] == 0) return kSkipped; // AttachObjectResource reads the part's slot 10 untested (0x4d5531)
            return CallSlot(face0, 7, static_cast<void *>(o)), 0;
        default:
            if (!o) return kSkipped;
            return CallSlot(face0, slot, static_cast<void *>(o)), 0;
        }
    }
    const int slot = op.code - oFace4;
    Fake *o = Obj(op.a);
    switch (slot) {
    case 3: case 6: case 18: return Map(CallSlot(face4, slot)); // 18: GetLotObject 0x4d3360
    case 4:
    case 5:
        // Shutdown: ClearObjects detaches the lot object untested (0x4d2067) and indexes the portals (0x4d2210).
        if (slot == 5 && (!PortalsCover(f, f->mObjects) || !f->mLotObject.Get())) return kSkipped;
        CallSlot(face4, slot);
        if (slot == 4 && f->mPortals.empty()) // the portals come back with the lot
            for (int i = 0; i < 2; ++i) f->mPortals.push_back(FolderRef(&gW.obj[26 + i]));
        return 0;
    case 7:
        if (f->mLoaded && !f->mGlobals) return kSkipped;
        return CallSlot(face4, 7);
    case 8: { // WriteObjectRef through the folder face's thunk 0x4d716c
        void *object = o;
        return CallSlot(face4, 8, static_cast<void *>(&gW.wstream), &object), 0;
    }
    case 9: { // ReadObjectRef through the folder face's thunk 0x4d5356
        gW.streamIndex = int16_t(op.b % 10 == 9 ? -1 : op.b % 10);
        gW.streamId = op.c % 2 ? gW.obj[op.c % kObjects].ret[28] : op.c;
        void *out = reinterpret_cast<void *>(0x4321);
        CallSlot(face4, 9, static_cast<void *>(&gW.rstream), &out);
        return Map(P(out));
    }
    case 35: return CallSlot<uint8_t>(face4, 35); // AnyLotScriptBusy 0x4d3c80
    case 24: case 30: case 31: return CallSlot(face4, slot), 0;
    // GetObjectCount 0x4d09c0, GetAllObjectCount 0x4d09e0, GetPortalCount 0x4d0a10
    case 10: case 11: case 12: case 56: return Map(CallSlot(face4, slot));
    // GetObjectAt 0x4d3570, FindObjectBySaveIndex 0x4d3910, GetPortalAt 0x4d3b30
    case 13: case 17: case 19: return Map(CallSlot(face4, slot, op.a));
    case 14: return Map(CallSlot(face4, 14, op.a)); // FindObject 0x4d37c0
    case 15: return Map(CallSlot(face4, 15, op.b % 2 ? P(&gW.beh[op.b % kObjects]) : op.a)); // FindObjectByObjectId 0x4d3710
    case 16: return Map(CallSlot(face4, 16, gW.objName[op.b % kObjects])); // FindObjectByName 0x4d3870
    case 20: return Map(CallSlot(face4, 20, static_cast<void *>(o), int32_t(op.b) - 1, int32_t(op.c), int32_t(op.d)));
    case 21: case 22: return Map(CallSlot(face4, slot, static_cast<void *>(o)));
    case 23: return Map(CallSlot(face4, 23, static_cast<void *>(o), int32_t(op.b)));
    case 25: {
        FolderObjectSet set;
        const uint32_t ok = CallSlot<uint8_t>(face4, 25, op.a, op.b ? &set : nullptr, uint32_t(op.c));
        SetToWord(set, step);
        for (void *p : set) static_cast<Fake *>(p)->refs += 0; // the set holds plain pointers
        return ok;
    }
    case 26: {
        FolderObjectSet set;
        void *package = op.a < kSrcPkgs ? &gW.srcPkg[op.a] : nullptr;
        const uint32_t ok = CallSlot<uint8_t>(face4, 26, package, op.b ? &set : nullptr);
        SetToWord(set, step);
        return ok;
    }
    case 27:
        if (!o) return kSkipped;
        if (anyFlag && !Contains(f->mObjects, o) && !f->mGlobals) return kSkipped;
        if (int32_t(o->ret[73]) >= 0 && uint32_t(int32_t(o->ret[73])) >= f->mPortals.size()) return kSkipped;
        return CallSlot<uint8_t>(face4, 27, static_cast<void *>(o));
    case 28: case 29: return CallSlot(face4, slot, static_cast<void *>(o)), 0;
    case 32: case 33:
        if (f->mLoaded && !f->mGlobals) return kSkipped;
        return CallSlot(face4, slot, static_cast<void *>(&gW.srcPkg[op.a % kSrcPkgs])), 0;
    case 34:
        if (!f->mGlobals) return kSkipped;
        return CallSlot(face4, 34), 0;
    case 36: return Map(CallSlot(face4, 36, int32_t(op.a) - 1)); // FindLotScript 0x4d3530
    // IsObject39To41 0x4d40f0, IsObject44Or45 0x4d4130, IsObject46 0x4d4180, IsObject48 0x4d4160
    case 43: case 49: case 50: case 51: {
        // an object or one of the special objects
        void *arg = o;
        if (op.a % 3 == 0) arg = CallSlot<void *>(face4, slot == 43 ? 39 + int(op.a / 3) % 3 : slot == 49 ? 44 + int(op.a / 3) % 2 : slot == 50 ? 46 : 48);
        return CallSlot<uint8_t>(face4, slot, arg);
    }
    // FindObject37 0x4d3b80, FindObject38 0x4d36e0 (slots 38 and 42), FindObject39 0x4d3b60, FindObject40 0x4d3340,
    // FindObject41 0x4d3330, FindObject44 0x4d3b70, FindObject45 0x4d3700, FindObject46 0x4d36f0, FindObject47 0x4d32f0,
    // FindObject48 0x4d3370
    case 37: case 38: case 39: case 40: case 41: case 42: case 44: case 45: case 46: case 47: case 48:
        return Map(CallSlot(face4, slot));
    case 52: {
        void *out = nullptr;
        const uint32_t iids[] = {0x3b2271bb, 0xabcd};
        const uint32_t ok = CallSlot<uint8_t>(face4, 52, iids[op.a % 2], &out, op.b % 5, static_cast<void *>(op.b % 3 ? &gW.obj[24] : &gW.obj[op.b % kLoadable]),
                                              gNames[5].s.AsInterface(), gNames[4].s.AsInterface(),
                                              op.c % 3 ? nullptr : gNames[op.c % 4].s.AsInterface(), uint32_t(op.d & 1),
                                              uint32_t((op.d >> 1) & 1), (op.d >> 2) % 3 == 0 ? 0u : gW.owner + 7);
        const uint32_t word = ok | (Map(P(out)) << 8);
        if (out) CallSlot(out, 2);
        return word;
    }
    case 53: return CallSlot<uint8_t>(face4, 53, static_cast<void *>(o), gNames[op.b % kNames].s.AsInterface());
    case 54: return CallSlot(face4, 54, static_cast<void *>(&gW.list)), 0;
    case 55: {
        FolderObjectSet set;
        set.insert(&gW.edit); // cleared first
        CallSlot(face4, 55, int32_t(op.a), &set);
        return SetToWord(set, step);
    }
    case 57: {
        FolderPackageInfos out;
        if (op.b) {
            FolderPackageInfo stale;
            stale.mName = "stale";
            out.push_back(stale);
        }
        CallSlot(face4, 57, &out);
        step.push_back(uint32_t(out.size()));
        for (const auto &info : out) {
            step.insert(step.end(), {TextHash(info.mName.c_str()), info.mFlag, uint32_t(info.mEntries.size())});
            for (const auto &list : info.mLists) step.push_back(uint32_t(list.size()));
        }
        return 0;
    }
    default: return Map(CallSlot(face4, slot, static_cast<void *>(o)));
    }
}

void *MakeSide(difftest::Context &ctx, int side)
{
    return side == 0 ? ctx.Original<void *(__cdecl *)()>(0x4d3280)() : static_cast<void *>(cTSObjectFolder::Create());
}

struct RunResult {
    std::vector<Blob> steps;
};

RunResult Run(difftest::Context &ctx, int side, uint32_t seed, const std::vector<Op> &ops)
{
    BuildWorld(seed);
    gLog.clear();
    gFailedSlot = false;
    RunResult out;
    void *face4 = MakeSide(ctx, side);
    auto *folder = static_cast<cTSObjectFolder *>(static_cast<cITSObjectFolder *>(face4));
    gFolder = folder;
    int refs = 1;
    {
        Blob first;
        first.push_back(CallSlot(face4, 1));
        Dump(first, folder);
        out.steps.push_back(std::move(first));
    }
    for (const Op &op : ops) {
        char mark[96];
        std::snprintf(mark, sizeof mark, "%s seed %u step %u op %d (%u %u)", side ? "ours" : "original", seed,
                      unsigned(out.steps.size()), op.code, op.a, op.b);
        ctx.Mark(mark);
        static const bool trace = GetEnvironmentVariableA("FOLDER_TRACE", nullptr, 0) != 0;
        if (trace) std::fprintf(stderr, "%s\n", mark);
        gLog.clear();
        Blob step;
        step.insert(step.end(), {0x0Fu, uint32_t(op.code), op.a, op.b, op.c, op.d});
        const uint32_t result = RunOp(face4, op, refs, step);
        step.push_back(result);
        step.insert(step.end(), gLog.begin(), gLog.end());
        Dump(step, folder);
        DumpCounts(step);
        out.steps.push_back(std::move(step));
        if (gFailedSlot) break;
    }
    // Release down to zero: the deleting destructor frees the folder and releases every reference.
    gLog.clear();
    Blob last;
    while (refs > 0) {
        last.push_back(CallSlot(face4, 2));
        --refs;
    }
    last.insert(last.end(), gLog.begin(), gLog.end());
    DumpCounts(last);
    out.steps.push_back(std::move(last));
    gFolder = nullptr;
    return out;
}

void Compare(difftest::Context &ctx, const RunResult &a, const RunResult &b, uint32_t seed, const std::vector<Op> &ops)
{
    ++ctx.cases;
    for (size_t i = 0; i < a.steps.size() && i < b.steps.size(); ++i) {
        if (a.steps[i] == b.steps[i]) continue;
        size_t at = 0;
        while (at < a.steps[i].size() && at < b.steps[i].size() && a.steps[i][at] == b.steps[i][at]) ++at;
        if (GetEnvironmentVariableA("FOLDER_DIFF", nullptr, 0)) {
            for (int s = 0; s < 2; ++s) {
                const Blob &x = s ? b.steps[i] : a.steps[i];
                std::fprintf(stderr, "seed %u step %u %s:", seed, unsigned(i), s ? "ours" : "orig");
                for (size_t k = at > 30 ? at - 30 : 0; k < at + 30 && k < x.size(); ++k)
                    std::fprintf(stderr, k == at ? " [%08x]" : " %08x", x[k]);
                std::fprintf(stderr, "\n");
            }
        }
        const int code = i >= 1 && i - 1 < ops.size() ? ops[i - 1].code : -1;
        ctx.Fail("seed %u step %u (op %d): differ at word %u (sizes %u/%u): %08x vs %08x", seed, unsigned(i), code,
                 unsigned(at), unsigned(a.steps[i].size()), unsigned(b.steps[i].size()),
                 at < a.steps[i].size() ? a.steps[i][at] : 0, at < b.steps[i].size() ? b.steps[i][at] : 0);
        return;
    }
    if (a.steps.size() != b.steps.size()) ctx.Fail("seed %u: step counts differ", seed);
}

void RunSeeds(difftest::Context &ctx, uint32_t first, uint32_t count, int ops)
{
    Fixture fixture(ctx);
    for (uint32_t seed = first; seed < first + count; ++seed) {
        BuildWorld(seed);
        const std::vector<Op> list = MakeOps(seed, ops);
        char mark[64];
        std::snprintf(mark, sizeof(mark), "original: seed %u", seed);
        ctx.Mark(mark);
        const RunResult original = Run(ctx, 0, seed, list);
        std::snprintf(mark, sizeof(mark), "ours: seed %u", seed);
        ctx.Mark(mark);
        const RunResult ours = Run(ctx, 1, seed, list);
        Compare(ctx, original, ours, seed, list);
        if (ctx.failures.size() > 8) return;
    }
}

} // namespace ik
} // namespace

// Construction and the start/stop cycle: the factory 0x4d3280, the constructor 0x4cef20, QueryInterface 0x4d4bd0, Release
// 0x4d55e0 and the destructor 0x4cf710 / 0x4cfbf0, Init 0x4d3cb0 (LoadGlobals 0x4d64f0 and the message registration),
// Shutdown 0x4d1ea0 (ReleaseLotScripts 0x4d5610, ClearObjects 0x4d1f90, DetachObjectFully 0x4d2210), the neighbourhood
// messages (DoMessage 0x4d22b0) and the queries around them.
DIFF_TEST(ObjectFolder_Cycle, 0x4d3cb0) { ik::RunSeeds(ctx, 1, 120, 12); }

// Random sequences of every slot of the three faces after Init: the finders and counters, the multi-tile finders 0x4d3b90 /
// 0x4d3380 / 0x4d3450 / 0x4d35c0, the loaders 0x4d41a0 / 0x4d43a0, RemoveObject 0x4d5680, the resource passes 0x4d6ee0 /
// 0x4d5b70 / 0x4d6d40 / 0x4d5930, the save table 0x4d46d0 / 0x4d49a0 / 0x4d4670, the import 0x4d0a20, AddLotObjectCopy
// 0x4d18d0, CollectTagged 0x4d4560, CollectByAttribute 0x4d3a00, GetPackageInfos 0x4d3300, the behaviour finder's slots
// (0x4d7130, 0x4d5240, 0x4d21e0, 0x4d5430, 0x4d5360, 0x4d6c70) and every message type.
DIFF_TEST(ObjectFolder_Slots, 0x4d22b0) { ik::RunSeeds(ctx, 1000, 200, 70); }
