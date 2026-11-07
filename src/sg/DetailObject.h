#pragma once
// The detail object getter (lane TI, scenegraph H2r leftover). RVAs are 1.18.0.187.
#include "game/owned.h"
#include "rz/Singleton_20ddfb.h"

#include <cstdint>

// The cached detail object (a scene-graph system service object; the callers use its slots 17 / 19 / 20 / 42 / 43).
TS2_OWNED_STATIC(void *, 10f9028);

namespace nGZSceneGraph {

// 0xaead80 (__cdecl, no arguments): returns the object cached at 0x10f9028. When that is null, the framework's service
// object is asked through its slot 5 for (0xe5074b6b, 0x25074b4d) into a held reference (the same service
// GetCachedObjectB 0xaeb820 asks), slot 9 of the answer is stored in the cell and the reference is released (0x1cbd79),
// also when slot 9 throws. A missing service or answer is called through unchecked, as in the original.
void *__cdecl GetDetailObject();

} // namespace nGZSceneGraph
