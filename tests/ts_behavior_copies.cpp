// Differential tests of the game's TS-side copies of cAbstractBehaviorTypeT<I, cRefSingleThreaded> (lane FD;
// src/ts/TSBehaviorCopies.h): one test per body (Release, OnEvent, NotifyListeners, AskListeners, Connect, Disconnect,
// GetEventCount, GetEventAttribute, GetEventManager), one case per copy, the image's body against cTSBehaviorCopy's
// adapter. The world is tests/sg_behavior_copies.cpp's (lane SV).
//
// The object is a hand-made behaviour: a vtable as long as the copy's (its slots 0-3 and the copy's deleting destructor
// slot test functions that note their calls; every other slot a trap), its +4 word the count and active bit the bodies
// keep. The world: a real connection manager (sgfx::Manager) in the copy's cell, or the cell null with the framework
// (in g_Framework 0x10c9378) handing out an event service whose GetConnectionManager answers the manager (or nothing,
// or no service); listener fakes connected to the object; the copy's registry stood in on both sides (the image's
// function and ours) by a test map, as a registry function (id, record) or a no-argument getter (the /O1 copies); a
// pattern planted in the copy's image missing record and in ours.
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "hook/hooks.h"
#include "ts/TSBehaviorCopies.h"

#include <cstring>
#include <random>

