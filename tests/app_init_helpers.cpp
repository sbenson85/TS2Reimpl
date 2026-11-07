// Differential tests for the startup helpers in src/app/AppInit.* :
//   0x9c5a5d  the language service getter (CachedServiceGetter instantiation)
//   0xc29a0b  InitLanguage (command-line switch "l", else the Steam language name)
//   0x1d0c00, 0x1d0c60, 0x1d0d10  forwarders to the cTSAppCommonImpl interface (slots 4, 5, 3)
//
// Method: the framework object (game global 0x10c9378) is a fake with a recording vtable; the
// language service and cTSAppCommonImpl objects are fakes as well. The original and ours run on the
// same globals and the recorded call logs, return values and globals afterwards are compared. The
// Steam import (IAT slot 0xd9a700) is replaced in the mapped image with a fake: the original reads
// it from there and ours reads the same slot through GameAddress.
#include "difftest.h"
#include "app/AppInit.h"
#include "rz/cRZCmdLine.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

struct Call {
    int what;
    const void *self;
    uint32_t a, b, c, d, e;
    std::string text;
    bool operator==(const Call &o) const
    {
        return what == o.what && self == o.self && a == o.a && b == o.b && c == o.c && d == o.d && e == o.e && text == o.text;
    }
};

std::vector<Call> gCalls;

enum : int { kFwCmdLine = 1, kFwApp, kFwQuery, kLangSet, kLangLookup, kQuery, kRelease, kSlot3, kSlot4, kSlot5, kSteamInit, kSteamLanguage, kFwQueryService };

struct FakeObject {
    void **vtable;
    uint32_t pad[3];
};

// ---- the framework fake --------------------------------------------------------------------

void *gCmdLineResult = nullptr;
void *gAppResult = nullptr;
void *gQueryResult = nullptr;

void *__fastcall FwGetCmdLine(void *self, void *)
{
    gCalls.push_back({kFwCmdLine, self, 0, 0, 0, 0, 0, {}});
    return gCmdLineResult;
}
void *__fastcall FwGetApp(void *self, void *)
{
    gCalls.push_back({kFwApp, self, 0, 0, 0, 0, 0, {}});
    return gAppResult;
}
uint32_t __fastcall FwQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({kFwQuery, self, a, b, 0, 0, 0, {}});
    *out = gQueryResult;
    return 0;
}

void *gFwVtable[64];
FakeObject gFramework;

// ---- the language service fake -----------------------------------------------------------------

bool gLookupResult = false;
bool gLookupWrites = false;
int32_t gLookupId = 0;
uint32_t __fastcall LangSet(void *self, void *, int32_t id)
{
    gCalls.push_back({kLangSet, self, static_cast<uint32_t>(id), 0, 0, 0, 0, {}});
    return 0xAA55AA00u + static_cast<uint32_t>(id & 0xff);
}
uint32_t __fastcall LangLookup(void *self, void *, const cRZString *name, int32_t *id)
{
    gCalls.push_back({kLangLookup, self, name->mVTable != nullptr, 0, 0, 0, 0, name->mString});
    if (gLookupWrites) *id = gLookupId;
    return gLookupResult ? 0xFFFFFF01u : 0xFFFFFF00u; // only al is read
}
void *gLangVtable[32];
FakeObject gLanguage;

// ---- Steam ---------------------------------------------------------------------------------

const char *gSteamLanguage = "english";
void *gSteamApps[2]; // [0] = the interface object; read through the pointer the import returns
void *gSteamInterface;
void *gSteamVtable[8];
void *gSteamContextResult;

const char *__fastcall SteamGetLanguage(void *self, void *)
{
    gCalls.push_back({kSteamLanguage, self, 0, 0, 0, 0, 0, {}});
    return gSteamLanguage;
}
void **__cdecl SteamContextInit(void *context)
{
    gCalls.push_back({kSteamInit, context, 0, 0, 0, 0, 0, {}});
    return &gSteamApps[0];
}

