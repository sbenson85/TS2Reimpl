// Differential tests for the texture manager's cached getter (0xae8750), the texture handles (0xae8240, 0xae8260,
// 0xae8280, 0xae82e0, 0xae82b0, 0xae8710, 0xae8730) and the resource-name helper (0xae7340); see sg/cTextureManager.h.
//
// The framework service (game global 0x10c9378), the scene graph system it hands out, the texture manager and the
// string are fakes whose slots record their calls. Both sides run from the same game globals (the getter's cache
// 0x10f8f94, the string-key service 0x10f8f2c), and the results, the handles, the calls and the globals are compared.
//   service:  slot 5 (a, b, out)   stores gQueryResult through out (true when it is not null)
//   system:   slot 2 (release), slot 6 (no arguments: the manager; throws on the throwing system)
//   manager:  slot 17 (name), 18 (id), 19 (id), 23 (id)
//   keys:     slot 0 (name: the group), slot 1 (AddRef)
//   string:   slot 4 (text), slot 26 (start, end), slot 31 (cdecl: format, ...)
#include "difftest.h"
#include "sg/cTextureManager.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace nGZSceneGraph;

namespace {

struct Event {
    int slot;
    const void *self;
    uint32_t a, b;
    std::string text;
    bool operator==(const Event &o) const { return slot == o.slot && self == o.self && a == o.a && b == o.b && text == o.text; }
};

std::vector<Event> gEvents;
void *gQueryResult = nullptr;   // what the service's slot 5 stores
void *gManagerResult = nullptr; // what the system's slot 6 returns
uint32_t gFindResult = 0;       // the manager's slot 17
void *gTextureResult = nullptr; // the manager's slot 23
uint32_t gKeyResult = 0;        // the keys' slot 0

constexpr uint32_t kService = 0x10c9378; // GetServiceObject
constexpr uint32_t kCache = 0x10f8f94;   // 0xae8750's cache
constexpr uint32_t kKeys = 0x10f8f2c;    // the string-key service

struct Globals {
    void *service, *cache, *keys;
    bool operator==(const Globals &o) const { return service == o.service && cache == o.cache && keys == o.keys; }
};

void Apply(difftest::Context &ctx, const Globals &g)
{
    *ctx.image.At<void *>(kService) = g.service;
    *ctx.image.At<void *>(kCache) = g.cache;
    *ctx.image.At<void *>(kKeys) = g.keys;
}

Globals Snapshot(difftest::Context &ctx)
{
    return {*ctx.image.At<void *>(kService), *ctx.image.At<void *>(kCache), *ctx.image.At<void *>(kKeys)};
}

void Record(int slot, const void *self, uint32_t a = 0, uint32_t b = 0, const char *text = nullptr)
{
    gEvents.push_back({slot, self, a, b, text ? std::string(text) : std::string("<null>")});
}

uint32_t __fastcall ServiceQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Record(5, self, a, b);
    *out = gQueryResult;
    return gQueryResult != nullptr; // success when there is an object (0xae7300 counts it only then)
}
uint32_t __fastcall Release(void *self, void *)
{
    Record(2, self);
    return 0;
}
uint32_t __fastcall AddRef(void *self, void *)
{
    Record(1, self);
    return 2;
}
void *__fastcall SystemManager(void *self, void *)
{
    Record(6, self);
    return gManagerResult;
}
void *__fastcall SystemThrows(void *self, void *)
{
    Record(6, self);
    throw 42;
}
uint32_t __fastcall ManagerFind(void *self, void *, const char *name)
{
    Record(17, self, 0, 0, name);
    return gFindResult;
}
void __fastcall ManagerAddRef(void *self, void *, uint32_t id) { Record(18, self, id); }
void __fastcall ManagerRelease(void *self, void *, uint32_t id) { Record(19, self, id); }
void *__fastcall ManagerTexture(void *self, void *, uint32_t id)
{
    Record(23, self, id);
    return gTextureResult;
}
uint32_t __fastcall KeysGroup(void *self, void *, const char *name)
{
    Record(0, self, 0, 0, name);
    return gKeyResult;
}
uint32_t __fastcall StringFromChar(void *self, void *, const char *text)
{
    Record(4, self, 0, 0, text);
    return 0x44;
}
uint32_t __fastcall StringErase(void *self, void *, uint32_t start, uint32_t end)
{
    Record(26, self, start, end);
    return 0x26;
}
uint32_t __cdecl StringSprintf(void *self, const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Record(31, self, 0, 0, (std::string(format) + "|" + buffer).c_str());
    return 0x31;
}

