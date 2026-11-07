// Differential tests for ts::cTSAppCommonImpl's registry handlers (lane MB, src/ts/cTSAppCommonImplRegistry.cpp): the
// resource factory entry 0xca5db0, the data-base segment entry 0xca5170 and its segment builder 0xca4f70.
//
// Nothing real is reached: the framework global 0x10c9378 is a fake whose GetSystemService (slot 5) hands out a logging
// resource manager; GetDirectorFrameworkObject 0x1ae18d (both sides call it at that address or as ours) is a stand-in
// returning a logging COM director whose GetClassObject (slot 3) hands out scripted segments; the language service
// (cache 0x10f51c8) is a fake whose HasLanguageName (slot 15) writes the case's name or nothing; the cached
// cITSAppCommon (0x10cc040) is an application-common object built by the original constructor and seeded with the
// case's data directories (option 0x4bcfce5e, per product mask), so both sides read its option list through the same
// code; PathExists (0x1c1242 and ours) answers from a hash of the path; and, for 0xca5170, the segment builder 0xca4f70
// is a logging stand-in on both sides. Compared: al and the log of every call with its arguments.
#define NOMINMAX
#include "app_debugger_fakes.h"
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "misc/HexParse.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSAppCommonImpl.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using Adapters = ts::cTSAppCommonImplAdapters;

constexpr uint32_t kOnResourceFactory = 0xca5db0;
constexpr uint32_t kOnPathRegistry = 0xca5170;
constexpr uint32_t kAddPathSegment = 0xca4f70;
constexpr uint32_t kDirectorObject = 0x1ae18d;
constexpr uint32_t kPathExists = 0x1c1242;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kLanguageCache = 0x10f51c8;
constexpr uint32_t kCommonImplCache = 0x10cc040;
constexpr uint32_t kCommonImplCtor = 0xca4cb0;
constexpr uint32_t kCommonImplDtor = 0xca4de0;
constexpr uint32_t kDataRootOption = 0x4bcfce5e;
constexpr int kAddOptionSlot = 3;
constexpr uint32_t kAtexit = 0x824cbe;
constexpr uint32_t kParseHex32 = 0x1b0e2c;

using Callback = void(__cdecl *)(cIGZString *, cIGZString *, void *);
using Builder = uint32_t(__cdecl *)(uint32_t, uint32_t, cIGZString *);

std::string gLog;
std::mt19937 gR;

