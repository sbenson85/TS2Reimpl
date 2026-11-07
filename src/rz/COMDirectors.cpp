#include "rz/RZMessageServer.h"
#include "rz/COMDirectors.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersA.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "rz/cTSIniFile.h"
#include "gz/cGZRealtimeDebugDisplay.h"
#include "gz/cIGZPropertySet.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cGZAllocatorService.h"
#include "sg/cTSSGSystem.h"
#include "ts/cTSPersistSystem.h"
#include "ts/cTSSimSystem.h"

#include <intrin.h>

#include <cstdlib>
#include <new>

// Library functions of the thread-safe statics protocol (our toolchain's copies, as the original
// code uses its own). The guard is a plain int.
extern "C" void __cdecl _Init_thread_header(int *target) noexcept;
extern "C" void __cdecl _Init_thread_footer(int *target) noexcept;
extern "C" void __cdecl _Init_thread_abort(int *target) noexcept;
extern "C" __declspec(thread) int _Init_thread_epoch; // our CRT's statics epoch, not the image's TLS block (docs/standalone-shape.md, "Owned statics")

namespace comdir {

namespace {
template <typename Fn> Fn Slot(const void *object, int slot) { return VirtualSlot<Fn>(object, slot); }
using Slot0Args = uint32_t(__thiscall *)(void *);
} // namespace

// ---------------------------------------------------------------------------------------------
// Constructors' helpers

bool SimProbe()
{
    // 0x416530 is an SEH-guarded probe: `if (word at 0x10118ac == 0) call [0x10118b4]`, returning 1
    // normally and 0 when anything is raised. The word holds 6 in the image. The pointer at
    // 0x10118b4 is 0x415a90, which tests the same word and calls the pointer again, so a zero word
    // recurses until the stack overflows and the handler returns 0.
    return *GameAddress<int>(0x10118ac) != 0;
}

void AddTSSGHelper(rz::cRZCOMDllDirector *director)
{
    // 0x140fe3.
    director->AddDirector(GetDirector(kDir_TSSG));
}

void AddEngineHelper(rz::cRZCOMDllDirector *director)
{
    // 0x140ff3: the engine directors, in this order.
    static constexpr unsigned kOrder[] = {kDir_Event, kDir_CompGeom, kDir_Graphics, kDir_Draw2D, kDir_TSUI,
                                          kDir_TSPersist, kDir_TSSim, kDir_PathPlan, kDir_Expat, kDir_TSWeb};
    for (unsigned index : kOrder) director->AddDirector(GetDirector(index));
}

void StartEngineDirectors(cIGZCOM *com)
{
    // 0x14107c: the engine directors' OnStart with the application director's argument, in this order (TSSG is in the
    // list, unlike AddEngineHelper's, and Draw2D starts Font and Draw itself).
    static constexpr unsigned kOrder[] = {kDir_Event, kDir_CompGeom, kDir_Graphics, kDir_Draw2D, kDir_TSSG, kDir_TSUI,
                                          kDir_TSPersist, kDir_TSSim, kDir_PathPlan, kDir_Expat, kDir_TSWeb};
    for (unsigned index : kOrder) GetDirector(index)->OnStart(com);
}

// ---------------------------------------------------------------------------------------------
// OnStart of the service-registering directors

namespace {

constexpr uint32_t kSystemServiceIid = 0x287fb697;

// The COM object's slot 3: creates the service object of class `clsid` for interface `iid` (the result in al).
bool CreateService(void *com, uint32_t clsid, uint32_t iid, void **out)
{
    return Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(com, 3)(com, clsid, iid, out);
}

// GetServiceObject()'s slot 3: AddSystemService(object).
void AddService(void *service, void *object)
{
    Slot<void(__thiscall *)(void *, void *)>(service, 3)(service, object);
}

void AddService(void *object) { AddService(GetServiceObject(), object); }

void ReleaseObject(void *object)
{
    Slot<Slot0Args>(object, 2)(object);
}

// slot 7 of the service object: AddHook(the director's hooks face).
void AddHooks(void *service, cIGZFrameWorkHooks *hooks)
{
    Slot<void(__thiscall *)(void *, void *)>(service, 7)(service, hooks);
}

// A local reference in a frame that releases it when an exception escapes (the original's unwind
// action is the unguarded release of RZAutoRef, which terminates if Release throws). Release() is the
// explicit release of the normal path, which the original runs in a noexcept region (the unwind map's
// action for that state is terminate()): a Release that throws ends the process there too.
struct LocalRef {
    void *p = nullptr;
    LocalRef() = default;
    LocalRef(const LocalRef &) = delete;
    LocalRef &operator=(const LocalRef &) = delete;
    ~LocalRef() noexcept
    {
        if (p) ReleaseObject(p);
    }
    void Release() noexcept
    {
        void *object = p;
        p = nullptr;
        if (object) ReleaseObject(object);
    }
};

// The shape of Event and Draw: the COM object creates the service, which is added even when nothing was created.
bool AddAlways(void *com, uint32_t clsid)
{
    void *out = nullptr;
    CreateService(com, clsid, kSystemServiceIid, &out);
    AddService(out);
    if (out) ReleaseObject(out);
    return true;
}

bool AddIfCreated(void *com, uint32_t clsid)
{
    void *out = nullptr;
    CreateService(com, clsid, kSystemServiceIid, &out);
    if (out) {
        AddService(out);
        ReleaseObject(out);
    }
    return true;
}

// SceneDBAnim, SceneGraph, TSWeb: one local reference, added even when nothing was created.
bool AddAlwaysFramed(void *com, uint32_t clsid)
{
    LocalRef out;
    CreateService(com, clsid, kSystemServiceIid, &out.p);
    AddService(out.p);
    out.Release();
    return true;
}

} // namespace

} // namespace comdir