namespace {

using namespace nTS::behavior_copies;
using drawfx::Fake;
using drawfx::P;
using nGZEvent::cEventAttribute;
using nGZEvent::cEventAttributeMap;

enum Body { kRelease, kOnEvent, kNotify, kAsk, kConnect, kDisconnect, kCount, kAttribute, kManager, kBodies };

struct Copy {
    const char *name;
    int slot;            // the deleting destructor's
    uint32_t cell;       // the connection-manager cell
    uint32_t registry;   // the registry function's (or getter's) RVA
    void *oursRegistry;  // ours of it
    bool getter;         // the registry is asked with no arguments
    uint32_t missing;    // the image's missing record
    uint32_t rva[kBodies]; // 0: the body is not this lane's
    void *ours[kBodies];
};

template <class C, class R>
Copy Make(const char *name, uint32_t cell, uint32_t registry, R oursRegistry, uint32_t missing,
          std::initializer_list<uint32_t> rvas)
{
    Copy c{name, C::kSlot, cell, registry, reinterpret_cast<void *>(oursRegistry),
           std::is_same_v<R, RegistryGetFn>, missing, {}, {}};
    std::copy(rvas.begin(), rvas.end(), c.rva);
    c.ours[kRelease] = MemberAddress(&C::HookRelease);
    c.ours[kOnEvent] = MemberAddress(&C::HookOnEvent);
    c.ours[kNotify] = MemberAddress(&C::HookNotifyListeners);
    c.ours[kAsk] = MemberAddress(&C::HookAskListeners);
    c.ours[kConnect] = MemberAddress(&C::HookConnect);
    c.ours[kDisconnect] = MemberAddress(&C::HookDisconnect);
    c.ours[kCount] = MemberAddress(&C::HookGetEventCount);
    c.ours[kAttribute] = MemberAddress(&C::HookGetEventAttribute);
    c.ours[kManager] = reinterpret_cast<void *>(&C::HookGetEventManager);
    return c;
}

const Copy &CopyAt(unsigned i)
{
    static const Copy copies[] = {
        Make<CameraControllerManagerCopy>("camera controller manager", 0x1107858, 0xcc3550,
                                          &CameraControllerManagerRegistryFor, 0x1088a10,
                                          {0xcc3630, 0xcc30b0, 0xcc3850, 0xcc2f20, 0xcc2d50, 0xcc2e90, 0xcc34c0, 0xcc2c70,
                                           0xcc2d70}),
        Make<ConfigurationChangeManagerCopy>("configuration change manager", 0x1107a5c, 0xcd6c00,
                                             &ConfigurationChangeManagerRegistryFor, 0x1089a28,
                                             {0xcd7160, 0xcd6940, 0xcd7240, 0xcd6180, 0xcd5da0, 0xcd5e40, 0xcd6b40,
                                              0xcd5cc0, 0xcd5dc0}),
        Make<SmoothFirstPersonCopy>("smooth first person controller", 0x10fb424, 0xb4f2a0, &SmoothFirstPersonRegistryFor,
                                    0x107a778,
                                    {0xb4f380, 0xb4ec50, 0xb4f870, 0xb4eb20, 0xb4e7c0, 0xb4e870, 0xb4f1d0, 0xb4e6c0,
                                     0xb4e7e0}),
        Make<ConfigDeviceSettingsCopy>("config device settings", 0x10fc5fc, 0xb6f910, &ConfigDeviceSettingsRegistryFor,
                                       0x107bb98,
                                       {0xb6f9f0, 0xb6ede0, 0xb6fa40, 0xb6ec20, 0xb6eb60, 0xb6ec00, 0xb6f0f0, 0xb6ea80,
                                        0xb6eb80}),
        Make<MainToolbarCopy>("main toolbar sink", 0x10dc5a8, 0x61fea4, &MainToolbarRegistry, 0x101e5f0,
                              {0x620469, 0x61e397, 0x620a60, 0x61e01f, 0x61b5b4, 0x61b97c, 0x6200b5, 0x61b062, 0x61b5d4}),
        Make<ScreenGrabberCopy>("screen grabber", 0x10dc430, 0x60e453, &ScreenGrabberRegistry, 0x101d2a0,
                                {0x60e6c2, 0x60e426, 0x60e71f, 0x60dd3e, 0x60dcb4, 0x60dd1e, 0x60e47f, 0x60dc7c, 0x60dcd4}),
        // GetEventCount is folded with the base registry's other users (not hooked).
        Make<WinSceneGraphCopy>("win scene graph", 0x10dc090, 0x1480d0, &nGZEvent::cAbstractBehaviorType::Registry,
                                0x10176c0,
                                {0x5bbe48, 0x5ba6c0, 0x5bc64e, 0x5b989b, 0x5b8af6, 0x5b8c7b, 0, 0x5b861a, 0x5b8b16}),
    };
    return copies[i];
}
constexpr unsigned kCopies = 7;

// ---- the object ----
constexpr uint32_t kObjectTag = 0x7c000000, kDataTag = 0x7b000000;
constexpr uint32_t kBehaviorType = 0xfb859324; // cIAbstractBehaviorType
constexpr uint32_t kConnectionChanged = 0x5b51e806;

struct Object {
    void **vt;
    uint32_t count;
    uint32_t refs; // the test's own AddRef / Release count (the bodies keep +4)
    uint32_t pad[5];
};
Object gObject;
void *gVTable[160];
uint32_t gData[4]; // the data pointer the events carry

bool __fastcall ObjectQuery(Object *self, void *, uint32_t iid, void **out)
{
    drawfx::Note("object query", iid);
    if (iid != kBehaviorType && iid != 1) return false;
    if (out) *out = self, ++self->refs;
    return true;
}
uint32_t __fastcall ObjectAddRef(Object *self, void *) { return ++self->refs; }
uint32_t __fastcall ObjectRelease(Object *self, void *) { return --self->refs; }
void __fastcall ObjectOnEvent(Object *, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    drawfx::Note("object event", {type, arg1, drawfx::Norm(P(data)), drawfx::Norm(arg3)});
}
void *__fastcall ObjectDeleting(Object *self, void *, uint32_t flag)
{
    drawfx::Note("deleting destructor", {drawfx::Norm(P(self)), flag, self->count});
    return self;
}
void __fastcall ObjectTrap(Object *, void *) { drawfx::Note("trap", 0u); }

void MakeObject(const Copy &c, uint32_t count)
{
    for (void *&s : gVTable) s = reinterpret_cast<void *>(&ObjectTrap);
    gVTable[0] = reinterpret_cast<void *>(&ObjectQuery);
    gVTable[1] = reinterpret_cast<void *>(&ObjectAddRef);
    gVTable[2] = reinterpret_cast<void *>(&ObjectRelease);
    gVTable[3] = reinterpret_cast<void *>(&ObjectOnEvent);
    gVTable[c.slot] = reinterpret_cast<void *>(&ObjectDeleting);
    gObject = {};
    gObject.vt = gVTable;
    gObject.count = count;
    gObject.refs = 50;
    drawfx::AddRange(&gObject, sizeof gObject, kObjectTag);
    drawfx::AddRange(gData, sizeof gData, kDataTag);
}

// ---- the world ----
inline constexpr drawfx::Slot kFrameworkSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {3, drawfx::Ret::Int, true, "GetSystemService", "uui"},
};
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 6, kFrameworkSlots};
inline constexpr drawfx::Slot kServiceSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "GetConnectionManager", ""},
};
inline constexpr drawfx::Iface kService{"cIEventSystemService", 4, kServiceSlots};

