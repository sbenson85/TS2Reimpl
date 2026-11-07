// Differential test for the common system service (lane LC): cTSCommonSystem (src/ts/cTSCommonSystem.h), in the standalone
// shape.
//
// CommonSystem_Run: a service built by the original factory (0xcb9dd0) or constructor (0xcb9330, on 0xc5 memory) against one
// built by ours (Create, or placement new), each driven through its own vtables by the same random sequence of operations,
// then destroyed by the last Release (the deleting destructor 0xcb9620 behind the +4 thunk 0xcb960e; the destructor
// 0xcb93f0). Operations: Init (0xcba7c0; through it RegisterExpressionFunctions 0xcbb420 and the file system's directory
// 0x9c5ae0), Shutdown (0xcbb560; through it UnregisterExpressionFunctions 0xcbb610 and the settings' assignment 0xcb94a0),
// OnTick (0xcbae10), QueryInterface (0xcbaed0; thunks 0xcbaf01 (+4), 0xcbaf09 (+0x1c), 0xcbaf11 (+0x20)) / AddRef / Release
// on every face, the base service's accessors, DoMessage (0xcb9d00) and every cITSCommonSystem slot: the getters,
// CollectTextureKeys (0xcb9ee0), NextId (0xcba330), IsValidId (0xcbadd0), ReadAppDword (0xcbb390), ReadAppString (0xcbb3e0),
// ReadAppStringAny (0xcbb400), WriteAppString (0xcbb7a0), ReadProductDword (0xcbaf20), ReadProductString (0xcbb060),
// ReadProductStringAny (0xcbb1f0), WriteProductString (0xcbb650), GetRegionCode (0xcba340), GetRegion (0xcba460),
// GetContentVersions (0xcba140), GetUserGuid (0xcba610; through it NewGuid 0xcb9e40).
//
// Stand-ins on both sides (ours too): the products' registry key 0xcb4690 and the app's product 0xcb4950 (seeded answers, every length
// around the subkey bound), the scenegraph walker 0xae8090 (fills the cell vector), the expression-function registry getter
// 0xae8980 (a registry fake behind a virtual base), the renderer hand-on 0xb3bbd0, the file system getter 0x9c59d9 and
// cRZFile::PathExists 0x1c1242 (ours too). The registry functions and CoCreateGuid are import fakes (a registry the writes
// change). Every object the code reaches is a recording fake (tests/ts_services_fixture.h): the framework (its services:
// the settings property set, the notifier, Init's holder, the content registry), the COM director (the four objects), the
// effects manager, the camera controllers, the render targets, the string pool, the app's common implementation (the
// effects directories), the name cells' cached object, the walked object and node, the messages. Compared after every
// operation: the result (the low byte where the original returns a bool in al), the outputs, the event log (strings by
// content), every field, the two collision statics, every fake's count and the stack balance of every slot call.
//
// Not driven (ORIGINAL-BUG paths that crash both): Init without a holder or notifier, Init without a string pool, OnTick
// without settings, GetContentVersions with 0x10000 entries or more (never ends), GetUserGuid's text with eight or more
// groups of eight digits (the original's inlined parse writes past its frame), a string value of no bytes (the reads append
// size - 1 = 0xffffffff characters: the string throws length_error on both sides; WriteProductString of an empty text makes
// one, as it writes the text without its terminator, so the test writes only non-empty texts).
#include "app/ProductInfo.h"
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZAny.h" // GZString
#include "rz/StaticGetters2.h" // ServiceCell
#include "misc/UtilitiesE.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSCatalog.h" // GZStringList
#include "ts/cTSCommonSystem.h"
#include "ts/cTSCommonSystemObjects.h"
#include "ts/cTSCustomContentSupport.h" // ContentString

#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace cs_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSCommonSystem;

constexpr uint32_t kTag = 0x7e000000;     // the service under test
constexpr uint32_t kTextTag = 0x7e300000; // the test's strings and buffers
constexpr uint32_t kStaticTag = 0x7e500000; // the game statics 0x1087510..0x1087523
constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kCommonImplCell = 0x10cc040;
constexpr uint32_t kCellsCache = 0x10f9070;   // GetCachedObjectB (the name cells' object)
constexpr uint32_t kContentCache = 0x10faf80; // the content registry's cached getter
constexpr uint32_t kStatics = 0x1087510;

