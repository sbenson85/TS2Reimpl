#pragma once
#include "gz/cIGZCOM.h"
#include "gz/cIGZCOMDirector.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZFrameWorkHooks.h"
#include "gz/cIGZString.h"
#include "rz/COMDirectorData.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <unordered_map>
#include <vector>

// The COM directors in the standalone shape (docs/standalone-shape.md; structure by lane EC, bodies by lane EO).
// RVAs are 1.18.0.187.
//
// The engine is a set of statically linked "DLLs", each with a director derived from cRZCOMDllDirector. A director owns
// a map from class id to factory (filled by AddCls in its constructor) and the list of directors it depends on (filled
// by AddDirector). The executable's root director is cGZCOMDirector (the static at 0x10c9390); the 41 game directors
// are function-local statics in the image (one, MadCow, a plain global built by a static initialiser), each built in
// place by our getter or initialiser (COMDirectors.cpp), so every one carries our vtables.
//
// The hook-shaped struct of the same name (rz/StaticGetters.h) holds the global name and is still the base of the
// audio director (comdir::AudioDirector, not in the shape yet), so these classes live in namespace rz.
namespace comdir {

// What a registered class id means to GetClassObject.
constexpr int32_t kObjectFactory = 1;  // factory() returns a new object; GetClassObject queries it
constexpr int32_t kCreateFunction = 2; // factory(iid, out) creates and returns the object itself

// The value of a node of the director's class map. The node (0x14 bytes) is: next, prev, class id (+8), factory (+0xc),
// kind (+0x10).
struct ClassEntry {
    void *factory;
    int32_t kind;
};

// The class map (0x20 bytes at +0x44: an unordered_map with 8 buckets, max load 1.0, as the original's).
using ClassMap = std::unordered_map<uint32_t, ClassEntry>;
static_assert(sizeof(ClassMap) == 0x20, "ClassMap layout");
static_assert(sizeof(std::pair<const uint32_t, ClassEntry>) == 12, "ClassMap node layout");

// EnumClassObjects callback: cdecl, called once per class id with 0 as the second argument.
using EnumCallback = void(__cdecl *)(uint32_t clsid, uint32_t zero, void *context);

// GetDirectorID of each director (kDirectorSpecs order; the tests check them against the table and the originals).
constexpr uint32_t kDirectorIds[kDirectorCount] = {
    0xcc361e28, 0x9d309433, 0x3b63609c, 0xdce48ec2, 0x3b1e8612, 0xcb6b39f6, 0x0c59fa79, 0xa4fbe888, 0xc93ef224,
    0xbb50c92d, 0xeb4d03f0, 0x63caeeec, 0x7d3f4d03, 0xfa2e78c2, 0x9a3559b0, 0x43cac0b9, 0x0b2ea939, 0x5aa40bd3,
    0xa41740d1, 0x041740ba, 0xfd21f3f0, 0xc3caec3b, 0xa431a334, 0xcc7e8cd1, 0xfb50af8f, 0xdb4ce149, 0x69cfc8f9,
    0xfd53ff17, 0x29a6f77e, 0x6d4df6db, 0xe9d3a40b, 0x9b2d3d2f, 0x1cec20be, 0xcb8b5e1e, 0x0a5a5504, 0x64ef0af8,
    0x252eba2e, 0xab27e940, 0xc93ef224, 0xabe2fc1d, 0xcde2eefc,
};

} // namespace comdir

namespace rz {

// The path string at +0x10: a cRZString (still hook-shaped: Construct() / Destruct() install the game's string
// vtable), built and destroyed by this holder at its place in the member order, as the original's inline code does.
struct cRZStringMember {
    union {
        cRZString value;
    };
    cRZStringMember() { value.Construct(); }
    ~cRZStringMember() { value.Destruct(); }
    cRZStringMember(const cRZStringMember &) = delete;
    cRZStringMember &operator=(const cRZStringMember &) = delete;
};
static_assert(sizeof(cRZStringMember) == 0x20, "cRZStringMember layout");

// RTTI bases cIGZCOMDirector +0 (game vtable 0xda265c, 17 slots: 16 interface slots and the deleting destructor),
// cIGZFrameWorkHooks +4 (0xda26a4, 11 slots: thunks 0x144913/0x1448e9/0x144975 and the eight shared `true` defaults).
// 0x64 bytes. Slot 2 (Release, folded 0x1ae2f0) jumps through the vtable to slot 10 (RemoveRef): the count is never
// taken to a delete, as every director is a static.
class cRZCOMDllDirector : public cIGZCOMDirector, public cIGZFrameWorkHooks {
public:
    cRZCOMDllDirector();          // 0x1adffa
    virtual ~cRZCOMDllDirector(); // body 0x1ae048; slot 16 (deleting) 0x1448ca

