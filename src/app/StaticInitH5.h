#pragma once
// Owned XC entries of package H5 (lane HQ; docs/subsystems/startup.md "Done in lane HQ"): statics whose constructors
// are now ours (src/rz/WinApiTables.h, src/rz/cRZStopwatchCalibration.h, src/rz/cRZMemoryReport.h,
// src/audio/MmxResampleTables.h, src/misc/StartupObjects.h) and the computed slot 193 (src/misc/Quaternion.h). Each
// runs in its own slot and registers its destructor thunk, when the image's entry registers one, with BoundAtExit().
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

enum class H5Kind : uint8_t {
    TryEnterLoader,   // 93: rz::TryEnterCriticalSectionLoader; thunk: Unload (FreeLibrary)
    ShellLoader,      // 94: rz::ShellExecuteLoader; thunk: Unload
    TimeZone,         // 98: _tzset() (the image's entry is `jmp _tzset`, in cRZDate's translation unit): our CRT's time-zone
                      // state, which our cRZTime reads (src/app/ReportText.size.cpp). The image's own CRT is then set by
                      // its time functions (localtime / mktime run __tzset) before every image-code read of _timezone
                      // (0x267ee0, 0x3a6bf0, 0x9914ee, 0xcd2190: each after _localtime64_s or _mktime64)
    WideFiles,        // 102: rz::WideFileFunctions
    WideDirectories,  // 103: rz::WideDirectoryFunctions
    Calibration,      // 105: cRZStopwatchCalibration
    RecordPoolReset,  // 134: startupobj::RecordPoolReset
    Rotation,         // 193: a quaternion = normalise(q(axis A, angle A) * q(axis B, angle B))
    MemoryReport,     // 754: cRZMemoryReport; thunk: its destructor 0xc69888 (the image's, through OriginalFunction)
    MemoryHook,       // 755: cRZMemoryHook (the text argument); thunk: Destruct
    MmxTables,        // 769: MmxResampleTables; thunk: nothing (the image's tests a flag and returns either way)
};

struct H5Static {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object or target (RVA)
    H5Kind kind;
    uint32_t thunk;    // the image's exit thunk (RVA), 0 when it registers none
};

// Slot 193's inputs (RVAs): the two axes (3 floats each, .data) and the two half angles (doubles, .rdata).
constexpr uint32_t kRotationAxisA = 0x1006a58, kRotationAxisB = 0x1006a40;
constexpr uint32_t kRotationHalfAngleA = 0xdb1dd0, kRotationHalfAngleB = 0xdbae10;

void __cdecl InitH5(const void *h5Static);
const H5Static *H5Statics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedH5(size_t *count);

} // namespace staticinit