namespace rz {

using namespace comdir;

template <> bool cCOMDirectorT<kDir_Event>::OnStart(cIGZCOM *com)
{
    // 0x1a83f5: the argument creates the service.
    return AddAlways(com, 0x3b1e8967);
}

template <> bool cCOMDirectorT<kDir_Draw>::OnStart(cIGZCOM *com)
{
    // 0x8b8030.
    return AddAlways(com, 0xab2ea3aa);
}

template <> bool cCOMDirectorT<kDir_Font>::OnStart(cIGZCOM *)
{
    // 0x90a048: the director's own COM object (slot 12).
    return AddIfCreated(GZCOM(), 0x20673960);
}

template <> bool cCOMDirectorT<kDir_UIScript>::OnStart(cIGZCOM *)
{
    // 0x85b355.
    return AddIfCreated(GZCOM(), 0xfa3562fb);
}

template <> bool cCOMDirectorT<kDir_RZMessage>::OnStart(cIGZCOM *com)
{
    // 0xc6ec41: the service object is fetched first.
    void *service = GetServiceObject();
    void *out = nullptr;
    CreateService(com, 0x252294e9, kSystemServiceIid, &out);
    if (out) {
        AddService(service, out);
        if (out) ReleaseObject(out);
    }
    return true;
}

template <> bool cCOMDirectorT<kDir_ResMan>::OnStart(cIGZCOM *com)
{
    // 0x9b33b0: added with slot 3 and then slot 11 of the service object (fetched again each time).
    void *out = nullptr;
    CreateService(com, 0x656b8f27, kSystemServiceIid, &out);
    if (out) {
        AddService(out);
        void *service = GetServiceObject();
        Slot<void(__thiscall *)(void *, void *)>(service, 11)(service, out);
        if (out) ReleaseObject(out);
    }
    return true;
}

template <> bool cCOMDirectorT<kDir_SceneDBAnim>::OnStart(cIGZCOM *)
{
    // 0x9d11d0.
    return AddAlwaysFramed(GZCOM(), 0x5afeed72);
}

template <> bool cCOMDirectorT<kDir_SceneGraph>::OnStart(cIGZCOM *)
{
    // 0xa34210.
    return AddAlwaysFramed(GZCOM(), 0x65074b5e);
}

template <> bool cCOMDirectorT<kDir_TSWeb>::OnStart(cIGZCOM *)
{
    // 0x1cb910.
    return AddAlwaysFramed(GZCOM(), 0x8bfc2f5c);
}

template <> bool cCOMDirectorT<kDir_SndEa>::OnStart(cIGZCOM *)
{
    // 0xb12c5f: the hooks, then one service; the result tells whether it was created and added.
    AddHooks(GetServiceObject(), this);
    void *out = nullptr;
    void *com = GZCOM();
    bool created = CreateService(com, 0x499b183a, kSystemServiceIid, &out);
    bool result = false;
    if (created && out) {
        AddService(out);
        result = true;
    }
    if (out) ReleaseObject(out);
    return result;
}

template <> bool cCOMDirectorT<kDir_Win>::OnStart(cIGZCOM *com)
{
    // 0x92f8da: the argument creates; the service object of the first call is used for both.
    void *service = GetServiceObject();
    AddHooks(service, this);
    void *out = nullptr;
    CreateService(com, 0x84174448, kSystemServiceIid, &out);
    if (out) {
        AddService(service, out);
        ReleaseObject(out);
        out = nullptr;
    }
    CreateService(com, 0xc2c2eb0f, kSystemServiceIid, &out);
    if (out) {
        AddService(service, out);
        ReleaseObject(out);
    }
    return true;
}

template <> bool cCOMDirectorT<kDir_HitMan>::OnStart(cIGZCOM *)
{
    // 0xb2a422.
    AddHooks(GetServiceObject(), this);
    void *out = nullptr;
    CreateService(GZCOM(), 0x7b1bd993, kSystemServiceIid, &out);
    if (out) {
        AddService(out);
        ReleaseObject(out);
        out = nullptr;
    }
    CreateService(GZCOM(), 0x3bc9e6e0, kSystemServiceIid, &out);
    if (out) {
        AddService(out);
        ReleaseObject(out);
    }
    return true;
}

template <> bool cCOMDirectorT<kDir_Sound>::OnStart(cIGZCOM *)
{
    // 0xb22420. The first object is released only when the second is requested.
    AddHooks(GetServiceObject(), this);
    void *out = nullptr;
    CreateService(GZCOM(), 0xe41ce9fd, kSystemServiceIid, &out);
    if (out) AddService(out);
    void *com = GZCOM();
    if (out) {
        void *old = out;
        out = nullptr;
        ReleaseObject(old);
    }
    CreateService(com, 0x7af85b98, kSystemServiceIid, &out);
    if (out) {
        AddService(out);
        if (out) ReleaseObject(out);
    }
    return true;
}

template <> bool cCOMDirectorT<kDir_Graphics>::OnStart(cIGZCOM *)
{
    // 0x154460: two services, each from a fresh slot 12 call; released in reverse order.
    LocalRef first;
    CreateService(GZCOM(), 0xbb637286, kSystemServiceIid, &first.p);
    AddService(first.p);
    LocalRef second;
    CreateService(GZCOM(), 0x855227cd, kSystemServiceIid, &second.p);
    AddService(second.p);
    second.Release();
    first.Release();
    return true;
}

template <> bool cCOMDirectorT<kDir_Draw2D>::OnStart(cIGZCOM *com)
{
    // 0x145f20: three services (the third with interface 0x4c9282f2), then the Font and Draw directors are started
    // with the same argument; the references are released in reverse order.
    LocalRef first;
    CreateService(GZCOM(), 0x20732180, kSystemServiceIid, &first.p);
    AddService(first.p);
    LocalRef second;
    CreateService(GZCOM(), 0x445fabbb, kSystemServiceIid, &second.p);
    AddService(second.p);
    LocalRef third;
    CreateService(GZCOM(), 0x4c9282f8, 0x4c9282f2, &third.p);
    AddService(third.p);
    GetDirector(kDir_Font)->OnStart(com);
    GetDirector(kDir_Draw)->OnStart(com);
    third.Release();
    second.Release();
    first.Release();
    return true;
}

// ---------------------------------------------------------------------------------------------
// OnStart of the directors that build their own services (handoff D2, lane ET)

namespace {

void AddRefObject(void *object) { Slot<Slot0Args>(object, 1)(object); }

// Slot 0 of a created object: QueryInterface(iid, out) (the result is not read by the callers).
void QueryObject(void *object, uint32_t iid, void **out)
{
    Slot<bool(__thiscall *)(void *, uint32_t, void **)>(object, 0)(object, iid, out);
}

// `new T` of a class that is still the game's: operator new, then its original constructor `construct` (when the memory
// is not null). The original's frame frees the memory (sized delete) when the constructor throws.
using CtorFn = void *(__thiscall *)(void *);
template <typename Construct> void *NewGameObject(size_t size, Construct construct)
{
    void *memory = ::operator new(size);
    if (!memory) return nullptr;
    try {
        return construct(memory);
    } catch (...) {
        ::operator delete(memory, size);
        throw;
    }
}

// A face of a new object, null for a null object (the original's neg/sbb/and).
void *FaceOf(void *object, uint32_t offset) { return object ? static_cast<uint8_t *>(object) + offset : nullptr; }

// One service through a class object: the director's COM object creates it (clsid, iid), and when that succeeds the
// class object's QueryInterface gives the system-service face, which is added (null or not) and released. The class
// object is held by the caller's frame (`factory`) until it returns.
void AddThroughFactory(void *com, uint32_t clsid, uint32_t iid, LocalRef &factory)
{
    if (CreateService(com, clsid, iid, &factory.p)) {
        LocalRef service;
        QueryObject(factory.p, kSystemServiceIid, &service.p);
        AddService(service.p);
        service.Release();
    }
}

// The service object's slot 5, GetSystemService(id, iid, out), into a fresh reference (null when there is no service
// object): the 37-byte getters 0x20db8e, 0x20db69, 0x212e8c, 0x22b0b2, 0xb29d53, each `ref = 0; if (service)
// service->slot5(id, iid, &ref)` for its own pair.
void *QueryService(uint32_t id, uint32_t iid)
{
    void *out = nullptr;
    if (void *service = GetServiceObject()) Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(service, 5)(service, id, iid, &out);
    return out;
}

} // namespace

template <> bool cCOMDirectorT<kDir_TSPersist>::OnStart(cIGZCOM *)
{
    // 0x1d3e20: the persist system (0x268 bytes, our constructor 0x1d8b90 called through its adapter, which the difftest
    // replaces with its fake constructor), referenced; its +4 face (null for none) is added as a service and passed to the
    // service object's slot 11; then its slot 56 is called (no null check) and the reference released.
    void *system = NewGameObject(0x268, [](void *memory) -> void * { return cTSPersistSystemAdapters::Construct(memory, nullptr); });
    if (system) AddRefObject(system);
    LocalRef held;
    held.p = system;
    void *face = FaceOf(system, 4);
    AddService(face);
    void *service = GetServiceObject();
    Slot<void(__thiscall *)(void *, void *)>(service, 11)(service, face);
    Slot<Slot0Args>(system, 56)(system);
    held.Release();
    return true;
}

template <> bool cCOMDirectorT<kDir_TSSG>::OnStart(cIGZCOM *com)
{
    // 0x2454c0: the SceneGraph and SceneDBAnim directors are started with the argument; then the scene-graph system
    // (0x3b8 bytes, our constructor of 0x246110 through its adapter) is referenced through its +0xc face and added when
    // there is one; then one service from this director's COM object, added when the creation succeeds.
    GetDirector(kDir_SceneGraph)->OnStart(com);
    GetDirector(kDir_SceneDBAnim)->OnStart(com);
    void *face = FaceOf(NewGameObject(0x3b8, [](void *memory) -> void * { return nTSSG::cTSSGSystemAdapters::Construct(memory, nullptr); }), 0xc);
    if (face) AddRefObject(face);
    LocalRef held;
    held.p = face;
    if (face) AddService(face);
    LocalRef out;
    if (CreateService(GZCOM(), 0xec59ab08, kSystemServiceIid, &out.p)) AddService(out.p);
    out.Release();
    held.Release();
    return true;
}

template <> bool cCOMDirectorT<kDir_TSSim>::OnStart(cIGZCOM *)
{
    // 0x416370: the sim system (0x170 bytes, constructor 0x416810), referenced and added (null or not); then one service
    // through a class object (0x2cf22aa2 for 0x2cf22a9b).
    void *system = NewGameObject(0x170, [](void *memory) -> void * { return cTSSimSystemAdapters::Construct(memory, nullptr); });
    if (system) AddRefObject(system);
    LocalRef held;
    held.p = system;
    AddService(system);
    LocalRef factory;
    AddThroughFactory(GZCOM(), 0x2cf22aa2, 0x2cf22a9b, factory);
    factory.Release();
    held.Release();
    return true;
}

template <> bool cCOMDirectorT<kDir_TSCommon>::OnStart(cIGZCOM *)
{
    // 0xcb5110: four services from this director's COM object, each added when its creation succeeds; the references
    // are held to the end and released in reverse order.
    LocalRef first;
    if (CreateService(GZCOM(), 0x4af76d96, kSystemServiceIid, &first.p)) AddService(first.p);
    LocalRef second;
    if (CreateService(GZCOM(), 0x2a490dda, kSystemServiceIid, &second.p)) AddService(second.p);
    LocalRef third;
    if (CreateService(GZCOM(), 0x4c8cc5b8, kSystemServiceIid, &third.p)) AddService(third.p);
    LocalRef fourth;
    if (CreateService(GZCOM(), 0xcdb467b8, kSystemServiceIid, &fourth.p)) AddService(fourth.p);
    fourth.Release();
    third.Release();
    second.Release();
    first.Release();
    return true;
}

template <> bool cCOMDirectorT<kDir_TSCAS>::OnStart(cIGZCOM *)
{
    // 0xcb54a0: three services through class objects, held to the end and released in reverse order.
    LocalRef first;
    AddThroughFactory(GZCOM(), 0xeb7d6500, 0xcb7d64f8, first);
    LocalRef second;
    AddThroughFactory(GZCOM(), 0x2c4f295b, 0x2c4f2954, second);
    LocalRef third;
    AddThroughFactory(GZCOM(), 0x0bcf1b60, 0x0bcf1b58, third);
    third.Release();
    second.Release();
    first.Release();
    return true;
}

template <> bool cCOMDirectorT<kDir_TSAppFW>::OnStart(cIGZCOM *com)
{
    // 0xc2beec. The service object is fetched once for most of the adds. One reference (`out`) is reused: the argument
    // creates three services, each added when one came back (the first two released at once, the third held); the
    // engine directors, TSCommon, TSCAS and Lua5 are started; one service from this director's COM object is added
    // (null or not) and released; the MadCow director is started with this director's COM object; the realtime debug
    // display (0x34 bytes, constructor 0x9704bb: ours) is referenced and added; the third service is released, and two more
    // come from the argument (the last one held); then the display and the last reference are released.
    void *service = GetServiceObject();
    LocalRef out;
    CreateService(com, 0xa3024279, kSystemServiceIid, &out.p);
    if (out.p) {
        AddService(service, out.p);
        out.Release();
    }
    CreateService(com, 0x04063236, kSystemServiceIid, &out.p);
    if (out.p) {
        AddService(service, out.p);
        out.Release();
    }
    CreateService(com, 0x252294e9, kSystemServiceIid, &out.p);
    if (out.p) AddService(service, out.p);
    StartEngineDirectors(com);
    GetDirector(kDir_TSCommon)->OnStart(com);
    GetDirector(kDir_TSCAS)->OnStart(com);
    GetDirector(kDir_Lua5)->OnStart(com);
    {
        LocalRef own;
        CreateService(GZCOM(), 0x8c6965a7, kSystemServiceIid, &own.p);
        AddService(own.p);
        own.Release();
    }
    rz::cRZCOMDllDirector *madCow = GetDirector(kDir_MadCow);
    cIGZCOM *ownCom = GZCOM();
    madCow->OnStart(ownCom);
    void *display = NewGameObject(sizeof(cGZRealtimeDebugDisplay), [](void *memory) -> void * { return cGZRealtimeDebugDisplayAdapters::Construct(memory, nullptr); });
    if (display) AddRefObject(display);
    LocalRef held;
    held.p = display;
    if (display) AddService(service, display);
    out.Release();
    CreateService(com, 0x51814215, kSystemServiceIid, &out.p);
    if (out.p) {
        AddService(service, out.p);
        out.Release();
    }
    CreateService(com, 0x7181c501, kSystemServiceIid, &out.p);
    if (out.p) AddService(service, out.p);
    held.Release();
    out.Release();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Hooks-face overrides (handoff D2, lane ET). None of them has an EH frame: an exception leaves the local references
// unreleased, as in the original (plain holders, released explicitly).

template <> bool cCOMDirectorT<kDir_Sound>::PostAppInit()
{
    // 0xb224d9: the sound service (0x23cecba0, 0x106077d2); without one the result is false. Otherwise its slot 43 gives
    // an object, referenced, sent slot 3 (0xcc3014ee, no null check), released.
    ScopedRef sound{QueryService(0x23cecba0, 0x106077d2)};
    bool result = false;
    if (sound.mObject) {
        void *object = Slot<void *(__thiscall *)(void *)>(sound.mObject, 43)(sound.mObject);
        if (object) AddRefObject(object);
        Slot<void(__thiscall *)(void *, uint32_t)>(object, 3)(object, 0xcc3014ee);
        result = true;
        if (object) ReleaseObject(object);
    }
    sound.ReleaseObject();
    return result;
}

template <> bool cCOMDirectorT<kDir_Sound>::PreAppShutdown()
{
    // 0xb2252e: the cached sound service (0x9c5b7e, still the game's), when there is one: its slot 34.
    using CachedFn = void *(__cdecl *)();
    if (OriginalFunction<CachedFn, 0x9c5b7e>()()) {
        void *sound = OriginalFunction<CachedFn, 0x9c5b7e>()();
        Slot<Slot0Args>(sound, 34)(sound);
    }
    return true;
}

template <> bool cCOMDirectorT<kDir_HitMan>::PreAppInit()
{
    // 0xb2a4c3: four registrations on the service (0x56b906e, 0x656b8efc) through its slot 20 (no null check), then the
    // object the framework's COM object creates (0x2b2eb869 for 0x3af9866f; null or not) is passed to slot 15 of the
    // service (0x1af85bfb, 0x3af85bb7) and released. The first holder is released as RZAutoRef (exceptions swallowed),
    // the second as ScopedRef.
    RZAutoRef registry{QueryService(0x56b906e, 0x656b8efc)};
    using Register = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    for (uint32_t id : {0xdb9a4f83u, 0xa3cd96cfu, 0x7b1acfcdu, 0xebfee345u}) Slot<Register>(registry.mObject, 20)(registry.mObject, 0x5b945038, id, 0);
    ScopedRef target{QueryService(0x1af85bfb, 0x3af85bb7)};
    void *created = nullptr;
    CreateService(GetDirectorFrameworkObject(), 0x2b2eb869, 0x3af9866f, &created);
    Slot<void(__thiscall *)(void *, void *)>(target.mObject, 15)(target.mObject, created);
    if (created) ReleaseObject(created);
    target.ReleaseObject();
    registry.ReleaseObject();
    return true;
}

template <> bool cCOMDirectorT<kDir_HitMan>::PreAppShutdown()
{
    // 0xb2a568: slot 4 of the service (0x1b2ce671, 0xbb1bd9a8) and slot 3 of the service (0xfbc9cd04, 0x7bba0cbd), each
    // when present; the second holder released with the /EHa catch (0x1be5d1), the first as ScopedRef.
    ScopedRef first{QueryService(0x1b2ce671, 0xbb1bd9a8)};
    if (first.mObject) Slot<Slot0Args>(first.mObject, 4)(first.mObject);
    AllocatorServiceHolder second{QueryService(0xfbc9cd04, 0x7bba0cbd)};
    if (second.mService) Slot<Slot0Args>(second.mService, 3)(second.mService);
    second.Release();
    first.ReleaseObject();
    return true;
}

// TSUI's OnStart and PostAppInit (lane FB).
namespace {

// 0x5b7b88 (__thiscall, ret 0xc, EH frame; `this` and the third argument, the service's name, are not read): the COM
// object creates `clsid` for the system-service interface and, when it says yes, the object (null or not) is added as
// a system service (the service object's slot 3). The reference is released (also on unwind). Not hooked (a local
// helper of TSUI's OnStart).
void AddCreatedService(cIGZCOM *com, uint32_t clsid)
{
    LocalRef made;
    if (CreateService(com, clsid, kSystemServiceIid, &made.p)) {
        void *service = GetServiceObject();
        Slot<uint32_t(__thiscall *)(void *, void *)>(service, 3)(service, made.p);
    }
    made.Release();
}

constexpr uint32_t kXBreakName = 0xdd88d8; // "XBreak"

// A cRZString in the frame: built on raw memory by our cRZString bodies and destroyed with the frame.
struct FrameString {
    alignas(cRZString) unsigned char mBytes[sizeof(cRZString)];
    cRZString *Get() { return reinterpret_cast<cRZString *>(mBytes); }
    FrameString() { Get()->Construct(); }
    explicit FrameString(const char *text) { Get()->Construct(text); }
    FrameString(const FrameString &) = delete;
    FrameString &operator=(const FrameString &) = delete;
    ~FrameString() { Get()->Destruct(); }
};

// The XBreak scan: the framework's slot 21 object reads option "XBreak" (slot 8: key, out, 1) and, when it has one,
// its comma-separated values are read (NextToken) until one reads as `wanted` (atoi, 0x830972:
// it parses bytes). True when one did.
bool XBreakListed(int wanted)
{
    void *service = GetServiceObject();
    void *options = Slot<void *(__thiscall *)(void *)>(service, 21)(service);
    FrameString value;
    bool found;
    {
        FrameString key(GameAddress<char>(kXBreakName));
        found = static_cast<uint8_t>(Slot<uint32_t(__thiscall *)(void *, void *, void *, uint32_t)>(options, 8)(options, key.Get(), value.Get(), 1)) != 0;
    }
    if (!found) return false;
    FrameString token;
    while (reinterpret_cast<cRZStringTokenizer *>(value.Get())->NextToken(',', token.Get()))
        if (std::atoi(token.Get()->mString.c_str()) == wanted) return true;
    return false;
}

// The dispatchers TSUI's OnStart asks (cdecl, two arguments; each calls a hook pointer of the game's or its default).
using DispatchFn = int(__cdecl *)(int, int);

// The tool classes OnStart removes from this director's class map (the ids their 6-byte getters return: 0x5e0064,
// 0x63983f, 0x691f3f cTSUserToolWall, 0x6865f5 cTSUserToolBulldozeLot, 0x6abfc1 cTSUserToolPlaceNewLot).
constexpr uint32_t kFirstTools[] = {0x6bb9232a, 0x27d98e43, 0xa9e3b32b};
constexpr uint32_t kLotTools[] = {0x29638d09, 0x4be71e86};

} // namespace

template <> bool cCOMDirectorT<kDir_TSUI>::OnStart(cIGZCOM *com)
{
    // 0x5b72e7. The hooks face is added to the service object; WinCOM, Script, UIScript and XmlUtils start with this
    // director's COM object (+0x30); five system services are created with the argument (0x5b7b88). Then two XBreak
    // checks: the dispatcher 0x5b696a(0x1cf, 0x1cf) is asked first, and value 7 runs 0x5b6aca(0, 0) and
    // 0x5b6912(0xaf111d00, twice); then 0x5b6a72(-22, -22), and value 8 runs 0x5b6b22(0, 0) and 0x5b6a1a(0x352d2ff1, twice).
    // Last, unless 0x5b68ba(first, first) gives 0x1cf back the first three tool classes leave the class map, and unless
    // 0x5b69c2(second, second) gives -22 back the two lot tools do. Returns true.
    AddHooks(GetServiceObject(), this);
    for (unsigned index : {kDir_Win, kDir_Script, kDir_UIScript, kDir_XmlUtils}) GetDirector(index)->OnStart(mCOM);
    for (uint32_t clsid : {0x77aa34e6u, 0x7834ba24u, 0xebd09b02u, 0xac0874d8u, 0x8b0c79d6u}) AddCreatedService(com, clsid);
    int first = OriginalFunction<DispatchFn, 0x5b696a>()(0x1cf, 0x1cf);
    if (XBreakListed(7)) {
        OriginalFunction<DispatchFn, 0x5b6aca>()(0, 0);
        OriginalFunction<DispatchFn, 0x5b6912>()(static_cast<int>(0xaf111d00), static_cast<int>(0xaf111d00));
    }
    int second = OriginalFunction<DispatchFn, 0x5b6a72>()(-0x16, -0x16);
    if (XBreakListed(8)) {
        OriginalFunction<DispatchFn, 0x5b6b22>()(0, 0);
        OriginalFunction<DispatchFn, 0x5b6a1a>()(0x352d2ff1, 0x352d2ff1);
    }
    // ORIGINAL-BUG: the original erases what find gives without testing it, so an absent class would erase end().
    if (OriginalFunction<DispatchFn, 0x5b68ba>()(first, first) != 0x1cf)
        for (uint32_t clsid : kFirstTools) mClasses.erase(mClasses.find(clsid));
    if (OriginalFunction<DispatchFn, 0x5b69c2>()(second, second) != -0x16)
        for (uint32_t clsid : kLotTools) mClasses.erase(mClasses.find(clsid));
    return true;
}

template <> bool cCOMDirectorT<kDir_TSUI>::PreFrameWorkInit()
{
    // 0x5b7722. With the window service (0xc2c2eb0f, 0x22c2eb1f): this director's COM object creates 0x6786ddc5 (iid
    // 0x2786ddc5; the result not tested), its QueryInterface (0xa2ed067f, through 0x5b4484; no null check) gives an
    // object passed to the window service's slot 24 when non-null; both released. Then the 25 ids are sent to slot 3 of
    // the service (0x17aa3541, 0x47aa34da) when present, 0x5b7a9c runs (still the game's), and the two holders are
    // released (ScopedRef; the frame releases them too when an exception escapes).
    LocalRef window;
    window.p = QueryService(0xc2c2eb0f, 0x22c2eb1f);
    if (window.p) {
        LocalRef made;
        CreateService(GZCOM(), 0x6786ddc5, 0x2786ddc5, &made.p);
        LocalRef face;
        QueryObject(made.p, 0xa2ed067f, &face.p);
        if (face.p) Slot<void(__thiscall *)(void *, void *)>(window.p, 24)(window.p, face.p);
        face.Release();
        made.Release();
    }
    LocalRef ids;
    ids.p = QueryService(0x17aa3541, 0x47aa34da);
    if (ids.p) {
        static constexpr uint32_t kIds[] = {0x380bff24, 0x2ca3dcdb, 0x17d98e3f, 0x27d98e43, 0x8c75943d, 0x77d98e46, 0x715335af,
                                            0x57aa295e, 0xcb983199, 0x17f5752a, 0x57d98e37, 0x77e17d08, 0xf155d4c0, 0x0c4484b2,
                                            0x47e17d13, 0x37e17d03, 0x47e17d0e, 0x37d5d3d7, 0x0b93f5c6, 0x6b943be0, 0x0ba6a89f,
                                            0xebaffbbd, 0x4c8004f5, 0x0d0cf3f7, 0xb534225f};
        for (uint32_t id : kIds) Slot<void(__thiscall *)(void *, uint32_t)>(ids.p, 3)(ids.p, id);
    }
    OriginalFunction<void(__cdecl *)(), 0x5b7a9c>()();
    ids.Release();
    window.Release();
    return true;
}
namespace {
constexpr uint32_t kSettingsService = 0x0ac1887e; // the settings service (QueryService pair of 0x1cbaf3)
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr uint32_t kTSUISettingName = 0xdd87cc;   // the setting PostAppInit reads (a C string of the game's)
constexpr uint32_t kTSUISettingMessage = 0xac646794;
constexpr uint32_t kAnyBooleanVTable = 0xd9c670;  // cGZAny<bool> (AnyBoolean)

// The AnyBoolean in the frame (the original resets its vtable to the base's, 0xd9c654, when it goes; nothing reads it).
struct BooleanValue {
    void *mVTable;
    uint8_t mValue;
    uint8_t mPad[3];
};
} // namespace

template <> bool cCOMDirectorT<kDir_TSUI>::PostAppInit()
{
    // 0x5b763c. The settings service (0x1cbaf3: 0x0ac1887e, 0x4bb0d269) when present reads a boolean setting (slot 9, the
    // name as a service-allocated string) into an AnyBoolean (false otherwise); a standard message (0x2c bytes from
    // RZMemory::Malloc, built as 0x1c033d does; referenced when the allocation worked, then used untested) gets the id
    // 0xac646794 (slot 5) and the value (slot 11) and is posted to the message server (0x9c5a89, still the game's; slot 4,
    // with 0). The message and the service are released; the frame releases them too when an exception escapes.
    LocalRef settings;
    settings.p = QueryService(kSettingsService, kSettingsIid);
    BooleanValue value{GameAddress<void>(kAnyBooleanVTable), 0, {}};
    if (settings.p) {
        GZServiceString name(GameAddress<char>(kTSUISettingName));
        Slot<void(__thiscall *)(void *, void *, void *)>(settings.p, 9)(settings.p, &name, &value);
    }
    auto *raw = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (raw) {
        raw->mVTable = &ts2_rva_da4064;
        raw->mRefCount = 0;
        raw->mFlags = 0;
        raw->mId = 0;
        for (uint32_t &v : raw->mValue) v = 0;
        raw->mPtr20 = raw->mPtr24 = raw->mPtr28 = nullptr;
    }
    LocalRef message;
    message.p = raw;
    if (message.p) Slot<Slot0Args>(message.p, 1)(message.p);
    Slot<void(__thiscall *)(void *, uint32_t)>(message.p, 5)(message.p, kTSUISettingMessage);
    Slot<void(__thiscall *)(void *, uint32_t)>(message.p, 11)(message.p, value.mValue);
    void *server = GetCachedMessageServer();
    Slot<void(__thiscall *)(void *, void *, uint32_t)>(server, 4)(server, message.p, 0);
    message.Release();
    settings.Release();
    return true;
}

#define RZ_COMDIR_INSTANTIATE(N) template class cCOMDirectorT<N>;
COMDIR_FOR_EACH_DIRECTOR(RZ_COMDIR_INSTANTIATE)
#undef RZ_COMDIR_INSTANTIATE

} // namespace rz

namespace comdir {

// ---------------------------------------------------------------------------------------------
// Getters of the function-local statics

template <unsigned N> void DirectorFns<N>::Destroy()
{
    using Self = rz::cCOMDirectorT<N>;
    GameAddress<Self>(kDirectorSpecs[N].staticRva)->Self::~cCOMDirectorT();
}

template <unsigned N> rz::cRZCOMDllDirector *DirectorFns<N>::Get()
{
    // The thread-safe statics protocol, as GetCOMDirector: the guard is not above the thread's epoch (our
    // CRT's) once the static is built. The director is built in place: our class, our vtables.
    const DirectorSpec &spec = kDirectorSpecs[N];
    auto *object = GameAddress<rz::cCOMDirectorT<N>>(spec.staticRva);
    if (spec.flags & kStaticInit) return object;
    auto *guard = GameAddress<int>(spec.guardRva);
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            if (spec.flags & kGetterUnwinds) {
                // The getter's EH frame abandons the guard when the constructor throws.
                try {
                    ::new (object) rz::cCOMDirectorT<N>;
                } catch (...) {
                    _Init_thread_abort(guard);
                    throw;
                }
            } else {
                ::new (object) rz::cCOMDirectorT<N>;
            }
            // The original registers a stub that destroys the static; ours registers Destroy.
            gRegisterExitHandler(&DirectorFns<N>::Destroy);
            _Init_thread_footer(guard);
        }
    }
    return object;
}

