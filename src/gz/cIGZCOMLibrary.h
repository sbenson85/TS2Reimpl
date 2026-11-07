#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZCOMDirector;
class cIGZString;

// cIGZCOMLibrary: one COM library (a DLL exporting GZDllGetGZCOMDirector) as cGZCOM keeps it (id 0x07d4922e; 8 slots,
// game vtable 0xd9b828 of cGZCOMLibrary). Lane EN (docs/subsystems/startup.md, "F1").
class cIGZCOMLibrary : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x07d4922e;

    virtual void GetPath(cIGZString *out) = 0;        // slot 3, 0x14580d (ret 4): out->Copy(path)
    virtual cIGZCOMDirector *GetDirector() = 0;       // slot 4, folded 0x145770 (+0xc)
    virtual void *GetModule() = 0;                    // slot 5, folded 0x1457d0 (+0x30, the HMODULE)
    virtual bool IsInitialized() = 0;                 // slot 6, folded 0x1458b0 (the byte at +4, raw)
    virtual ~cIGZCOMLibrary() = default;              // slot 7: the deleting destructor (0x145133 in cGZCOMLibrary)
};
