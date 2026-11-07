#pragma once
#include <cstdint>

// The text of a tree sim error code (the value of cTSTreeSim slot 17), as cTSObject::vf261 (0x5290e0) gives it, or null for a
// code the function has no fixed text for. Codes 0x25, 0x80 and 0x81 have a text that the function extends or replaces with
// details of the routing slot; 0x9b and 0xa1 take their text from a service and have no entry.
const char *TreeErrorText(int32_t code);