void *gVtService[8], *gVtSystem[8], *gVtThrowing[8], *gVtManager[30], *gVtKeys[4], *gVtString[40];
void *gServiceObj[1] = {gVtService};
void *gSystem[1] = {gVtSystem};
void *gThrowing[1] = {gVtThrowing};
void *gManager[1] = {gVtManager};
void *gOtherManager[1] = {gVtManager};
void *gKeysObj[1] = {gVtKeys};
void *gString[1] = {gVtString};

void SetupFakes()
{
    std::memset(gVtService, 0, sizeof(gVtService));
    std::memset(gVtSystem, 0, sizeof(gVtSystem));
    std::memset(gVtThrowing, 0, sizeof(gVtThrowing));
    std::memset(gVtManager, 0, sizeof(gVtManager));
    std::memset(gVtKeys, 0, sizeof(gVtKeys));
    std::memset(gVtString, 0, sizeof(gVtString));
    gVtService[5] = reinterpret_cast<void *>(&ServiceQuery);
    gVtSystem[2] = reinterpret_cast<void *>(&Release);
    gVtSystem[6] = reinterpret_cast<void *>(&SystemManager);
    gVtThrowing[2] = reinterpret_cast<void *>(&Release);
    gVtThrowing[6] = reinterpret_cast<void *>(&SystemThrows);
    gVtManager[17] = reinterpret_cast<void *>(&ManagerFind);
    gVtManager[18] = reinterpret_cast<void *>(&ManagerAddRef);
    gVtManager[19] = reinterpret_cast<void *>(&ManagerRelease);
    gVtManager[23] = reinterpret_cast<void *>(&ManagerTexture);
    gVtKeys[0] = reinterpret_cast<void *>(&KeysGroup);
    gVtKeys[1] = reinterpret_cast<void *>(&AddRef);
    gVtString[4] = reinterpret_cast<void *>(&StringFromChar);
    gVtString[26] = reinterpret_cast<void *>(&StringErase);
    gVtString[31] = reinterpret_cast<void *>(&StringSprintf);
}

const char *Show(const std::vector<Event> &events)
{
    static std::string text;
    text.clear();
    for (const Event &e : events) {
        char line[96];
        std::snprintf(line, sizeof(line), " [%d %p %x %x ", e.slot, e.self, e.a, e.b);
        text += line;
        text += e.text + "]";
    }
    return text.c_str();
}

// The manager states the handle cases run in: cached, or fetched through the system (the first call caches it).
const Globals kStates[] = {{gServiceObj, gManager, nullptr}, {gServiceObj, nullptr, nullptr}};

const uint32_t kIds[] = {0u, 5u, 0xffffffffu, 0x1234u};

} // namespace

DIFF_TEST(SG_TextureManagerGetter, 0xae8750)
{
    SetupFakes();
    auto orig = ctx.Original<void *(__cdecl *)()>(0xae8750);
    // A null service or system is dereferenced by the original (slot 6 of the null holder): not run.
    for (void *cacheIn : {static_cast<void *>(nullptr), static_cast<void *>(gOtherManager)})
        for (void *manager : {static_cast<void *>(nullptr), static_cast<void *>(gManager)}) {
            const Globals g{gServiceObj, cacheIn, nullptr};
            gQueryResult = gSystem;
            gManagerResult = manager;
            Apply(ctx, g);
            gEvents.clear();
            void *const a = orig();
            const std::vector<Event> eventsA = gEvents;
            const Globals afterA = Snapshot(ctx);

            Apply(ctx, g);
            gEvents.clear();
            void *const b = TextureManager();
            const Globals afterB = Snapshot(ctx);
            ++ctx.cases;
            if (a != b || !(afterA == afterB) || eventsA != gEvents)
                ctx.Fail("getter cache=%p manager=%p: original %p cache %p%s; ours %p cache %p%s", cacheIn, manager, a, afterA.cache,
                         Show(eventsA), b, afterB.cache, Show(gEvents));
        }

    // Slot 6 throws: the system is released on the way out and nothing is cached.
    const Globals g{gServiceObj, nullptr, nullptr};
    gQueryResult = gThrowing;
    bool thrownA = false, thrownB = false;
    Apply(ctx, g);
    gEvents.clear();
    try {
        orig();
    } catch (int) {
        thrownA = true;
    }
    const std::vector<Event> eventsA = gEvents;
    const Globals afterA = Snapshot(ctx);
    Apply(ctx, g);
    gEvents.clear();
    try {
        TextureManager();
    } catch (int) {
        thrownB = true;
    }
    const Globals afterB = Snapshot(ctx);
    ++ctx.cases;
    if (!thrownA || !thrownB || !(afterA == afterB) || eventsA != gEvents)
        ctx.Fail("getter throwing: original thrown=%d%s; ours thrown=%d%s", thrownA, Show(eventsA), thrownB, Show(gEvents));
    Apply(ctx, Globals{});
}

