#define _CRT_SECURE_NO_WARNINGS
// Differential test for ts::cTSAppFW::Init (0xc27c3e), lane FW: both routes (own vtable: each side's application from its
// own constructor, called through its own cIGZSystemService slot 8; adapter: game-built applications, the adapter called,
// its own slot trapped in the copy of the game's vtable).
//
// Everything with effects outside the process is a fake on both sides: the imports (GetCommandLineW, GetDiskFreeSpaceExA,
// RegOpenKeyExA / RegQueryValueExA / RegCloseKey / RegCreateKeyExA / RegSetValueExA, in the image's import table), the
// control files (cRZFile's game vtable: SetPath slot 29 and Exists slot 22; AddRef / Release logged and run), the product
// queries 0xcb4cc0 / 0xcb4690 (and ours) / 0xc2896d, the directory checks (0x1c4f94 and ours, the scan 0x1c48da / 0x1c49eb and ours), the
// string-table texts (cTSString::Construct 0x1d0e90 and ours), 0xb417c0, the disk-space question 0xc2dbf1 and the fatal
// message box (0xc2da5a and ours: never shown). The framework (g_Framework 0x10c9378: GetSystemService slot 5, AddToTick
// slot 9, CommandLine slot 21), the cached globals (0x20ddfb and ours: slots 31 and 21), the option getter (0x1d0c00 and
// ours), the message server 0x9c5a89, RZMalloc (0x1be3b0 and ours, a static block) with the standard message's game
// vtable patched to log, the configuration's 0xcb1780 and the sysMem setter 0x1c9e49 are logging fakes. The image's
// operator new / sized delete go to ours (so files and property sets made on one side can be freed by either code).
// Compared: al, the log (every string argument's text), the application digest and the message block.
#include "rz/RZMessageServer.h"
#include "app_fixture.h"

#include "app/AppServices2.h"
#include "app/ProductInfo.h"
#include "ts/cTSAppFWHelpers.h"
#include "app/WinMain.h"
#include "rz/DirectoryScan.h"
#include "gz/cGZPropertySet.h"
#include "rz/Singleton_20ddfb.h"
#include "ts/cTSAppPropertyFiles.h"
#include "ts/cTSString.h"

#include <utility>

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
const char *Text(const cRZString *s) { return s ? s->mString.c_str() : "(null)"; }

// ---- generic fake objects: every slot logs; arity-specific slots are patched in

struct Fake {
    void **vt;
    const char *name;
    uint32_t answer[48];
};
void *gFwVt[48], *gGlobalsVt[48], *gTargetVt[48], *gDelegateVt[48], *gOldVt[48], *gServerVt[48];
Fake gFw{gFwVt, "fw", {}}, gGlobals{gGlobalsVt, "globals", {}}, gTarget{gTargetVt, "target", {}};
Fake gDelegate{gDelegateVt, "delegate", {}}, gOld{gOldVt, "old", {}}, gServer{gServerVt, "server", {}};
alignas(8) uint8_t gMessageBlock[0x40];
alignas(8) uint8_t gConfiguration[0x10];

std::string Who(const void *q)
{
    const auto *p = static_cast<const uint8_t *>(q);
    if (!p) return "null";
    if (gAppBase && p >= gAppBase && p < gAppBase + 0x268) return "@" + std::to_string(p - gAppBase);
    for (Fake *f : {&gFw, &gGlobals, &gTarget, &gDelegate, &gOld, &gServer})
        if (q == f) return f->name;
    if (p == gMessageBlock) return "message";
    if (p == gConfiguration) return "configuration";
    char b[16];
    std::snprintf(b, sizeof(b), "%p", q);
    return b;
}

template <int N> uint32_t __fastcall S0(void *self, void *)
{
    auto *f = static_cast<Fake *>(self);
    Log("%s.s%d", f->name, N);
    return f->answer[N];
}
template <int N> uint32_t __fastcall S1(void *self, void *, void *a)
{
    auto *f = static_cast<Fake *>(self);
    Log("%s.s%d(%s)", f->name, N, Who(a).c_str());
    return f->answer[N];
}
template <size_t... I> void FillS0(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = reinterpret_cast<void *>(&S0<int(I)>)), ...);
}

// ---- the framework, the globals and their target

