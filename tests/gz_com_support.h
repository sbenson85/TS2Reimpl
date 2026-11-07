#pragma once
// Differential tests for cGZCOM and cGZCOMLibrary (src/gz/GZCOM.*), in the standalone shape.
//
// Each test builds two objects over the same fill: side 0 by the original constructor (the game's vtables), side 1 by
// our constructor (through its hook adapter: our vtables). The original runs on side 0 at its RVA; ours runs on side 1
// through our own vtable (`Call`: the slot of the side's pristine vtable copy) or, for the non-virtual helpers, as a
// member. The tests compare the returns, a description of the whole state (the library set in order, the class map,
// the plain fields), the object's bytes with only the vtable words normalised (`Canon`: each word named as one of the
// side's own vtables) and a log of every call the code makes through a vtable or an import. Objects are described by
// content (paths, class ids), never by address, because the two sides have their own heap nodes.
//
// Fakes: the critical section's vtable logs Lock/Unlock; the COM object can carry copies of its side's own vtables
// with logging AddRef/RemoveRef (InstallVtables), the libraries copies of theirs with a logging AddRef; the import
// address table slots the library code calls are replaced by scripted fakes for the duration of a test, and the
// original's static director at 0x10c9390 is replaced by a fake with a logging vtable (the getter's fast path, as
// tests/static_getters.cpp does). Libraries in a side-0 object carry the game's library vtable, in a side-1 object ours.
#include "difftest.h"
#include "game/game.h"
#include "gz/GZCOM.h"
#include "rz/StaticGetters.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0x144e52, kDtor = 0x145075, kDeleting = 0x145114, kDeletingThunk = 0x14510c;
constexpr uint32_t kQI = 0x145b14, kQISys = 0x145b36, kQIUnk = 0x145b3e;
constexpr uint32_t kRelease = 0x145bfc, kReleaseSys = 0x145c04, kReleaseUnk = 0x145c0c;
constexpr uint32_t kGetClassObject = 0x145702, kGetFramework = 0x145520, kAddLibrary = 0x1451a3;
constexpr uint32_t kFreeUnused = 0x1455a6, kRealInit = 0x145b7e, kRealShutdown = 0x145be4;
constexpr uint32_t kCreateGuid = 0x145325, kCreateGuids = 0x1453ab;
constexpr uint32_t kSetRunning = 0x145c1c, kSetRunningSys = 0x145c24;
constexpr uint32_t kEnumCallback = 0x1454e7, kEnumArray = 0x14548b;
constexpr uint32_t kCanUnload = 0x1452ff, kUnloadAll = 0x145556, kInitLibrary = 0x145c2c;
constexpr uint32_t kLoadAndGet = 0x1457d4, kRegisterCallback = 0x145152;
constexpr uint32_t kLibCtor = 0x144eff, kLibCopy = 0x144ec2, kLibDtor = 0x1450b1, kLibDeleting = 0x145133;
constexpr uint32_t kLibQI = 0x145b46, kLibRelease = 0x145c14, kLibGetPath = 0x14580d, kLibUnload = 0x145524;
constexpr uint32_t kLibInit = 0x1458b4, kLibLess = 0x1450c6, kLibIsInit = 0x1458b0, kLibGetModule = 0x1457d0;
constexpr uint32_t kLibAddRef = 0x145260, kLibGetDirector = 0x145770;
constexpr uint32_t kConvertBytes = 0x1afc9d, kModulePath = 0x1b139f;

constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatGetLastError = 0xd9a118, kIatGetVersion = 0xd9a11c;
constexpr uint32_t kIatFreeLibrary = 0xd9a120, kIatGetProcAddress = 0xd9a124, kIatLoadLibraryA = 0xd9a128;
constexpr uint32_t kIatLoadLibraryW = 0xd9a12c, kIatLocalFree = 0xd9a130, kIatFormatMessageA = 0xd9a134;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kDirectorStatic = 0x10c9390, kDirectorGuard = 0x10c93f4, kFrameworkGlobal = 0x10c9378;

