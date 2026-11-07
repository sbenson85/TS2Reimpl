// Differential tests for nGZEvent::cGZEventReflector and its behaviour base (src/gz/cGZEventReflector.size.cpp,
// standalone shape). The original side builds a reflector with the original constructor 0x1a999a (game vtables 0xda22d4
// / 0xda2318), ours through the constructor hook's adapter; both run the same random script through their own vtables,
// called through our interface declarations (whose slots are the game's).
//
// The world, built afresh for each side: the event registry (the function-local static map 0x10c94f4 the original's
// 0x1485f0 returns, here a map of three records placed over it with its guards marked done), a connection manager (ours,
// lane LR: verified) in the event manager cell 0x10cadc8 (or handed out by a fake event system service through a fake
// framework in 0x10c9378), the game allocator (messages), and drawfx fakes: listeners of the whole behaviour interface
// (OnEvent and OnQuery logged with the event's contents; OnQuery answers per fake, one with a high byte only), sources
// whose slot 14 hands out a record (id 0 for unknown events, another id for some) and whose slot 8 connects through the
// manager for real, and a message target logging every message's fields (its answer per seed).
#include "difftest.h"
#include "free_recorder.h"
#include "gz_registry_fx.h"
#include "drawfx_core.h"
#include "ts_services_fixture.h"

#include "gz/cGZEventReflector.h"
#include "rz/cRZMessage2Standard.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <vector>

using namespace nGZEvent;

namespace {

constexpr uint32_t kCtor = 0x1a999a;
constexpr uint32_t kFactory = 0x1a9e0b;
constexpr uint32_t kRegistryMap = 0x10c94f4;
constexpr uint32_t kRegistryMapGuard = 0x10c94fc;
constexpr uint32_t kRegistryOnce = 0x10c9510;
constexpr uint32_t kRegistryHolderGuard = 0x10c9518;
constexpr uint32_t kEventManagerCell = 0x10cadc8;
constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kObjectTag = 0x7e000000;
constexpr uint32_t kNamesTag = 0x7d000000;
constexpr uint32_t kDataTag = 0x7c000000;
constexpr uint32_t kManagerTag = 0x7b000000;
constexpr uint32_t kHolderTag = 0x7a000000;
constexpr uint32_t kUnreflected = 0x7777; // an event no record has
using F0 = uint32_t(__thiscall *)(void *);

// ---- the vtables the script reaches (checked by GZEventReflector_VTables) ----
struct VSlot {
    uint32_t vtable, slot, rva;
};
constexpr VSlot kVSlots[] = {
    {0xda22d4, 0, 0x1aa03e},  {0xda22d4, 1, 0x1a9a34},  {0xda22d4, 2, 0x1aa0b7},  {0xda22d4, 3, 0x1a9e5f},
    {0xda22d4, 4, 0x1a9f05},  {0xda22d4, 5, 0x1aa0c4},  {0xda22d4, 6, 0x1a9d56},  {0xda22d4, 7, 0x377e0},
    {0xda22d4, 8, 0x1a9ce1},  {0xda22d4, 9, 0x1a9d36},  {0xda22d4, 10, 0x377e0},  {0xda22d4, 11, 0x147cf0},
    {0xda22d4, 12, 0x1a9ffa}, {0xda22d4, 13, 0x1a9b15}, {0xda22d4, 14, 0x1a9c11}, {0xda22d4, 15, 0x1a9a06},
    {0xda2318, 0, 0x1aa069},  {0xda2318, 1, 0x1a9a39},  {0xda2318, 2, 0x1aa0bc},  {0xda2318, 3, 0x1a9fb2},
    {0xda2318, 4, 0x1aa166},  {0xda2318, 5, 0x1aa15e},  {0xda2318, 6, 0x1a9c4a},
    {0xda2290, 0, 0x1aa007},  {0xda2290, 1, 0x1a9a25},  {0xda2290, 2, 0x1aa071},  {0xda2290, 3, 0x1a9e32},
    {0xda2290, 4, 0x1a9f00},  {0xda2290, 5, 0x1aa0c4},  {0xda2290, 6, 0x1a9d56},  {0xda2290, 8, 0x1a9ce1},
    {0xda2290, 9, 0x1a9d36},  {0xda2290, 11, 0x147cf0}, {0xda2290, 12, 0x1a9ff1}, {0xda2290, 13, 0x1a9a41},
    {0xda2290, 14, 0x1a9bd9}, {0xda2290, 15, 0x1a99e7},
};

// ---- fakes ----
inline constexpr drawfx::Slot kBehaviorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
    {3, drawfx::Ret::Int, true, "NotifyListeners", "uui"},
    {3, drawfx::Ret::Int, true, "AskListeners", "uui"},
    {3, drawfx::Ret::Int, true, "Slot7", "uuu"},
    {3, drawfx::Ret::Int, true, "Connect", "uiu"},
    {3, drawfx::Ret::Int, true, "Disconnect", "uiu"},
    {3, drawfx::Ret::Int, true, "Slot10", "uuu"},
    {0, drawfx::Ret::Int, true, "IsActive", ""},
    {0, drawfx::Ret::Int, true, "GetEventCount", ""},
    {1, drawfx::Ret::Int, true, "GetEventIds", "i"},
    {2, drawfx::Ret::Int, true, "GetEventAttribute", "iu"},
};
inline constexpr drawfx::Iface kBehavior{"cIAbstractBehavior", 15, kBehaviorSlots};