bool gDelegatePresent;
uint32_t __fastcall FwGetService(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    Log("fw.GetSystemService(%x, %x, %s)", id, iid, *out ? "set" : "null");
    if (id == 0x9b3798f7 && iid == 0xfb3798e8 && gDelegatePresent) {
        *out = &gDelegate;
        return 1;
    }
    return 0;
}
uint32_t __fastcall FwAddToTick(void *, void *, void *service)
{
    Log("fw.AddToTick(%s)", Who(service).c_str());
    return 1;
}
CmdLineDouble gCmd;
void *__fastcall FwCommandLine(void *, void *)
{
    Log("fw.CommandLine");
    return &gCmd;
}
void *__cdecl FakeGetGlobals()
{
    Log("GetCachedSingleton");
    return &gGlobals;
}
void *__fastcall GlobalsTarget(void *, void *)
{
    Log("globals.s21");
    return &gTarget;
}
// A property set: its vtable kind (each side's own) and reference count.
std::string Props(difftest::Context &ctx, const void *p);
difftest::Context *gCtx;
const void *gOurPropsVt;
uint32_t __fastcall TargetSetProperties(void *, void *, void *props)
{
    Log("target.s17(%s)", Props(*gCtx, props).c_str());
    return 0;
}
void __fastcall FakeConfigurationSet(void *self, void *, void *props)
{
    Log("0xcb1780(%s, %s)", Who(self).c_str(), Props(*gCtx, props).c_str());
}
std::string Props(difftest::Context &ctx, const void *p)
{
    if (!p) return "null";
    const void *vt = *static_cast<void *const *>(p);
    // The game's set (0x22b157 is not hooked in the harness) or ours: the same class.
    const char *kind = vt == ctx.image.At<void>(0xd9f718) || vt == gOurPropsVt ? "set" : "?";
    char b[64];
    std::snprintf(b, sizeof(b), "props %s rc %u count %u", kind, static_cast<const uint32_t *>(p)[1], static_cast<const uint32_t *>(p)[2] == static_cast<const uint32_t *>(p)[3] ? 0u : 1u);
    return b;
}

// ---- the message and its server

void *__cdecl FakeMalloc(uint32_t bytes)
{
    Log("Malloc(%x)", bytes);
    std::memset(gMessageBlock, 0x77, sizeof(gMessageBlock));
    return gMessageBlock;
}
uint32_t __fastcall MessageAddRef(void *self, void *)
{
    Log("%s.AddRef", Who(self).c_str());
    return 1;
}
uint32_t __fastcall MessageRelease(void *self, void *)
{
    Log("%s.Release", Who(self).c_str());
    return 0;
}
template <int N> uint32_t __fastcall MessageSetWord(void *self, void *, uint32_t v)
{
    Log("%s.s%d(%x)", Who(self).c_str(), N, v);
    return 0;
}
uint32_t __fastcall MessageSetText(void *self, void *, cRZString *text)
{
    Log("%s.s30(%s)", Who(self).c_str(), text ? Text(text) : "null");
    if (text) {
        text->Destruct();
        ::operator delete(text, 0x20);
    }
    return 0;
}
void *__cdecl FakeGetServer()
{
    Log("GetMessageServer");
    return &gServer;
}
uint32_t __fastcall ServerPost(void *, void *, void *message, uint32_t flags)
{
    Log("server.s4(%s, %x)", Who(message).c_str(), flags);
    return 1;
}
uint32_t __fastcall ServerSlot11(void *, void *, uint32_t v)
{
    Log("server.s11(%x)", v);
    return 1;
}
void __cdecl FakeSysMem(int a, int b)
{
    Log("sysMem(%d, %d)", a, b);
}

// ---- imports