    // cIGZUnknown (both faces; the +4 thunks 0x144913, 0x1448e9, 0x144975 are the compiler's)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1ae298
    uint32_t AddRef() override;                              // folded 0x145260 (++mRefCount)
    uint32_t Release() override;                             // folded 0x1ae2f0 (this->RemoveRef())

    // cIGZCOMDirector
    bool InitializeCOM(cIGZCOM *com, const cIGZString &libraryPath) override; // 0x1ae24d
    bool OnStart(cIGZCOM *com) override;                                     // shared 0x148260 (true)
    void EnumClassObjects(void *callback, void *context) override;           // 0x1ae148
    bool GetClassObject(uint32_t clsid, uint32_t iid, void **ppv) override;   // 0x1ae199
    bool CanUnloadNow() override;                                            // 0x1ae11d
    bool OnUnload() override;                                                // shared 0x144910
    uint32_t RefCount() override;                                            // folded 0x46390
    uint32_t RemoveRef() override;                                           // 0x1ae2f5
    cIGZFrameWork *FrameWork() override;                                     // folded 0x147800
    cIGZCOM *GZCOM() override;                                               // folded 0x1457d0
    uint32_t GetDirectorID() override;                                       // _purecall in the original's vtable
    bool GetLibraryPath(cIGZString &out) override;                           // 0x1ae22c
    uint32_t GetHeapAllocatedSize() override;                                // 0x1ae227

    // cIGZFrameWorkHooks (all the shared `mov al, 1; ret` 0x144910)
    bool PreFrameWorkInit() override;
    bool PreAppInit() override;
    bool PostAppInit() override;
    bool PreAppShutdown() override;
    bool PostAppShutdown() override;
    bool PostSystemServiceShutdown() override;
    bool AbortiveQuit() override;
    bool OnInstall() override;

    // Non-virtual. 0x1ae080 (ret 8): registers class `clsid` with an object factory (kind 1); an id already registered
    // keeps its first entry. 0x1ae0ad: the same with a creating function (kind 2). 0x1ae0da (ret 4): initialises `sub`
    // with this director's GZCOM() and path (sub's slot 3), adds sub's own sub-directors first (recursively), then sub.
    void AddCls(uint32_t clsid, void *factory);
    void AddClsDirect(uint32_t clsid, void *factory);
    void AddDirector(cRZCOMDllDirector *sub);

    uint32_t mRefCount;                         // +0x08
    uint32_t mField0C;                          // +0x0c 0 after construction, no reader found
    cRZStringMember mLibraryPath;               // +0x10
    cIGZCOM *mCOM;                              // +0x30 set by InitializeCOM
    cIGZFrameWork *mFramework;                  // +0x34 com->FrameWork(), set by InitializeCOM
    std::vector<cRZCOMDllDirector *> mChildren; // +0x38 sub-directors (AddDirector)
    comdir::ClassMap mClasses;                  // +0x44 class id -> (factory, kind)

protected:
    // The class-registration steps of director `index` (COMDirectorData.cpp: AddCls / AddDirector, in order).
    void RunSteps(unsigned index);
};
static_assert(sizeof(cRZCOMDllDirector) == 0x64, "cRZCOMDllDirector size");
static_assert(offsetof(cRZCOMDllDirector, mRefCount) == 0x08, "cRZCOMDllDirector layout: hooks face at +4");
static_assert(offsetof(cRZCOMDllDirector, mLibraryPath) == 0x10, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mCOM) == 0x30, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mFramework) == 0x34, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mChildren) == 0x38, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mClasses) == 0x44, "cRZCOMDllDirector layout");

// cGZCOMDirector: the executable's root director (the static at 0x10c9390; game vtables 0xd9b754 (18 slots),
// 0xd9b7a0). Overrides GetDirectorID (0x1448f1) and adds slot 17 (0x1448f7). The constructor stores the framework's
// GetCOMObject() (cIGZFrameWork slot 23) at +0x30 inline, before any InitializeCOM.
class cGZCOMDirector : public cRZCOMDllDirector {
public:
    static constexpr uint32_t kId = 0xa3cad79a;
    cGZCOMDirector();                  // 0x144897
    uint32_t GetDirectorID() override; // 0x1448f1
    // Slot 17, 0x1448f7 (no arguments, plain ret): stores the framework (the service object, 0x1427b5) at +0x34 and
    // returns whether it is non-null. No caller found (EC's header had an argument: the disassembly has none).
    virtual bool StoreFrameWork();
};
static_assert(sizeof(cGZCOMDirector) == 0x64, "cGZCOMDirector size");