inline constexpr drawfx::Slot kTargetSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {1, drawfx::Ret::Int, true, "DoMessage", "i"},
};
inline constexpr drawfx::Iface kTarget{"cIGZMessageTarget2", 4, kTargetSlots};

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

constexpr unsigned kListeners = 4;
constexpr unsigned kSources = 2;
drawfx::Fake gListeners[kListeners];
drawfx::Fake gSources[kSources];
drawfx::Fake gTarget;
drawfx::Fake gFramework;
drawfx::Fake gService;
cConnectionManager *gManager = nullptr;
char gNames[3][8];        // the registry records' names
uint32_t gData[4];        // data the script passes
const uint32_t kRegistryIds[] = {0x10, 0x11, 0x12};

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

bool OnStack(uint32_t p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= uintptr_t(tib->StackLimit) && p < uintptr_t(tib->StackBase);
}
bool InData(uint32_t p) { return p >= Word(gData) && p < Word(gData + 4); }

// The listeners' OnEvent: what the event points at. The manager's connection changes (its stack
// cConnectionChangedEvent: the words, the ends normalised), the reflector's stack cNullEvent (its vtable), the script's
// data words; other values are not pointers.
uint32_t ListenerOnEvent(drawfx::Fake &, const uint32_t *args)
{
    const uint32_t data = args[2];
    if (args[0] == cConnectionChangedEvent::kType && OnStack(data)) {
        const auto *e = reinterpret_cast<const cConnectionChangedEvent *>(uintptr_t(data));
        drawfx::Note("change", {Word(e->mVTable) == Word(GameAddress<void>(cConnectionChangedEvent::kVTable)) ? 1u : 0u,
                                e->mDisconnected, drawfx::Norm(Word(e->mSource)), drawfx::Norm(Word(e->mTarget)),
                                e->mSourceEvent, e->mTargetEvent});
    } else if (OnStack(data)) {
        const uint32_t vtable = *reinterpret_cast<const uint32_t *>(uintptr_t(data));
        drawfx::Note("stack event", vtable == Word(GameAddress<void>(cGZEventReflector::kNullEventVTable)) ? 1u : vtable);
    } else if (InData(data)) {
        drawfx::Note("data", *reinterpret_cast<const uint32_t *>(uintptr_t(data)));
    }
    return 0x5a;
}
uint32_t ListenerOnQuery(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }

// The sources: slot 14 hands out a record (id 0 for events divisible by 3, id+0x20 for 4 mod 5), slot 8 connects.
uint32_t SourceAttribute(drawfx::Fake &self, const uint32_t *args)
{
    auto *out = reinterpret_cast<cEventAttribute *>(uintptr_t(args[0]));
    const uint32_t event = args[1];
    out->mId = event % 3 == 0 ? 0 : event % 5 == 4 ? event + 0x20 : event;
    out->mName = gNames[event % 3];
    out->mWord08 = event * 3 + self.id;
    out->mWord0C = 0x0c0c0000 | event;
    out->mWord10 = self.id;
    out->mWord14 = 0x99;
    return args[0];
}
uint32_t SourceConnect(drawfx::Fake &self, const uint32_t *args)
{
    static_cast<cIConnectionManager *>(gManager)->Connect(reinterpret_cast<cIGZUnknown *>(&self), args[0],
                                                          reinterpret_cast<cIGZUnknown *>(uintptr_t(args[1])), args[2]);
    return 0;
}

// The target: every message's fields (the game's vtable, count, flags, type, three data words).
uint32_t TargetDoMessage(drawfx::Fake &self, const uint32_t *args)
{
    const auto *m = reinterpret_cast<const cRZMessage2Standard *>(uintptr_t(args[0]));
    drawfx::Note("message", {Word(m->mVTable) == Word(GameAddress<void>(cRZMessage2Standard::kVTable)) ? 1u : 0u,
                             m->mRefCount, m->mFlags, m->mId, m->mValue[0], m->mValue[1], drawfx::Norm(m->mValue[2])});
    return self.value[0];
}

// The framework: GetSystemService answers the event system service (counted) when value[0] says so.
uint32_t FrameworkGetService(drawfx::Fake &self, const uint32_t *args)
{
    if (!self.value[0] || args[0] != 0x5b1e896f) return 0;
    ++gService.refs;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &gService;
    return 1;
}