// The game's vtables: cRZSystemService's two faces and cRZUnknownMT's (what the destructors leave behind).
constexpr uint32_t kGameServiceVTable = 0xda3058, kGameServiceUnknownVTable = 0xda3090, kGameUnknownMTVTable = 0xda2fec;

constexpr int kFaceOffset[3] = {0, 4, 8}; // cIGZCOM, cIGZSystemService, cRZUnknownMT
constexpr int kFaceSlots[3] = {14, 13, 6};
constexpr int kOriginal = 0, kOurs = 1;

// ---- the call log ----

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gLog.emplace_back(buf);
}

std::string PathOf(const void *lib)
{
    return static_cast<const cGZCOMLibrary *>(lib)->mPath.mString;
}

template <typename T = void *> T &Word(const void *object, size_t offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(object)) + offset);
}

// ---- each side's vtables ----

struct SideVtables {
    void **com[3];
    void **service[2];
    void **unknownMT;
    void **lib;
};
SideVtables gVt[2];

void InitVtables()
{
    static bool done = false;
    if (done) return;
    done = true;
    gVt[0].com[0] = GameAddress<void *>(cGZCOM::kVTable);
    gVt[0].com[1] = GameAddress<void *>(cGZCOM::kSystemVTable);
    gVt[0].com[2] = GameAddress<void *>(cGZCOM::kUnknownVTable);
    gVt[0].service[0] = GameAddress<void *>(kGameServiceVTable);
    gVt[0].service[1] = GameAddress<void *>(kGameServiceUnknownVTable);
    gVt[0].unknownMT = GameAddress<void *>(kGameUnknownMTVTable);
    gVt[0].lib = GameAddress<void *>(cGZCOMLibrary::kVTable);
    // Ours: read from objects our constructors build.
    auto *com = static_cast<cGZCOM *>(::operator new(sizeof(cGZCOM)));
    cGZCOMAdapters::Construct(com, nullptr);
    for (int f = 0; f < 3; ++f) gVt[1].com[f] = Word<void **>(com, kFaceOffset[f]);
    com->cGZCOM::~cGZCOM();
    ::operator delete(com);
    {
        rz::cRZSystemService service(1, 2);
        gVt[1].service[0] = Word<void **>(&service, 0);
        gVt[1].service[1] = Word<void **>(&service, 4);
    }
    {
        rz::cRZUnknownMT unknown;
        gVt[1].unknownMT = Word<void **>(&unknown, 0);
    }
    cRZString text;
    text.Construct("v");
    alignas(cGZCOMLibrary) unsigned char storage[sizeof(cGZCOMLibrary)];
    auto *lib = ::new (storage) cGZCOMLibrary(*text.AsInterface());
    gVt[1].lib = Word<void **>(lib, 0);
    lib->cGZCOMLibrary::~cGZCOMLibrary();
    text.Destruct();
}

// A vtable word as a name: one of the side's own vtables, or its raw value.
uint32_t VtCode(uint32_t word, int side)
{
    const SideVtables &v = gVt[side];
    for (int f = 0; f < 3; ++f)
        if (word == reinterpret_cast<uint32_t>(v.com[f])) return 0x100 + f;
    for (int f = 0; f < 2; ++f)
        if (word == reinterpret_cast<uint32_t>(v.service[f])) return 0x200 + f;
    if (word == reinterpret_cast<uint32_t>(v.unknownMT)) return 0x300;
    if (word == reinterpret_cast<uint32_t>(v.lib)) return 0x400;
    return word;
}

// The three vtable words of a COM object at `base` within `bytes`, named.
void NormComVtables(std::vector<unsigned char> &bytes, size_t base, int side)
{
    for (int f = 0; f < 3; ++f) {
        uint32_t word;
        std::memcpy(&word, bytes.data() + base + kFaceOffset[f], 4);
        word = VtCode(word, side);
        std::memcpy(bytes.data() + base + kFaceOffset[f], &word, 4);
    }
}