std::string Fmt(const char *fmt, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

uint32_t Hash(const std::string &text, uint32_t salt)
{
    uint32_t h = 2166136261u ^ salt;
    for (unsigned char c : text) h = (h ^ c) * 16777619u;
    return h;
}

// A cRZString on the stack (its own members build and tear it down).
struct Text {
    union {
        cRZString s;
    };
    explicit Text(const std::string &text) { s.Construct(text.c_str()); }
    Text(const Text &) = delete;
    ~Text() { s.Destruct(); }
};

// A fake object: a vtable of up to 64 slots, the unset ones null (a call through one crashes the test, loudly).
struct Fake {
    const void *const *vt;
    const void *slots[64];
    const char *name;
    explicit Fake(const char *n) : vt(slots), name(n) { Clear(); }
    void Clear()
    {
        for (auto &s : slots) s = nullptr;
    }
    template <typename F> void Set(int slot, F fn) { slots[slot] = reinterpret_cast<const void *>(fn); }
};

const char *NameOf(void *object) { return object ? static_cast<Fake *>(object)->name : "null"; }

// ---- the resource manager and the framework

uint32_t gDefaultSegment = 0x5e650000;
uint32_t __fastcall ManagerRelease(void *self, void *)
{
    gLog += Fmt("%s.release ", NameOf(self));
    return 0x77;
}
uint32_t __fastcall ManagerRegisterFactory(void *, void *, uint32_t factoryId, uint32_t type, void *factory)
{
    gLog += Fmt("registerFactory(%x,%x,%p) ", factoryId, type, factory);
    return 0x12345601;
}
uint32_t __fastcall ManagerAddSegment(void *, void *, void *segment)
{
    gLog += Fmt("addSegment(%s) ", NameOf(segment));
    return 0x12345600;
}
void *__fastcall ManagerDefaultSegment(void *, void *)
{
    gLog += "defaultSegment ";
    return reinterpret_cast<void *>(static_cast<uintptr_t>(gDefaultSegment));
}
Fake gManager("manager");

bool gHaveManager = true;
uint32_t __fastcall FwGetSystemService(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    gLog += Fmt("service(%x,%x,%s) ", id, iid, *out ? "set" : "null");
    if (!gHaveManager) return 0;
    *out = &gManager;
    return 1;
}
Fake gFramework("framework");

// ---- the director and its segments

struct SegmentScript {
    bool classObject = true; // GetClassObject's al
    bool hand = true;        // it hands out the segment (also when it fails)
    bool init = true, directoryFiles = false, multiPacked = false, setPath = true, open = true;
};
SegmentScript gScript;

uint32_t Al(bool value) { return (gR() & 0xffffff00u) | (value ? 1u : 0u); }

Fake gSegment("segment"), gDirectoryFiles("directoryFiles"), gMultiPacked("multiPacked"), gDirector("director");

uint32_t __fastcall SegRelease(void *self, void *)
{
    gLog += Fmt("%s.release ", NameOf(self));
    return 0;
}
uint32_t __fastcall SegQueryInterface(void *, void *, uint32_t iid, void **out)
{
    gLog += Fmt("qi(%x,%s) ", iid, *out ? "set" : "null");
    bool yes = false;
    if (iid == 0x856c8cf7 && gScript.directoryFiles) *out = &gDirectoryFiles, yes = true;
    if (iid == 0xe90f85f3 && gScript.multiPacked) *out = &gMultiPacked, yes = true;
    return Al(yes);
}
uint32_t __fastcall SegInit(void *, void *)
{
    gLog += "init ";
    return Al(gScript.init);
}
uint32_t __fastcall SegOpen(void *, void *, uint32_t read, uint32_t write)
{
    gLog += Fmt("open(%x,%x) ", read, write);
    return Al(gScript.open);
}
uint32_t __fastcall SegSetPath(void *, void *, cIGZString *path)
{
    gLog += Fmt("setPath(%s) ", path->ToChar());
    return Al(gScript.setPath);
}
uint32_t __fastcall SegSetSegmentId(void *, void *, uint32_t *id)
{
    gLog += Fmt("setSegmentId(%x) ", *id);
    return 0;
}
uint32_t __fastcall SegSetCache(void *, void *, void *cache)
{
    gLog += Fmt("setCache(%p) ", cache);
    return 0;
}
uint32_t __fastcall DirectoryFiles15(void *, void *, uint32_t a, uint32_t b)
{
    gLog += Fmt("directoryFiles15(%x,%x) ", a, b);
    return 0;
}
uint32_t __fastcall DirGetClassObject(void *, void *, uint32_t classId, uint32_t iid, void **out)
{
    gLog += Fmt("getClassObject(%x,%x,%s) ", classId, iid, *out ? "set" : "null");
    if (gScript.hand) *out = &gSegment;
    return Al(gScript.classObject);
}

void *gDirectorReturned = &gDirector;
void *__cdecl FakeDirectorObject()
{
    gLog += "directorObject ";
    return gDirectorReturned;
}

// ---- the language service, PathExists and the segment builder's stand-in

std::string gLanguageName;
int gLanguageMode = 0; // 0 nothing written, 1 the name written
uint32_t __fastcall LangHasName(void *, void *, cIGZString *out, uint32_t language)
{
    gLog += Fmt("languageName('%s',%x) ", out->ToChar(), language);
    if (gLanguageMode == 1) out->FromChar(gLanguageName.c_str());
    return Al(gLanguageMode == 1);
}
Fake gLanguages("languages");

uint32_t gSalt = 0, gExistsBias = 2;
bool __cdecl FakePathExists(const cIGZString &path)
{
    const std::string p = path.ToChar();
    const bool r = (Hash(p, gSalt) >> 5) % 4 < gExistsBias;
    gLog += Fmt("exists(%s)=%d ", p.c_str(), int(r));
    return r;
}

bool __cdecl FakeAddPathSegment(uint32_t classId, uint32_t typeId, cIGZString *path)
{
    const std::string p = path->ToChar();
    gLog += Fmt("addPathSegment(%x,%x,%s) ", classId, typeId, p.c_str());
    return (Hash(p, gSalt) & 1) != 0;
}

int __cdecl NoAtexit(void (*)()) { return 0; }

void InitFakes()
{
    gManager.Set(2, &ManagerRelease);
    gManager.Set(20, &ManagerRegisterFactory);
    gManager.Set(27, &ManagerAddSegment);
    gManager.Set(47, &ManagerDefaultSegment);
    gFramework.Set(5, &FwGetSystemService);
    gSegment.Set(0, &SegQueryInterface);
    gSegment.Set(2, &SegRelease);
    gSegment.Set(3, &SegInit);
    gSegment.Set(5, &SegOpen);
    gSegment.Set(12, &SegSetPath);
    gSegment.Set(16, &SegSetSegmentId);
    gSegment.Set(17, &SegSetCache);
    gDirectoryFiles.Set(2, &SegRelease);
    gDirectoryFiles.Set(15, &DirectoryFiles15);
    gMultiPacked.Set(2, &SegRelease);
    gDirector.Set(3, &DirGetClassObject);
    gLanguages.Set(15, &LangHasName);
}

uint32_t Address(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::string Differ(const std::string &a, const std::string &b)
{
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    at = at > 60 ? at - 60 : 0;
    return Fmt("from offset %u:\n  original: %.400s\n  ours:     %.400s", unsigned(at), a.c_str() + at, b.c_str() + at);
}

const char *const kNumbers[] = {"0x1234", "0X7fffFFFF", "12345", "0", "", "zz", " 0x10", "-5", "0x", "4294967295", "0x1ffffffff", "077"};

} // namespace

// ---- 0xca5db0: the key and value (decimal or hexadecimal numbers, and junk), the manager there or not.

DIFF_TEST(CommonImplRegistry_OnResourceFactoryEntry, kOnResourceFactory)
{
    InitFakes();
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, Address(&gFramework));
    std::mt19937 rng(kOnResourceFactory);
    auto original = ctx.Original<Callback>(kOnResourceFactory);
    constexpr size_t kCount = sizeof(kNumbers) / sizeof(kNumbers[0]);
    for (unsigned i = 0; i < 300; ++i) {
        const std::string key = rng() % 5 ? kNumbers[rng() % kCount] : Fmt("0x%x", unsigned(rng()));
        const std::string value = rng() % 5 ? kNumbers[rng() % kCount] : Fmt("%u", unsigned(rng()));
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gR.seed(i);
            Text k(key), v(value);
            (side == 0 ? original : &Adapters::OnResourceFactoryEntry)(k.s.AsInterface(), v.s.AsInterface(),
                                                                       reinterpret_cast<void *>(0xc0c0c0c0));
            results[side] = gLog;
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u ('%s'='%s'), %s", i, key.c_str(), value.c_str(), Differ(results[0], results[1]).c_str());
    }
}