void ResetFakes(uint32_t seed)
{
    for (unsigned i = 0; i < kListeners; ++i) {
        gListeners[i].Reset<kBehavior>(0x900 + i);
        gListeners[i].AddFace(cIAbstractBehaviorType::kIid, gListeners[i]);
        gListeners[i].On(3, &ListenerOnEvent);
        gListeners[i].On(4, &ListenerOnQuery);
        gListeners[i].value[0] = i == 3 ? 0x100u : (i + seed) % 3 != 0 ? 1u : 0u;
    }
    for (unsigned i = 0; i < kSources; ++i) {
        gSources[i].Reset<kBehavior>(0x940 + i);
        gSources[i].AddFace(cIAbstractBehaviorType::kIid, gSources[i]);
        gSources[i].On(14, &SourceAttribute);
        gSources[i].On(8, &SourceConnect);
    }
    gTarget.Reset<kTarget>(0x960);
    gTarget.On(3, &TargetDoMessage);
    gTarget.value[0] = seed % 3 == 0 ? 0x100u : seed % 2;
    gFramework.Reset<kFramework>(0x970);
    gFramework.On(5, &FrameworkGetService);
    gFramework.value[0] = seed % 4 != 1;
    gService.Reset<kService>(0x971);
    drawfx::AddRange(gNames, sizeof(gNames), kNamesTag);
    drawfx::AddRange(gData, sizeof(gData), kDataTag);
}

void NoteRefs()
{
    for (const drawfx::Fake &f : gListeners) drawfx::Note("listener refs", uint32_t(f.refs));
    for (const drawfx::Fake &f : gSources) drawfx::Note("source refs", uint32_t(f.refs));
    drawfx::Note("other refs", {uint32_t(gTarget.refs), uint32_t(gService.refs), uint32_t(gFramework.refs)});
}

// ---- the registry: a map placed over the original's static, its guards marked done ----
class RegistryScope {
public:
    RegistryScope()
    {
        std::memcpy(saved_, GameAddress<void>(kRegistryMap), sizeof(saved_));
        savedMapGuard_ = *GameAddress<int32_t>(kRegistryMapGuard);
        savedOnce_ = *GameAddress<uint8_t>(kRegistryOnce);
        savedHolderGuard_ = *GameAddress<int32_t>(kRegistryHolderGuard);
        map_ = ::new (GameAddress<void>(kRegistryMap)) cEventAttributeMap;
        *GameAddress<int32_t>(kRegistryMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kRegistryOnce) = 1;
        *GameAddress<int32_t>(kRegistryHolderGuard) = INT_MIN;
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof(gNames[i]), "n%u", i);
            (*map_)[kRegistryIds[i]] = {kRegistryIds[i], gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    ~RegistryScope()
    {
        map_->~cEventAttributeMap();
        std::memcpy(GameAddress<void>(kRegistryMap), saved_, sizeof(saved_));
        *GameAddress<int32_t>(kRegistryMapGuard) = savedMapGuard_;
        *GameAddress<uint8_t>(kRegistryOnce) = savedOnce_;
        *GameAddress<int32_t>(kRegistryHolderGuard) = savedHolderGuard_;
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    uint8_t saved_[sizeof(cEventAttributeMap)];
    int32_t savedMapGuard_;
    uint8_t savedOnce_;
    int32_t savedHolderGuard_;
    cEventAttributeMap *map_;
};
static_assert(sizeof(cEventAttributeMap) == 8, "the registry map is two words, as the original's");

// The cells the world sets (restored at the end of the test).
class WorldCells {
public:
    WorldCells() : manager_(*GameAddress<uint32_t>(kEventManagerCell)), framework_(*GameAddress<uint32_t>(kFrameworkCell))
    {
    }
    ~WorldCells()
    {
        *GameAddress<uint32_t>(kEventManagerCell) = manager_;
        *GameAddress<uint32_t>(kFrameworkCell) = framework_;
    }
    WorldCells(const WorldCells &) = delete;
    WorldCells &operator=(const WorldCells &) = delete;

private:
    uint32_t manager_, framework_;
};

// A side's world: a fresh manager in the cell (or only through the service, mode 0), the framework fake.
void BuildWorld(uint32_t seed)
{
    ResetFakes(seed);
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    gService.Returns(3, gManager);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kManagerTag);
    *GameAddress<void *>(kEventManagerCell) = seed % 4 == 0 ? nullptr : gManager;
    *GameAddress<void *>(kFrameworkCell) = &gFramework;
}
void DestroyWorld()
{
    cConnectionManagerAdapters::Destruct(gManager, nullptr);
    ::operator delete(gManager);
    gManager = nullptr;
}

// ---- the reflector under test ----
void *BuildOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cGZEventReflector::kSize);
    ctx.Original<F0>(kCtor)(memory);
    return memory;
}
void *BuildOurs() { return cGZEventReflectorAdapters::Construct(::operator new(cGZEventReflector::kSize), nullptr); }

cIAbstractBehavior *Primary(void *r) { return static_cast<cIAbstractBehavior *>(r); }
cIGZEventReflector *Face(void *r) { return reinterpret_cast<cIGZEventReflector *>(static_cast<char *>(r) + 8); }