enum : unsigned {
    kSvc = 0,
    kCom,
    kHolder,
    kHolderObj,
    kSettings,
    kNotifier,
    kCamera,
    kEffects,
    kTargets,
    kPool,
    kExpr,
    kFs,
    kCells,
    kCommon,
    kContent,
    kObject,
    kNode,
    kMsg,
    kMsg2,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte
bool gOurs;
difftest::Context *gCtx;

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
void LogText(const char *text, size_t n)
{
    Ev(uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
}
void LogChars(const void *at)
{
    const char *text = static_cast<const char *>(at);
    if (!text) {
        Ev(0xdead0000);
        return;
    }
    LogText(text, std::strlen(text));
}
void LogChars(uint32_t at) { LogChars(reinterpret_cast<const char *>(uintptr_t(at))); }
void LogRz(uint32_t at)
{
    const std::string &s = reinterpret_cast<const cRZString *>(uintptr_t(at))->mString;
    LogText(s.data(), s.size());
}
void LogGz(uint32_t at)
{
    const GZString &s = *reinterpret_cast<const GZString *>(uintptr_t(at));
    LogText(s.data(), s.size());
}
void Call(Obj *self, unsigned slot)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(slot);
}
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Rva(uint32_t address) { return address ? address - P(GameAddress<void>(0)) : 0; }
uint32_t Sum(const void *data, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum = sum * 31 + static_cast<const uint8_t *>(data)[i];
    return sum;
}

// ---- the world for a seed ----

const char *const kProducts[] = {"Sims2.exe", "Sims2EP9.exe", "Other.exe", "", nullptr};
constexpr uint32_t kProductCount = 5;
uint32_t ProductIndex(const char *product)
{
    if (!product) return 4;
    for (uint32_t i = 0; i < 4; ++i)
        if (std::strcmp(product, kProducts[i]) == 0) return i;
    return 3;
}
// Subkeys: none, empty, around the bound (lookup lengths 0x100 / 0x101 below), deeper, long (wraps with 0xfffffff0).
const std::string kLong(0x60, 'L');
const char *const kSubkeys[] = {nullptr, "", "\\a", "\\ab", "\\Sub", "\\x\\y", kLong.c_str()};
constexpr uint32_t kSubkeyCount = 7;
const uint32_t kLengthModes[] = {0xffffffff /* the key's */, 0, 0x100, 0x101, 0x102, 0x103, 0xfffffff0};
const char *const kNames[] = {"Region", "User", "Value", "Num", "", nullptr, "Missing"};
constexpr uint32_t kNameCount = 7;
const char kFallback[] = "Software\\EA Games\\The Sims 2";

const char *const kRegions[] = {"14917",   "MX08004661", "14917EKRTCCD", "14917SCCD",   "EMW-4164", "14917TH", "14917HU",
                                "MXHG8004261", "MXHG7704637", "14726",   "mx08004661", "14917X",      "",        "1491"};
constexpr uint32_t kRegionCount = 14;
// GUID texts up to seven groups of eight digits (see the file comment).
const char *const kGuids[] = {"{01234567-89ab-cdef-0123-456789abcdef}",
                              "0123456789ABCDEF0123456789abcdef",
                              "",
                              "zz",
                              "1-2-3",
                              "deadbeef",
                              "11111111-22222222-33333333-44444444-55555555-66666666-77777777",
                              "{12345678-9abc-def0-1234-56789abcdef0",
                              "g1234567-89ab"};
constexpr uint32_t kGuidCount = 9;
// Texts: values written, outputs' starting contents.
const char *const kTexts[] = {"", "pre", "a value", "{01234567-89ab-cdef-0123-456789abcdef}", "14917", "x\ty"};
constexpr uint32_t kTextCount = 6;
const char *const kDirs[] = {"C:\\Fx\\", "", "D:\\Other Effects\\", "relative"};
const char *const kFsDirs[] = {"C:\\Game\\Res\\", "", "x"};
const char *const kCellNames[] = {"wall", "", "a_b", "Floor-Tile 3", "averyveryverylongnameofatextureforthetest"};
const uint32_t kCounters[] = {0, 1, 2, 0xfffffffe, 0xffffffff, 0x80000000};
const uint32_t kContentCounts[] = {0, 1, 2, 3, 4, 7, 12};
const uint32_t kVersionIds[] = {1, 0, 0xffffffff, 7, 123456};
const uint16_t kHalves[] = {0, 1, 0x8000, 0xffff, 12, 0x7fff};

struct RegValue {
    uint32_t type;
    std::string bytes;
};
using RegKey = std::map<std::string, RegValue>;
std::map<std::string, RegKey> gReg;
std::vector<std::string> gHandles;

struct World {
    bool service;       // the framework service cell is set (else Init and OnTick are not driven)
    bool settings;      // the framework answers the settings
    bool camera, effects, targets;
    bool content;       // the content registry
    uint32_t contentCount;
    uint32_t salt;
    uint32_t found;     // products the lookup knows (bits)
    uint32_t partial;   // what the lookup leaves when it doesn't know one
    uint32_t lengthMode;
    uint32_t appProduct; // 0xcb4950: nothing, Sims2EP9.exe, Other.exe (false)
    bool openFail, createFail, setFail, guidFail;
    long guidFailCode; // E_FAIL or S_FALSE: a nonzero success code also counts as failure (the original tests != 0)
    uint8_t guid[16];
    uint32_t dirs, exists, fsDir, cells, cellBase;
    uint8_t floor, wall;   // the settings' values
};
World gWorld;

void AddValue(RegKey &key, std::mt19937 &rng, const char *name)
{
    if (rng() % 3 == 0) return;
    RegValue v;
    const uint32_t pick = rng() % 10;
    if (pick < 5) {
        v.type = 1;
        const char *text = std::strcmp(name, "Region") == 0 ? kRegions[rng() % kRegionCount]
                           : std::strcmp(name, "User") == 0 ? kGuids[rng() % kGuidCount]
                                                            : kTexts[rng() % kTextCount];
        v.bytes.assign(text, std::strlen(text) + 1);
    } else if (pick < 8) {
        v.type = 4;
        const uint32_t value = rng() % 2 ? rng() : rng() % 4;
        v.bytes.assign(reinterpret_cast<const char *>(&value), rng() % 6 ? 4 : 2);
    } else {
        v.type = pick == 8 ? 2 : 3; // REG_EXPAND_SZ, REG_BINARY
        const char *text = kTexts[rng() % kTextCount];
        v.bytes.assign(text, std::strlen(text) + 1);
    }
    key[name] = v;
}

void BuildRegistry(std::mt19937 &rng)
{
    gReg.clear();
    gHandles.clear();
    std::vector<std::string> bases;
    for (uint32_t p = 0; p < 4; ++p) bases.push_back(std::string("Software\\EA\\") + kProducts[p]);
    bases.push_back(kFallback);
    bases.push_back("Partial");
    bases.push_back("");
    for (const std::string &base : bases) {
        for (uint32_t s = 0; s < kSubkeyCount; ++s) {
            if (rng() % 3 == 0) continue;
            const std::string path = base + (kSubkeys[s] ? kSubkeys[s] : "");
            RegKey &key = gReg[path];
            for (uint32_t n = 0; n < kNameCount; ++n)
                if (kNames[n]) AddValue(key, rng, kNames[n]);
        }
    }
}

// ---- fakes ----

uint32_t SvcQuery(Obj *, uint32_t *a)
{
    const uint32_t id = a[0], iid = a[1], out = a[2];
    if (id == 0x0ac1887e && iid == 0x4bb0d269) return gWorld.settings ? HandOut(out, F(kSettings)) | 0x100 : 0x100;
    if (id == 0x0ac1887e && iid == 0x6bb22f38) return HandOut(out, F(kNotifier));
    if (id == 0xe5074b6b && iid == 0x25074b4d) return HandOut(out, F(kHolder));
    if (gWorld.content && id == 0x0dc7057a) return HandOut(out, F(kContent));
    return 0x200;
}

// The COM director: 3 GetClassObject.
uint32_t ComClassObject(Obj *, uint32_t *a)
{
    const uint32_t cls = a[0], out = a[2];
    switch (cls) {
    case 0x6afa13be: return gWorld.camera ? HandOut(out, F(kCamera)) | 0x300 : 0x300;
    case 0x49822f75: return gWorld.effects ? HandOut(out, F(kEffects)) | 0x300 : 0x300;
    case 0xed0e4590: return gWorld.targets ? HandOut(out, F(kTargets)) | 0x300 : 0x300;
    case 0xf074e038: return HandOut(out, F(kPool)) | 0x300;
    default: return 0x300;
    }
}

// The settings: 9 GetPropertyValue (the static's value byte written).
uint32_t __fastcall SettingsGet(Obj *self, void *, uint32_t key, uint32_t out)
{
    Call(self, 9);
    LogGz(key);
    Ev(svcfx::Norm(out));
    const GZString &name = *reinterpret_cast<const GZString *>(uintptr_t(key));
    reinterpret_cast<uint8_t *>(uintptr_t(out))[4] = name == "floorCollisions" ? gWorld.floor : gWorld.wall;
    return Bool();
}

// The notifier: 4 Watch (target, id, name, default value).
uint32_t __fastcall NotifierWatch(Obj *self, void *, uint32_t target, uint32_t id, uint32_t name, uint32_t any)
{
    Call(self, 4);
    Ev(svcfx::Norm(target));
    Ev(id);
    LogGz(name);
    Ev(svcfx::Norm(any) == 0x5ac00000);
    using GetType = uint32_t(__thiscall *)(uint32_t);
    Ev((*reinterpret_cast<GetType **>(uintptr_t(any)))[4](any));
    Ev(reinterpret_cast<const uint8_t *>(uintptr_t(any))[4]);
    return 0;
}

// The effects manager: 6 AddPaths, 27 / 29 SetNamedA / SetNamedB (the object's vtables and count logged, then freed).
uint32_t __fastcall EffectsAddPaths(Obj *self, void *, uint32_t first, uint32_t second)
{
    Call(self, 6);
    LogChars(first);
    LogChars(second);
    return 0;
}
// The objects Init and RegisterExpressionFunctions make (lane LF): ours carry our vtables, the original's the game's. Our
// classes' vtable words (read from objects our classes' constructors build) are paired with the game's vtables of the same
// class and face, and a vtable word is logged as that game RVA on both sides (the vtable words are the bytes the standalone
// shape normalises; a wrong class still logs another class's RVA).
std::vector<std::pair<uint32_t, uint32_t>> gOurVTables;
template <class T> void AddOurVTables(uint32_t primary, uint32_t second)
{
    T *object = new T;
    const auto *words = reinterpret_cast<const uint32_t *>(object);
    gOurVTables.push_back({words[0], primary});
    gOurVTables.push_back({words[1], second});
    delete object;
}
void CollectOurVTables()
{
    using namespace common_system;
    gOurVTables.clear();
    AddOurVTables<cIsDayFunction>(0xeb7cec, 0xeb7cfc);
    AddOurVTables<cIsLightMaterialFunction>(0xeb7d44, 0xeb7d54);
    AddOurVTables<cPinToFloorConstraint>(0xeb7bb0, 0xeb7bc4);
    AddOurVTables<cPinToTopsConstraint>(0xeb7be0, 0xeb7bf4);
    AddOurVTables<cWallsAndFloorsCollider>(0xeb7c2c, 0xeb7c50);
    AddOurVTables<cWallsCollider>(0xeb7cac, 0xeb7cd0);
    AddOurVTables<cFloorsCollider>(0xeb7c6c, 0xeb7c90);
}
uint32_t VtRva(uint32_t word)
{
    for (const auto &pair : gOurVTables)
        if (pair.first == word) return pair.second;
    return Rva(word);
}

void LogNamed(Obj *self, unsigned slot, uint32_t name, uint32_t object)
{
    Call(self, slot);
    LogChars(name);
    if (!object) {
        Ev(0);
        return;
    }
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(object));
    Ev(VtRva(words[0]));
    Ev(VtRva(words[1]));
    Ev(words[2]);
    ::operator delete(reinterpret_cast<void *>(uintptr_t(object)));
}
uint32_t __fastcall EffectsNamedA(Obj *self, void *, uint32_t name, uint32_t object)
{
    LogNamed(self, 27, name, object);
    return 0;
}
uint32_t __fastcall EffectsNamedB(Obj *self, void *, uint32_t name, uint32_t object)
{
    LogNamed(self, 29, name, object);
    return 0;
}
uint32_t gRendererBlob[4];