const wchar_t *gCommandLineW = L"";
const wchar_t *WINAPI FakeGetCommandLineW()
{
    Log("GetCommandLineW");
    return gCommandLineW;
}
bool gDiskOk;
uint64_t gFreeBytes;
BOOL WINAPI FakeGetDiskFreeSpaceExA(const char *path, ULARGE_INTEGER *available, ULARGE_INTEGER *total, ULARGE_INTEGER *free)
{
    Log("GetDiskFreeSpaceExA(%s, %llx %llx %llx)", path, available->QuadPart, total->QuadPart, free->QuadPart);
    if (gDiskOk) {
        available->QuadPart = gFreeBytes / 2;
        total->QuadPart = gFreeBytes * 3;
        free->QuadPart = gFreeBytes;
    }
    return gDiskOk ? TRUE : FALSE;
}
LONG gOpenResult;
LONG WINAPI FakeRegOpenKeyExA(HKEY root, const char *key, DWORD options, REGSAM access, HKEY *out)
{
    Log("RegOpenKeyExA(%x, %s, %u, %x) -> %d", P(root), key, options, access, gOpenResult);
    if (gOpenResult == 0) *out = reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x1001));
    return gOpenResult;
}
std::string gDisplayName;
LONG WINAPI FakeRegQueryValueExA(HKEY key, const char *name, DWORD *reserved, DWORD *type, BYTE *data, DWORD *size)
{
    Log("RegQueryValueExA(%x, %s, %s, %u)", P(key), name, reserved ? "?" : "null", *size);
    *type = REG_SZ;
    std::memcpy(data, gDisplayName.c_str(), gDisplayName.size() + 1);
    *size = static_cast<DWORD>(gDisplayName.size() + 1);
    return 0;
}
LONG WINAPI FakeRegCloseKey(HKEY key)
{
    Log("RegCloseKey(%x)", P(key));
    return 0;
}
LONG gCreateResults[3], gSetResults[3];
int gCreates, gSets;
LONG WINAPI FakeRegCreateKeyExA(HKEY root, const char *key, DWORD reserved, char *cls, DWORD options, REGSAM access, void *security, HKEY *out, DWORD *disposition)
{
    const LONG r = gCreateResults[gCreates % 3];
    Log("RegCreateKeyExA(%x, %s, %u, %s, %u, %x, %s) -> %d", P(root), key, reserved, cls ? "?" : "null", options, access, security ? "?" : "null", r);
    if (r == 0) {
        *out = reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x2000 + gCreates));
        *disposition = 1;
    }
    ++gCreates;
    return r;
}
LONG WINAPI FakeRegSetValueExA(HKEY key, const char *name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    const LONG r = gSetResults[gSets++ % 3];
    Log("RegSetValueExA(%x, %s, %u, %u, %s, %u) -> %d", P(key), name ? name : "null", reserved, type, reinterpret_cast<const char *>(data), size, r);
    return r;
}

// ---- the product and the files

uint8_t gNewerAnswer;
const char *gNewerBuffer;
uint32_t __cdecl FakeNewerProduct(char *executable, uint32_t *out)
{
    Log("0xcb4cc0(%s)", out ? "out" : "null");
    std::strcpy(executable, "Sims2EP9.exe");
    gNewerBuffer = executable;
    return 0x6600u | gNewerAnswer;
}
std::string gKeyText, gDirectoryText;
uint32_t __cdecl FakeProductPaths(const char *executable, char *key, uint32_t *keyLength, char *directory, uint32_t *directoryLength, void *a, void *b, void *c)
{
    const char *which = executable == *gCtx->image.At<const char *>(0x10819b8) ? "exe" : executable == gNewerBuffer ? "newer" : "?";
    Log("0xcb4690(%s, %s %d, %s %d, %s %s %s)", which, key ? "key" : "null", keyLength ? static_cast<int>(*keyLength) : -1,
        directory ? "dir" : "null", directoryLength ? static_cast<int>(*directoryLength) : -1, a ? "?" : "0", b ? "?" : "0", c ? "?" : "0");
    if (key && keyLength) {
        std::strcpy(key, gKeyText.c_str());
        *keyLength = static_cast<uint32_t>(gKeyText.size());
    }
    if (directory && directoryLength) {
        std::strcpy(directory, gDirectoryText.c_str());
        *directoryLength = static_cast<uint32_t>(gDirectoryText.size());
    }
    return 0x6601;
}
uint32_t __cdecl FakeElevation()
{
    Log("0xc2896d");
    return 0x55;
}