#define COMDIR_INSTANTIATE(N) template struct DirectorFns<N>;
COMDIR_FOR_EACH_DIRECTOR(COMDIR_INSTANTIATE)
#undef COMDIR_INSTANTIATE

void InitMadCow()
{
    // 0x1408d0: the director (its four AddCls) built on the global, then atexit of its destructor (the original
    // registers the stub 0xd98a00).
    ::new (GameAddress<void>(kDirectorSpecs[kDir_MadCow].staticRva)) rz::cCOMDirectorT<kDir_MadCow>;
    gRegisterExitHandler(&DirectorFns<kDir_MadCow>::Destroy);
}

rz::cRZCOMDllDirector *GetDirector(unsigned index)
{
    static rz::cRZCOMDllDirector *(*const kGetters[kDirectorCount])() = {
#define COMDIR_GETTER(N) &DirectorFns<N>::Get,
        COMDIR_FOR_EACH_DIRECTOR(COMDIR_GETTER)
#undef COMDIR_GETTER
    };
    return kGetters[index]();
}

// ---------------------------------------------------------------------------------------------
// The audio director

namespace {

constexpr uint32_t kAudioServiceClass = 0x499b183a; // asked of the framework object by OnStart
constexpr uint32_t kAudioServiceIid = 0x899b182d;
constexpr uint32_t kAudioLogGlobal = 0xff5f10;    // a word the "No sound hardware" line passes to the log
constexpr uint32_t kSourceFileName = 0xda7e80;    // "C:\dev\sims2remaster\DL\TS\TSAudioD\source\TSAudioCOMDirector.cp(p)"
constexpr uint32_t kNoSoundHardwareText = 0xda7ec4; // "No sound hardware is present.\n"
constexpr uint32_t kBeginPreAppInitText = 0xda7ee4; // "TSAudio: Begin PreAppInit\n"
constexpr uint32_t kEndPreAppInitText = 0xda7f0c;   // "TSAudio: End PreAppInit\n"
constexpr uint32_t kSims2LoadText = 0xda7f00;       // "Sims2Load"

// A line for the log: a cRZStd built for (mode 8, source file, line), then its slot 2 called (cdecl,
// this first) with a channel word, a level and the text.
void WriteLog(uint32_t line, uint32_t channel, uint32_t level, uint32_t text)
{
    cRZStd log;
    cRZStd *object = log.Construct(8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<char>(kSourceFileName))), line);
    Slot<void(__cdecl *)(cRZStd *, uint32_t, uint32_t, const char *)>(object, 2)(object, channel, level, GameAddress<char>(text));
}

