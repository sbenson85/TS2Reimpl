// The adjustor thunks of cTSNHoodTerrain's faces sit in the size-optimised part of the original image (lane DF). In the
// standalone shape (lane FO) our vtables hold the compiler's thunks; these adapters serve the old entry points.
#include "ts/cTSNHoodTerrain.h"

using Adapters = cTSNHoodTerrainAdapters;

// 0x4b0602, 0x4b0612, 0x4b060a: this - face offset, then the primary QueryInterface 0x4b05b0.
bool __fastcall Adapters::TerrainQueryInterface(cITSNHoodTerrain *face, void *, uint32_t riid, void **out)
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
