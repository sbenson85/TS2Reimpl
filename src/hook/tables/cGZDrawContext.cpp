#include "hook/hooks.h"
#include "gz/cGZDrawContext.h"
#include "gz/GZScanConvert.h"

namespace {

// The constructor (cGZDrawSystem slot 5's) and the COM director's creator are the class's switch
// (docs/standalone-shape.md): every draw context carries our vtable, so the slots, the destructor 0x8bb120 and the
// members 0x8bbef0 / 0x8befb0 are in effect through their callers (status "in_effect": "callers").
const HookSpec kHooks[] = {
    {"cGZDrawContext_Construct", 0x8baff0, 32, 0xb45e1b21, reinterpret_cast<void *>(&cGZDrawContextAdapters::Construct)},
    {"cGZDrawContext_Create", 0x8bf7d0, 32, 0x7afe88bf, reinterpret_cast<void *>(&cGZDrawContext::Create)},
    // Dispatch is the cut of the builders' cycle: they call it back (kinds 0, 2, 4, 5), so hooking it makes the whole family
    // in effect through its callers.
    {"cGZDrawContext_Dispatch", 0x8bc040, 32, 0x7c794fdc, reinterpret_cast<void *>(&cGZDrawContextAdapters::Dispatch)},
    // The polygon scan converter (the builders and FillPolygon are ours; a triangle routine 0x9ce1d0 of game code calls it).
    {"GZScanConvertPolygon", 0x9cf0c0, 32, 0x9b8c1723, reinterpret_cast<void *>(&GZScanConvertAdapters::Convert)},
};
REGISTER_HOOKS(kHooks);

} // namespace