// A slot called on the side's object: the original at its RVA (side 0) or the slot of our pristine vtable (side 1).
template <typename R, typename... A> R Call(difftest::Context &ctx, int side, const void *object, int face, int slot, uint32_t rva, A... args)
{
    void *self = const_cast<char *>(static_cast<const char *>(object)) + kFaceOffset[face];
    if (side == kOriginal) return ctx.Original<R(__thiscall *)(void *, A...)>(rva)(self, args...);
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(gVt[1].com[face][slot])(self, args...);
}

template <typename R, typename... A> R CallLib(difftest::Context &ctx, int side, const void *lib, int slot, uint32_t rva, A... args)
{
    void *self = const_cast<void *>(lib);
    if (side == kOriginal) return ctx.Original<R(__thiscall *)(void *, A...)>(rva)(self, args...);
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(gVt[1].lib[slot])(self, args...);
}

// ---- fakes ----

void *gCritVt[8];
void *gLibVt[8];
void *gPrimaryVt[14], *gSysVt[13], *gUnkVt[6];
unsigned char *gBase = nullptr;

int32_t __fastcall FakeLock(void *, void *)
{
    Log("L");
    return 1;
}
int32_t __fastcall FakeUnlock(void *, void *)
{
    Log("U");
    return 0;
}
uint32_t __fastcall FakeLibAddRef(void *self, void *)
{
    Log("libAddRef %s", PathOf(self).c_str());
    return 7;
}
uint32_t __fastcall FakeComAddRef(void *self, void *)
{
    Log("comAddRef +%x", static_cast<unsigned>(static_cast<unsigned char *>(self) - gBase));
    return 9;
}
uint32_t __fastcall FakeUnknownRemoveRef(void *self, void *)
{
    Log("removeRef +%x", static_cast<unsigned>(static_cast<unsigned char *>(self) - gBase));
    return 0x77;
}

// The libraries' directors (an object whose first dword is its vtable).
struct FakeDirector {
    void **vt; // the first dword is the vtable pointer
    void *table[16];
    const char *name;
};
FakeDirector gDirA, gDirB;
void *gStaticDirector = nullptr; // the image's static director while it is replaced
bool gDirInitResult = true;
bool gCanUnloadResult = true;
bool gGetClassResult = true;
std::vector<std::pair<uint32_t, uint32_t>> gRegisterScript; // (class id, version) the director registers
cGZCOM *gCurrentCom = nullptr;

const char *DirName(void *self)
{
    return self == gStaticDirector ? "static" : static_cast<FakeDirector *>(self)->name;
}

// The framework object the COM object stores (GetClassObject asks it for class 0x3e8); also the
// value GetDirectorFrameworkObject returns, so a director can tell it was handed.
struct FakeFramework {
    void **vt;
    void *table[4];
};
FakeFramework gFramework;
bool __fastcall FakeFrameworkQI(void *self, void *, uint32_t iid, void **ppv)
{
    Log("framework.qi %x ppv=%s self=%s", iid, ppv ? "set" : "null", self == &gFramework ? "fw" : "other");
    return true;
}
void InitFramework()
{
    std::memset(gFramework.table, 0, sizeof(gFramework.table));
    gFramework.table[0] = reinterpret_cast<void *>(&FakeFrameworkQI);
    gFramework.vt = gFramework.table;
}
cIGZFrameWork *FakeFrameworkPtr()
{
    return reinterpret_cast<cIGZFrameWork *>(&gFramework);
}

