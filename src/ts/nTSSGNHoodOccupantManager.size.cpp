// The adjustor thunks of nTSSG::cNHoodOccupantManager's faces sit in the size-optimised part of the original image. Our vtables
// hold the compiler's thunks; these adapters serve the old entry points (lane GO).
#include "ts/nTSSGNHoodOccupantManager.h"

namespace nTSSG {

using Adapters = cNHoodOccupantManagerAdapters;
using Self = cNHoodOccupantManager;

// 0x2a8deb / 0x2a8df3: this - 8 / this - 0xc, then the primary QueryInterface 0x2a8da0.
bool __fastcall Adapters::SceneQueryInterface(cINHoodOccupantManager *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::MessageQueryInterface(cIGZMessageTarget2 *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryInterface(riid, out);
}

} // namespace nTSSG
