#pragma once
#include "rz/COMDirectorData.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/StaticGetters.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

// The COM directors (RVAs are 1.18.0.187).
//
// The engine is a set of statically linked "DLLs", each with a director derived from
// cRZCOMDllDirector (vtables 0xda265c / 0xda26a4). A director owns a map from class id to a
// factory (filled by AddCls in its constructor) and a list of the directors it depends on (filled
// by AddDirector). The base class layout, constructor and destructor are in StaticGetters.h; this
// file adds the base class behaviour (the 17 + 11 vtable slots, AddCls, AddDirector) and the 41
// derived directors, which are described by data (COMDirectorData.cpp: for each director its
// vtables, its constructor's list of steps, the addresses of its function-local static and guard)
// and built by one generic constructor and one generic getter.
//
// Class ids and factory addresses are data of the game (a factory is game code: only its address is
// recorded and stored, never called by us); the id thunks of the original (`mov eax, id; ret`) are
// not called either: their values are in the table.
namespace comdir {

static_assert(sizeof(ClassMap) == sizeof(HashIdSet), "ClassMap layout");

// What one step of a director constructor does.
enum Op : uint8_t {
    kAddCls = 1,              // AddCls(clsid a, factory at RVA b)
    kAddClsDirect = 2,        // the variant with kind 2 (0x1ae0ad)
    kAddDirector = 3,         // AddDirector(getter of director a)
    kAddDirectorIfPresent = 4,// if (getter of director a != null) AddDirector(getter of director a)
    kAddClsIfSimProbe = 5,    // AddCls(a, b) only when the probe at 0x416530 succeeds
    kAddTSSGHelper = 6,       // 0x140fe3: AddDirector(TSSG getter) on the director in `this`
    kAddEngineHelper = 7,     // 0x140ff3: AddDirector of the ten engine directors
};

struct Step {
    uint8_t op;
    uint32_t a;
    uint32_t b;
};

// Constructor/getter properties.
constexpr uint8_t kCtorUnwinds = 1;   // the constructor has an EH frame: a throw destroys the base
constexpr uint8_t kGetterUnwinds = 2; // the getter has an EH frame: a throw aborts the static's guard
constexpr uint8_t kInlineCtor = 4;    // the constructor body is inlined into the getter
constexpr uint8_t kStaticInit = 8;    // not a function-local static: built by a static initialiser

struct DirectorSpec {
    const char *name;     // RTTI class name
    uint32_t id;          // GetDirectorID (slot 13)
    uint32_t ctorRva;     // the constructor (0 when inlined into the getter or the initialiser)
    uint32_t getterRva;   // the function-local static's getter (for kStaticInit: a plain accessor)
    uint32_t initRva;     // the static initialiser (kStaticInit only)
    uint32_t staticRva;   // the director object
    uint32_t guardRva;    // its thread-safe-static guard (0 for kStaticInit)
    // (the game's vtables are in tests/com_directors.cpp, kGameVTables: our code never stores them, and the host must
    // not hold them, or tools/callers.py counts them as live)
    uint16_t firstStep;   // into kSteps
    uint16_t stepCount;
    uint8_t flags;
};

extern const DirectorSpec kDirectorSpecs[kDirectorCount];
extern const Step kSteps[];

// The hook-shaped director: only the base of the audio director (AudioDirector below, not in the shape yet), on the
// hook-shaped layout of rz/StaticGetters.h. Its members forward to rz::cRZCOMDllDirector's (same layout, same bodies).
struct COMDirector : cRZCOMDllDirector {
    rz::cRZCOMDllDirector *AsShape() { return reinterpret_cast<rz::cRZCOMDllDirector *>(this); }
    void AddCls(uint32_t clsid, void *factory) { AsShape()->rz::cRZCOMDllDirector::AddCls(clsid, factory); }
    void AddDirector(rz::cRZCOMDllDirector *sub) { AsShape()->rz::cRZCOMDllDirector::AddDirector(sub); }
};
static_assert(sizeof(COMDirector) == 0x64, "COMDirector layout");

// A director's GetDirectorID: `mov eax, id; ret` (the audio director's id thunk).
template <uint32_t Id> struct FixedId {
    uint32_t Get() const { return Id; }
};

// The helpers the application director's constructor calls (cdecl, the director as the argument).
void AddTSSGHelper(rz::cRZCOMDllDirector *director);   // 0x140fe3
void AddEngineHelper(rz::cRZCOMDllDirector *director); // 0x140ff3
// 0x14107c (cdecl): OnStart(com) of the eleven engine directors, in the application director's order.
void StartEngineDirectors(cIGZCOM *com);

// 0x416530: whether the debug-hook word the Sim director checks is set (see COMDirectors.cpp).
bool SimProbe();

// 0x1408d0 (__cdecl, no arguments): the static initialiser of the MadCow director (a plain global at 0x10fff78, not a
// function-local static): builds it (our class, in place) and registers its destructor with atexit.
void InitMadCow();

// The getters of the function-local statics. `GetDirector(index)` is the getter of the director with that index
// (kDirectorSpecs order).
rz::cRZCOMDllDirector *GetDirector(unsigned index);

template <unsigned N> struct DirectorFns {
    // The getter: returns the director's static, built in place (rz::cCOMDirectorT<N>) on first use (thread-safe
    // statics protocol, like GetCOMDirector).
    static rz::cRZCOMDllDirector *Get();
    // What the getter registers with atexit: destroys the director's static.
    static void Destroy();
};

// How the getters register their exit handlers (atexit by default; the tests record instead): gRegisterExitHandler,
// shared with GetCOMDirector.

// ---------------------------------------------------------------------------------------------
// The audio director (cTSAudioCOMDirector, the 42nd). Unlike the 41 above it is bigger than a base
// director (0xa8 bytes: a third subobject, cIGZMessageTarget2, at +0x64), so it is a class of its own.
// Vtables 0xda7d50 (+0), 0xda7d98 (+4, cIGZFrameWorkHooks), 0xda7dc8 (+0x64, cIGZMessageTarget2).

struct ClassFactory {
    uint32_t clsid;
    uint32_t factoryRva;
};
extern const ClassFactory kAudioClasses[]; // AddCls steps of the constructor, in order
extern const unsigned kAudioClassCount;

struct AudioDirector : COMDirector {
    static constexpr uint32_t kVTable = 0xda7d50;
    static constexpr uint32_t kVTableAt4 = 0xda7d98;
    static constexpr uint32_t kVTableAt64 = 0xda7dc8;
    static constexpr uint32_t kId = 0x9c45c57e;
    static constexpr uint32_t kStaticRva = 0x10cc328; // the function-local static, then its guard
    static constexpr uint32_t kGuardRva = 0x10cc3d0;
    static constexpr unsigned kRefCount = 10;

