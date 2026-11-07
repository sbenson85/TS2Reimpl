#pragma once
#include "gz/cIGZCOM.h"
#include "gz/cIGZCOMLibrary.h"
#include "gz/cIGZString.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>

class cIGZCOMDirector;
class cIGZFrameWork;

// cGZCOM: the game's COM registry, embedded (0x70 bytes) at +8 of the framework object, and its library record
// cGZCOMLibrary, in the standalone shape (docs/standalone-shape.md; lane EN, startup item F1). RVAs are 1.18.0.187.
//
// A COM library is a DLL exporting a function that returns a cIGZCOMDirector; cGZCOM keeps the libraries it was told
// about (AddLibrary) in a set ordered by path, and a map from class id to the library (and version) that registered the
// class. GetClassObject finds the library for a class id, loads it on first use, and asks its director for the class
// object. The set's ordering asks the right-hand library for its path through its vtable (slot 3) into a temporary
// cRZString and compares the left-hand library's std::string with it.
//
// Creation: the framework's base constructor 0x141d76 builds the object at framework+8 by calling 0x144e52. Ours
// (fw_lifecycle::ConstructBase) placement-news this class there, and the hook on 0x144e52 does the same for the
// original framework constructor. In game both run before the hooks are installed (WinMain's function-local static),
// so the live object still carries the game's vtables: the RVA hooks are adapters that forward to our members.

// ---- cGZCOMLibrary (0x34 bytes; cIGZCOMLibrary, 8 slots, game vtable 0xd9b828) ----
// Built only by cGZCOM: AddLibrary's key (0x144eff) and the library set's node copies (0x144ec2); the vtable is
// referenced by those two constructors and the destructor 0x1450b1 only.
class cGZCOMLibrary : public cIGZCOMLibrary {
public:
    static constexpr uint32_t kVTable = 0xd9b828; // the game's (tests, adapters' trap copies)

    explicit cGZCOMLibrary(const cIGZString &path); // 0x144eff (ret 4)
    cGZCOMLibrary(const cGZCOMLibrary &other);      // 0x144ec2 (ret 4)
    ~cGZCOMLibrary() override;                      // 0x1450b1; deleting 0x145133 (slot 7)
    cGZCOMLibrary &operator=(const cGZCOMLibrary &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override; // slot 0, 0x145b46 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x145260 (++count)
    uint32_t Release() override;                             // slot 2, 0x145c14: decrements, never deletes
    void GetPath(cIGZString *out) override;                  // slot 3, 0x14580d (ret 4)
    cIGZCOMDirector *GetDirector() override;                 // slot 4, folded 0x145770
    void *GetModule() override;                              // slot 5, folded 0x1457d0
    bool IsInitialized() override;                           // slot 6, folded 0x1458b0

    bool operator<(const cGZCOMLibrary &other) const; // 0x1450c6 (thiscall, ret 4): the set's ordering
    bool Unload();                                    // 0x145524: free the module; always true
    bool Initialize();                                // 0x1458b4: load the module and get its director

    bool mInitialized;          // +0x04 (a byte handed out raw by slot 6)
    uint8_t pad05[3];
    int32_t mRefCount;          // +0x08
    cIGZCOMDirector *mDirector; // +0x0c from the module's export
    union {
        cRZString mPath;        // +0x10 (hook-shaped: built and destroyed by hand; std::string at +0x14)
    };
    void *mModule;              // +0x30 HMODULE
};
static_assert(sizeof(cGZCOMLibrary) == 0x34, "cGZCOMLibrary size");
static_assert(offsetof(cGZCOMLibrary, mInitialized) == 4, "cGZCOMLibrary layout");
static_assert(offsetof(cGZCOMLibrary, mRefCount) == 8, "cGZCOMLibrary layout");
static_assert(offsetof(cGZCOMLibrary, mPath) == 0x10, "cGZCOMLibrary layout");
static_assert(offsetof(cGZCOMLibrary, mModule) == 0x30, "cGZCOMLibrary layout");

// What the class map holds: the version a library registered a class with and the library.
struct cGZCOMClassEntry {
    uint32_t mVersion;       // the second argument of the registration callback
    cGZCOMLibrary *mLibrary; // the library being initialised when the class was registered
};

// ---- cGZCOM (0x70 bytes) ----
// RTTI bases cIGZCOM +0, cRZSystemService +4 (cIGZSystemService +4, cRZUnknownMT +8), cRZThreadSafeObject +0x20; game
// vtables 0xd9b84c (14), 0xd9b888 (13), 0xd9b8c0 (6). Service id 0xa3cd8db3, priority 10,000,000 (initialised first,
// shut down last). The secondary faces' slots are the compiler's: +4 QI/AddRef/Release 0x145b36/0x145248/0x145c04,
// +4 slot 7 0x145c24 (one SetServiceRunning override covers cIGZCOM slot 11 and cIGZSystemService slot 7), +8
// 0x145b3e/0x145250/0x145c0c and the destructor thunk 0x14510c. Init/Shutdown/OnTick/OnIdle and the +4 face's folded
// accessors are cRZSystemService's.
class cGZCOM : public cIGZCOM, public rz::cRZThreadSafeObject, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xd9b84c, kSystemVTable = 0xd9b888, kUnknownVTable = 0xd9b8c0; // the game's
    static constexpr uint32_t kServiceId = 0xa3cd8db3;
    static constexpr int32_t kPriority = 10000000;
    static constexpr uint32_t kFrameworkClassId = 0x3e8; // GetClassObject answers it with the framework