// The expression registry: 7 Register (the function's face: its vtables, its count; held), 5 Unregister (by name).
std::vector<uint32_t> gHeldFaces;
uint32_t __fastcall ExprRegister(Obj *self, void *, uint32_t face)
{
    Call(self, 7);
    if (!face) {
        Ev(0);
        return 0;
    }
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(face));
    Ev(VtRva(words[-1]));
    Ev(VtRva(words[0]));
    Ev(words[1]);
    using AddRefFn = uint32_t(__thiscall *)(uint32_t);
    (*reinterpret_cast<AddRefFn **>(uintptr_t(face)))[1](face);
    gHeldFaces.push_back(face);
    return 0;
}
uint32_t __fastcall ExprUnregister(Obj *self, void *, uint32_t name)
{
    Call(self, 5);
    LogChars(name);
    return 0;
}
// 0xae8980's result: an object whose virtual base (table at +4, entry 2) is the registry fake.
struct ExprRegistryShell {
    uint32_t pad;
    const int32_t *vbptr;
} gShell;
int32_t gVbTable[3];

// The app's common implementation: 5 (the effects directories into the list).
uint32_t __fastcall CommonSlot5(Obj *self, void *, uint32_t option, uint32_t list, uint32_t a3, uint32_t a4, uint32_t a5)
{
    Call(self, 5);
    Ev(option);
    Ev(svcfx::Norm(list) == 0x5ac00000);
    Ev(a3);
    Ev(a4);
    Ev(a5);
    auto *dirs = reinterpret_cast<GZStringList *>(uintptr_t(list));
    for (uint32_t i = 0; i < gWorld.dirs; ++i) dirs->push_back(GZString(kDirs[i]));
    return Bool();
}