// ---- 0xca4f70: every outcome of the segment's calls (class object, init, the two interfaces, path, open), the
// directory object and the default segment as the case's.

DIFF_TEST(CommonImplRegistry_AddPathSegment, kAddPathSegment)
{
    InitFakes();
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, Address(&gFramework));
    shape::StandIn director{kDirectorObject, &GetDirectorFrameworkObject, &FakeDirectorObject};
    auto original = ctx.Original<Builder>(kAddPathSegment);
    const char *const paths[] = {"C:\\TS2\\TSData\\Res\\Objects\\objects.package", "", "D:/x/y/", "relative\\dir\\"};
    for (unsigned i = 0; i < 2 * 128 * 4; ++i) {
        const unsigned bits = i % 128;
        gScript.classObject = bits & 1;
        gScript.hand = (bits & 1) || (i / 128) % 2;
        gScript.init = bits & 2;
        gScript.directoryFiles = bits & 4;
        gScript.multiPacked = bits & 8;
        gScript.setPath = bits & 16;
        gScript.open = bits & 32;
        gDefaultSegment = bits & 64 ? 0 : 0x5e650000 + i;
        const std::string path = paths[(i / 256) % 4];
        const uint32_t classId = 0x1000000u * (i % 7) + i, typeId = ~i;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gR.seed(i);
            Text p(path);
            const uint32_t al = (side == 0 ? original(classId, typeId, p.s.AsInterface())
                                           : uint32_t(Adapters::AddPathSegment(classId, typeId, p.s.AsInterface()))) & 0xff;
            results[side] = Fmt("%x | ", al) + gLog;
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (bits %x), %s", i, bits, Differ(results[0], results[1]).c_str());
    }
}

// ---- 0xca5170: the group markers, type ids, class ids with and without their ';', the two tokens (none, either,
// both, repeated, misplaced), the language service writing a name or not, and data directories of several products
// existing or not.