struct World {
    Fake listeners[3]; // ids 500-502
    Fake framework;    // 460
    Fake service;      // 461
    sgfx::Manager manager;
    cEventAttributeMap map;
    uint32_t events[4];
};
World w;

uint32_t FrameworkGetService(Fake &self, const uint32_t *a)
{
    if (!self.value[0] || a[0] != 0x5b1e896f) return 0;
    ++w.service.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &w.service;
    return 1;
}
uint32_t ListenerEvent(Fake &, const uint32_t *a)
{
    drawfx::Note("heard", {a[0], a[1], drawfx::Norm(a[2]), drawfx::Norm(a[3])});
    return 0;
}

cEventAttributeMap *__cdecl StandRegistry(uint32_t id, const cEventAttribute *attribute)
{
    const uint32_t *words = reinterpret_cast<const uint32_t *>(attribute);
    drawfx::Note("registry", {id, words[0], words[1], words[2], words[3], words[4], words[5]});
    return &w.map;
}
cEventAttributeMap *__cdecl StandRegistryGet()
{
    drawfx::Note("registry", 0u);
    return &w.map;
}

// Modes of the cell: 0 the manager, 1 null and the service answers it, 2 null and the service answers none, 3 null and
// no service. `nullOk`: bodies that survive a null manager (Release, GetEventManager) get modes 2 and 3 too.
void Build(const Copy &c, uint32_t seed, drawfx::Cells &cells, bool nullOk)
{
    std::mt19937 rng(seed * 31u + 7u);
    static const uint32_t counts[] = {1, 2, 3, 0x80000001u, 0x80000002u, 0x80000003u, 0, 0x80000000u};
    MakeObject(c, counts[rng() % 8]);
    for (unsigned i = 0; i < 3; ++i) {
        w.listeners[i].Reset<sgfx::kBehavior>(500 + i);
        w.listeners[i].AddFace(kBehaviorType, w.listeners[i]);
        w.listeners[i].On(3, &ListenerEvent);
        w.listeners[i].Returns(4, uint32_t(rng() % 3 != 0));
    }
    w.framework.Reset<kFramework>(460);
    w.framework.On(5, &FrameworkGetService);
    w.service.Reset<kService>(461);
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, c.cell);
    const unsigned mode = rng() % (nullOk ? 4 : 2);
    w.framework.value[0] = mode != 3;
    w.service.Returns(3, mode == 2 ? nullptr : static_cast<const void *>(w.manager.manager));
    cells.Set(0x10c9378, &w.framework);
    for (uint32_t &e : w.events) e = rng() % 4 ? 0x100 + rng() % 3 : kConnectionChanged;
    // Listeners connected to the object (before the cell is cleared: through the manager itself).
    const unsigned connections = rng() % 5;
    for (unsigned k = 0; k < connections; ++k)
        w.manager.Listen(&gObject, w.events[rng() % 4], w.listeners[rng() % 3], 0x500 + rng() % 4);
    if (mode) cells.Set(c.cell, 0u);
    gObject.count = counts[rng() % 8]; // after the connections (the manager's connection events leave it alone)
    w.map.clear();
    const unsigned records = rng() % 5;
    for (unsigned k = 0; k < records; ++k) {
        cEventAttribute a{};
        uint32_t *words = reinterpret_cast<uint32_t *>(&a);
        for (unsigned i = 0; i < 6; ++i) words[i] = rng();
        w.map[k == 0 ? w.events[0] : rng() % 4 ? 0x100 + rng() % 3 : rng()] = a;
    }
}