// The name cells' object: 8 the name of a cell value, 7 the cell's release.
uint32_t CellName(Obj *, uint32_t *a) { return P(kCellNames[a[0] % 5]); }

// The content registry: 11 GetEntryVersion (index as a half-word; the record's state before logged).
uint32_t __fastcall ContentVersion(Obj *self, void *, uint32_t index, uint32_t record)
{
    Call(self, 11);
    const uint32_t i = index & 0xffff;
    Ev(i);
    Ev(svcfx::Norm(record) == 0x5ac00000);
    auto *words = reinterpret_cast<uint32_t *>(uintptr_t(record));
    Ev(words[0]);
    Ev(words[1]);
    const bool ok = (i * 7 + gWorld.salt) % 3 != 0;
    if (ok && i % 5 != 4) {
        words[0] = kVersionIds[(i + gWorld.salt) % 5];
        auto *halves = reinterpret_cast<uint16_t *>(words + 1);
        halves[0] = kHalves[(i * 3 + gWorld.salt) % 6];
        halves[1] = kHalves[(i * 5 + gWorld.salt + 1) % 6];
    }
    return 0x100u | ok;
}

// The walked object: 0 QueryInterface (mode: 0 fails, 1 the node, 2 fails but sets it).
uint32_t gNodeMode;
uint32_t ObjectQuery(Obj *, uint32_t *a)
{
    if (gNodeMode == 0) return 0x400;
    HandOut(a[1], F(kNode));
    return gNodeMode == 1 ? 0x401 : 0x400;
}

// The message: 0 QueryInterface (the same modes), the standard message's 4 / 13 / 15.
uint32_t gMsgMode;
uint8_t gAnyBuf[8];
uint32_t MsgQuery(Obj *, uint32_t *a)
{
    if (gMsgMode == 0) return 0x400;
    HandOut(a[1], F(kMsg2));
    return gMsgMode == 1 ? 0x401 : 0x400;
}
uint32_t BoolResult(Obj *, uint32_t *) { return Bool(); }