bool __fastcall FakeDirInit(void *self, void *, void *framework, void *path)
{
    Log("%s.init fw=%s path=%s", DirName(self), framework == &gFramework ? "marker" : framework == gCurrentCom ? "com" : "other",
        static_cast<cRZString *>(path)->mString.c_str());
    return gDirInitResult;
}
void __fastcall FakeDirOnAdded(void *self, void *, void *com)
{
    Log("%s.onAdded com=%s", DirName(self), com == gCurrentCom ? "com" : "other");
}
void __fastcall FakeDirRegister(void *self, void *, void (__cdecl *callback)(uint32_t, uint32_t, void *), void *com)
{
    Log("%s.register com=%s", DirName(self), com == gCurrentCom ? "com" : "other");
    for (auto &entry : gRegisterScript) {
        Log("callback %x v%u", entry.first, entry.second);
        callback(entry.first, entry.second, com);
    }
}
bool __fastcall FakeDirGetClass(void *self, void *, uint32_t clsid, uint32_t iid, void **ppv)
{
    Log("%s.getClass %x %x ppv=%s", DirName(self), clsid, iid, ppv ? "set" : "null");
    return gGetClassResult;
}
bool __fastcall FakeDirCanUnload(void *self, void *)
{
    Log("%s.canUnload", DirName(self));
    return gCanUnloadResult;
}
void *__fastcall FakeDirFrameworkObject(void *, void *)
{
    return &gFramework;
}

void InitDirector(FakeDirector &d, const char *name)
{
    std::memset(d.table, 0, sizeof(d.table));
    d.table[3] = reinterpret_cast<void *>(&FakeDirInit);
    d.table[4] = reinterpret_cast<void *>(&FakeDirOnAdded);
    d.table[5] = reinterpret_cast<void *>(&FakeDirRegister);
    d.table[6] = reinterpret_cast<void *>(&FakeDirGetClass);
    d.table[7] = reinterpret_cast<void *>(&FakeDirCanUnload);
    d.table[12] = reinterpret_cast<void *>(&FakeDirFrameworkObject);
    d.name = name;
    d.vt = d.table;
}
cIGZCOMDirector *AsDirector(FakeDirector &d)
{
    return reinterpret_cast<cIGZCOMDirector *>(&d);
}

// ---- import fakes ----

std::map<std::string, void *> gLoadable;
bool gExportPresent = true;
void *gDirectorReturned = nullptr;
DWORD gVersion = 5;
const char *gSystemMessage = "boom";
bool gFormatSucceeds = true;
bool gThrowOnGetProc = false;
bool gThrowOnLoad = false;
bool gThrowOnFree = false;

void *WINAPI FakeDirectorExport()
{
    Log("export called");
    return gDirectorReturned;
}
DWORD WINAPI FakeGetVersion()
{
    return gVersion;
}
void *WINAPI FakeLoadLibraryA(const char *name)
{
    Log("LoadLibraryA %s", name);
    if (gThrowOnLoad) RaiseException(0xE0001234, 0, 0, nullptr);
    auto it = gLoadable.find(name);
    return it == gLoadable.end() ? nullptr : it->second;
}
void *WINAPI FakeLoadLibraryW(const wchar_t *name)
{
    std::string narrow;
    for (const wchar_t *p = name; *p; ++p) narrow += static_cast<char>(*p);
    Log("LoadLibraryW %s", narrow.c_str());
    if (gThrowOnLoad) RaiseException(0xE0001234, 0, 0, nullptr);
    auto it = gLoadable.find(narrow);
    return it == gLoadable.end() ? nullptr : it->second;
}
void *WINAPI FakeGetProcAddress(void *module, const char *name)
{
    Log("GetProcAddress %p %s", module, name);
    if (gThrowOnGetProc) RaiseException(0xE0001234, 0, 0, nullptr);
    if (module && gExportPresent && std::strcmp(name, "GZDllGetGZCOMDirector") == 0) return reinterpret_cast<void *>(&FakeDirectorExport);
    return nullptr;
}
BOOL WINAPI FakeFreeLibrary(void *module)
{
    Log("FreeLibrary %p", module);
    if (gThrowOnFree) RaiseException(0xE0001234, 0, 0, nullptr);
    return TRUE;
}
DWORD WINAPI FakeGetLastError()
{
    return 1157;
}
DWORD WINAPI FakeFormatMessageA(DWORD flags, const void *source, DWORD id, DWORD lang, void *buffer, DWORD size, void *args)
{
    Log("FormatMessageA flags=%x src=%p id=%u lang=%x size=%u args=%p", flags, source, id, lang, size, args);
    if (!gFormatSucceeds) return 0;
    *static_cast<const char **>(buffer) = gSystemMessage;
    return static_cast<DWORD>(std::strlen(gSystemMessage));
}
HLOCAL WINAPI FakeLocalFree(HLOCAL p)
{
    Log("LocalFree %s", p == static_cast<const void *>(gSystemMessage) ? "message" : "other");
    return nullptr;
}

