// Differential tests of the game's copies of cAbstractBehaviorTypeT<I, cRefSingleThreaded> (lane SV, scenegraph F1;
// src/sg/SGBehaviorCopies.h): one test per body (Release, OnEvent, NotifyListeners, AskListeners, Connect, Disconnect,
// GetEventAttribute, GetEventManager), one case per copy, the image's body against cBehaviorCopy's adapter.
//
// The object is a hand-made behaviour: a vtable as long as the copy's (its slots 0-3 and the copy's deleting destructor
// slot test functions that note their calls; every other slot a trap), its +4 word the count and active bit the bodies
// keep. The world: a real connection manager (sgfx::Manager) in the copy's cell, or the cell null with the framework
// (in g_Framework 0x10c9378) handing out an event service whose GetConnectionManager answers the manager (or nothing,
// or no service); listener fakes connected to the object; the copy's registry stood in on both sides (the image's
// function and ours) by a test map; a pattern planted in the copy's image missing record and in ours.
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "hook/hooks.h"
#include "sg/SGBehaviorCopies.h"

#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using namespace nGZSceneGraph::behavior_copies;
using drawfx::Fake;
using drawfx::P;
using nGZEvent::cEventAttribute;
using nGZEvent::cEventAttributeMap;

enum Body { kRelease, kOnEvent, kNotify, kAsk, kConnect, kDisconnect, kAttribute, kManager, kBodies };

struct Copy {
    const char *name;
    int slot;            // the deleting destructor's
    uint32_t cell;       // the connection-manager cell
    uint32_t registry;   // the registry function's RVA
    RegistryForFn oursRegistry;
    uint32_t missing;    // the image's missing record (0: the copy's GetEventAttribute is not ours)
    uint32_t rva[kBodies]; // 0: the body is not this lane's
    void *ours[kBodies];
};

template <class C>
Copy Make(const char *name, uint32_t cell, uint32_t registry, RegistryForFn oursRegistry, uint32_t missing,
          std::initializer_list<uint32_t> rvas)
{
    Copy c{name, C::kSlot, cell, registry, oursRegistry, missing, {}, {}};
    std::copy(rvas.begin(), rvas.end(), c.rva);
    c.ours[kRelease] = MemberAddress(&C::HookRelease);
    c.ours[kOnEvent] = MemberAddress(&C::HookOnEvent);
    c.ours[kNotify] = MemberAddress(&C::HookNotifyListeners);
    c.ours[kAsk] = MemberAddress(&C::HookAskListeners);
    c.ours[kConnect] = MemberAddress(&C::HookConnect);
    c.ours[kDisconnect] = MemberAddress(&C::HookDisconnect);
    c.ours[kAttribute] = MemberAddress(&C::HookGetEventAttribute);
    c.ours[kManager] = reinterpret_cast<void *>(&C::HookGetEventManager);
    return c;
}