void *gRealFileSlots[3];
uint8_t gExists[2];
int gExistsCalls;
uint32_t __fastcall FileAddRef(void *self, void *)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealFileSlots[1])(self);
    Log("file.AddRef -> %u", r);
    return r;
}
uint32_t __fastcall FileRelease(void *self, void *)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealFileSlots[2])(self);
    Log("file.Release -> %u", r);
    return r;
}
uint32_t __fastcall FileExists(void *, void *)
{
    const uint8_t a = gExists[gExistsCalls++ % 2];
    Log("file.Exists -> %u", a);
    return 0x3300u | a;
}
void __fastcall FileSetPath(void *, void *, const cRZString *path)
{
    Log("file.SetPath(%s)", Text(path));
}

// ---- options, directories, texts, boxes

std::string gControlDirectory, gCheckDirectory, gUserData;
uint8_t gCheckAnswer;
uint32_t __cdecl FakeGetOption(uint32_t key, uint32_t out, uint32_t c, uint32_t d, uint32_t e)
{
    auto *s = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(out));
    Log("GetOption(%x, [%s], %u, %u, %x)", key, Text(s), c, d, e);
    if (key == 0x2ce990aa) s->mString = gControlDirectory;
    else if (key == 0x4bcfce5e) {
        s->mString = gCheckDirectory;
        return 0x7700u | gCheckAnswer;
    } else if (key == 0x2bcfce83) s->mString = gUserData;
    return 0x7701;
}
uint8_t gDirectoryExists;
uint32_t __cdecl FakeDirectoryExists(cRZString *path)
{
    Log("DirectoryExists(%s)", Text(path));
    return 0x4400u | gDirectoryExists;
}
uint32_t gScanCount;
void *__fastcall FakeScanBuild(uint8_t *self, void *, cRZString *path, uint32_t now, uint32_t a, uint32_t b)
{
    Log("scan.Build(%s, %u, %u, %u)", Text(path), now & 0xff, a & 0xff, b & 0xff);
    std::memset(self, 0x5c, 0x5c);
    std::memcpy(self + 8, &gScanCount, 4);
    return self;
}
void __fastcall FakeScanDestroy(void *, void *)
{
    Log("scan.Destroy");
}

bool gBodyShopTextEmpty;
cTSString *__fastcall FakeTSStringConstruct(cTSString *self, void *, uint32_t table, uint32_t entry, uint32_t group, uint32_t type)
{
    Log("cTSString(%x, %x, %x, %x)", table & 0xffff, entry & 0xffff, group, type);
    char text[64];
    if ((table & 0xffff) == 0xb5 && (entry & 0xffff) == 9) std::snprintf(text, sizeof(text), "Newer product: %%s.");
    else if ((table & 0xffff) == 0xcd && (entry & 0xffff) == 1 && gBodyShopTextEmpty) text[0] = 0;
    else std::snprintf(text, sizeof(text), "text %x/%u", table & 0xffff, entry & 0xffff);
    self->mString.Construct(text);
    self->mResourceType = type;
    self->mGroup = group;
    self->mEntry = static_cast<uint16_t>(entry);
    self->mTable = static_cast<uint16_t>(table);
    self->mKeyWord3 = 0;
    self->mLoaded = text[0] != 0;
    self->mLoadFlags = 0;
    return self;
}
uint32_t __cdecl FakeDefaultProductText(cRZString *s)
{
    Log("0xb417c0([%s])", Text(s));
    s->mString = "Body Shop default";
    return 1;
}
uint8_t gGoOn;
uint32_t __cdecl FakeQuestion(const char *text, const char *caption)
{
    Log("question(%s, %s) -> %u", text, caption, gGoOn);
    return 0x1100u | gGoOn;
}
// The same over ours (lane MB): winmain::AskYesNo returns a bool, which the ABI holds as 0 / 1 in al (the original
// 0xc2dbf1 sets al with sete too), so the junk-byte answers above only make sense to the original Init's own test.
bool __cdecl FakeQuestionOurs(const char *text, const char *caption)
{
    FakeQuestion(text, caption);
    return gGoOn != 0;
}
void __cdecl FakeFatal(const char *text, const char *caption)
{
    Log("FATAL(%s, %s)", text, caption);
}

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