// ---- the cTSAppCommonImpl fake -----------------------------------------------------------------

uint32_t __fastcall CommonQuery(void *self, void *, uint32_t riid, void **out)
{
    gCalls.push_back({kQuery, self, riid, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out)), 0, 0, 0, {}});
    *out = gQueryResult;
    return 1;
}
uint32_t __fastcall CommonRelease(void *self, void *)
{
    gCalls.push_back({kRelease, self, 0, 0, 0, 0, 0, {}});
    return 7;
}
template <int W> uint32_t __fastcall CommonSlot(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    gCalls.push_back({W, self, a, b, c, d, e, {}});
    return a * 31u + b * 17u + c * 7u + d * 3u + e + W;
}
void *gCommonVtable[16];
FakeObject gCommon;

void SetupFakes()
{
    std::memset(gFwVtable, 0, sizeof(gFwVtable));
    gFwVtable[5] = reinterpret_cast<void *>(&FwQuery);
    gFwVtable[21] = reinterpret_cast<void *>(&FwGetCmdLine);
    gFwVtable[37] = reinterpret_cast<void *>(&FwGetApp);
    gFramework.vtable = gFwVtable;

    std::memset(gLangVtable, 0, sizeof(gLangVtable));
    gLangVtable[2] = reinterpret_cast<void *>(&CommonRelease);
    gLangVtable[8] = reinterpret_cast<void *>(&LangSet);
    gLangVtable[18] = reinterpret_cast<void *>(&LangLookup);
    gLanguage.vtable = gLangVtable;

    std::memset(gSteamVtable, 0, sizeof(gSteamVtable));
    gSteamVtable[4] = reinterpret_cast<void *>(&SteamGetLanguage);
    gSteamInterface = gSteamVtable;
    gSteamApps[0] = &gSteamInterface; // the import returns a pointer to this slot, which holds the object

    std::memset(gCommonVtable, 0, sizeof(gCommonVtable));
    gCommonVtable[0] = reinterpret_cast<void *>(&CommonQuery);
    gCommonVtable[2] = reinterpret_cast<void *>(&CommonRelease);
    gCommonVtable[3] = reinterpret_cast<void *>(&CommonSlot<kSlot3>);
    gCommonVtable[4] = reinterpret_cast<void *>(&CommonSlot<kSlot4>);
    gCommonVtable[5] = reinterpret_cast<void *>(&CommonSlot<kSlot5>);
    gCommon.vtable = gCommonVtable;
}

constexpr uint32_t kFrameworkGlobal = 0x10c9378, kLanguageCache = 0x10f51c8, kCommonCache = 0x10cc040, kSteamSlot = 0xd9a700;

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    return a == b;
}

// ---- 0x9c5a5d ------------------------------------------------------------------------------

} // namespace

DIFF_TEST(AppInit_LanguageServiceGetter, 0x9c5a5d)
{
    SetupFakes();
    using Getter = void *(__cdecl *)();
    Getter original = ctx.Original<Getter>(0x9c5a5d);
    Getter ours = &appinit::GetLanguageService;
    void *caches[] = {nullptr, &gLanguage};
    void *services[] = {nullptr, &gFramework};
    void *results[] = {nullptr, &gCommon, &gLanguage};
    for (void *cached : caches)
        for (void *service : services)
            for (void *result : results) {
                *ctx.image.At<void *>(kLanguageCache) = cached;
                *ctx.image.At<void *>(kFrameworkGlobal) = service;
                gQueryResult = result;
                gCalls.clear();
                void *a = original();
                void *cacheA = *ctx.image.At<void *>(kLanguageCache);
                std::vector<Call> callsA = gCalls;

                *ctx.image.At<void *>(kLanguageCache) = cached;
                gCalls.clear();
                void *b = ours();
                void *cacheB = *ctx.image.At<void *>(kLanguageCache);
                ++ctx.cases;
                if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                    ctx.Fail("language getter cache=%p service=%p result=%p: original %p/%p (%zu calls), ours %p/%p (%zu calls)", cached, service, result, a,
                             cacheA, callsA.size(), b, cacheB, gCalls.size());
                if (service && !cached && (callsA.empty() || callsA[0].a != 0x441e5070 || callsA[0].b != 0x54b7d5))
                    ctx.Fail("language getter: unexpected ids in the original's query");
            }
    *ctx.image.At<void *>(kLanguageCache) = nullptr;
    *ctx.image.At<void *>(kFrameworkGlobal) = nullptr;
}