void NoteEnd(const Copy &c)
{
    drawfx::Note("count", gObject.count);
    drawfx::Note("refs", {gObject.refs, uint32_t(w.service.refs), uint32_t(w.framework.refs),
                          uint32_t(w.listeners[0].refs), uint32_t(w.listeners[1].refs), uint32_t(w.listeners[2].refs)});
    drawfx::Note("cell", drawfx::Norm(*GameAddress<uint32_t>(c.cell)));
    if (w.manager.manager)
        drawfx::Note("connected", {uint8_t(w.manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&gObject))),
                                   uint8_t(w.manager->HasConnectionsTo(reinterpret_cast<cIGZUnknown *>(&w.listeners[0]))),
                                   uint8_t(w.manager->HasConnectionsTo(reinterpret_cast<cIGZUnknown *>(&w.listeners[1])))});
}

// The copy's missing record and ours hold the same pattern for the scope.
struct MissingRecordScope {
    uint32_t at;
    uint32_t saved[6];
    cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    explicit MissingRecordScope(uint32_t rva) : at(rva)
    {
        if (!at) return;
        std::memcpy(saved, GameAddress<void>(at), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(at)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i;
        }
    }
    ~MissingRecordScope()
    {
        if (at) std::memcpy(GameAddress<void>(at), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

// Runs `call(fn)` for every copy whose `body` is this lane's, each side with its own function.
template <class Fn, class Call> void ForCopies(difftest::Context &ctx, Body body, unsigned seeds, bool nullOk, Call &&call)
{
    for (unsigned i = 0; i < kCopies; ++i) {
        const Copy &c = CopyAt(i);
        if (!c.rva[body]) continue;
        shape::StandIn registry(c.registry, c.oursRegistry,
                                c.getter ? reinterpret_cast<const void *>(&StandRegistryGet)
                                         : reinterpret_cast<const void *>(&StandRegistry));
        MissingRecordScope missing(body == kAttribute ? c.missing : 0);
        sgfx::CheckSides(
            ctx, c.name, seeds, [&](uint32_t seed, drawfx::Cells &cells) { Build(c, seed, cells, nullOk); },
            [&](bool original, std::mt19937 &rng) {
                const Fn fn = original ? ctx.Original<Fn>(c.rva[body]) : reinterpret_cast<Fn>(c.ours[body]);
                call(fn, rng);
                NoteEnd(c);
            });
        w.manager.Drop();
    }
}

uint32_t Event(std::mt19937 &rng) { return rng() % 3 ? w.events[rng() % 4] : rng() % 2 ? kConnectionChanged : 0x777; }
cIGZUnknown *Listener(std::mt19937 &rng) { return reinterpret_cast<cIGZUnknown *>(&w.listeners[rng() % 3]); }

constexpr unsigned kSeeds = 40;

} // namespace

// Release (slot 2): counts 0-3 with and without the active bit; the last one disconnects everything (when there is a
// manager) and deletes through the copy's deleting destructor slot.
DIFF_TEST(ts_behavior_copies_release, 0xcc3630)
{
    using Fn = uint32_t(__thiscall *)(void *);
    ForCopies<Fn>(ctx, kRelease, kSeeds, true, [](Fn fn, std::mt19937 &) { drawfx::Note("release", fn(&gObject)); });
}

// OnEvent (slot 3): the connection-changed event resyncs the active bit with whether anyone listens; others do nothing.
DIFF_TEST(ts_behavior_copies_on_event, 0xcc30b0)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t);
    ForCopies<Fn>(ctx, kOnEvent, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) fn(&gObject, rng() % 2 ? kConnectionChanged : Event(rng), rng(), gData, rng());
    });
}