// The hooks face of a hook-shaped director (null for a null director, as the original).
cIGZFrameWorkHooks *HooksOf(void *director)
{
    return director ? reinterpret_cast<cIGZFrameWorkHooks *>(static_cast<uint8_t *>(director) + 4) : nullptr;
}

uint32_t Address(uint32_t rva) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<char>(rva))); }

// Release of a reference field: slot 2 when non-null (the original does not clear the field).
void ReleaseField(void *object)
{
    if (object) Slot<Slot0Args>(object, 2)(object);
}

} // namespace

AudioDirector *AudioDirector::Construct()
{
    cRZCOMDllDirector::Construct();
    // (the original stores a vtable at +0x64 first, 0xda49bc, and overwrites it before anything else runs)
    mVTable = GameAddress<void>(kVTable);
    mVTableAt4 = GameAddress<void>(kVTableAt4);
    mMessageTargetVTable = GameAddress<void>(kVTableAt64);
    for (void *&ref : mRefs) ref = nullptr;
    mField90 = mField94 = mField98 = mField9C = 0;
    bool idsBuilt = false;
    try {
        new (&mMessageIds) std::set<uint32_t>();
        idsBuilt = true;
        for (unsigned i = 0; i < kAudioClassCount; ++i) AddCls(kAudioClasses[i].clsid, GameAddress<void>(kAudioClasses[i].factoryRva));
        // Sound, HitMan, SndEa, Freshness (through the jump at 0xb0e1d0), HitMp3, Speex.
        AddDirector(GetDirector(kDir_Sound));
        AddDirector(GetDirector(kDir_HitMan));
        AddDirector(GetDirector(kDir_SndEa));
        AddDirector(GetDirector(kDir_Freshness));
        AddDirector(GetDirector(kDir_HitMp3));
        AddDirector(GetDirector(kDir_SpeexCodec));
        mMessageIds.insert(0xca);
        mMessageIds.insert(0xcb);
        mMessageIds.insert(0xeded3807);
    } catch (...) {
        // The constructor's unwind: the id set (once built), the ten references (all null here), then the base.
        if (idsBuilt) mMessageIds.~set();
        for (unsigned i = kRefCount; i-- > 0;) ReleaseField(mRefs[i]);
        cRZCOMDllDirector::Destruct();
        throw;
    }
    return this;
}