struct IatFakes {
    difftest::Context &ctx;
    struct Saved {
        uint32_t rva;
        void *value;
    };
    std::vector<Saved> saved;
    explicit IatFakes(difftest::Context &c) : ctx(c)
    {
        Set(kIatGetVersion, &FakeGetVersion);
        Set(kIatLoadLibraryA, &FakeLoadLibraryA);
        Set(kIatLoadLibraryW, &FakeLoadLibraryW);
        Set(kIatGetProcAddress, &FakeGetProcAddress);
        Set(kIatFreeLibrary, &FakeFreeLibrary);
        Set(kIatGetLastError, &FakeGetLastError);
        Set(kIatFormatMessageA, &FakeFormatMessageA);
        Set(kIatLocalFree, &FakeLocalFree);
    }
    template <typename F> void Set(uint32_t rva, F fn)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.push_back({rva, *slot});
        *slot = reinterpret_cast<void *>(fn);
    }
    ~IatFakes()
    {
        for (auto &s : saved) *ctx.image.At<void *>(s.rva) = s.value;
    }
};

// The original's static director replaced by a fake for the duration of a test (GetCOMDirector's
// fast path returns it untouched, GetDirectorFrameworkObject asks its slot 12).
struct FakeStaticDirector {
    difftest::Context &ctx;
    unsigned char saved[0x64];
    int savedGuard;
    int savedEpoch;
    int *tls;
    explicit FakeStaticDirector(difftest::Context &c) : ctx(c)
    {
        auto *dir = ctx.image.At<unsigned char>(kDirectorStatic);
        std::memcpy(saved, dir, sizeof(saved));
        savedGuard = *ctx.image.At<int>(kDirectorGuard);
        InitDirector(gDirB, "static");
        gStaticDirector = dir;
        *reinterpret_cast<void **>(dir) = gDirB.table;
        *ctx.image.At<int>(kDirectorGuard) = 5;
        uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
        tls = reinterpret_cast<int **>(__readfsdword(0x2c))[index];
        savedEpoch = tls[1];
        tls[1] = 0x7fffffff;
    }
    ~FakeStaticDirector()
    {
        gStaticDirector = nullptr;
        tls[1] = savedEpoch;
        *ctx.image.At<int>(kDirectorGuard) = savedGuard;
        std::memcpy(ctx.image.At<unsigned char>(kDirectorStatic), saved, sizeof(saved));
    }
};

// ---- objects ----

template <typename T> T *RawObject(size_t size, int fill)
{
    auto *p = static_cast<T *>(::operator new(size));
    std::memset(p, fill, size);
    return p;
}

// An object built by the original constructor (side 0) or ours through its adapter (side 1), over the same fill.
cGZCOM *MakeCom(difftest::Context &ctx, int side, int fill = 0xCD)
{
    InitVtables();
    auto *com = RawObject<cGZCOM>(sizeof(cGZCOM), fill);
    if (side == kOriginal) {
        // The constructor seeds its generator from time() and clock() (seed -1). The image's clock()
        // divides by a tick frequency its startup never set; with the start counter at -1 it
        // returns -1 straight away. The seed therefore differs from ours (masked in Canon).
        uint32_t *clockStart = ctx.image.At<uint32_t>(0x10dfcd0);
        uint32_t saved[2] = {clockStart[0], clockStart[1]};
        clockStart[0] = clockStart[1] = 0xffffffff;
        ctx.Original<cGZCOM *(__thiscall *)(void *)>(kCtor)(com);
        clockStart[0] = saved[0];
        clockStart[1] = saved[1];
    } else {
        cGZCOMAdapters::Construct(com, nullptr);
    }
    return com;
}