    void *mMessageTargetVTable;      // +0x64
    void *mRefs[kRefCount];          // +0x68..+0x8c: ten owned references (zero after construction), released in reverse order
    uint32_t mField90, mField94, mField98, mField9C; // +0x90..+0x9c: zero after construction; DoMessage passes &mField90 to 0x20f805
    std::set<uint32_t> mMessageIds;  // +0xa0: ids 0xca, 0xcb, 0xeded3807 (the keys PreAppInit / PostAppShutdown walk)

    // 0x20e164 (__thiscall, no arguments, EH frame): base constructor, the three vtables, the fields,
    // 57 AddCls, six AddDirector (Sound, HitMan, SndEa, Freshness, HitMp3, Speex) and three ids.
    AudioDirector *Construct();
    // 0x20e676 (__thiscall, noexcept): vtables, the id set, the ten references (slot 2 of each, from the
    // last), the base destructor 0x1ae048.
    void Destruct() noexcept;
    // 0x20e790 (slot 16, __thiscall, ret 4): the destructor, then operator delete(this, 0xa8) when flags & 1.
    AudioDirector *DeletingDestructor(uint32_t flags);
    // 0x20f454 (slot 4, __thiscall, ret 4): see COMDirectors.cpp.
    bool OnStart(void *framework);
};

static_assert(offsetof(AudioDirector, mRefs) == 0x68, "AudioDirector layout");
static_assert(offsetof(AudioDirector, mMessageIds) == 0xa0, "AudioDirector layout");
static_assert(sizeof(AudioDirector) == 0xa8, "AudioDirector layout");

// 0x20efaf (__cdecl, no arguments): the function-local static at 0x10cc328, built on first use (the
// thread-safe statics protocol; the exit handler destroys it).
AudioDirector *GetAudioDirector();

// Slots of the +4 subobject (cIGZFrameWorkHooks); `this` is director + 4 (the QueryInterface, AddRef
// and Release thunks of this subobject are COMDirectorHooks).
struct AudioDirectorHooks {
    // 0x20f571 (slot 4, __thiscall, no stack arguments): walks the message ids calling slot 5 of the
    // message service for the +0x64 subobject, then logs "TSAudio: Begin PreAppInit" and "End PreAppInit"
    // (the walk comes before both lines). Returns true.
    bool PreAppInit();
    // 0x20f555 (slot 7): walks the message ids calling slot 6 for the +0x64 subobject.
    bool PostAppShutdown();
};

// Slots of the +0x64 subobject (cIGZMessageTarget2); `this` is director + 0x64.
struct AudioMessageTarget {
    bool QueryInterface(uint32_t iid, void **out); // 0x20f5f1
    uint32_t AddRef();                              // 0x20e7bf
    uint32_t Release();                             // 0x20f606
    // 0x20e7c7 (slot 3, __thiscall, ret 4, EH frame): the message handler. Null message: false. Otherwise the message is
    // asked for cIGZMessage2 (slot 0, iid 0x65297976; the reference it gives is released on the way out) and, when it
    // answers, its type (slot 4) picks: 0xca (application init) creates the audio services into the director's ten
    // references, registers two cheats and hands reference 9 to the sound service, returning true; 0xcb (shutdown)
    // shuts the references down and clears them; 0xeded3807 runs 0x20f60e and 0x20f028. Anything else: false.
    bool DoMessage(void *message);
};

} // namespace comdir