void AudioDirector::Destruct() noexcept
{
    mVTable = GameAddress<void>(kVTable);
    mVTableAt4 = GameAddress<void>(kVTableAt4);
    mMessageTargetVTable = GameAddress<void>(kVTableAt64);
    mMessageIds.~set();
    for (unsigned i = kRefCount; i-- > 0;) ReleaseField(mRefs[i]);
    cRZCOMDllDirector::Destruct();
}

AudioDirector *AudioDirector::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(AudioDirector));
    return this;
}

namespace {
void DestroyAudioDirector() { GameAddress<AudioDirector>(AudioDirector::kStaticRva)->Destruct(); }
} // namespace

AudioDirector *GetAudioDirector()
{
    // The thread-safe statics protocol, as DirectorFns<N>::Get; the getter's EH frame abandons the guard
    // when the constructor throws.
    auto *object = GameAddress<AudioDirector>(AudioDirector::kStaticRva);
    auto *guard = GameAddressOpaque<int>(AudioDirector::kGuardRva); // opaque: ICE with the constant form (game.h)
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                object->Construct();
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            // The original registers a stub that destroys the static; ours registers DestroyAudioDirector.
            gRegisterExitHandler(&DestroyAudioDirector);
            _Init_thread_footer(guard);
        }
    }
    return object;
}