template <class Fn> void Shim(Obj &obj, unsigned slot, unsigned args, Fn fn)
{
    obj.Set(slot, args);
    obj.table[slot] = reinterpret_cast<void *>(fn);
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11);
    svcfx::ResetAll();
    gHeldFaces.clear();
    gWorld.service = rng() % 8 != 0;
    gWorld.settings = rng() % 5 != 0;
    gWorld.camera = rng() % 4 != 0;
    gWorld.effects = rng() % 4 != 0;
    gWorld.targets = rng() % 4 != 0;
    gWorld.content = rng() % 6 != 0;
    gWorld.contentCount = seed % 97 == 0 ? 0xffff : kContentCounts[rng() % 7];
    gWorld.salt = rng() % 15;
    gWorld.found = rng() % 32;
    gWorld.partial = rng() % 3;
    gWorld.lengthMode = rng() % 7;
    gWorld.appProduct = rng() % 3;
    gWorld.openFail = rng() % 12 == 0;
    gWorld.createFail = rng() % 6 == 0;
    gWorld.setFail = rng() % 6 == 0;
    gWorld.guidFail = rng() % 5 == 0;
    gWorld.guidFailCode = rng() % 2 ? long(0x80004005) : 1;
    for (uint8_t &b : gWorld.guid) b = uint8_t(rng());
    gWorld.dirs = rng() % 5;
    gWorld.exists = rng() % 16;
    gWorld.fsDir = rng() % 3;
    gWorld.cells = rng() % 5;
    gWorld.cellBase = rng() % 2 ? 0x100 : 0xfffffffe;
    gWorld.floor = uint8_t(rng() % 3);
    gWorld.wall = uint8_t(rng() % 3);
    BuildRegistry(rng);

    svcfx::gFrameworkIndex = kCom;
    F(kSvc).Set(5, "ddp", 0, &SvcQuery);
    F(kCom).Set(3, "ddp", 0, &ComClassObject);
    F(kHolder).Set(4, 0, P(&F(kHolderObj)));
    F(kHolderObj).Set(0, "d");
    Shim(F(kSettings), 9, 2, &SettingsGet);
    Shim(F(kNotifier), 4, 4, &NotifierWatch);
    F(kCamera).Set(15, 0, 0x1234);
    F(kCamera).Set(16, 0, 0x2345);
    Obj &effects = F(kEffects);
    effects.Set(3, 0, 0, &BoolResult);
    effects.Set(4, 0, 0, &BoolResult);
    Shim(effects, 6, 2, &EffectsAddPaths);
    Shim(effects, 27, 2, &EffectsNamedA);
    Shim(effects, 29, 2, &EffectsNamedB);
    effects.Set(33, 0, P(gRendererBlob));
    F(kTargets).Set(3, 0, 0x3456);
    F(kTargets).Set(4, 0, 0x4567);
    F(kPool).Set(4, "d", 0x5678);
    Shim(F(kExpr), 7, 1, &ExprRegister);
    Shim(F(kExpr), 5, 1, &ExprUnregister);
    F(kFs).Set(8, 0, P(kFsDirs[gWorld.fsDir]));
    F(kCells).Set(8, "d", 0, &CellName);
    F(kCells).Set(7, "d");
    F(kCells).Set(6, 1);
    Shim(F(kCommon), 5, 5, &CommonSlot5);
    F(kContent).Set(10, 0, gWorld.contentCount);
    Shim(F(kContent), 11, 2, &ContentVersion);
    F(kObject).Set(0, "dp", 0, &ObjectQuery);
    F(kMsg).Set(0, "dp", 0, &MsgQuery);
    F(kMsg2).Set(13, 0, 0, &BoolResult);
    F(kMsg2).Set(4, 0);
    F(kMsg2).Set(15, 0, P(gAnyBuf));

    gVbTable[0] = -4;
    gVbTable[1] = 0;
    gVbTable[2] = int32_t(P(&F(kExpr)) - P(&gShell.vbptr));
    gShell.vbptr = gVbTable;

    *GameAddress<void *>(kServiceCell) = gWorld.service ? &F(kSvc) : nullptr;
    *GameAddress<void *>(kCommonImplCell) = &F(kCommon);
    *GameAddress<void *>(kCellsCache) = &F(kCells);
    *GameAddress<void *>(kContentCache) = nullptr;
    // The collision statics: the game's AnyBoolean objects (vtable words left as the image has them).
    GameAddress<uint8_t>(kStatics)[8] = 0x5a;
    GameAddress<uint8_t>(kStatics)[0x10] = 0x5b;
}

// ---- stand-ins ----

// 0xcb4690 (cdecl): the product's key (when known: "Software\EA\<product>"; else what gWorld.partial says) and a length.
uint32_t __cdecl StandLookup(const char *product, char *key, uint32_t *length, char *dir, uint32_t *dirLength, uint32_t a,
                             uint32_t b, uint32_t c)
{
    Ev(0xcb4690);
    LogChars(product);
    Ev(svcfx::Norm(P(key)));
    Ev(Sum(key, 0x104)); // the caller's buffer, zeroed
    Ev(length ? *length : 0xdeadbeef);
    Ev(svcfx::Norm(P(dir)));
    Ev(svcfx::Norm(P(dirLength)));
    Ev(a);
    Ev(b);
    Ev(c);
    const uint32_t p = ProductIndex(product);
    const bool found = p < 4 && (gWorld.found >> p & 1);
    std::string text = found ? std::string("Software\\EA\\") + kProducts[p] : gWorld.partial == 1 ? "Partial" : "";
    if (!text.empty()) std::memcpy(key, text.c_str(), text.size() + 1);
    const uint32_t mode = kLengthModes[gWorld.lengthMode];
    *length = mode == 0xffffffff ? uint32_t(text.size()) : mode;
    return 0x5500u | found;
}

// 0xcb4950 (cdecl): the app's product into the buffer.
uint32_t __cdecl StandAppProduct(char *buffer)
{
    Ev(0xcb4950);
    Ev(svcfx::Norm(P(buffer)));
    Ev(uint8_t(buffer[0]));
    if (gWorld.appProduct == 1) std::strcpy(buffer, "Sims2EP9.exe");
    if (gWorld.appProduct == 2) std::strcpy(buffer, "Other.exe");
    return 0x5500u | (gWorld.appProduct == 1);
}

// 0xae8090 (cdecl): the walker's name cells into the vector.
void __cdecl StandWalk(std::vector<ServiceCell> *out, uint32_t node)
{
    Ev(0xae8090);
    Ev(svcfx::Norm(P(out)) == 0x5ac00000);
    Ev(svcfx::Norm(node));
    Ev(uint32_t(out->size()));
    for (uint32_t i = 0; i < gWorld.cells; ++i) out->push_back(ServiceCell{gWorld.cellBase + i});
}

void *__cdecl StandExprRegistry()
{
    Ev(0xae8980);
    return &gShell;
}

void __cdecl StandRenderer(uint32_t renderer)
{
    Ev(0xb3bbd0);
    Ev(svcfx::Norm(renderer));
}

void *__cdecl StandFileSystem()
{
    Ev(0x9c59d9);
    return &F(kFs);
}

