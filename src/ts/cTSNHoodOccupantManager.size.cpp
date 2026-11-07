// The adjustor thunks of cTSNHoodOccupantManager's faces sit in the size-optimised part of the original image (lane DI). In the
// standalone shape (lane FY) our vtables hold the compiler's thunks; these adapters serve the old entry points.
#include "ts/cTSNHoodOccupantManager.h"

using Adapters = cTSNHoodOccupantManagerAdapters;

// 0x4aa0e7, 0x4aa0ff, 0x4aa0ef, 0x4aa0f7: this - face offset, then the primary QueryInterface 0x4aa090.
bool __fastcall Adapters::ManagerQueryInterface(cITSNHoodOccupantManager *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::SerialQueryInterface(cIGZSerializable *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::PersistQueryInterface(cIGZPersistResource *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::MessageQueryInterface(cIGZMessageTarget2 *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryInterface(riid, out);
}