struct World {
    difftest::Context &ctx;
    std::vector<std::unique_ptr<Redirect>> redirects;
    std::vector<std::unique_ptr<IatPatch>> imports;
    void **framework;
    void *savedFramework;
    void *savedMessageVt[32];
    void *savedFileVt[30];
    uint8_t savedCaption[0x20];
    explicit World(difftest::Context &c) : ctx(c)
    {
        gCtx = &c;
        for (void **vt : {gFwVt, gGlobalsVt, gTargetVt, gDelegateVt, gOldVt, gServerVt}) FillS0(vt, std::make_index_sequence<48>());
        gFwVt[5] = reinterpret_cast<void *>(&FwGetService);
        gFwVt[9] = reinterpret_cast<void *>(&FwAddToTick);
        gFwVt[21] = reinterpret_cast<void *>(&FwCommandLine);
        gGlobalsVt[21] = reinterpret_cast<void *>(&GlobalsTarget);
        gTargetVt[17] = reinterpret_cast<void *>(&TargetSetProperties);
        gServerVt[4] = reinterpret_cast<void *>(&ServerPost);
        gServerVt[11] = reinterpret_cast<void *>(&ServerSlot11);
        Add(c.image.At(0x824632), &ImageNew);
        Add(c.image.At(0x824624), &ImageSizedDelete);
        Add(c.image.At(0xcb4cc0), &FakeNewerProduct);
        Add(c.image.At(0xcb4690), &FakeProductPaths);
        Add(reinterpret_cast<void *>(&product::FindNewerProduct), &FakeNewerProduct); // ours (lane MB)
        Add(reinterpret_cast<void *>(&product::GetProductPaths), &FakeProductPaths);
        Add(c.image.At(0xc2896d), &FakeElevation);
        Add(reinterpret_cast<void *>(&appfw::IsUserAdministrator), &FakeElevation); // ours (lane MB)
        Add(c.image.At(0x20ddfb), &FakeGetGlobals);
        Add(reinterpret_cast<void *>(&GetCachedSingleton), &FakeGetGlobals);
        Add(c.image.At(0x1d0c00), &FakeGetOption);
        Add(reinterpret_cast<void *>(&appinit::CommonImplSlot4), &FakeGetOption);
        Add(c.image.At(0x1c4f94), &FakeDirectoryExists);
        Add(reinterpret_cast<void *>(&as2::DirectoryExists), &FakeDirectoryExists);
        Add(c.image.At(0x1c48da), &FakeScanBuild);
        Add(c.image.At(0x1c49eb), &FakeScanDestroy);
        Add(reinterpret_cast<void *>(&DirectoryScanAdapters::Construct), &FakeScanBuild); // ours (lane LY)
        Add(reinterpret_cast<void *>(&DirectoryScanAdapters::Destruct), &FakeScanDestroy);
        Add(c.image.At(0x1d0e90), &FakeTSStringConstruct);
        Add(MemberAddress(static_cast<cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint32_t)>(&cTSString::Construct)), &FakeTSStringConstruct);
        Add(c.image.At(0xb417c0), &FakeDefaultProductText);
        Add(reinterpret_cast<void *>(&appfw::BodyShopProductName), &FakeDefaultProductText); // ours (lane MB)
        Add(c.image.At(0xc2dbf1), &FakeQuestion);
        Add(reinterpret_cast<void *>(&winmain::AskYesNo), &FakeQuestionOurs); // ours (lane MB)
        Add(c.image.At(0xc2da5a), &FakeFatal);
        Add(reinterpret_cast<void *>(&winmain::ShowFatalMessage), &FakeFatal);
        Add(c.image.At(0x1be3b0), &FakeMalloc);
        Add(reinterpret_cast<void *>(&RZMemory::Malloc), &FakeMalloc);
        Add(c.image.At(0x9c5a89), &FakeGetServer);
        Add(reinterpret_cast<void *>(&GetCachedMessageServer), &FakeGetServer); // ours
        Add(c.image.At(0xcb1780), &FakeConfigurationSet);
        Add(MemberAddress(&ts::cTSAppPropertyFiles::LoadGlobalInto), &FakeConfigurationSet); // ours (lane MA)
        Add(c.image.At(0x1c9e49), &FakeSysMem);
        Import(0xd9a398, &FakeGetCommandLineW);
        Import(0xd9a3e0, &FakeGetDiskFreeSpaceExA);
        Import(0xd9a028, &FakeRegOpenKeyExA);
        Import(0xd9a054, &FakeRegQueryValueExA);
        Import(0xd9a02c, &FakeRegCloseKey);
        Import(0xd9a030, &FakeRegCreateKeyExA);
        Import(0xd9a024, &FakeRegSetValueExA);
        framework = c.image.At<void *>(0x10c9378);
        savedFramework = *framework;
        *framework = &gFw;
        void **messageVt = c.image.At<void *>(0xda4064);
        std::memcpy(savedMessageVt, messageVt, sizeof(savedMessageVt));
        messageVt[1] = reinterpret_cast<void *>(&MessageAddRef);
        messageVt[2] = reinterpret_cast<void *>(&MessageRelease);
        messageVt[5] = reinterpret_cast<void *>(&MessageSetWord<5>);
        messageVt[11] = reinterpret_cast<void *>(&MessageSetWord<11>);
        messageVt[16] = reinterpret_cast<void *>(&MessageSetWord<16>);
        messageVt[30] = reinterpret_cast<void *>(&MessageSetText);
        void **fileVt = c.image.At<void *>(0xda42a0);
        std::memcpy(savedFileVt, fileVt, sizeof(savedFileVt));
        for (int i = 0; i < 3; ++i) gRealFileSlots[i] = fileVt[i];
        fileVt[1] = reinterpret_cast<void *>(&FileAddRef);
        fileVt[2] = reinterpret_cast<void *>(&FileRelease);
        fileVt[22] = reinterpret_cast<void *>(&FileExists);
        fileVt[29] = reinterpret_cast<void *>(&FileSetPath);
        auto *caption = c.image.At<cRZString>(0x10fda54);
        std::memcpy(savedCaption, caption, sizeof(savedCaption));
        caption->Construct("The Sims 2");
        // Our property set's vtable, from one of ours.
        ::cGZPropertySetString probe;
        gOurPropsVt = *reinterpret_cast<void **>(&probe);
        gMoreSwitches.clear();
        for (const char *name : {"load", "sysMem"}) gMoreSwitches.push_back({name, Switch{}});
    }
    ~World()
    {
        auto *caption = ctx.image.At<cRZString>(0x10fda54);
        caption->Destruct();
        std::memcpy(caption, savedCaption, sizeof(savedCaption));
        std::memcpy(ctx.image.At<void *>(0xda42a0), savedFileVt, sizeof(savedFileVt));
        std::memcpy(ctx.image.At<void *>(0xda4064), savedMessageVt, sizeof(savedMessageVt));
        *framework = savedFramework;
        imports.clear();
        while (!redirects.empty()) redirects.pop_back();
        gMoreSwitches.clear();
    }
    template <class F> void Add(void *at, F *fake) { redirects.push_back(std::make_unique<Redirect>(at, reinterpret_cast<void *>(fake))); }
    template <class F> void Import(uint32_t rva, F *fake) { imports.push_back(std::make_unique<IatPatch>(ctx, rva, reinterpret_cast<const void *>(fake))); }
};