// cRZFile::PathExists (0x1c1242, ours too): whether the directory exists (gWorld.exists by directory).
uint32_t __cdecl StandExists(const cRZString *path)
{
    Ev(0x1c1242);
    LogText(path->mString.data(), path->mString.size());
    for (uint32_t i = 0; i < 4; ++i)
        if (path->mString == kDirs[i]) return 0x5500u | (gWorld.exists >> i & 1);
    return 0x5500;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

// ---- the registry and CoCreateGuid ----

const RegKey *KeyOf(uint32_t handle)
{
    const uint32_t i = handle - 0x4b00;
    if (i >= gHandles.size()) return nullptr;
    const auto it = gReg.find(gHandles[i]);
    return it == gReg.end() ? nullptr : &it->second;
}
uint32_t NewHandle(const std::string &path)
{
    gHandles.push_back(path);
    return 0x4b00 + uint32_t(gHandles.size() - 1);
}

long __stdcall FakeOpen(uint32_t root, const char *sub, uint32_t options, uint32_t sam, uint32_t *out)
{
    Ev(0x4f50454e);
    Ev(root);
    LogChars(sub);
    Ev(options);
    Ev(sam);
    Ev(svcfx::Norm(P(out)) == 0x5ac00000);
    if (gWorld.openFail || !sub || !gReg.count(sub)) return 2;
    *out = NewHandle(sub);
    return 0;
}

long __stdcall FakeQuery(uint32_t handle, const char *name, uint32_t *reserved, uint32_t *type, uint8_t *data, uint32_t *size)
{
    Ev(0x51554552);
    Ev(handle);
    LogChars(name);
    Ev(P(reserved));
    Ev(type != nullptr); // its value going in is not read (ReadProductDword leaves it uninitialised)
    Ev(svcfx::Norm(P(data)) == 0x5ac00000);
    Ev(size ? *size : 0xdeadbeef);
    if (data && size && *size <= 0x200) Ev(Sum(data, *size));
    const RegKey *key = KeyOf(handle);
    if (!key) return 6;
    const auto it = key->find(name ? name : "");
    if (it == key->end()) return 2;
    const RegValue &v = it->second;
    if (type) *type = v.type;
    if (data && size) {
        if (*size < v.bytes.size()) {
            *size = uint32_t(v.bytes.size());
            return 234;
        }
        std::memcpy(data, v.bytes.data(), v.bytes.size());
    }
    if (size) *size = uint32_t(v.bytes.size());
    return 0;
}

long __stdcall FakeClose(uint32_t handle)
{
    Ev(0x434c4f53);
    Ev(handle);
    return 0;
}

long __stdcall FakeCreate(uint32_t root, const char *sub, uint32_t reserved, const char *cls, uint32_t options, uint32_t sam,
                          const void *security, uint32_t *out, uint32_t *disposition)
{
    Ev(0x43524541);
    Ev(root);
    LogChars(sub);
    Ev(reserved);
    Ev(P(cls));
    Ev(options);
    Ev(sam);
    Ev(P(security));
    Ev(svcfx::Norm(P(out)) == 0x5ac00000);
    Ev(svcfx::Norm(P(disposition)) == 0x5ac00000);
    if (gWorld.createFail || !sub) return 5;
    const bool existed = gReg.count(sub) != 0;
    gReg[sub];
    *out = NewHandle(sub);
    if (disposition) *disposition = existed ? 2 : 1;
    return 0;
}

long __stdcall FakeSet(uint32_t handle, const char *name, uint32_t reserved, uint32_t type, const uint8_t *data, uint32_t size)
{
    Ev(0x53455456);
    Ev(handle);
    LogChars(name);
    Ev(reserved);
    Ev(type);
    LogText(reinterpret_cast<const char *>(data), size);
    if (gWorld.setFail) return 5;
    const uint32_t i = handle - 0x4b00;
    if (i >= gHandles.size()) return 6;
    gReg[gHandles[i]][name ? name : ""] = RegValue{type, std::string(reinterpret_cast<const char *>(data), size)};
    return 0;
}

long __stdcall FakeGuid(uint8_t *out)
{
    Ev(0x47554944);
    Ev(svcfx::Norm(P(out)) == 0x5ac00000);
    if (gWorld.guidFail) return gWorld.guidFailCode;
    std::memcpy(out, gWorld.guid, 16);
    return 0;
}

struct Iat {
    void **slot;
    void *old;
    Iat(uint32_t rva, const void *fake) : slot(GameAddress<void *>(rva)), old(*slot) { *slot = const_cast<void *>(fake); }
    ~Iat() { *slot = old; }
    Iat(const Iat &) = delete;
    Iat &operator=(const Iat &) = delete;
};

// ---- driving ----

int32_t gStackSkew;
uint32_t CallChecked(void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    static_assert(sizeof(R) == 4, "dword result");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return R(CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A))));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

bool gFactory;
void *BuildService(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *object = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xcb9dd0)();
        b.push_back(object ? 1 : 0);
        if (object) svcfx::AddRange(object, Sys::kSize, kTag); // the constructor's words logged it as a heap block
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    svcfx::AddRange(memory, Sys::kSize, kTag); // before the constructor, so its words log pointers into it by the tag
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0xcb9330)(memory);
    b.push_back(r == memory);
    return memory;
}

std::unique_ptr<ContentString[]> gTexts;
ContentString *gOut;
CommonTextureKeys *gKeys;
uint32_t *gGuid; // four words

void DumpOut(Blob &b)
{
    b.push_back(uint32_t(gOut->s.mString.size()));
    for (char ch : gOut->s.mString) b.push_back(uint8_t(ch));
}