// The reflector's state: count, target, the records (names normalised).
void NoteState(void *r)
{
    const uint32_t *w = static_cast<const uint32_t *>(r);
    drawfx::Note("count", w[1]);
    drawfx::Note("target", drawfx::Norm(w[3]));
    const auto *first = reinterpret_cast<const cEventAttribute *>(uintptr_t(w[4]));
    const auto *last = reinterpret_cast<const cEventAttribute *>(uintptr_t(w[5]));
    drawfx::Note("records", uint32_t(last - first));
    for (const cEventAttribute *a = first; a != last; ++a)
        drawfx::Note("record", {a->mId, drawfx::Norm(Word(a->mName)), a->mWord08, a->mWord0C, a->mWord10, a->mWord14});
}

// Walks an id range (Get / Next / IsEqual, a clone), then releases it: the frees by size.
void NoteIdRange(cAttributeIDRange range)
{
    drawfx::Note("ids present", {range.mCurrent ? 1u : 0u, range.mBegin ? 1u : 0u, range.mEnd ? 1u : 0u});
    unsigned steps = 0;
    while (steps < 20 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        if (steps == 1) {
            cIAttributeIDIterator *clone = range.mCurrent->Clone();
            drawfx::Note("clone", {clone->Get(), clone->IsEqual(range.mCurrent) ? 1u : 0u, clone->AddRef()});
            clone->Release();
        }
        range.mCurrent->Next();
        ++steps;
    }
    drawfx::Note("id steps", {steps, range.mBegin->IsEqual(range.mEnd) ? 1u : 0u});
    difftest::FreeRecorder rec;
    const uint32_t a = range.mCurrent->Release();
    const uint32_t b = range.mBegin->Release();
    const uint32_t c = range.mEnd->Release();
    rec.Stop();
    size_t sizes[8];
    const size_t n = rec.SortedSizes(sizes, 8);
    drawfx::Note("id releases", {a, b, c, uint32_t(n)});
    for (size_t i = 0; i < n; ++i) drawfx::Note("id freed", uint32_t(sizes[i]));
}