// The 41 game directors: one class per director, a template over its index in comdir::kDirectorSpecs (COMDirectorData:
// the RTTI name, the id, the constructor's steps). They differ from the base in GetDirectorID, the steps, and the slots
// some override: OnStart (22 of them), the hooks face (cGZSound 5/6, cHitMan 4/6, cTSUI 3/5). The overrides are explicit
// specialisations (below, bodies in COMDirectors.cpp); the others inherit the base's.
struct NoSteps {}; // builds a director without its registration steps (the tests read our vtables from one)

template <unsigned N> class cCOMDirectorT : public cRZCOMDllDirector {
public:
    cCOMDirectorT() { RunSteps(N); }
    explicit cCOMDirectorT(NoSteps) {}
    uint32_t GetDirectorID() override { return comdir::kDirectorIds[N]; }
    bool OnStart(cIGZCOM *com) override { return cRZCOMDllDirector::OnStart(com); }
    bool PreFrameWorkInit() override { return cRZCOMDllDirector::PreFrameWorkInit(); }
    bool PreAppInit() override { return cRZCOMDllDirector::PreAppInit(); }
    bool PostAppInit() override { return cRZCOMDllDirector::PostAppInit(); }
    bool PreAppShutdown() override { return cRZCOMDllDirector::PreAppShutdown(); }
};

// OnStart overrides. Ours (verified bodies, the services each creates and registers): Event 0x1a83f5, Draw 0x8b8030,
// Font 0x90a048, UIScript 0x85b355, RZMessage 0xc6ec41, ResMan 0x9b33b0, SceneDBAnim 0x9d11d0, SceneGraph 0xa34210,
// TSWeb 0x1cb910, SndEa 0xb12c5f, Win 0x92f8da, HitMan 0xb2a422, Sound 0xb22420, Graphics 0x154460, Draw2D 0x145f20;
// lane ET (D2): TSPersist 0x1d3e20, TSSG 0x2454c0, TSSim 0x416370, TSAppFW 0xc2beec, TSCommon 0xcb5110, TSCAS 0xcb54a0.
// lane FB: TSUI 0x5b72e7.
#define RZ_COMDIR_ONSTART(N) template <> bool cCOMDirectorT<comdir::N>::OnStart(cIGZCOM *com);
RZ_COMDIR_ONSTART(kDir_Event)
RZ_COMDIR_ONSTART(kDir_Draw)
RZ_COMDIR_ONSTART(kDir_Font)
RZ_COMDIR_ONSTART(kDir_UIScript)
RZ_COMDIR_ONSTART(kDir_RZMessage)
RZ_COMDIR_ONSTART(kDir_ResMan)
RZ_COMDIR_ONSTART(kDir_SceneDBAnim)
RZ_COMDIR_ONSTART(kDir_SceneGraph)
RZ_COMDIR_ONSTART(kDir_TSWeb)
RZ_COMDIR_ONSTART(kDir_SndEa)
RZ_COMDIR_ONSTART(kDir_Win)
RZ_COMDIR_ONSTART(kDir_HitMan)
RZ_COMDIR_ONSTART(kDir_Sound)
RZ_COMDIR_ONSTART(kDir_Graphics)
RZ_COMDIR_ONSTART(kDir_Draw2D)
RZ_COMDIR_ONSTART(kDir_TSPersist)
RZ_COMDIR_ONSTART(kDir_TSSG)
RZ_COMDIR_ONSTART(kDir_TSSim)
RZ_COMDIR_ONSTART(kDir_TSUI)
RZ_COMDIR_ONSTART(kDir_TSAppFW)
RZ_COMDIR_ONSTART(kDir_TSCommon)
RZ_COMDIR_ONSTART(kDir_TSCAS)
#undef RZ_COMDIR_ONSTART

// Hooks-face overrides: Sound slot 5 0xb224d9 and slot 6 0xb2252e, HitMan slot 4 0xb2a4c3 and slot 6 0xb2a568 (ours, lane
// ET); TSUI slot 3 0x5b7722 (ET) and slot 5 0x5b763c (lane FB).
template <> bool cCOMDirectorT<comdir::kDir_TSUI>::PreFrameWorkInit();
template <> bool cCOMDirectorT<comdir::kDir_TSUI>::PostAppInit();
template <> bool cCOMDirectorT<comdir::kDir_Sound>::PostAppInit();
template <> bool cCOMDirectorT<comdir::kDir_Sound>::PreAppShutdown();
template <> bool cCOMDirectorT<comdir::kDir_HitMan>::PreAppInit();
template <> bool cCOMDirectorT<comdir::kDir_HitMan>::PreAppShutdown();