void DumpService(Blob &b, void *object)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[4][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kCommonVTable}, {0x20, Sys::kMessageVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(gOurs ? !game : game);
    }
    // +0x19..+0x1b (after the running flag) are never written: the 0xc5 fill, or heap leftovers from the factory.
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(off == 0x18 && gFactory ? m[0x18] : Word(m, off));
    for (uint32_t off = 0x24; off < 0x38; off += 4) b.push_back(svcfx::Norm(Word(m, off)));
    b.push_back(Word(m, 0x38));
    b.push_back(GameAddress<uint8_t>(kStatics)[8]);
    b.push_back(GameAddress<uint8_t>(kStatics)[0x10]);
}

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 20;
    if (rng() % 4) ops.push_back({0, uint32_t(rng()), uint32_t(rng())}); // usually Init first
    for (unsigned i = 0; i < n; ++i) ops.push_back({unsigned(rng() % 18), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

const char *Product(uint32_t a) { return kProducts[a % kProductCount]; }
const char *Subkey(uint32_t a) { return kSubkeys[a % kSubkeyCount]; }
const char *Name(uint32_t a) { return kNames[a % kNameCount]; }
// A value to write: never the empty text (see the file comment).
cIGZString *Text(uint32_t a) { return gTexts[1 + a % (kTextCount - 1)].Interface(); }
cIGZString *Out(uint32_t a)
{
    gOut->s.mString.assign(kTexts[a % 3 ? 0 : 1 + (a >> 2) % (kTextCount - 1)]);
    return gOut->Interface();
}

void ServiceOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const face = Face(object, 0x1c);
    void *const target = Face(object, 0x20);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: // Init (with the framework's services only: see the file comment)
        if (gWorld.service) b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        break;
    case 1: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 2: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kIids[] = {1, cITSCommonSystem::kIid, 0x287fb697, 0x6a7a1f2c, 0x12345678};
        void *faces[] = {primary, Face(object, 4), face, target};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 10) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 3: // the base service's accessors
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    case 4: // OnTick (with settings: see the file comment)
        if (Word(object, 0x34)) b.push_back(Slot<uint32_t>(primary, 10, c) & 0xff);
        break;
    case 5: // the getters
        b.push_back(svcfx::Norm(Slot<uint32_t>(face, 3)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(face, 4)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(face, 5)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(face, 20)));
        break;
    case 6: { // CollectTextureKeys into a list holding 0-2 keys
        CommonTextureKeys().swap(*gKeys);
        if ((a >> 4) % 2) gKeys->reserve(a % 3 + (a >> 5) % 3);
        for (uint32_t i = 0; i < a % 3; ++i) gKeys->push_back(StringKeyParser::Result{i, ~i, 7, 9});
        gNodeMode = (a >> 2) % 3;
        b.push_back(Slot<uint32_t>(face, 6, static_cast<void *>(&F(kObject)), gKeys) & 0xff);
        b.push_back(uint32_t(gKeys->size()));
        b.push_back(uint32_t(gKeys->capacity()));
        for (const StringKeyParser::Result &key : *gKeys) {
            const auto *words = reinterpret_cast<const uint32_t *>(&key);
            for (int i = 0; i < 4; ++i) b.push_back(words[i]);
        }
        break;
    }
    case 7: { // NextId / IsValidId around the counter's ends
        if (a % 3 == 0) *reinterpret_cast<uint32_t *>(Face(object, 0x38)) = kCounters[(a >> 2) % 6];
        for (uint32_t i = 0; i < 1 + (a >> 5) % 3; ++i) b.push_back(Slot<uint32_t>(face, 7));
        const uint32_t counter = Word(object, 0x38);
        const uint32_t ids[] = {0, 1, counter - 1, counter, counter + 1, 0xffffffff, c};
        for (uint32_t id : ids) b.push_back(Slot<uint32_t>(face, 8, id) & 0xff);
        break;
    }
    case 8: { // ReadAppDword
        uint32_t out = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(face, 9, Name(a), &out, Subkey(a >> 4)) & 0xff);
        b.push_back(out);
        break;
    }
    case 9: { // ReadAppString / ReadAppStringAny / WriteAppString
        cIGZString *out = Out(c);
        const unsigned slot = 10 + a % 3;
        b.push_back(Slot<uint32_t>(face, slot, Name(a >> 2), slot == 12 ? Text(c >> 4) : out, Subkey(a >> 5)) & 0xff);
        DumpOut(b);
        break;
    }
    case 10: { // ReadProductDword
        uint32_t out = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(face, 13, Product(a), Name(a >> 3), &out, Subkey(a >> 6)) & 0xff);
        b.push_back(out);
        break;
    }
    case 11: { // ReadProductString / ReadProductStringAny
        cIGZString *out = Out(c);
        b.push_back(Slot<uint32_t>(face, 14 + a % 2, Product(a >> 1), Name(a >> 4), out, Subkey(a >> 7)) & 0xff);
        DumpOut(b);
        break;
    }
    case 12: // WriteProductString
        b.push_back(Slot<uint32_t>(face, 16, Product(a), Name(a >> 3), Text(c), Subkey(a >> 6)) & 0xff);
        break;
    case 13: // GetRegionCode
        b.push_back(Slot<uint32_t>(face, 17, Out(c)) & 0xff);
        DumpOut(b);
        break;
    case 14: b.push_back(Slot<uint32_t>(face, 18)); break; // GetRegion
    case 15: // GetContentVersions
        b.push_back(Slot<uint32_t>(face, 19, Out(c)) & 0xff);
        DumpOut(b);
        break;
    case 16: // GetUserGuid
        for (int i = 0; i < 4; ++i) gGuid[i] = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(face, 21, gGuid) & 0xff);
        for (int i = 0; i < 4; ++i) b.push_back(gGuid[i]);
        break;
    default: { // DoMessage
        static const uint32_t kTypes[] = {Sys::kFloorCollisionsMessage, Sys::kWallCollisionsMessage, 0x31746e25, 0};
        gMsgMode = a % 3;
        F(kMsg2).ret[4] = kTypes[(a >> 2) % 4];
        gAnyBuf[4] = uint8_t((a >> 4) % 3 ? (a >> 6) % 2 : 0x80 | (a >> 6));
        b.push_back(Slot<uint32_t>(target, 3, (a >> 9) % 7 ? static_cast<void *>(&F(kMsg)) : nullptr) & 0xff);
        break;
    }
    }
}

