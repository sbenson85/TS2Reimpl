#include "platform/SteamStub.h"

#include <cstdio>
#include <cstring>

namespace steamstub {

namespace {

std::string gLanguage = "english";
void (*gLog)(const char *) = nullptr;

// The game's table (InitLanguage 0xc29a0b; src/app/AppInit.cpp has the ids).
const char *const kGameLanguages[] = {
    "english", "french",  "brazilian",  "czech",    "russian", "hungarian", "german",   "italian", "spanish", "dutch",   "danish",
    "swedish", "norwegian", "finnish", "portuguese", "japanese", "polish",   "tchinese", "schinese", "thai",   "koreana",
};

void Report(const char *fmt, const char *a, int b)
{
    if (!gLog) return;
    char line[256];
    std::snprintf(line, sizeof(line), fmt, a, b);
    gLog(line);
}

// Interface slots the image never calls (it uses ISteamUser slot 2 and ISteamApps slot 4 only):
// logged, answering 0. Their real arguments are unknown here, so they are no-argument thiscalls.
template <int Interface, int Slot> uint32_t __fastcall Unexpected(void *, void *)
{
    Report("steam stub: unexpected call to %s slot %d", Interface == 0 ? "ISteamUser" : "ISteamApps", Slot);
    return 0;
}

// ISteamUser023: 0 GetHSteamUser, 1 BLoggedOn, 2 GetSteamID (CSteamID returned through a hidden pointer).
int32_t __fastcall UserGetHSteamUser(void *, void *) { return kSteamUser; }
uint8_t __fastcall UserLoggedOn(void *, void *) { return 1; }
uint32_t *__fastcall UserGetSteamId(void *, void *, uint32_t *out)
{
    out[0] = kSteamIdLow;
    out[1] = kSteamIdHigh;
    return out;
}

// ISteamApps008: 0 BIsSubscribed, 1 BIsLowViolence, 2 BIsCybercafe, 3 BIsVACBanned,
// 4 GetCurrentGameLanguage, 5 GetAvailableGameLanguages.
uint8_t __fastcall AppsTrue(void *, void *) { return 1; }
uint8_t __fastcall AppsFalse(void *, void *) { return 0; }
const char *__fastcall AppsLanguage(void *, void *) { return gLanguage.c_str(); }
const char *__fastcall AppsAvailableLanguages(void *, void *) { return gLanguage.c_str(); }

void *const kUserVtable[] = {
    reinterpret_cast<void *>(&UserGetHSteamUser), reinterpret_cast<void *>(&UserLoggedOn), reinterpret_cast<void *>(&UserGetSteamId),
    reinterpret_cast<void *>(&Unexpected<0, 3>),  reinterpret_cast<void *>(&Unexpected<0, 4>), reinterpret_cast<void *>(&Unexpected<0, 5>),
    reinterpret_cast<void *>(&Unexpected<0, 6>),  reinterpret_cast<void *>(&Unexpected<0, 7>),
};
void *const kAppsVtable[] = {
    reinterpret_cast<void *>(&AppsTrue),     reinterpret_cast<void *>(&AppsFalse),
    reinterpret_cast<void *>(&AppsFalse),    reinterpret_cast<void *>(&AppsFalse),
    reinterpret_cast<void *>(&AppsLanguage), reinterpret_cast<void *>(&AppsAvailableLanguages),
    reinterpret_cast<void *>(&Unexpected<1, 6>), reinterpret_cast<void *>(&Unexpected<1, 7>),
};

struct Interface {
    const void *const *vtable;
};
const Interface kUser{kUserVtable};
const Interface kApps{kAppsVtable};

// SteamInternal_ContextInit's counter: a context whose counter differs is (re)initialised.
constexpr uintptr_t kContextCounter = 1;

} // namespace

void SetLanguage(const std::string &name) { gLanguage = name; }
const std::string &Language() { return gLanguage; }

bool IsGameLanguage(const std::string &name)
{
    for (const char *l : kGameLanguages)
        if (name == l) return true;
    return false;
}

void SetLogger(void (*log)(const char *line)) { gLog = log; }

int32_t __cdecl SteamAPIInit(const char *, char *errorMessage)
{
    if (errorMessage) errorMessage[0] = '\0';
    return 0; // k_ESteamAPIInitResult_OK
}

// contextData is the accessor's static {init function, counter, interface pointer}; the result
// points to the interface pointer, filled by the init function when the counter is stale.
void **__cdecl ContextInit(void *contextData)
{
    auto **data = static_cast<void **>(contextData);
    if (reinterpret_cast<uintptr_t>(data[1]) != kContextCounter) {
        reinterpret_cast<void(__cdecl *)(void **)>(data[0])(&data[2]);
        data[1] = reinterpret_cast<void *>(kContextCounter);
    }
    return &data[2];
}

void *__cdecl FindOrCreateUserInterface(int32_t, const char *version)
{
    if (version && std::strcmp(version, "SteamUser023") == 0) return const_cast<Interface *>(&kUser);
    if (version && std::strcmp(version, "STEAMAPPS_INTERFACE_VERSION008") == 0) return const_cast<Interface *>(&kApps);
    Report("steam stub: no interface %s (%d)", version ? version : "(null)", 0);
    return nullptr;
}

int32_t __cdecl GetHSteamUser() { return kSteamUser; }

void __cdecl RunCallbacks() {}

void __cdecl RegisterCallback(void *callback, int32_t id)
{
    auto *c = static_cast<CallbackBase *>(callback);
    c->flags |= 1;
    c->id = id;
}

void __cdecl UnregisterCallback(void *callback)
{
    auto *c = static_cast<CallbackBase *>(callback);
    c->flags &= static_cast<uint8_t>(~1u);
}

void __cdecl Shutdown() {}

void *Resolve(const char *name)
{
    struct Entry {
        const char *name;
        void *fn;
    };
    static const Entry kTable[] = {
        {"SteamInternal_SteamAPI_Init", reinterpret_cast<void *>(&SteamAPIInit)},
        {"SteamInternal_ContextInit", reinterpret_cast<void *>(&ContextInit)},
        {"SteamInternal_FindOrCreateUserInterface", reinterpret_cast<void *>(&FindOrCreateUserInterface)},
        {"SteamAPI_GetHSteamUser", reinterpret_cast<void *>(&GetHSteamUser)},
        {"SteamAPI_RunCallbacks", reinterpret_cast<void *>(&RunCallbacks)},
        {"SteamAPI_RegisterCallback", reinterpret_cast<void *>(&RegisterCallback)},
        {"SteamAPI_UnregisterCallback", reinterpret_cast<void *>(&UnregisterCallback)},
        {"SteamAPI_Shutdown", reinterpret_cast<void *>(&Shutdown)},
    };
    if (reinterpret_cast<uintptr_t>(name) < 0x10000) return nullptr; // by ordinal: not used by the image
    for (const Entry &e : kTable)
        if (std::strcmp(name, e.name) == 0) return e.fn;
    Report("steam stub: no export %s (%d)", name, 0);
    return nullptr;
}

} // namespace steamstub
