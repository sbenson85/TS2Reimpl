// Compile check for the startup structure pass (lane EC): the headers' static_asserts are the layout facts. No tests
// yet; the implementation lanes add theirs (notes/feedback/EC.md, "## Handoff") and may drop this file.
#include "gz/FrameworkShape.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/StaticGetters.h"
#include "ts/cTSAppFWShape.h"

#include <type_traits>

// The shape classes are layout-compatible with the hook-shaped ones the verified code works on.
static_assert(sizeof(::cGZCOM) == 0x70, "cGZCOM: in the shape (lane EN), the hook-shaped struct is gone");
static_assert(sizeof(gz::cGZFramework) == sizeof(::cGZFramework), "cGZFramework: shape and hook shape agree");
static_assert(sizeof(gz::cCTGFramework) == sizeof(::cCTGFramework), "cCTGFramework: shape and hook shape agree");
static_assert(sizeof(rz::cRZSystemService) == sizeof(::cRZSystemService), "cRZSystemService: shape and hook shape agree");
static_assert(sizeof(rz::cRZCOMDllDirector) == sizeof(::cRZCOMDllDirector), "cRZCOMDllDirector: shape and hook shape agree");
static_assert(sizeof(gz::cGZApp) == sizeof(appbase::cGZApp), "cGZApp: shape and hook shape agree");
static_assert(sizeof(ts::cTSAppCommonImpl) == sizeof(appbase::cTSAppCommonImpl), "cTSAppCommonImpl: shape and hook shape agree");
static_assert(sizeof(ts::cTSAppFW) == 0x268, "cTSAppFW: the game object size (the hook-shaped struct is gone, lane FG)");

// The faces sit where the game's vtables are.
static_assert(offsetof(gz::cGZFramework, mCOM) == 8, "cIGZFrameWork +0, cIGZFrameWorkW32 +4");
static_assert(offsetof(gz::cGZFramework, mCOM) + offsetof(::cGZCOM, mServiceId) == 0x18,
              "the COM object's service id, framework +0x18");
// Lane EO: slot 13 of the base is ours (it calls _purecall, as the original's vtable entry), so the base constructor's
// hook can build a base object.
static_assert(!std::is_abstract_v<rz::cRZCOMDllDirector>, "the base director's slot 13 calls _purecall");
static_assert(!std::is_abstract_v<rz::cGZCOMDirector>, "the root director is complete");
// The game's vtable has _purecall at AddRef/Release/InitOptions; ours calls _purecall from those slots (lane EP), so
// the class can be built on its own (its constructor hook, the tests).
static_assert(!std::is_abstract_v<ts::cTSAppCommonImpl>, "cTSAppCommonImpl: _purecall bodies at slots 1, 2, 13");
static_assert(!std::is_abstract_v<ts::cTSAppFW>, "the application object is complete");
static_assert(!std::is_abstract_v<gz::cCTGFramework>, "the framework is complete");
static_assert(!std::is_abstract_v<::cGZCOM>, "cGZCOM is complete");