std::string RouteName(Route route) { return route == Route::Adapter ? "adapter" : "own vtable"; }

const wchar_t *const kCommandLines[] = {L"Sims2EP9.exe", L"Sims2EP9.exe -activeUserDataDir:C:/x", L"\"C:\\Program Files\\EA\\Sims2EP9.exe\" -w -activeUserDataDir",
                                        L"Sims2EP9.exe -activeUserDataDirX"};

std::string PathText(std::mt19937 &r, const char *stem)
{
    switch (r() % 4) {
    case 0: return "";
    case 1: return std::string(stem).substr(0, 1 + r() % 6);
    default: return std::string("C:/Users/Someone/Documents/EA Games/") + stem + std::to_string(r() % 1000) + "/";
    }
}

uint8_t Answer(std::mt19937 &r, int zeroOneIn)
{
    if (r() % zeroOneIn == 0) return 0;
    return static_cast<uint8_t>(r() % 2 ? 1 : (r() | 1));
}

uint8_t SwitchAnswer(std::mt19937 &r)
{
    switch (r() % 3) {
    case 0: return 0;
    case 1: return static_cast<uint8_t>(r() | 1);
    default: return static_cast<uint8_t>((r() | 2) & 0xfe);
    }
}

} // namespace

// ---- 0xc27c3e Init (cIGZSystemService slot 8)