    cGZCOM();            // 0x144e52
    ~cGZCOM() override;  // 0x145075; deleting 0x145114 (+8 face slot 3, thunk 0x14510c)

    bool QueryInterface(uint32_t riid, void **out) override; // 0x145b14
    uint32_t AddRef() override;                              // folded 0x145240 (add ecx, 4: cRZSystemService's)
    uint32_t Release() override;                             // 0x145bfc (the +8 face's RemoveRef: never deletes)

    uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **ppv) override; // slot 3, 0x145702
    cIGZFrameWork *FrameWork() override;                                         // slot 4, folded 0x145520
    bool AddLibrary(const cIGZString &path) override;                            // slot 5, 0x1451a3
    void FreeUnusedLibraries() override;                                         // slot 6, 0x1455a6
    bool CreateGuid(uint32_t *out) override;                                     // slot 7, 0x145325
    bool CreateGuids(uint32_t *out, uint32_t count) override;                    // slot 8, 0x1453ab
    bool RealInit() override;                                                    // slot 9, 0x145b7e
    void RealShutdown() override;                                                // slot 10, 0x145be4
    void SetServiceRunning(bool running) override;                               // slot 11, 0x145c1c
    // slot 12, 0x1454e7: callback is bool (__cdecl *)(cIGZCOMLibrary *, void *context); false stops.
    uint32_t EnumerateLibraries(void *callback, void *context) override;
    uint32_t EnumerateLibrariesToArray(void **out, uint32_t *count) override;    // slot 13, 0x14548b

    // Non-virtual helpers.
    bool CanUnloadLibrary(cGZCOMLibrary *library);                                                   // 0x1452ff
    void UnloadLibraries();                                                                          // 0x145556
    bool InitLibrary(cGZCOMLibrary *library);                                                        // 0x145c2c
    bool LoadAndGetClassObject(cGZCOMLibrary *library, uint32_t classId, uint32_t iid, void **ppv); // 0x1457d4
    // The callback InitLibrary hands the library's director (0x145152, __cdecl): registers a class for the library
    // being initialised (the one at +0x44), replacing an older version of it.
    static void __cdecl RegisterClassCallback(uint32_t classId, uint32_t version, cGZCOM *com);

    cRZCriticalSection mCritSect;                         // +0x24 (mLock, +0x20, points here)
    bool mInitialized;                                    // +0x2c set by RealInit
    uint8_t pad2d[3];
    uint32_t mField30;                                    // +0x30 zero, otherwise unused
    std::map<uint32_t, cGZCOMClassEntry> mClasses;        // +0x34
    std::set<cGZCOMLibrary> mLibraries;                   // +0x3c ordered by cGZCOMLibrary::operator<
    cGZCOMLibrary *mCurrentLibrary;                       // +0x44 the library InitLibrary is working on
    cIGZFrameWork *mFramework;                            // +0x48 set by RealInit
    cIGZCOMDirector *mDirector;                           // +0x4c GetCOMDirector(), set by RealInit
    cRZRandom mRandom;                                    // +0x50
    uint8_t pad5c[12];                                    // +0x5c never written
    uint32_t mLastGuidTime;                               // +0x68 (time + 1 of the last GUID batch)
    uint8_t pad6c[4];                                     // +0x6c never written
};
static_assert(sizeof(cGZCOM) == 0x70, "cGZCOM size");
static_assert(offsetof(cGZCOM, mServiceId) == 0x10, "cGZCOM layout: cRZSystemService at +4");
static_assert(offsetof(cGZCOM, mRefCount) == 0xc, "cGZCOM layout: cRZUnknownMT at +8");
static_assert(offsetof(cGZCOM, mRunning) == 0x1c, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mLock) == 0x20, "cGZCOM layout: cRZThreadSafeObject after the polymorphic bases");
static_assert(offsetof(cGZCOM, mCritSect) == 0x24, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mInitialized) == 0x2c, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mClasses) == 0x34, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mLibraries) == 0x3c, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mCurrentLibrary) == 0x44, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mFramework) == 0x48, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mDirector) == 0x4c, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mRandom) == 0x50, "cGZCOM layout");
static_assert(offsetof(cGZCOM, mLastGuidTime) == 0x68, "cGZCOM layout");