namespace {

const char *const kKeys[] = {"BEGIN_GROUP", "END_GROUP", "begin_group", "BEGIN_GROUP ", "0x4b58975b", "4b58975b", "", "zz", "0X1"};
const char *const kValues[] = {
    "0x6534284a;%DataDirectory%Res\\Objects\\objects.package",
    "6534284a;%Language%\\Text\\%DataDirectory%x",
    "0x1;%DataDirectory%Res\\Locale\\%Language%\\",
    "0x1;Res\\Locale\\%Language%\\strings.package",
    "0x2;%Language%",
    "0x3;%Language%%Language%",
    "0x4;%DataDirectory%",
    "0x5;a%DataDirectory%b%DataDirectory%c",
    "0x6;plain\\path.package",
    "no class id here",
    ";%DataDirectory%x",
    "",
    "0x7;",
    "0x8;%language%\\lower%datadirectory%",
    "0x9;C:\\Absolute\\%Language%\\%DataDirectory%\\Very Long Name That Leaves The Small Buffer\\file.package",
};
const char *const kDirectories[] = {"C:\\Program Files\\EA GAMES\\The Sims 2\\TSData\\", "D:/EP1/TSData/", "", "x",
                                    "C:\\Long Directory Name That Is Over Sixteen\\SP4\\TSData\\"};
const char *const kLanguageNames[] = {"Deutsch", "", "English", "Fran\xe7" "ais", "A Language Name Longer Than Sixteen"};
const uint32_t kMasks[] = {0x20000, 0x1, 0x2, 0xffffffff, 0x20001, 0x40000, 0xfffdffff};

} // namespace

DIFF_TEST(CommonImplRegistry_OnPathRegistryEntry, kOnPathRegistry)
{
    InitFakes();
    adt::AllocatorFake allocator(ctx); // the option list's nodes and strings (allocator service)
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, Address(&gFramework));
    adt::GlobalOverride<uint32_t> languages(ctx, kLanguageCache, Address(&gLanguages));
    adt::GlobalOverride<uint32_t> cache(ctx, kCommonImplCache);
    shape::StandIn director{kDirectorObject, &GetDirectorFrameworkObject, &FakeDirectorObject};
    shape::StandIn exists{kPathExists, &cRZFile::PathExists, &FakePathExists};
    shape::StandIn builder{kAddPathSegment, &Adapters::AddPathSegment, &FakeAddPathSegment};
    // The image's statics registration and its ParseHex32 (the CRT's locale through imports) are not usable unmapped:
    // atexit is a no-op, ParseHex32 our verified one (both sides then parse alike).
    shape::CodePatch atexitPatch{kAtexit, reinterpret_cast<const void *>(&NoAtexit)};
    shape::CodePatch hexPatch{kParseHex32, reinterpret_cast<const void *>(&hexparse::ParseHex32)};
    auto original = ctx.Original<Callback>(kOnPathRegistry);
    std::mt19937 rng(kOnPathRegistry);
    constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
    constexpr size_t kValueCount = sizeof(kValues) / sizeof(kValues[0]);
    for (unsigned i = 0; i < 600; ++i) {
        const std::string key = kKeys[rng() % kKeyCount];
        const std::string value = kValues[rng() % kValueCount];
        gLanguageMode = int(rng() % 2);
        gLanguageName = kLanguageNames[rng() % (sizeof(kLanguageNames) / sizeof(kLanguageNames[0]))];
        gSalt = rng();
        gExistsBias = rng() % 5;
        std::vector<std::pair<std::string, uint32_t>> directories;
        for (unsigned n = rng() % 6; n; --n)
            directories.push_back({kDirectories[rng() % (sizeof(kDirectories) / sizeof(kDirectories[0]))],
                                   kMasks[rng() % (sizeof(kMasks) / sizeof(kMasks[0]))]});
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            // The application-common object both sides read the directories from (the original's constructor, slots).
            uint8_t *common = static_cast<uint8_t *>(::operator new(0x18));
            ctx.Original<void *(__thiscall *)(void *)>(kCommonImplCtor)(common);
            using AddOption = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, const char *, uint32_t, int32_t);
            for (const auto &d : directories)
                reinterpret_cast<AddOption>((*reinterpret_cast<void *const *const *>(common))[kAddOptionSlot])(
                    common, kDataRootOption, 0, d.first.c_str(), 0, int32_t(d.second));
            *ctx.image.At<uint32_t>(kCommonImplCache) = Address(common);
            gLog.clear();
            gR.seed(i);
            {
                Text k(key), v(value);
                (side == 0 ? original : &Adapters::OnPathRegistryEntry)(k.s.AsInterface(), v.s.AsInterface(), common);
            }
            results[side] = gLog;
            ctx.Original<void(__thiscall *)(void *)>(kCommonImplDtor)(common);
            ::operator delete(common);
        }
        allocator.Release();
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u ('%s'='%s'), %s", i, key.c_str(), value.c_str(), Differ(results[0], results[1]).c_str());
    }
}