// The object's own vtables back (after InstallVtables).
void RestoreVtables(cGZCOM *com, int side)
{
    for (int f = 0; f < 3; ++f) Word<void **>(com, kFaceOffset[f]) = gVt[side].com[f];
}

void RestoreLibVtables(cGZCOM *com, int side)
{
    for (auto &lib : com->mLibraries) Word<void **>(&lib, 0) = gVt[side].lib;
}

// Tears an object down with its own side's destructor (and frees it).
void DestroyCom(difftest::Context &ctx, cGZCOM *com, int side)
{
    RestoreVtables(com, side);
    RestoreLibVtables(com, side);
    com->mCritSect.mVTable = GameAddress(cRZCriticalSection::kVTable);
    if (side == kOriginal) {
        ctx.Original<void(__thiscall *)(void *)>(kDtor)(com);
    } else {
        com->cGZCOM::~cGZCOM();
    }
    ::operator delete(com);
}

// Copies of the side's own vtables with logging fakes, so the calls the code makes through `this` are seen.
void InstallVtables(cGZCOM *com, int side)
{
    gBase = reinterpret_cast<unsigned char *>(com);
    std::memcpy(gPrimaryVt, gVt[side].com[0], sizeof(gPrimaryVt));
    std::memcpy(gSysVt, gVt[side].com[1], sizeof(gSysVt));
    std::memcpy(gUnkVt, gVt[side].com[2], sizeof(gUnkVt));
    gPrimaryVt[1] = gSysVt[1] = gUnkVt[1] = reinterpret_cast<void *>(&FakeComAddRef);
    gUnkVt[4] = reinterpret_cast<void *>(&FakeUnknownRemoveRef);
    Word<void **>(com, 0) = gPrimaryVt;
    Word<void **>(com, 4) = gSysVt;
    Word<void **>(com, 8) = gUnkVt;
}

void InstallCritVtable(difftest::Context &ctx, cGZCOM *com)
{
    std::memcpy(gCritVt, ctx.image.At<void *>(cRZCriticalSection::kVTable), sizeof(gCritVt));
    gCritVt[1] = reinterpret_cast<void *>(&FakeLock);
    gCritVt[2] = reinterpret_cast<void *>(&FakeUnlock);
    com->mCritSect.mVTable = gCritVt;
}

void InstallLibVtable(cGZCOMLibrary *lib, int side)
{
    std::memcpy(gLibVt, gVt[side].lib, sizeof(gLibVt));
    gLibVt[1] = reinterpret_cast<void *>(&FakeLibAddRef);
    Word<void **>(lib, 0) = gLibVt;
}

void InstallAllLibVtables(cGZCOM *com, int side)
{
    for (auto &lib : com->mLibraries) InstallLibVtable(const_cast<cGZCOMLibrary *>(&lib), side);
}

// A library with this path as the side's constructor would leave it, in storage the holder owns. The fields are set
// last, by assignment, so that a fault in a constructor under test cannot also change the inputs the tests build.
struct Lib {
    alignas(cGZCOMLibrary) unsigned char storage[sizeof(cGZCOMLibrary)];
    cGZCOMLibrary *p;
    Lib(const char *path, int side, bool initialized = false, void *director = nullptr, void *module = nullptr, int refCount = 0)
    {
        InitVtables();
        cRZString text;
        text.Construct(path);
        p = ::new (storage) cGZCOMLibrary(*text.AsInterface());
        text.Destruct();
        Word<void **>(p, 0) = gVt[side].lib;
        p->mInitialized = initialized;
        p->mDirector = static_cast<cIGZCOMDirector *>(director);
        p->mModule = module;
        p->mRefCount = refCount;
    }
    ~Lib()
    {
        p->mPath.mVTable = GameAddress(cRZString::kVTable);
        p->cGZCOMLibrary::~cGZCOMLibrary();
    }
    Lib(const Lib &) = delete;
    Lib &operator=(const Lib &) = delete;
    cGZCOMLibrary *operator->() const { return p; }
};