// Seed control (host/control_seed.cpp) sets these in controlled runs so new GUIDs repeat between runs: the clock
// CreateGuid / CreateGuids read (time() and the seconds since the GUID epoch), in the original's call order. Null: the
// real clock. The original's call sites get the same fixed clock by redirection; with our cGZCOM in effect only these do.
extern int64_t (__cdecl *gGZCOMGuidTimeOverride)(int64_t *);
extern uint32_t (__cdecl *gGZCOMGuidSecondsOverride)();

// The RVA hooks' entry points (docs/standalone-shape.md, step 6): `this` in ecx (the face the slot is called on), the
// dummy edx, the stack arguments. They serve the game's object (built before the hooks, game vtables) and the game's
// direct calls; each forwards to our member without dispatch.
struct cGZCOMAdapters {
    static cGZCOM *__fastcall Construct(void *memory, void *edx);                                     // 0x144e52
    static void __fastcall Destruct(cGZCOM *self, void *edx);                                         // 0x145075
    static cGZCOM *__fastcall DeletingDestructor(cGZCOM *self, void *edx, uint32_t flags);            // 0x145114
    static bool __fastcall QueryInterface(cIGZCOM *self, void *edx, uint32_t riid, void **out);       // 0x145b14
    static uint32_t __fastcall Release(cIGZCOM *self, void *edx);                                     // 0x145bfc
    static uint32_t __fastcall GetClassObject(cIGZCOM *self, void *edx, uint32_t classId, uint32_t iid, void **ppv); // 0x145702
    static bool __fastcall AddLibrary(cIGZCOM *self, void *edx, const cIGZString &path);              // 0x1451a3
    static void __fastcall FreeUnusedLibraries(cIGZCOM *self, void *edx);                             // 0x1455a6
    static bool __fastcall CreateGuid(cIGZCOM *self, void *edx, uint32_t *out);                       // 0x145325
    static bool __fastcall CreateGuids(cIGZCOM *self, void *edx, uint32_t *out, uint32_t count);      // 0x1453ab
    static bool __fastcall RealInit(cIGZCOM *self, void *edx);                                        // 0x145b7e
    static void __fastcall RealShutdown(cIGZCOM *self, void *edx);                                    // 0x145be4
    static void __fastcall SetServiceRunning(cIGZCOM *self, void *edx, bool running);                 // 0x145c1c
    static uint32_t __fastcall EnumerateLibraries(cIGZCOM *self, void *edx, void *callback, void *context); // 0x1454e7
    static uint32_t __fastcall EnumerateLibrariesToArray(cIGZCOM *self, void *edx, void **out, uint32_t *count); // 0x14548b
    static bool __fastcall CanUnloadLibrary(cGZCOM *self, void *edx, cGZCOMLibrary *library);         // 0x1452ff
    static void __fastcall UnloadLibraries(cGZCOM *self, void *edx);                                  // 0x145556
    static bool __fastcall InitLibrary(cGZCOM *self, void *edx, cGZCOMLibrary *library);              // 0x145c2c
    static bool __fastcall LoadAndGetClassObject(cGZCOM *self, void *edx, cGZCOMLibrary *library, uint32_t classId,
                                                 uint32_t iid, void **ppv);                           // 0x1457d4
};

struct cGZCOMLibraryAdapters {
    static cGZCOMLibrary *__fastcall Construct(void *memory, void *edx, const cIGZString &path);      // 0x144eff
    static cGZCOMLibrary *__fastcall CopyConstruct(void *memory, void *edx, const cGZCOMLibrary &other); // 0x144ec2
    static void __fastcall Destruct(cGZCOMLibrary *self, void *edx);                                  // 0x1450b1
    static cGZCOMLibrary *__fastcall DeletingDestructor(cGZCOMLibrary *self, void *edx, uint32_t flags); // 0x145133
    static bool __fastcall QueryInterface(cIGZCOMLibrary *self, void *edx, uint32_t riid, void **ppv); // 0x145b46
    static uint32_t __fastcall Release(cIGZCOMLibrary *self, void *edx);                             // 0x145c14
    static void __fastcall GetPath(cIGZCOMLibrary *self, void *edx, cIGZString *out);                 // 0x14580d
    static bool __fastcall Less(const cGZCOMLibrary *self, void *edx, const cGZCOMLibrary &other);    // 0x1450c6
    static bool __fastcall Unload(cGZCOMLibrary *self, void *edx);                                    // 0x145524
    static bool __fastcall Initialize(cGZCOMLibrary *self, void *edx);                                // 0x1458b4
};

// 0x1afc9d (__cdecl): converts srcLength units of src (encoding srcEncoding) to the string's own
// UTF-8 and stores them with the string's FromChar (slot 3). Returns the source units consumed.
// The same encoding (or -2) is stored as is.
uint32_t ConvertBytesToString(const void *src, uint32_t srcLength, int32_t srcEncoding, cIGZString *dst);

// 0x1b139f (__cdecl): the path of the running executable into *str (GetModuleFileNameW looked up
// once in Kernel32, else the ANSI call); returns the string's length (slot 7).
uint32_t GetModulePath(cIGZString *str);