bool AudioDirector::OnStart(void *framework)
{
    // 0x20f454. The framework object (slot 12) creates the audio service object (class 0x499b183a,
    // interface 0x899b182d); the service answers slot 5, and when it says yes the director's hooks are added
    // to the application's service object and the argument is passed to OnStart (slot 4) of Sound,
    // HitMan, SndEa, Freshness and HitMp3, in that order. Otherwise a line is logged. The reference is
    // released on the way out (an exception releases it too; a Release that throws ends the process).
    LocalRef service;
    CreateService(Slot<void *(__thiscall *)(void *)>(this, 12)(this), kAudioServiceClass, kAudioServiceIid, &service.p);
    bool started = false;
    if (service.p && static_cast<uint8_t>(Slot<Slot0Args>(service.p, 5)(service.p)) != 0) {
        AddHooks(GetServiceObject(), HooksOf(this));
        for (unsigned index : {kDir_Sound, kDir_HitMan, kDir_SndEa, kDir_Freshness, kDir_HitMp3}) {
            rz::cRZCOMDllDirector *director = GetDirector(index);
            Slot<bool(__thiscall *)(void *, void *)>(director, 4)(director, framework);
        }
        started = true;
    } else {
        WriteLog(0xbe, *GameAddress<uint32_t>(kAudioLogGlobal), 2, kNoSoundHardwareText);
    }
    service.Release();
    return started;
}