const Copy &CopyAt(unsigned i)
{
    static const Copy copies[] = {
        Make<CompositionTreeNodeCopy>("composition tree node", 0x10f5f98, 0x263f30,
                                      &cCompositionTreeNodeBehaviorTraits::RegistryFor, 0x1069fb0,
                                      {0xa38d30, 0xa37c70, 0xa38de0, 0xa377b0, 0xa37660, 0xa37740, 0xa37280, 0xa37680}),
        Make<TransformNodeCopy>("transform node", 0x10f5ffc, 0x2640f0, &cTransformNodeBehaviorTraits::RegistryFor,
                                0x106a1f0, {0xa3af50, 0xa3a750, 0xa3afc0, 0xa3a560, 0xa3a4a0, 0xa3a540, 0xa3a1f0, 0xa3a4c0}),
        Make<ExplicitBoundNodeCopy>("explicit bound node", 0x10f616c, 0x2640f0,
                                    &cTransformNodeBehaviorTraits::RegistryFor, 0x106ad88,
                                    {0xa4c3d0, 0xa4bce0, 0xa4c440, 0xa4bad0, 0xa4ba10, 0xa4bab0, 0xa4b760, 0xa4ba30}),
        Make<RenderableNodeCopy>("renderable node", 0x10fc820, 0x2640f0, &cTransformNodeBehaviorTraits::RegistryFor,
                                 0x107cd74, {0xbbce10, 0xbba7e0, 0xbbcf30, 0xbb9640, 0xbb93c0, 0xbb9620, 0xbb9190, 0xbb93e0}),
        // Release, Connect, Disconnect and GetEventAttribute are cGeometryNode's (src/sg/cGeometryNode.cpp).
        Make<GeometryNodeCopy>("geometry node", 0x10f6080, 0xa42f30, &cGeometryNodeBehaviorTraits::RegistryFor, 0,
                               {0, 0xa42720, 0xa432f0, 0xa423f0, 0, 0, 0, 0xa3f100}),
        Make<LightCopy>("light", 0x10da038, 0x3a5d00, &cSimpleLightBehaviorTraits::RegistryFor, 0x100b6a0,
                        {0x3a5e80, 0x3a56c0, 0x3a6000, 0x3a5540, 0x3a53a0, 0x3a5440, 0x3a51e0, 0x3a53c0}),
        Make<StandardLightCopy>("standard light", 0x10da480, 0xa49210, &cStandardLightBehaviorTraits::RegistryFor,
                                0x106ab68, {0x3c8f90, 0x3c87f0, 0x3c9840, 0x3c8690, 0x3c85d0, 0x3c8670, 0xa46fa0, 0x3c85f0}),
        // GetEventAttribute 0xa46ec0 is shared by the three lights below (not hooked).
        Make<DrawStateLightCopy>("draw state light", 0x10f614c, 0x3a5d00, &cSimpleLightBehaviorTraits::RegistryFor, 0,
                                 {0xa492f0, 0xa47dc0, 0xa49ae0, 0xa47690, 0xa47450, 0xa47630, 0, 0xa474b0}),
        Make<LinearFogLightCopy>("linear fog light", 0x10f6148, 0x3a5d00, &cSimpleLightBehaviorTraits::RegistryFor, 0,
                                 {0xa49340, 0xa47e00, 0xa49bd0, 0xa477b0, 0xa47470, 0xa47650, 0, 0xa47530}),
        Make<TextureLightCopy>("texture light", 0x10f6144, 0x3a5d00, &cSimpleLightBehaviorTraits::RegistryFor, 0,
                               {0xa49390, 0xa47e40, 0xa49cc0, 0xa478d0, 0xa47490, 0xa47670, 0, 0xa475b0}),
        Make<ShapeCopy>("shape", 0x10f6324, 0xa5f010, &cShapeBehaviorTraits::RegistryFor, 0x106b9c0,
                        {0xa5f0f0, 0xa5d6d0, 0xa5f3d0, 0xa5d250, 0xa5d120, 0xa5d230, 0xa5ce10, 0xa5d140}),
        Make<ShapeRefNodeCopy>("shape ref node", 0x10f62dc, 0xa585a0, &ShapeRefRegistryFor, 0x106b620,
                               {0xa58680, 0xa552f0, 0xa58760, 0xa55110, 0xa55010, 0xa550d0, 0xa54c00, 0xa55030}),
        Make<LightRefNodeCopy>("light ref node", 0x10f6380, 0xa633d0, &LightRefRegistryFor, 0x106bb88,
                               {0xa634b0, 0xa61af0, 0xa63550, 0xa61920, 0xa61840, 0xa61900, 0xa61460, 0xa61860}),
        Make<ViewerRefNodeCopy>("viewer ref node", 0x10f63e0, 0xa68db0, &ViewerRefRegistryFor, 0x106bee0,
                                {0xa68ee0, 0xa660d0, 0xa69150, 0xa65e50, 0xa65bd0, 0xa65d10, 0xa65270, 0xa65c70}),
        Make<ViewerRefNodeRecursiveCopy>("viewer ref node recursive", 0x10f6424, 0x2640f0,
                                         &cTransformNodeBehaviorTraits::RegistryFor, 0x106c1c0,
                                         {0xa6c290, 0xa6ada0, 0xa6c770, 0xa6ab10, 0xa6aa50, 0xa6aaf0, 0xa6a400, 0xa6aa70}),
        Make<ObjectGraphNodeCopy>("object graph node", 0x10f63dc, 0x264010, &cObjectGraphBehaviorTraits::RegistryFor,
                                  0x106bee0, {0xa68e90, 0xa66090, 0xa69060, 0xa65d30, 0xa65bb0, 0xa65cf0, 0xa65190, 0xa65bf0}),
        Make<DisplayListCopy>("display list", 0x10f6628, 0xaa2210, &DisplayListRegistryFor, 0x106deb8,
                              {0xaa22f0, 0xaa0f40, 0xaa2a20, 0xaa0c90, 0xaa05f0, 0xaa06a0, 0xa9fa00, 0xaa0610}),
        Make<MaterialManagerCopy>("material manager", 0x10f6998, 0xacc550, &cMaterialManagerBehaviorTraits::RegistryFor,
                                  0x106fad0, {0xaccac0, 0xacb9e0, 0xacd1e0, 0xacb660, 0xacad30, 0xacb3e0, 0xacab80, 0xacad50}),
        Make<TextureManagerCopy>("texture manager", 0x10f8c50, 0xade400, &cTextureManagerBehaviorTraits::RegistryFor,
                                 0x1070058, {0xade780, 0xade1b0, 0xade840, 0xaddc20, 0xaddb40, 0xaddbe0, 0xadda30, 0xaddb60}),
        Make<CameraEffectsSupportCopy>("camera effects support", 0x10d9c7c, 0x361720, &CameraEffectsRegistryFor,
                                       0x1007c08,
                                       {0x361800, 0x361020, 0x361860, 0x360ca0, 0x3609b0, 0x360a50, 0x3608d0, 0x3609d0}),
    };
    return copies[i];
}
constexpr unsigned kCopies = 20;

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
        shape::StandIn registry(c.registry, c.oursRegistry, &StandRegistry);
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
DIFF_TEST(sg_behavior_copies_release, 0xa38d30)
{
    using Fn = uint32_t(__thiscall *)(void *);
    ForCopies<Fn>(ctx, kRelease, kSeeds, true, [](Fn fn, std::mt19937 &) { drawfx::Note("release", fn(&gObject)); });
}

