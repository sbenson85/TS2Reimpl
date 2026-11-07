#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZFrameWork;
class cIGZString;

// cIGZCOM: the COM registry's interface (id 0x4241330d; 14 slots), implemented by cGZCOM (vtable 0xd9b84c), embedded
// at +8 of the framework (cIGZFrameWork::GetCOMObject). Every COM director caches it at +0x30 (cIGZCOMDirector::GZCOM)
// and the root director's is what GetDirectorFrameworkObject 0x1ae18d returns. Lane EC (docs/subsystems/startup.md).
//
// Slots 12 and 13 are an overload pair in the community headers (both "EnumerateLibraries"); they are named apart here
// (ifacegen has no overloads), so declaration order = slot order holds.
class cIGZCOM : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x4241330d;

    // slot 3, 0x145702 (ret 0xc): the class object for `classId` queried for `iid`; 32-bit result (lane CK: callers
    // never test it as a byte). Class id 0x3e8 answers with the framework.
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **ppv) = 0;
    virtual cIGZFrameWork *FrameWork() = 0;                       // slot 4, folded 0x145520 (+0x48)
    virtual bool AddLibrary(const cIGZString &path) = 0;          // slot 5, 0x1451a3 (ret 4)
    virtual void FreeUnusedLibraries() = 0;                       // slot 6, 0x1455a6
    virtual bool CreateGuid(uint32_t *out) = 0;                   // slot 7, 0x145325 (ret 4)
    virtual bool CreateGuids(uint32_t *out, uint32_t count) = 0;  // slot 8, 0x1453ab (ret 8)
    virtual bool RealInit() = 0;                                  // slot 9, 0x145b7e (GZFrameWork_Startup state 1)
    virtual void RealShutdown() = 0;                              // slot 10, 0x145be4
    virtual void SetServiceRunning(bool running) = 0;             // slot 11, 0x145c1c (ret 4)
    // slot 12, 0x1454e7 (ret 8): callback(library, context) per library (__cdecl; false stops); returns the count.
    virtual uint32_t EnumerateLibraries(void *callback, void *context) = 0;
    // slot 13, 0x14548b (ret 8): the libraries into an array (out may be null: count only).
    virtual uint32_t EnumerateLibrariesToArray(void **out, uint32_t *count) = 0;
};