bool AudioDirectorHooks::PreAppInit()
{
    // 0x20f571. `this` is the +4 subobject; the message target is director + 0x64 (null for a null director).
    auto *self = reinterpret_cast<uint8_t *>(this);
    uint32_t target = self == reinterpret_cast<uint8_t *>(4) ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self + 0x60));
    reinterpret_cast<KeyMapSlot5Walker *>(self + 0x9c)->CallSlot5ForEachKey(target, nullptr);
    WriteLog(0xd3, Address(kSims2LoadText), 6, kBeginPreAppInitText);
    WriteLog(0xd4, Address(kSims2LoadText), 6, kEndPreAppInitText);
    return true;
}

bool AudioDirectorHooks::PostAppShutdown()
{
    // 0x20f555.
    auto *self = reinterpret_cast<uint8_t *>(this);
    uint32_t target = self == reinterpret_cast<uint8_t *>(4) ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self + 0x60));
    reinterpret_cast<KeyMapWalker *>(self + 0x9c)->CallSlot6ForEachKey(target, nullptr);
    return true;
}

bool AudioMessageTarget::QueryInterface(uint32_t iid, void **out)
{
    // 0x20f5f1: adjusts to the director and jumps to its QueryInterface (0x1ae298 through 0x20f5e4).
    return reinterpret_cast<COMDirector *>(reinterpret_cast<uint8_t *>(this) - 0x64)->AsShape()->rz::cRZCOMDllDirector::QueryInterface(iid, out);
}

uint32_t AudioMessageTarget::AddRef()
{
    // 0x20e7bf: adjusts and jumps to 0x145260 (through 0x20e7b2).
    return reinterpret_cast<COMDirector *>(reinterpret_cast<uint8_t *>(this) - 0x64)->AsShape()->rz::cRZCOMDllDirector::AddRef();
}

uint32_t AudioMessageTarget::Release()
{
    // 0x20f606: adjusts and jumps to 0x1ae2f0 (through 0x20f5f9).
    return reinterpret_cast<COMDirector *>(reinterpret_cast<uint8_t *>(this) - 0x64)->AsShape()->rz::cRZCOMDllDirector::Release();
}

// ---------------------------------------------------------------------------------------------
// The audio director's message handler (0x20e7c7)