// A library added to the object's set (a node copy, carrying the side's library vtable).
cGZCOMLibrary *Add(cGZCOM *com, int side, const char *path, bool initialized = false, void *director = nullptr, void *module = nullptr, int refCount = 0)
{
    Lib lib(path, kOurs);
    auto *node = const_cast<cGZCOMLibrary *>(&*com->mLibraries.insert(*lib.p).first);
    Word<void **>(node, 0) = gVt[side].lib;
    node->mInitialized = initialized;
    node->mDirector = static_cast<cIGZCOMDirector *>(director);
    node->mModule = module;
    node->mRefCount = refCount;
    return node;
}

cGZCOMLibrary *FindLib(cGZCOM *com, const char *name)
{
    for (auto &l : com->mLibraries) {
        if (l.mPath.mString == name) return const_cast<cGZCOMLibrary *>(&l);
    }
    return nullptr;
}

// ---- descriptions ----

std::string Describe(const cGZCOM *com)
{
    std::string s;
    char buf[160];
    for (auto &lib : com->mLibraries) {
        std::snprintf(buf, sizeof(buf), "lib[%s init=%d ref=%d dir=%p mod=%p] ", lib.mPath.mString.c_str(), Word<uint8_t>(&lib, 4), lib.mRefCount,
                      static_cast<void *>(lib.mDirector), lib.mModule);
        s += buf;
    }
    for (auto &c : com->mClasses) {
        std::snprintf(buf, sizeof(buf), "class[%x v%u -> %s] ", c.first, c.second.mVersion, c.second.mLibrary ? PathOf(c.second.mLibrary).c_str() : "null");
        s += buf;
    }
    std::snprintf(buf, sizeof(buf), "cur=%s init=%d fw=%p dir=%p rt=%d/%d f30=%x last=%x", com->mCurrentLibrary ? PathOf(com->mCurrentLibrary).c_str() : "null",
                  Word<uint8_t>(com, 0x2c), static_cast<void *>(com->mFramework), static_cast<void *>(com->mDirector), com->mRefCount,
                  Word<uint8_t>(com, 0x1c), com->mField30, com->mLastGuidTime);
    s += buf;
    return s;
}

// The object's bytes with the heap pointers masked (the lock, its block, the two container heads, the clock-based
// seed) and the vtable words named (the side's own vtables are the only bytes that differ by design).
std::vector<unsigned char> Canon(const cGZCOM *com, int side)
{
    std::vector<unsigned char> bytes(sizeof(cGZCOM));
    std::memcpy(bytes.data(), com, sizeof(cGZCOM));
    for (uint32_t off : {0x20u, 0x28u, 0x34u, 0x3cu, 0x50u, 0x54u}) std::memset(bytes.data() + off, 0, 4);
    NormComVtables(bytes, 0, side);
    return bytes;
}

// A head node is its own neighbour on all three links, with the "nil" flags set.
bool HeadOk(const void *head)
{
    auto *n = static_cast<const uintptr_t *>(head);
    auto *flags = static_cast<const uint16_t *>(head);
    return n[0] == reinterpret_cast<uintptr_t>(head) && n[1] == n[0] && n[2] == n[0] && flags[6] == 0x0101;
}

} // namespace

// Compares the two sides' outputs: returns, descriptions, logs. `what` names the case.
#define CHECK_SAME(ctx, what, a, b)                                                                  \
    do {                                                                                              \
        ++(ctx).cases;                                                                                \
        if (!((a) == (b))) (ctx).Fail("%s: %s", (what), "mismatch " #a);                              \
    } while (0)

namespace {

void FailText(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, a.c_str(), b.c_str());
}

std::string Joined(const std::vector<std::string> &log)
{
    std::string s;
    for (auto &l : log) s += l + " | ";
    return s;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) FailText(ctx, what, a, b);
}

void ExpectSameBytes(difftest::Context &ctx, const char *what, const std::vector<unsigned char> &a, const std::vector<unsigned char> &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t i = 0;
        while (i < a.size() && a[i] == b[i]) ++i;
        ctx.Fail("%s: bytes differ first at +0x%zx (%02x vs %02x)", what, i, a[i], b[i]);
    }
}

} // namespace