// OnEvent (slot 3): the connection-changed event resyncs the active bit with whether anyone listens; others do nothing.
DIFF_TEST(sg_behavior_copies_on_event, 0xa37c70)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t);
    ForCopies<Fn>(ctx, kOnEvent, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) fn(&gObject, rng() % 2 ? kConnectionChanged : Event(rng), rng(), gData, rng());
    });
}

// NotifyListeners (slot 5): an active object tells each listener of the event (its own event id, the data, itself).
DIFF_TEST(sg_behavior_copies_notify_listeners, 0xa38de0)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, void *);
    ForCopies<Fn>(ctx, kNotify, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) fn(&gObject, Event(rng), rng(), gData);
    });
}

// AskListeners (slot 6): every listener's answer, true only when there is one and none says no.
DIFF_TEST(sg_behavior_copies_ask_listeners, 0xa377b0)
{
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, void *);
    ForCopies<Fn>(ctx, kAsk, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) drawfx::Note("asked", uint8_t(fn(&gObject, Event(rng), rng(), gData)));
    });
}

// Connect (slot 8): the manager connects the object's event to a listener's.
DIFF_TEST(sg_behavior_copies_connect, 0xa37660)
{
    using Fn = void(__thiscall *)(void *, uint32_t, cIGZUnknown *, uint32_t);
    ForCopies<Fn>(ctx, kConnect, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 3; ++k) fn(&gObject, Event(rng), Listener(rng), 0x500 + rng() % 4);
    });
}

// Disconnect (slot 9): the manager drops a connection (one the object has, or not).
DIFF_TEST(sg_behavior_copies_disconnect, 0xa37740)
{
    using Fn = void(__thiscall *)(void *, uint32_t, cIGZUnknown *, uint32_t);
    ForCopies<Fn>(ctx, kDisconnect, kSeeds, false, [](Fn fn, std::mt19937 &rng) {
        for (int k = 0; k < 4; ++k) fn(&gObject, Event(rng), Listener(rng), 0x500 + rng() % 4);
    });
}

// GetEventAttribute (slot 14): the registry's record for the event (asked with id 0 and the default record), or the
// missing record; written through the hidden out (canaries around it).
DIFF_TEST(sg_behavior_copies_get_event_attribute, 0xa37280)
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
DIFF_TEST(sg_behavior_copies_get_event_manager, 0xa37680)
{
    using Fn = nGZEvent::cIConnectionManager *(__cdecl *)();
    ForCopies<Fn>(ctx, kManager, kSeeds, true, [](Fn fn, std::mt19937 &) {
        drawfx::Note("manager", drawfx::Norm(P(fn())));
        drawfx::Note("again", drawfx::Norm(P(fn())));
    });
}