namespace {

// Runs one handle operation on both sides from the same state: `run(original, handle)` returns the result word.
template <typename Run>
void CheckHandle(difftest::Context &ctx, const char *what, const Globals &g, uint32_t idIn, Run run)
{
    gQueryResult = gSystem;
    gManagerResult = gManager;
    cTextureHandle *a = reinterpret_cast<cTextureHandle *>(new uint32_t(idIn));
    cTextureHandle *b = reinterpret_cast<cTextureHandle *>(new uint32_t(idIn));
    Apply(ctx, g);
    gEvents.clear();
    const uintptr_t ra = run(true, a);
    const std::vector<Event> eventsA = gEvents;
    const Globals afterA = Snapshot(ctx);

    Apply(ctx, g);
    gEvents.clear();
    const uintptr_t rb = run(false, b);
    const Globals afterB = Snapshot(ctx);
    // A returned `this` is compared as the handle each side was given.
    const uintptr_t na = ra == reinterpret_cast<uintptr_t>(a) ? 1 : ra;
    const uintptr_t nb = rb == reinterpret_cast<uintptr_t>(b) ? 1 : rb;
    ++ctx.cases;
    if (na != nb || a->mId != b->mId || !(afterA == afterB) || eventsA != gEvents)
        ctx.Fail("%s cache=%p id=%x: original %x id %x%s; ours %x id %x%s", what, g.cache, idIn, unsigned(na), a->mId,
                 Show(eventsA), unsigned(nb), b->mId, Show(gEvents));
    delete reinterpret_cast<uint32_t *>(a);
    delete reinterpret_cast<uint32_t *>(b);
}

} // namespace

DIFF_TEST(SG_TextureHandleCopy, 0xae8240)
{
    SetupFakes();
    auto orig = ctx.Original<cTextureHandle *(__thiscall *)(cTextureHandle *, const cTextureHandle *)>(0xae8240);
    for (const Globals &g : kStates)
        for (uint32_t other : kIds) {
            const cTextureHandle *const source = reinterpret_cast<const cTextureHandle *>(&other);
            CheckHandle(ctx, "copy", g, 0xabababab, [&](bool original, cTextureHandle *h) {
                return reinterpret_cast<uintptr_t>(original ? orig(h, source) : h->CopyFrom(source));
            });
        }
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_TextureHandleFromId, 0xae8260)
{
    SetupFakes();
    auto orig = ctx.Original<cTextureHandle *(__thiscall *)(cTextureHandle *, uint32_t)>(0xae8260);
    for (const Globals &g : kStates)
        for (uint32_t id : kIds)
            CheckHandle(ctx, "from id", g, 0xabababab, [&](bool original, cTextureHandle *h) {
                return reinterpret_cast<uintptr_t>(original ? orig(h, id) : h->InitFromId(id));
            });
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_TextureHandleFromName, 0xae8280)
{
    SetupFakes();
    auto orig = ctx.Original<cTextureHandle *(__thiscall *)(cTextureHandle *, const char *)>(0xae8280);
    for (const Globals &g : kStates)
        for (uint32_t found : kIds)
            for (const char *name : {"DefaultWhiteA1Texture", "brick"}) {
                gFindResult = found;
                CheckHandle(ctx, "from name", g, 0xabababab, [&](bool original, cTextureHandle *h) {
                    return reinterpret_cast<uintptr_t>(original ? orig(h, name) : h->InitFromName(name));
                });
            }
    gFindResult = 0;
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_TextureHandleAssign, 0xae82e0)
{
    SetupFakes();
    auto orig = ctx.Original<cTextureHandle *(__thiscall *)(cTextureHandle *, const cTextureHandle *)>(0xae82e0);
    for (const Globals &g : kStates)
        for (uint32_t mine : kIds) {
            for (uint32_t other : kIds) {
                const cTextureHandle *const source = reinterpret_cast<const cTextureHandle *>(&other);
                CheckHandle(ctx, "assign", g, mine, [&](bool original, cTextureHandle *h) {
                    return reinterpret_cast<uintptr_t>(original ? orig(h, source) : h->Assign(source));
                });
            }
            // Assigned from itself: counted, then released, the same id.
            CheckHandle(ctx, "self-assign", g, mine, [&](bool original, cTextureHandle *h) {
                return reinterpret_cast<uintptr_t>(original ? orig(h, h) : h->Assign(h));
            });
        }
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_TextureHandleDestroy, 0xae82b0)
{
    SetupFakes();
    auto orig = ctx.Original<void(__thiscall *)(cTextureHandle *)>(0xae82b0);
    for (const Globals &g : kStates)
        for (uint32_t id : kIds)
            CheckHandle(ctx, "destroy", g, id, [&](bool original, cTextureHandle *h) {
                original ? orig(h) : h->Destroy();
                return uintptr_t{0};
            });
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_TextureHandleReset, 0xae8710)
{
    SetupFakes();
    auto orig = ctx.Original<void(__thiscall *)(cTextureHandle *)>(0xae8710);
    for (const Globals &g : kStates)
        for (uint32_t id : kIds)
            CheckHandle(ctx, "reset", g, id, [&](bool original, cTextureHandle *h) {
                original ? orig(h) : h->Reset();
                return uintptr_t{0};
            });
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_TextureHandleGet, 0xae8730)
{
    SetupFakes();
    auto orig = ctx.Original<cIGZUnknown *(__thiscall *)(const cTextureHandle *)>(0xae8730);
    for (const Globals &g : kStates)
        for (uint32_t id : kIds)
            for (void *texture : {static_cast<void *>(nullptr), static_cast<void *>(gString)}) {
                gTextureResult = texture;
                CheckHandle(ctx, "get", g, id, [&](bool original, cTextureHandle *h) {
                    return reinterpret_cast<uintptr_t>(original ? orig(h) : h->Get());
                });
            }
    gTextureResult = nullptr;
    Apply(ctx, Globals{});
}

