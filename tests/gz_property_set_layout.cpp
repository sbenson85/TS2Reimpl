// Compile check for the property-set structure pass (lane DM): the headers' static_asserts are the layout facts.
// No tests yet; the implementation lanes add theirs (notes/feedback/DM.md, "## Handoff") and may drop this file.
#include "cas/cTSBinInfoFactory.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetResource.h"
#include "gz/cGZPropertySetSerializer.h"
#include "ts/cTSSaveGameInfo.h"

// The string resource is a real class since lane EL (base offsets are not constant expressions: tests/gz_property_set_resource.cpp
// checks the faces at +0x1c and +0x40). The set's key text sits at +0x18 of the set (the resource constructor zeroes +0x34..+0x3c).
static_assert(offsetof(cGZPropertySetString, mKeyText) == 0x18, "the key text vector");
static_assert(sizeof(cGZPropertySetSerializerString) == 0x10, "the dispatcher, the resource's last base");