namespace {

constexpr uint32_t kMessage2Iid = 0x65297976;      // cIGZMessage2, asked of the message
constexpr uint32_t kMsgAppInit = 0xca;             // the message types it was registered for (PreAppInit)
constexpr uint32_t kMsgAppShutdown = 0xcb;
constexpr uint32_t kMsgAudioReset = 0xeded3807;
constexpr uint32_t kStartedIid = 0x89e51472;       // asked of reference 1 (slot 0) before it is used
constexpr uint32_t kAudioEventCheatVTable = 0xda7e10;        // cTSAudioEventCheat (12 bytes)
constexpr uint32_t kAudioEventHandlerCheatVTable = 0xda7ddc; // cTSAudioEventHandlerCheat (12 bytes)

// The low byte of a bool slot's result (the original tests al).
bool Answer(void *object, int slot) { return static_cast<uint8_t>(Slot<Slot0Args>(object, slot)(object)) != 0; }

void CallSlot(void *object, int slot) { Slot<Slot0Args>(object, slot)(object); }

void AssignRef(void *&field, void *object) { reinterpret_cast<RZUnknownSlot *>(&field)->Assign(object); }

// The director's framework COM object (its slot 12, asked again for every service) creates class `clsid` for `iid`.
void CreateFromFramework(AudioDirector *director, uint32_t clsid, uint32_t iid, LocalRef &out)
{
    void *com = Slot<void *(__thiscall *)(void *)>(director, 12)(director);
    out.Release(); // (out is still empty: the holder's out-parameter reset)
    CreateService(com, clsid, iid, &out.p);
}

void LogFailure(uint32_t line, uint32_t text) { WriteLog(line, *GameAddress<uint32_t>(kAudioLogGlobal), 2, text); }

// One service of the init message: created, then kept in `field` when it answers `checkSlot`. `logWhenNull`: a missing
// object logs too (only the first service does that); text 0 logs nothing.
void KeepService(AudioDirector *director, LocalRef &holder, uint32_t clsid, uint32_t iid, void *&field, uint32_t line, uint32_t text, bool logWhenNull)
{
    CreateFromFramework(director, clsid, iid, holder);
    if (holder.p && Answer(holder.p, 3))
        AssignRef(field, holder.p);
    else if (text && (holder.p || logWhenNull))
        LogFailure(line, text);
}

// A cheat command of the audio director (cITSCheatSystem::cCheatCommand: the game's vtable, then two zero words),
// handed to 0xb39640, which registers it (the original passes null when its allocation fails; ours throws instead).
struct AudioCheatCommand {
    void *mVTable;
    uint32_t mWord4;
    uint32_t mWord8;
};
static_assert(sizeof(AudioCheatCommand) == 0xc, "AudioCheatCommand layout");

void RegisterCheat(uint32_t vtableRva)
{
    auto *command = static_cast<AudioCheatCommand *>(::operator new(sizeof(AudioCheatCommand)));
    command->mWord4 = 0;
    command->mWord8 = 0;
    command->mVTable = GameAddress<void>(vtableRva);
    OriginalFunction<void(__cdecl *)(void *), 0xb39640>()(command);
}

// Message 0xca. The ten references in creation order (the holders are released in reverse at the end).
void StartAudioServices(AudioDirector *director)
{
    void **refs = director->mRefs;
    LocalRef r8, r0, r2, r3, r4, r6;
    KeepService(director, r8, 0xcce5d633, 0xcce5d62f, refs[8], 0xf8, 0xda7f28, true);
    KeepService(director, r0, 0x6b8d3d09, 0x4b8d3d02, refs[0], 0, 0, false);
    CreateFromFramework(director, 0x4a47dc0d, 0x4ba8123a, r2);
    if (r2.p) {
        if (Answer(r2.p, 15)) {
            AssignRef(refs[2], r2.p);
            if (refs[9] && !Answer(refs[9], 7)) {
                AssignRef(refs[9], nullptr);
                OriginalFunction<void(__thiscall *)(void *, void *), 0x20f805>()(director, &director->mField90);
            }
        } else {
            LogFailure(0x113, 0xda7f50);
        }
    }
    KeepService(director, r3, 0xef73718f, 0x0f737196, refs[3], 0x120, 0xda7f7c, false);
    KeepService(director, r4, 0x0bb90bdd, 0xabb90bdb, refs[4], 0x12d, 0xda7fb0, false);
    KeepService(director, r6, 0xebb90bd1, 0x8bb90bcc, refs[6], 0x13a, 0xda7fdc, false);
    LocalRef started; // what reference 1's object answers for kStartedIid (asked without testing the answer)
    LocalRef r5, r1, r7;
    KeepService(director, r5, 0x6c1a7e40, 0x6c1a7e3d, refs[5], 0x14a, 0xda7fdc, false);
    // Reference 1: the created object (the original uses it untested) is asked for kStartedIid and the object it gives
    // (untested too) answers slot 3.
    CreateFromFramework(director, 0x1c45c26d, 0xbb523bb2, r1);
    started.Release();
    Slot<bool(__thiscall *)(void *, uint32_t, void **)>(r1.p, 0)(r1.p, kStartedIid, &started.p);
    if (!Answer(started.p, 3))
        LogFailure(0x154, 0xda800c);
    else
        AssignRef(refs[1], r1.p);
    KeepService(director, r7, 0x0c0d3773, 0xec0d3755, refs[7], 0x162, 0xda8038, false);

    RegisterCheat(kAudioEventCheatVTable);
    RegisterCheat(kAudioEventHandlerCheatVTable);

    // Reference 9 goes to the sound service (0x9c5b7e, still the game's): its slot 13 makes an object whose slot 6
    // object takes the reference (slot 33); the made object is handed back (slot 8) and the reference dropped.
    LocalRef sound;
    sound.p = OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()();
    if (sound.p) CallSlot(sound.p, 1);
    if (sound.p && refs[9]) {
        LocalRef made;
        Slot<void(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t, uint32_t, uint32_t)>(sound.p, 13)(sound.p, 0x2bc250e7, 0x5b523ba4, &made.p, 0, 0,
                                                                                                                        0, 0);
        void *target = Slot<void *(__thiscall *)(void *)>(made.p, 6)(made.p);
        Slot<void(__thiscall *)(void *, void *)>(target, 33)(target, refs[9]);
        Slot<void(__thiscall *)(void *, void *)>(sound.p, 8)(sound.p, made.p);
        AssignRef(refs[9], nullptr);
        made.Release();
    }
    sound.Release();
    r7.Release();
    r1.Release();
    r5.Release();
    started.Release();
    r6.Release();
    r4.Release();
    r3.Release();
    r2.Release();
    r0.Release();
    r8.Release();
}

// Message 0xcb: each reference that is set is shut down (slot 4; reference 2: slot 16; reference 1: slot 4 of what it
// answers for kStartedIid, when it answers) and cleared, in this order. Reference 9 is left alone.
void StopAudioServices(AudioDirector *director)
{
    void **refs = director->mRefs;
    if (refs[1]) {
        LocalRef started;
        if (Slot<bool(__thiscall *)(void *, uint32_t, void **)>(refs[1], 0)(refs[1], kStartedIid, &started.p)) CallSlot(started.p, 4);
        AssignRef(refs[1], nullptr);
        started.Release();
    }
    for (unsigned index : {4u, 5u, 6u, 3u, 2u, 0u, 7u, 8u}) {
        if (!refs[index]) continue;
        CallSlot(refs[index], index == 2 ? 16 : 4);
        AssignRef(refs[index], nullptr);
    }
}

} // namespace

bool AudioMessageTarget::DoMessage(void *message)
{
    // 0x20e7c7. `this` is director + 0x64.
    if (!message) return false;
    auto *director = reinterpret_cast<AudioDirector *>(reinterpret_cast<uint8_t *>(this) - 0x64);
    LocalRef asked; // the message's answer for cIGZMessage2, released on the way out whatever happens
    bool handled = false;
    if (Slot<bool(__thiscall *)(void *, uint32_t, void **)>(message, 0)(message, kMessage2Iid, &asked.p)) {
        uint32_t type = Slot<Slot0Args>(message, 4)(message);
        if (type == kMsgAppInit) {
            StartAudioServices(director);
            handled = true;
        } else if (type == kMsgAppShutdown) {
            StopAudioServices(director);
        } else if (type == kMsgAudioReset) {
            OriginalFunction<void(__thiscall *)(void *), 0x20f60e>()(director);
            OriginalFunction<void(__thiscall *)(void *), 0x20f028>()(director);
        }
    }
    asked.Release();
    return handled;
}

} // namespace comdir