// ---- 0x1d0c00, 0x1d0c60, 0x1d0d10 ---------------------------------------------------------------

namespace {

void CheckForwarder(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t(__cdecl *ours)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))
{
    SetupFakes();
    using Fn = uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    Fn original = ctx.Original<Fn>(rva);
    std::mt19937 rng(rva);
    void *caches[] = {nullptr, &gCommon};
    for (int round = 0; round < 6; ++round)
        for (void *cached : caches) {
            uint32_t args[5];
            for (uint32_t &v : args) v = (round == 0) ? 0 : (round == 1) ? 0xffffffffu : rng();
            // Framework slot 37 returns the common fake itself: its slot 0 is the QueryInterface that stores the cached object.
            gQueryResult = &gCommon;
            gAppResult = &gCommon;

            *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
            *ctx.image.At<void *>(kCommonCache) = cached;
            gCalls.clear();
            uint32_t a = original(args[0], args[1], args[2], args[3], args[4]);
            void *cacheA = *ctx.image.At<void *>(kCommonCache);
            std::vector<Call> callsA = gCalls;

            *ctx.image.At<void *>(kCommonCache) = cached;
            gCalls.clear();
            uint32_t b = ours(args[0], args[1], args[2], args[3], args[4]);
            void *cacheB = *ctx.image.At<void *>(kCommonCache);
            ++ctx.cases;
            if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                ctx.Fail("%s round %d cache=%p: original %08x/%p (%zu calls), ours %08x/%p (%zu calls)", name, round, cached, a, cacheA, callsA.size(), b,
                         cacheB, gCalls.size());
            size_t expected = cached ? 1 : 4; // [framework slot 37, QueryInterface, Release,] slot N
            if (callsA.size() != expected) ctx.Fail("%s: the original made %zu calls, expected %zu", name, callsA.size(), expected);
        }
    *ctx.image.At<void *>(kFrameworkGlobal) = nullptr;
    *ctx.image.At<void *>(kCommonCache) = nullptr;
}

} // namespace

DIFF_TEST(AppInit_CommonImplSlot4, 0x1d0c00) { CheckForwarder(ctx, "1d0c00", 0x1d0c00, &appinit::CommonImplSlot4); }
DIFF_TEST(AppInit_CommonImplSlot5, 0x1d0c60) { CheckForwarder(ctx, "1d0c60", 0x1d0c60, &appinit::CommonImplSlot5); }
DIFF_TEST(AppInit_CommonImplSlot3, 0x1d0d10) { CheckForwarder(ctx, "1d0d10", 0x1d0d10, &appinit::CommonImplSlot3); }

// ---- 0xc29a0b ---------------------------------------------------------------------------------

