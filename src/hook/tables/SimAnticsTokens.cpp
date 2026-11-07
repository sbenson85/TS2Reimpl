#include "hook/hooks.h"
#include "app/AppInit.h"
#include "rz/ServiceGetters.h"
#include "simantics/TokenExpander.h"
#include "simantics/TokenFinder.h"

namespace {

// String-token expansion (cTSObject::vf262, shared by cTSObject, cTSPerson and cEdithMTObject; lane AE) and the marker
// search it is built on. db/ records the expansion as 20,690 bytes; the entry's size here is the 32-byte CRC window.
const HookSpec kHooks[] = {
    {"cTSObject_vf262_ExpandStringTokens", 0x535130, 32, 0xd9a9971c, MemberAddress(&TokenObject::ExpandStringTokens)},
    {"TokenFinder_FindToken", 0x534d90, 32, 0x35ef838a, reinterpret_cast<void *>(&FindToken)},
    {"FormatPropertyText", 0x5307b0, 32, 0x2902f5a2, reinterpret_cast<void *>(&tokens::FormatPropertyText)},
    // The language service under its second cache (CachedServiceGetter instantiation, as 0x9c5a5d).
    {"GetSecondLanguageService", 0x20de26, 32, 0x7c652224,
     reinterpret_cast<void *>(&CachedServiceGetter<tokens::kSecondLanguageCacheRva, appinit::kLanguageServiceFirstId, appinit::kLanguageServiceSecondId>)},
};
REGISTER_HOOKS(kHooks);

} // namespace