// The old entry points (hooks): static __fastcall functions, `this` (the face of the slot) in ecx, the dummy edx, the
// stack arguments; each forwards with a qualified call (no dispatch). The constructors build our class in place.
struct cRZCOMDllDirectorAdapters {
    static cRZCOMDllDirector *__fastcall Construct(void *memory, void *);                                  // 0x1adffa
    static void __fastcall Destruct(cIGZCOMDirector *face, void *);                                         // 0x1ae048
    static cRZCOMDllDirector *__fastcall DeletingDestructor(cIGZCOMDirector *face, void *, uint32_t flags);  // 0x1448ca
    static bool __fastcall QueryInterface(cIGZCOMDirector *face, void *, uint32_t riid, void **out);        // 0x1ae298
    static bool __fastcall InitializeCOM(cIGZCOMDirector *face, void *, cIGZCOM *com, const cIGZString &path); // 0x1ae24d
    static void __fastcall EnumClassObjects(cIGZCOMDirector *face, void *, void *callback, void *context);   // 0x1ae148
    static bool __fastcall GetClassObject(cIGZCOMDirector *face, void *, uint32_t clsid, uint32_t iid, void **ppv); // 0x1ae199
    static bool __fastcall CanUnloadNow(cIGZCOMDirector *face, void *);                                    // 0x1ae11d
    static uint32_t __fastcall RemoveRef(cIGZCOMDirector *face, void *);                                   // 0x1ae2f5
    static bool __fastcall GetLibraryPath(cIGZCOMDirector *face, void *, cIGZString &out);                 // 0x1ae22c
    static bool __fastcall HooksQueryInterface(cIGZFrameWorkHooks *face, void *, uint32_t riid, void **out); // 0x144913
    static uint32_t __fastcall HooksRelease(cIGZFrameWorkHooks *face, void *);                             // 0x144975
    static uint32_t __fastcall HooksAddRef(cIGZFrameWorkHooks *face, void *); // the audio director's copy 0x20e7b7
    static void __fastcall AddCls(cRZCOMDllDirector *self, void *, uint32_t clsid, void *factory);          // 0x1ae080
    static void __fastcall AddClsDirect(cRZCOMDllDirector *self, void *, uint32_t clsid, void *factory);    // 0x1ae0ad
    static void __fastcall AddDirector(cRZCOMDllDirector *self, void *, cRZCOMDllDirector *sub);            // 0x1ae0da
};

struct cGZCOMDirectorAdapters {
    static cGZCOMDirector *__fastcall Construct(void *memory, void *);       // 0x144897
    static uint32_t __fastcall GetDirectorID(cIGZCOMDirector *face, void *); // 0x1448f1
    static bool __fastcall StoreFrameWork(cIGZCOMDirector *face, void *);    // 0x1448f7
};

template <unsigned N> struct cCOMDirectorAdapters {
    using Self = cCOMDirectorT<N>;
    static Self *__fastcall Construct(void *memory, void *) { return ::new (memory) Self; }
    static void __fastcall Destruct(cIGZCOMDirector *face, void *)
    {
        [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::~cCOMDirectorT();
    }
    static Self *__fastcall DeletingDestructor(cIGZCOMDirector *face, void *, uint32_t flags)
    {
        Self *self = static_cast<Self *>(face);
        [[msvc::forceinline_calls]] self->Self::~cCOMDirectorT();
        if (flags & 1) ::operator delete(self, sizeof(Self));
        return self;
    }
    static uint32_t __fastcall GetDirectorID(cIGZCOMDirector *face, void *)
    {
        [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetDirectorID();
    }
    static bool __fastcall OnStart(cIGZCOMDirector *face, void *, cIGZCOM *com)
    {
        [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::OnStart(com);
    }
    // The hooks-face overrides (`this` is the +4 face, as the game's vtable calls them; lane ET).
    static bool __fastcall PreFrameWorkInit(cIGZFrameWorkHooks *face, void *)
    {
        [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::PreFrameWorkInit();
    }
    static bool __fastcall PreAppInit(cIGZFrameWorkHooks *face, void *)
    {
        [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::PreAppInit();
    }
    static bool __fastcall PostAppInit(cIGZFrameWorkHooks *face, void *)
    {
        [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::PostAppInit();
    }
    static bool __fastcall PreAppShutdown(cIGZFrameWorkHooks *face, void *)
    {
        [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::PreAppShutdown();
    }
};


} // namespace rz