namespace {

using InitFn = void(__fastcall *)(void *, void *);

struct LanguageCase {
    std::string commandLine;
    bool lookupResult;
    bool lookupWrites;
    int32_t lookupId;
    std::string steamLanguage;
};

void RunLanguage(difftest::Context &ctx, const LanguageCase &c, InitFn original, InitFn ours, cRZCmdLine *cmd)
{
    gCmdLineResult = cmd;
    gLookupResult = c.lookupResult;
    gLookupWrites = c.lookupWrites;
    gLookupId = c.lookupId;
    gSteamLanguage = c.steamLanguage.c_str();
    *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
    *ctx.image.At<void *>(kLanguageCache) = &gLanguage;

    gCalls.clear();
    original(&gFramework, nullptr);
    std::vector<Call> callsA = gCalls;
    gCalls.clear();
    ours(&gFramework, nullptr);
    ++ctx.cases;
    if (!SameCalls(callsA, gCalls)) {
        ctx.Fail("InitLanguage <%s> lookup=%d writes=%d steam=<%s>: original %zu calls, ours %zu calls", c.commandLine.c_str(), c.lookupResult,
                 c.lookupWrites, c.steamLanguage.c_str(), callsA.size(), gCalls.size());
        for (size_t i = 0; i < callsA.size() && i < gCalls.size(); ++i)
            if (!(callsA[i] == gCalls[i]))
                ctx.Fail("  first difference at call %zu: kind %d/%d a=%u/%u text <%s>/<%s>", i, callsA[i].what, gCalls[i].what, callsA[i].a, gCalls[i].a,
                         callsA[i].text.c_str(), gCalls[i].text.c_str());
    }
}

} // namespace

DIFF_TEST(AppInit_InitLanguage, 0xc29a0b)
{
    SetupFakes();
    // The import the original reads: replace it in the mapped image.
    void **iat = ctx.image.At<void *>(kSteamSlot);
    void *savedIat = *iat;
    *iat = reinterpret_cast<void *>(&SteamContextInit);

    InitFn original = ctx.Original<InitFn>(0xc29a0b);
    InitFn ours = &appinit::InitLanguage;

    const char *names[] = {"english", "french", "brazilian", "czech", "russian", "hungarian", "german", "italian", "spanish", "dutch", "danish",
                           "swedish", "norwegian", "finnish", "portuguese", "japanese", "polish", "tchinese", "schinese", "thai", "koreana",
                           // not listed, or listed with a difference
                           "", "English", "ENGLISH", "englis", "englishx", "german ", "chinese", "arabic", "turkish", "korean", "greek", "\x01"};
    const char *lines[] = {"game.exe", "game.exe -l:german", "game.exe /l:french", "game.exe -l", "game.exe -l:", "game.exe -l:\"italian\"",
                           "game.exe -L:german", "game.exe -lang:german -w", "game.exe -w -l:xx -l:spanish", "game.exe l:german", ""};
    std::mt19937 rng(0xc29a0b);
    for (const char *line : lines) {
        cRZCmdLine *cmd = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
        std::memset(cmd, 0xCD, sizeof(cRZCmdLine));
        cmd->Construct(line);
        for (const char *name : names)
            for (int mode = 0; mode < 4; ++mode) {
                LanguageCase c{line, (mode & 1) != 0, (mode & 2) != 0, static_cast<int32_t>(rng() % 40) - 3, name};
                RunLanguage(ctx, c, original, ours, cmd);
            }
        cmd->Destruct();
        ::operator delete(cmd);
    }

    // Every listed name through the helper, against the original's mapping (steam path only).
    for (const char *name : names) {
        int32_t id = appinit::LanguageIdFromName(name);
        cRZCmdLine *cmd = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
        std::memset(cmd, 0xCD, sizeof(cRZCmdLine));
        cmd->Construct("game.exe");
        gCmdLineResult = cmd;
        gSteamLanguage = name;
        *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
        *ctx.image.At<void *>(kLanguageCache) = &gLanguage;
        gCalls.clear();
        original(&gFramework, nullptr);
        int32_t viaOriginal = 12345;
        for (const Call &call : gCalls)
            if (call.what == kLangSet) viaOriginal = static_cast<int32_t>(call.a);
        ++ctx.cases;
        if (viaOriginal != id) ctx.Fail("LanguageIdFromName(<%s>) = %d, the original sets %d", name, id, viaOriginal);
        cmd->Destruct();
        ::operator delete(cmd);
    }

    *iat = savedIat;
    *ctx.image.At<void *>(kFrameworkGlobal) = nullptr;
    *ctx.image.At<void *>(kLanguageCache) = nullptr;
}