void NoteAttribute(const cEventAttribute &a)
{
    drawfx::Note("attribute", {a.mId, drawfx::Norm(Word(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
}

// One random script on the reflector at r. The model keeps the target set (a reflected event with no target is the
// original's crash: the target is assumed) and the connections made (to disconnect existing ones).
void RunScript(void *r, uint32_t seed, int steps)
{
    std::mt19937 rng(seed);
    cIAbstractBehavior *self = Primary(r);
    cIGZEventReflector *face = Face(r);
    bool targetSet = false;
    struct Conn {
        uint32_t event;
        unsigned listener;
        uint32_t listenerEvent;
    };
    std::vector<Conn> live;
    drawfx::AddRange(r, cGZEventReflector::kSize, kObjectTag);
    drawfx::Note("add ref", self->AddRef());
    const uint32_t events[] = {1, 2, 3, 4, 9, 0x10, 0x11, cGZEventReflector::kReflectedEvent};
    auto event = [&] { return events[rng() % 8]; };
    for (int step = 0; step < steps; ++step) {
        const unsigned op = rng() % 22;
        const unsigned l = rng() % kListeners;
        const uint32_t e = event();
        const uint32_t a = rng() % 50;
        void *data = rng() % 3 ? static_cast<void *>(&gData[rng() % 4]) : reinterpret_cast<void *>(uintptr_t(rng() % 7));
        switch (op) {
        case 0:
        case 1: {
            const uint32_t le = rng() % 2 ? e : rng() % 4;
            self->Connect(e, reinterpret_cast<cIGZUnknown *>(&gListeners[l]), le);
            live.push_back({e, l, le});
            drawfx::Note("connect", {e, l, le});
            break;
        }
        case 2: {
            if (live.empty()) break;
            const size_t pick = rng() % live.size();
            const Conn c = live[pick];
            self->Disconnect(c.event, reinterpret_cast<cIGZUnknown *>(&gListeners[c.listener]), c.listenerEvent);
            live.erase(live.begin() + pick);
            drawfx::Note("disconnect", {c.event, c.listener, c.listenerEvent});
            break;
        }
        case 3:
        case 4: {
            const uint32_t type = targetSet ? e : kUnreflected;
            self->OnEvent(type, a, data, rng() % 9);
            drawfx::Note("on event", type);
            break;
        }
        case 5: {
            const uint32_t type = targetSet ? e : kUnreflected;
            drawfx::Note("on query", {type, self->OnQuery(type, a, data) ? 1u : 0u});
            break;
        }
        case 6: self->OnEvent(cConnectionChangedEvent::kType, 0, nullptr, 0); break;
        case 7:
        case 8: {
            // The listener range's iterators are released at the end of the walk: count what that frees.
            difftest::FreeRecorder rec;
            if (op == 7) self->NotifyListeners(e, a, data);
            else drawfx::Note("ask", {e, self->AskListeners(e, a, data) ? 1u : 0u});
            rec.Stop();
            drawfx::Note("walk frees", uint32_t(rec.Count()));
            break;
        }
        case 9:
            self->SetAttribute(e, a, 3);
            self->GetAttribute(e, a, 4);
            break;
        case 10: drawfx::Note("active", {self->IsActive(), self->GetEventCount()}); break;
        case 11: NoteIdRange(self->GetEventIds()); break;
        case 12: NoteAttribute(self->GetEventAttribute(rng() % 2 ? e : e + 0x20)); break;
        case 13:
            if (rng() % 3 == 0) {
                face->ReflectRegistered();
                drawfx::Note("reflect registered", 0);
            }
            break;
        case 14:
            if (rng() % 4 == 0) {
                face->Reset();
                targetSet = false;
                live.clear();
                drawfx::Note("reset", 0);
            }
            break;
        case 15: {
            const bool set = rng() % 4 != 0;
            void *field = face->SetTarget(set ? reinterpret_cast<cIGZMessageTarget2 *>(&gTarget) : nullptr);
            targetSet = set;
            drawfx::Note("set target", {set ? 1u : 0u, drawfx::Norm(Word(field))});
            break;
        }
        case 16:
        case 17: {
            const unsigned s = rng() % kSources;
            face->Reflect(reinterpret_cast<cIAbstractBehavior *>(&gSources[s]), e);
            drawfx::Note("reflect", {s, e});
            break;
        }
        case 18: {
            static const uint32_t kIids[] = {1, cAbstractBehaviorType::kBehaviorIid2, cIAbstractBehaviorType::kIid,
                                             cIGZEventReflector::kIid, 0x12345678};
            const uint32_t riid = kIids[rng() % 5];
            const bool nullOut = riid != cIGZEventReflector::kIid && rng() % 5 == 0;
            void *out = reinterpret_cast<void *>(0x1234);
            const bool viaFace = rng() % 2;
            const bool ok = viaFace ? face->QueryInterface(riid, nullOut ? nullptr : &out)
                                    : self->QueryInterface(riid, nullOut ? nullptr : &out);
            drawfx::Note("qi", {riid, viaFace ? 1u : 0u, ok ? 1u : 0u, drawfx::Norm(Word(out))});
            if (ok && riid != cIAbstractBehaviorType::kIid) drawfx::Note("qi release", self->Release());
            break;
        }
        case 19: drawfx::Note("refs", {face->AddRef(), self->AddRef(), face->Release(), self->Release()}); break;
        case 20: NoteState(r); break;
        case 21: drawfx::Note("cell", drawfx::Norm(*GameAddress<uint32_t>(kEventManagerCell))); break;
        }
    }
    NoteState(r);
    NoteRefs();
    // The last reference: the reflector is disconnected from everything and deleted (the frees by size).
    difftest::FreeRecorder rec;
    const uint32_t last = self->Release();
    rec.Stop();
    size_t sizes[16];
    const size_t n = rec.SortedSizes(sizes, 16);
    drawfx::Note("last release", {last, uint32_t(n)});
    for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
    for (unsigned i = 0; i < kListeners; ++i)
        drawfx::Note("listener connected", {i, static_cast<cIConnectionManager *>(gManager)->HasConnectionsTo(
                                                   reinterpret_cast<cIGZUnknown *>(&gListeners[i])) ? 1u : 0u});
    NoteRefs();
}

template <class Run> bool Compare(difftest::Context &ctx, const char *what, uint32_t seed, Run run)
{
    auto a = drawfx::Side([&] { BuildWorld(seed); }, [&] {
        run(BuildOriginal(ctx));
        DestroyWorld();
    });
    auto b = drawfx::Side([&] { BuildWorld(seed); }, [&] {
        run(BuildOurs());
        DestroyWorld();
    });
    return drawfx::Same(ctx, a.first, b.first, what, seed, a.second.empty() ? b.second : a.second);
}

} // namespace

// The reflector under a random script. Reaches Reflect 0x1a9c4a with its record copy (the cEventAttribute constructor
// 0x1a996e, inlined in ours), the registry 0x1480d0 (ReflectRegistered), the event manager getter 0x1a9d01 (the cell
// empty one seed in four) and the destructor 0x1a99c8 (the last Release).
DIFF_TEST(GZEventReflector_Script, 0x1a9c4a)
{
    svcfx::AllocatorScope allocator;
    RegistryScope registry;
    shape::StandIn registryStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    WorldCells cells;
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        const int steps = seed % 10 == 0 ? 250 : 60;
        if (!Compare(ctx, "script", seed, [&](void *r) { RunScript(r, seed, steps); })) return;
    }
}

// The base's own slots 12-14 (the reflector overrides them, so only a direct call reaches the original's): the
// registry's (0x1480d0) size, its ids and its records (an unknown id: the static record 0xff2374).
DIFF_TEST(GZEventReflector_BaseRegistrySlots, 0x1a9a41)
{
    svcfx::AllocatorScope allocator;
    RegistryScope registry;
    shape::StandIn registryStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    WorldCells cells;
    using Count = uint32_t(__thiscall *)(void *);
    using Ids = cAttributeIDRange *(__thiscall *)(void *, cAttributeIDRange *);
    using Attribute = cEventAttribute *(__thiscall *)(void *, cEventAttribute *, uint32_t);
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        const uint32_t ids[] = {0x10, 0x11, 0x12, 0, 5, 0x13};
        if (!Compare(ctx, "base slots", seed, [&](void *r) {
                const bool ours = *static_cast<uint32_t *>(r) != Word(GameAddress<void>(cGZEventReflector::kVTable));
                auto *base = static_cast<cAbstractBehaviorType *>(static_cast<cGZEventReflector *>(r));
                drawfx::Note("count", ours ? base->cAbstractBehaviorType::GetEventCount()
                                           : ctx.Original<Count>(0x1a9ff1)(r));
                cAttributeIDRange range{};
                if (ours)
                    range = base->cAbstractBehaviorType::GetEventIds();
                else
                    ctx.Original<Ids>(0x1a9a41)(r, &range);
                NoteIdRange(range);
                for (uint32_t id : ids) {
                    cEventAttribute a{};
                    if (ours)
                        a = base->cAbstractBehaviorType::GetEventAttribute(id);
                    else
                        ctx.Original<Attribute>(0x1a9bd9)(r, &a, id);
                    NoteAttribute(a);
                }
                Primary(r)->AddRef();
                Primary(r)->Release(); // the last reference: deleted
            }))
            return;
    }
}

// The factory: the face of a new reflector (count 0, no target, no records), its class id and its frees on release.
DIFF_TEST(GZEventReflector_Factory, 0x1a9e0b)
{
    svcfx::AllocatorScope allocator;
    RegistryScope registry;
    shape::StandIn registryStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    WorldCells cells;
    using Factory = cIGZEventReflector *(__cdecl *)();
    for (uint32_t seed = 0; seed < 4; ++seed) {
        auto side = [&](bool ours) {
            return drawfx::Side([&] { BuildWorld(seed + 1); }, [&] {
                cIGZEventReflector *face = ours ? cGZEventReflector::Create() : ctx.Original<Factory>(kFactory)();
                void *r = static_cast<char *>(static_cast<void *>(face)) - 8;
                drawfx::AddRange(r, cGZEventReflector::kSize, kObjectTag);
                drawfx::Note("vtables", {*static_cast<uint32_t *>(r) == Word(GameAddress<void>(cGZEventReflector::kVTable)),
                                         *reinterpret_cast<uint32_t *>(face) ==
                                             Word(GameAddress<void>(cGZEventReflector::kReflectorVTable))});
                NoteState(r);
                drawfx::Note("add ref", face->AddRef());
                if (seed & 1) face->SetTarget(reinterpret_cast<cIGZMessageTarget2 *>(&gTarget));
                if (seed & 2) face->ReflectRegistered();
                NoteRefs();
                difftest::FreeRecorder rec;
                drawfx::Note("release", face->Release());
                rec.Stop();
                size_t sizes[8];
                const size_t n = rec.SortedSizes(sizes, 8);
                drawfx::Note("frees", uint32_t(n));
                for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
                NoteRefs();
                DestroyWorld();
            });
        };
        auto a = side(false);
        auto b = side(true);
        // Ours carries our vtables: the "vtables" note differs by design, so it is compared apart.
        if (a.first.size() != b.first.size() || a.first.empty()) {
            ctx.Fail("factory seed %u: logs differ in length", seed);
            return;
        }
        a.first.erase(a.first.begin());
        b.first.erase(b.first.begin());
        if (!drawfx::Same(ctx, a.first, b.first, "factory", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// Without an event manager (the cell empty, no event system service), the last Release deletes without disconnecting.
DIFF_TEST(GZEventReflector_NoManager, 0x1aa071)
{
    svcfx::AllocatorScope allocator;
    RegistryScope registry;
    shape::StandIn registryStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    WorldCells cells;
    for (uint32_t seed = 0; seed < 2; ++seed) {
        if (!Compare(ctx, "no manager", seed, [&](void *r) {
                *GameAddress<void *>(kEventManagerCell) = nullptr;
                gFramework.value[0] = 0;
                if (seed) *GameAddress<void *>(kFrameworkCell) = nullptr;
                cIAbstractBehavior *self = Primary(r);
                drawfx::Note("add ref", {self->AddRef(), self->AddRef()});
                drawfx::Note("release", self->Release());
                Face(r)->Reset(); // no service: nothing disconnected
                difftest::FreeRecorder rec;
                drawfx::Note("last", self->Release());
                rec.Stop();
                drawfx::Note("frees", uint32_t(rec.Count()));
                drawfx::Note("cell", *GameAddress<uint32_t>(kEventManagerCell));
                NoteRefs();
            }))
            return;
    }
}

// A bare behaviour (the base constructor 0x1a98d0 on a block of the side's own operator new): its first answers, a
// connection made and dropped through the base's vtable, and the last Release through the base's deleting destructor
// (0x1aa071 -> slot 15, 0x1a99e7 -> 0x1a99c1): the frees and the manager's view after.
DIFF_TEST(GZEventReflector_BaseObject, 0x1a99e7)
{
    svcfx::AllocatorScope allocator;
    RegistryScope registry;
    shape::StandIn registryStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    WorldCells cells;
    using BaseCtor = void *(__thiscall *)(void *);
    using GameNew = void *(__cdecl *)(size_t);
    for (uint32_t seed = 0; seed < 4; ++seed) {
        auto side = [&](bool ours) {
            return drawfx::Side([&] { BuildWorld(seed + 2); }, [&] {
                cIAbstractBehavior *self =
                    ours ? static_cast<cIAbstractBehavior *>(new cAbstractBehaviorType)
                         : static_cast<cIAbstractBehavior *>(
                               ctx.Original<BaseCtor>(0x1a98d0)(ctx.Original<GameNew>(0x824632)(8)));
                drawfx::AddRange(self, 8, kObjectTag);
                drawfx::Note("fresh", {self->IsActive(), static_cast<uint32_t *>(static_cast<void *>(self))[1]});
                void *out = nullptr;
                drawfx::Note("qi", {self->QueryInterface(1, &out) ? 1u : 0u, drawfx::Norm(Word(out))});
                drawfx::Note("add ref", self->AddRef());
                if (seed & 1) {
                    self->Connect(0x40 + seed, reinterpret_cast<cIGZUnknown *>(&gListeners[0]), 0x50);
                    drawfx::Note("connected", self->IsActive());
                }
                if (seed & 2) self->Disconnect(0x40 + seed, reinterpret_cast<cIGZUnknown *>(&gListeners[0]), 0x50);
                drawfx::Note("release", {self->Release(), self->IsActive()});
                difftest::FreeRecorder rec;
                drawfx::Note("last", self->Release());
                rec.Stop();
                size_t sizes[4];
                const size_t n = rec.SortedSizes(sizes, 4);
                drawfx::Note("frees", uint32_t(n));
                for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
                drawfx::Note("listening", static_cast<cIConnectionManager *>(gManager)->HasConnectionsTo(
                                              reinterpret_cast<cIGZUnknown *>(&gListeners[0])) ? 1u : 0u);
                NoteRefs();
                DestroyWorld();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "base object", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The free helpers: SyncActiveBit (0x1aa148) over counts and flags (the flag's high bytes ignored), the record search
// 0x1a98af, the event service holder 0x1a991d and the range constructor 0x1a98e0 with original iterators.
DIFF_TEST(GZEventReflector_Helpers, 0x1aa148)
{
    using Sync = void(__cdecl *)(uint32_t *, uint32_t);
    const uint32_t counts[] = {0, 1, 0x7fffffff, 0x80000000, 0x80000005, 0xffffffff};
    const uint32_t flags[] = {0, 1, 0x100, 0x101}; // a bool's byte (callers pass movzx of one), high bytes leftovers
    for (uint32_t c : counts)
        for (uint32_t f : flags) {
            ++ctx.cases;
            uint32_t a = c, b = c;
            ctx.Original<Sync>(0x1aa148)(&a, f);
            reinterpret_cast<Sync>(&SyncActiveBit)(&b, f);
            if (a != b) ctx.Fail("SyncActiveBit(0x%x, 0x%x): orig 0x%x ours 0x%x", c, f, a, b);
        }

    using Find = const cEventAttribute **(__cdecl *)(const cEventAttribute **, const cEventAttribute *,
                                                      const cEventAttribute *, uint32_t);
    cEventAttribute records[5] = {{3}, {5}, {3}, {0}, {9}};
    for (unsigned first = 0; first <= 5; ++first)
        for (unsigned last = first; last <= 5; ++last)
            for (uint32_t id : {0u, 3u, 5u, 9u, 7u}) {
                ++ctx.cases;
                const cEventAttribute *o = nullptr;
                ctx.Original<Find>(0x1a98af)(&o, records + first, records + last, id);
                const cEventAttribute *u = cGZEventReflector::FindRecord(records + first, records + last, id);
                if (o != u) ctx.Fail("FindRecord [%u, %u) id %u: orig %d ours %d", first, last, id, int(o - records),
                                     int(u - records));
            }

    using Acquire = cEventServiceRef *(__thiscall *)(cEventServiceRef *);
    WorldCells cells;
    for (uint32_t mode = 0; mode < 3; ++mode) {
        auto run = [&](bool ours) {
            return drawfx::Side([&] { BuildWorld(mode == 2 ? 1 : 2); }, [&] {
                if (mode == 0) *GameAddress<void *>(kFrameworkCell) = nullptr;
                cEventServiceRef *holder = static_cast<cEventServiceRef *>(::operator new(4));
                holder->mRef.mObject = reinterpret_cast<void *>(0x5a5a5a5a);
                drawfx::AddRange(holder, 4, kHolderTag);
                cEventServiceRef *r = ours ? cGZEventReflectorAdapters::AcquireEventService(holder, nullptr)
                                           : ctx.Original<Acquire>(0x1a991d)(holder);
                drawfx::Note("acquire", {r == holder ? 1u : 0u, drawfx::Norm(Word(holder->mRef.mObject))});
                ::operator delete(holder);
                NoteRefs();
                DestroyWorld();
            });
        };
        auto a = run(false);
        auto b = run(true);
        if (!drawfx::Same(ctx, a.first, b.first, "acquire", mode, a.second.empty() ? b.second : a.second)) return;
    }

    ++ctx.cases;
    using ClassIdFn = uint32_t(__cdecl *)();
    if (ctx.Original<ClassIdFn>(0x1a9e2c)() != cGZEventReflector::ClassId()) ctx.Fail("class id differs");

    // The range release 0x146670: the order of the Release calls (the fakes log them), null words skipped.
    using ReleaseRange = void(__thiscall *)(cIGZUnknown *const *);
    for (uint32_t mask = 0; mask < 8; ++mask) {
        auto run = [&](bool ours) {
            return drawfx::Side([] {}, [&] {
                drawfx::Fake f[3];
                cIGZUnknown *range[3];
                for (unsigned i = 0; i < 3; ++i) {
                    f[i].Reset<kService>(0x990 + i);
                    f[i].refs = 5;
                    range[i] = (mask >> i) & 1 ? reinterpret_cast<cIGZUnknown *>(&f[i]) : nullptr;
                }
                if (ours)
                    cGZEventReflectorAdapters::ReleaseIteratorRange(range, nullptr);
                else
                    ctx.Original<ReleaseRange>(0x146670)(range);
                drawfx::Note("refs", {uint32_t(f[0].refs), uint32_t(f[1].refs), uint32_t(f[2].refs)});
            });
        };
        auto a = run(false);
        auto b = run(true);
        if (!drawfx::Same(ctx, a.first, b.first, "release range", mask, a.second.empty() ? b.second : a.second)) return;
    }

    // The range constructor over original iterators (0x1a9942 on a counted id list): the counts it adds.
    using MakeIterator = cIAttributeIDIterator *(__thiscall *)(void *, cIGZUnknown *, const uint32_t *const *);
    using MakeRange = cAttributeIDRange *(__thiscall *)(cAttributeIDRange *, cIAttributeIDIterator *,
                                                        cIAttributeIDIterator *);
    for (uint32_t withEnd = 0; withEnd < 2; ++withEnd) {
        auto run = [&](bool ours) {
            return drawfx::Side([] {}, [&] {
                static const uint32_t ids[3] = {7, 8, 9};
                drawfx::Fake list;
                list.Reset<kService>(0x980);
                const uint32_t *cursor = ids;
                auto *begin = ctx.Original<MakeIterator>(0x1a9942)(::operator new(0x10), reinterpret_cast<cIGZUnknown *>(&list), &cursor);
                cursor = ids + 3;
                auto *end = withEnd ? ctx.Original<MakeIterator>(0x1a9942)(::operator new(0x10), reinterpret_cast<cIGZUnknown *>(&list), &cursor)
                                    : nullptr;
                begin->AddRef();
                if (end) end->AddRef();
                cAttributeIDRange range{};
                if (ours)
                    cGZEventReflectorAdapters::MakeIteratorRange(&range, nullptr, begin, end);
                else
                    ctx.Original<MakeRange>(0x1a98e0)(&range, begin, end);
                drawfx::Note("range", {range.mCurrent != begin && range.mCurrent ? 1u : 0u, range.mBegin == begin ? 1u : 0u,
                                       range.mEnd == end ? 1u : 0u, range.mCurrent->Get(), uint32_t(list.refs)});
                drawfx::Note("counts", {range.mCurrent->Release(), begin->Release(), end ? end->Release() : 9u});
                drawfx::Note("last", {begin->Release(), end ? end->Release() : 9u, uint32_t(list.refs)});
            });
        };
        auto a = run(false);
        auto b = run(true);
        if (!drawfx::Same(ctx, a.first, b.first, "range", withEnd, a.second.empty() ? b.second : a.second)) return;
    }
}

DIFF_TEST(GZEventReflector_VTables, 0xda22d4)
{
    for (const VSlot &v : kVSlots) {
        ++ctx.cases;
        void *const *table = GameAddress<void *const>(v.vtable);
        if (table[v.slot] != GameAddress<void>(v.rva))
            ctx.Fail("vtable 0x%x slot %u is not 0x%x", v.vtable, v.slot, v.rva);
    }
    // Our faces sit where the game's do.
    ++ctx.cases;
    cGZEventReflector *r = nullptr;
    alignas(8) static uint8_t storage[sizeof(cGZEventReflector)];
    r = reinterpret_cast<cGZEventReflector *>(storage);
    if (reinterpret_cast<uint8_t *>(static_cast<cIGZEventReflector *>(r)) - storage != 8 ||
        reinterpret_cast<uint8_t *>(static_cast<cIAbstractBehavior *>(r)) != storage)
        ctx.Fail("face offsets differ from the game's (+0, +8)");
}