// NotifyListeners (slot 5): an active object tells each listener of the event (its own event id, the data, itself).
DIFF_TEST(ts_behavior_copies_notify_listeners, 0xcc3850)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, void *);
    ForCopies<Fn>(ctx, kNotify, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) fn(&gObject, Event(rng), rng(), gData);
    });
}

// AskListeners (slot 6): every listener's answer, true only when there is one and none says no.
DIFF_TEST(ts_behavior_copies_ask_listeners, 0xcc2f20)
{
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, void *);
    ForCopies<Fn>(ctx, kAsk, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) drawfx::Note("asked", uint8_t(fn(&gObject, Event(rng), rng(), gData)));
    });
}

// Connect (slot 8): the manager connects the object's event to a listener's.
DIFF_TEST(ts_behavior_copies_connect, 0xcc2d50)
{
    using Fn = void(__thiscall *)(void *, uint32_t, cIGZUnknown *, uint32_t);
    ForCopies<Fn>(ctx, kConnect, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) fn(&gObject, Event(rng), Listener(rng), 0x500 + rng() % 4);
    });
}

// Disconnect (slot 9): the manager drops a connection (one the object has, or not).
DIFF_TEST(ts_behavior_copies_disconnect, 0xcc2e90)
{
    using Fn = void(__thiscall *)(void *, uint32_t, cIGZUnknown *, uint32_t);
    ForCopies<Fn>(ctx, kDisconnect, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 4; ++k) fn(&gObject, Event(rng), Listener(rng), 0x500 + rng() % 4);
    });
}

// GetEventCount (slot 12): the registry's size (asked with id 0 and the default record, or through the getter).
DIFF_TEST(ts_behavior_copies_get_event_count, 0xcc34c0)
{
    using Fn = uint32_t(__thiscall *)(void *);
    ForCopies<Fn>(ctx, kCount, kSeeds, false, [](Fn fn, std::mt19937 &) { drawfx::Note("events", fn(&gObject)); });
}

// GetEventAttribute (slot 14): the registry's record for the event (asked with id 0 and the default record), or the
// missing record; written through the hidden out (canaries around it).
DIFF_TEST(ts_behavior_copies_get_event_attribute, 0xcc2c70)
{
    using Fn = cEventAttribute *(__thiscall *)(void *, cEventAttribute *, uint32_t);
    ForCopies<Fn>(ctx, kAttribute, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) {
            uint32_t out[8];
            for (uint32_t &x : out) x = 0xc0c0c0c0u;
            const uint32_t event = rng() % 2 ? w.events[rng() % 4] : rng() % 2 ? 0x100 + rng() % 3 : rng();
            cEventAttribute *r = fn(&gObject, reinterpret_cast<cEventAttribute *>(out + 1), event);
            drawfx::Note("attribute", {uint8_t(P(r) == P(out + 1)), out[0], out[1], out[2], out[3], out[4], out[5],
                                       out[6], out[7]});
        }
    });
}

// GetEventManager (cdecl): the cell's manager, or, with the cell null, the event service's (stored in the cell; the
// service released), or null.
DIFF_TEST(ts_behavior_copies_get_event_manager, 0xcc2d70)
{
    using Fn = nGZEvent::cIConnectionManager *(__cdecl *)();
    ForCopies<Fn>(ctx, kManager, kSeeds, true, [](Fn fn, std::mt19937 &) {
        drawfx::Note("manager", drawfx::Norm(P(fn())));
        drawfx::Note("again", drawfx::Norm(P(fn())));
    });
}
