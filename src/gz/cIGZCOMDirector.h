#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZCOM;
class cIGZFrameWork;
class cIGZString;

// cIGZCOMDirector: a COM library's director (16 slots on cRZCOMDllDirector's primary vtable 0xda265c; slot 16 there is
// the class's deleting destructor, not part of the interface). The game's own executable is a library too: its root
// director is cGZCOMDirector (the static at 0x10c9390, GetCOMDirector 0x14491b), whose constructor chain adds the 41
// game directors as sub-directors (rz/COMDirectors.h). Lane EC (docs/subsystems/startup.md). Slot order = declaration
// order.
class cIGZCOMDirector : public cIGZUnknown {
public:
    // slot 3, 0x1ae24d (ret 8): caches com (+0x30) and com->FrameWork() (+0x34), copies the path (+0x10), then
    // initialises every sub-director; false for a null com.
    virtual bool InitializeCOM(cIGZCOM *com, const cIGZString &libraryPath) = 0;
    virtual bool OnStart(cIGZCOM *com) = 0;                        // slot 4, default 0x148260 (ret 4); 22 overrides
    // slot 5, 0x1ae148 (ret 8): callback(clsid, 0, context) (__cdecl) for every registered class.
    virtual void EnumClassObjects(void *callback, void *context) = 0;
    virtual bool GetClassObject(uint32_t clsid, uint32_t iid, void **ppv) = 0; // slot 6, 0x1ae199 (ret 0xc)
    virtual bool CanUnloadNow() = 0;                               // slot 7, 0x1ae11d
    virtual bool OnUnload() = 0;                                   // slot 8, default 0x144910
    virtual uint32_t RefCount() = 0;                               // slot 9, folded 0x46390 (+8)
    virtual uint32_t RemoveRef() = 0;                              // slot 10, 0x1ae2f5 (never deletes)
    virtual cIGZFrameWork *FrameWork() = 0;                        // slot 11, folded 0x147800 (+0x34)
    virtual cIGZCOM *GZCOM() = 0;                                  // slot 12, folded 0x1457d0 (+0x30)
    virtual uint32_t GetDirectorID() = 0;                          // slot 13, pure in the base
    virtual bool GetLibraryPath(cIGZString &out) = 0;              // slot 14, 0x1ae22c (ret 4)
    virtual uint32_t GetHeapAllocatedSize() = 0;                   // slot 15, 0x1ae227 (0)
};