DIFF_TEST(SG_ResolveResourceName, 0xae7340)
{
    SetupFakes();
    auto orig = ctx.Original<bool(__cdecl *)(cIGZString *, const char *, uint32_t, const char *)>(0xae7340);
    cIGZString *const out = reinterpret_cast<cIGZString *>(gString);
    const char *const texts[] = {nullptr, "", "plain", "##abc", "##abc!x", "grp!tex", "a!b!c", "x##y!z", "!lead", "a!##b", "#1!z"};
    const uint32_t groups[] = {0u, 0x1c0532fau, 0x12345678u, 0xffffffffu};
    const char *const groupNames[] = {nullptr, "MyGroup", ""};
    // The string-key service: cached; asked for and handed out (slot 1 counts it); or absent (CRC24 of the name).
    void *const keysCached[] = {gKeysObj, nullptr, nullptr};
    void *const keysQueried[] = {nullptr, gKeysObj, nullptr};
    for (const char *text : texts)
        for (uint32_t group : groups)
            for (const char *groupName : groupNames)
                for (int k = 0; k < 3; ++k)
                    for (uint32_t keyResult : {0u, 0x1c0532fau, 0x7f00beefu}) {
                        if (groupName == nullptr && (k != 0 || keyResult != 0)) continue; // the service is not asked
                        if (k == 2 && keyResult != 0) continue;                           // nor its slot 0
                        const Globals g{gServiceObj, nullptr, keysCached[k]};
                        gQueryResult = keysQueried[k];
                        gKeyResult = keyResult;
                        Apply(ctx, g);
                        gEvents.clear();
                        const uint8_t a = static_cast<uint8_t>(orig(out, text, group, groupName));
                        const std::vector<Event> eventsA = gEvents;
                        const Globals afterA = Snapshot(ctx);

                        Apply(ctx, g);
                        gEvents.clear();
                        const uint8_t b = static_cast<uint8_t>(ResolveResourceName(out, text, group, groupName));
                        const Globals afterB = Snapshot(ctx);
                        ++ctx.cases;
                        if (a != b || !(afterA == afterB) || eventsA != gEvents)
                            ctx.Fail("resolve text=%s group=%x name=%s keys=%d result=%x: original %u keys %p%s; ours %u keys %p%s",
                                     text ? text : "<null>", group, groupName ? groupName : "<null>", k, keyResult, a, afterA.keys,
                                     Show(eventsA), b, afterB.keys, Show(gEvents));
                    }
    gKeyResult = 0;
    gQueryResult = nullptr;
    Apply(ctx, Globals{});
}