void Compare(difftest::Context &ctx, const char *name, uint32_t seed, std::vector<Blob> (&results)[2], const std::vector<Op> &ops,
             uint32_t &stop)
{
    for (size_t i = 0; i < results[0].size() || i < results[1].size(); ++i) {
        const Blob empty;
        const Blob &x = i < results[0].size() ? results[0][i] : empty;
        const Blob &y = i < results[1].size() ? results[1][i] : empty;
        if (!svcfx::Same(ctx, x, y, name, seed)) {
            if (svcfx::gTrace)
                for (size_t w = 0; w < x.size() || w < y.size(); ++w)
                    fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < x.size() ? x[w] : 0, w < y.size() ? y[w] : 0);
            const size_t op = i ? i - 1 : 0;
            ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2), i && op < ops.size() ? ops[op].kind : 99u);
            stop = 1;
            return;
        }
    }
}

void Finish(Blob &b)
{
    for (uint32_t face : gHeldFaces) b.push_back(reinterpret_cast<const uint32_t *>(uintptr_t(face))[1]); // the held functions' counts
    gHeldFaces.clear(); // held for good (the game's classes: their release frees through the game's heap)
    b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
    gStackSkew = 0;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
}

struct Scope {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    shape::StandIn lookup{0xcb4690, &product::GetProductPaths, &StandLookup};
    shape::StandIn appProduct{0xcb4950, &product::NewestProductExecutable, &StandAppProduct};
    shape::CodePatch walk{0xae8090, shape::CodeOf(&StandWalk)};
    shape::StandIn exprRegistry{0xae8980, &GetMaterialParserCached, &StandExprRegistry};
    shape::CodePatch renderer{0xb3bbd0, shape::CodeOf(&StandRenderer)};
    shape::CodePatch fileSystem{0x9c59d9, shape::CodeOf(&StandFileSystem)};
    shape::StandIn exists{0x1c1242, &cRZFile::PathExists, &StandExists};
    Iat open{0xd9a028, reinterpret_cast<const void *>(&FakeOpen)};
    Iat query{0xd9a054, reinterpret_cast<const void *>(&FakeQuery)};
    Iat close{0xd9a02c, reinterpret_cast<const void *>(&FakeClose)};
    Iat create{0xd9a030, reinterpret_cast<const void *>(&FakeCreate)};
    Iat set{0xd9a024, reinterpret_cast<const void *>(&FakeSet)};
    Iat guid{0xd9a6d0, reinterpret_cast<const void *>(&FakeGuid)};
    explicit Scope(difftest::Context &ctx)
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        globals.SetWord(kServiceCell, 0);
        globals.SetWord(kCommonImplCell, 0);
        globals.SetWord(kCellsCache, 0);
        globals.SetWord(kContentCache, 0);
        for (uint32_t off = 0; off < 0x14; off += 4) globals.SetWord(kStatics + off, *GameAddress<uint32_t>(kStatics + off));
        gCtx = &ctx;
        CollectOurVTables();
        gTexts = std::make_unique<ContentString[]>(kTextCount);
        for (uint32_t i = 0; i < kTextCount; ++i) gTexts[i].s.mString.assign(kTexts[i]);
        gOut = new ContentString;
        gKeys = new CommonTextureKeys;
        gGuid = new uint32_t[4];
    }
    ~Scope()
    {
        delete gOut;
        delete gKeys;
        delete[] gGuid;
        gTexts.reset();
        svcfx::gFrameworkIndex = 0;
        svcfx::gRanges.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

// The test's own blocks (the object under test registers itself: BuildService).
void AddRanges()
{
    svcfx::AddRange(gOut, sizeof(ContentString), kTextTag);
    svcfx::AddRange(gTexts.get(), sizeof(ContentString) * kTextCount, kTextTag + 0x1000);
    svcfx::AddRange(gGuid, 16, kTextTag + 0x2000);
    svcfx::AddRange(gKeys, sizeof(CommonTextureKeys), kTextTag + 0x3000);
    svcfx::AddRange(gRendererBlob, sizeof(gRendererBlob), kTextTag + 0x4000);
    svcfx::AddRange(gAnyBuf, sizeof(gAnyBuf), kTextTag + 0x5000);
    svcfx::AddRange(GameAddress<void>(kStatics), 0x14, kStaticTag);
}

} // namespace cs_test

using namespace cs_test;

DIFF_TEST(CommonSystem_Run, 0xcba7c0)
{
    // Also reached (see the file comment): 0xcb9dd0 (factory), 0xcb9330 (constructor), 0xcb93f0 (destructor), 0xcb9620
    // (deleting destructor), 0xcb960e (its +4 thunk), 0xcbaed0 (QueryInterface), 0xcbaf01 / 0xcbaf09 / 0xcbaf11 (its thunks),
    // 0xcbb560, 0xcbae10, 0xcb9d00, 0xcb9ee0, 0xcba330, 0xcbadd0, 0xcbb390, 0xcbb3e0, 0xcbb400, 0xcbb7a0, 0xcbaf20, 0xcbb060,
    // 0xcbb1f0, 0xcbb650, 0xcba340, 0xcba460, 0xcba140, 0xcba610, 0xcb94a0, 0xcbb420, 0xcbb610, 0xcb9e40, 0x9c5ae0.
    Scope scope(ctx);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            Blob first;
            AddRanges();
            void *object = BuildService(ctx, first, gOurs);
            DumpService(first, object);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    ServiceOp(b, object, ops[i]);
                    DumpService(b, object);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys the service
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        uint32_t stop = 0;
        Compare(ctx, "CommonSystem_Run", seed, results, ops, stop);
        if (stop) break;
    }
}