DIFF_TEST(cTSAppFW_Init, 0xc27c3e)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    World world(ctx);
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 400; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 3301 + static_cast<int>(route));
                const bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                if (route == Route::Adapter) app.SetSlot(0, 8, reinterpret_cast<const void *>(&TrapNoArgs));
                // Mostly a healthy installation, so that the start-up half runs often.
                const bool healthy = r() % 3 != 0;
                app.raw[0x6d] = healthy || r() % 3 ? 0 : static_cast<uint8_t>(r() | 1);
                app.raw[0x6e] = healthy || r() % 3 ? 0 : static_cast<uint8_t>(r() | 1);
                app.raw[0x6c] = static_cast<uint8_t>(r() % 2);
                *reinterpret_cast<void **>(app.raw + 0x60) = gConfiguration;
                *reinterpret_cast<void **>(app.raw + 0x80) = r() % 2 ? &gOld : nullptr;
                gNewerAnswer = healthy ? 0 : Answer(r, 2);
                gCommandLineW = kCommandLines[r() % 4];
                gControlDirectory = PathText(r, "Config");
                gExists[0] = healthy ? 1 : Answer(r, 3);
                gExists[1] = healthy ? static_cast<uint8_t>(r() | 1) : Answer(r, 3);
                gExistsCalls = 0;
                gGlobals.answer[31] = healthy || r() % 2 ? 0x2200 : 0x2200u | Answer(r, 2);
                gKeyText = healthy || r() % 2 ? "Software\\Electronic Arts\\The Sims 2 Ultimate Collection 25\\" : (r() % 2 ? "" : "K");
                gDirectoryText = r() % 4 == 0 && !healthy ? "" : (r() % 2 ? "C:\\Program Files\\EA GAMES\\The Sims 2 Ultimate Collection" : "D:");
                gCheckDirectory = PathText(r, "UserData");
                gCheckAnswer = healthy ? 1 : Answer(r, 3);
                gDirectoryExists = healthy ? 1 : Answer(r, 3);
                gScanCount = healthy ? 1 + r() % 5 : r() % 3;
                gBodyShopTextEmpty = r() % 2;
                gOpenResult = r() % 3 ? 0 : 2;
                gDisplayName = r() % 2 ? "The Sims 2 Mansion and Garden" : "";
                gUserData = PathText(r, "The Sims 2");
                gDiskOk = r() % 5 != 0;
                const uint64_t kSizes[] = {0, 1999999999ull, 2000000000ull, 0x100000005ull, 0x7fffffffffull};
                gFreeBytes = kSizes[healthy ? 2 + r() % 3 : r() % 5];
                gGoOn = Answer(r, 2);
                for (LONG &x : gCreateResults) x = r() % 4 ? 0 : 5;
                for (LONG &x : gSetResults) x = r() % 4 ? 0 : 5;
                gCreates = gSets = 0;
                gDelegatePresent = r() % 3 != 0;
                gSwitches[3].al = r() % 3 ? 0 : SwitchAnswer(r); // installUserData
                gMoreSwitches[0].second.al = r() % 2 ? 0 : SwitchAnswer(r);
                gMoreSwitches[0].second.value = r() % 2 ? "Neighborhoods/N001/N001_Neighborhood.package" : "N001;N002;N003";
                gMoreSwitches[1].second.al = r() % 2 ? 0 : SwitchAnswer(r);
                gMoreSwitches[1].second.value = r() % 3 ? std::to_string(r() % 4096) : "abc";
                std::memset(gMessageBlock, 0x55, sizeof(gMessageBlock));
                gNewerBuffer = nullptr;
                gLog.clear();
                uint32_t result;
                if (route == Route::Adapter)
                    result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc27c3e)(app.raw)
                                      : ts::cTSAppFWAdapters::Init(reinterpret_cast<ts::cTSAppFW *>(app.raw), nullptr);
                else
                    result = CallSlot<uint32_t>(app.Face(0), 8);
                uint8_t block[0x2c];
                std::memcpy(block, gMessageBlock, sizeof(block));
                const bool gameVt = *reinterpret_cast<void **>(block) == ctx.image.At<void>(0xda4064);
                std::memset(block, 0, 4);
                results[side] = Outcome(result, app.Digest()) + (gameVt ? "msgvt " : "other ") + Hex(block, sizeof(block));
            }
            ExpectSame(ctx, (RouteName(route) + " Init " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}
