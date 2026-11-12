#pragma once
// The light level of the game-derived lights (lane TL, scenegraph N2r): the two cLightT instantiations the game's own
// classes still stand on, as base parts.
//
//   cLightT<cIStandardLight, 0>  0xa45a40   cStandardLightBase's constructor 0xa46250 calls it (the ambient, directional,
//                                           point and spot constructors call that; the nTSSG lights reach it through
//                                           cAmbientFromLightMapLight, cHeightMapShadowLight and cHalfspacePointLight)
//   cLightT<cILight, 4000>       0x3c7cb0   nTSSG::cShadeLight (0x3c80b0) and cTerrainShadeLight (0x3c8120), and the two
//                                           other nTSSG constructors 0x3c10a0 / 0x2cfd60 that reach them
//
// Both are base-part constructors: the derived constructor sets the vbptr at +0x1c and calls them (the argument, always 0,
// is the most-derived flag), and stores its own vtables afterwards. Like cSGResource's 0xa3c270 (lane OE) the hook is our
// compiler's own constructor of the template, same thiscall + flag + `ret 4` ABI, which with flag 0 builds the cSGResource
// part, the referent node and its Impl exactly where the game's constructor does and stores our vtables (the derived
// constructor overwrites them). C++ cannot name a constructor's address: a C name stands for it (/alternatename in
// cLightLevels.cpp, a wrong decoration fails the link).
//
// The destructor 0xa463d0 (standard) stays the game's: it stores the game's vtables and tidies the same fields. The shade
// one 0x3c81c0 is ours since lane 261108n (sg/nTSSGLights.h, cShadeLightBase's), in effect through its callers: the two
// shade lights' constructors are hooked, so only our destructors run on them. (Both instantiations' constructors build the node the way tests/sg_lights_standard.cpp already checks.)
#include "game/owned.h"
#include "sg/SGLightTemplates.h"
#include "sg/cLinearFogLight.h" // SimpleLightEventRegistry
#include "sg/cStandardLight.h"

#include <cstdint>

namespace nGZSceneGraph {

// The behaviour cells of cAbstractBehaviorTypeT<cILight, cRefSingleThreaded> (the cILight copy 0x3c85f0's neighbour:
// its cell 0x10da038 is the one SGBehaviorCopies.h names LightCopy).
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10da038); // the cell below (game/owned.h; LightCopy)
struct cLightBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10da038;
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10da038; }
    static nGZEvent::cEventAttributeMap *Registry() { return SimpleLightEventRegistry(); }
};

using cStandardLightLevel = cLightT<cIStandardLight, cStandardLightBehaviorTraits>; // 0xa45a40
using cShadeLightLevel = cLightT<cILight, cLightBehaviorTraits>;                    // 0x3c7cb0

} // namespace nGZSceneGraph

// The hook targets: the addresses of the two constructors.
extern "C" const uint8_t cLightT_StandardLight_Constructor;
extern "C" const uint8_t cLightT_ShadeLight_Constructor;

// The C names are /alternatename aliases, which do not make the linker pull cLightLevels.obj out of the static library
// (only a definition does): a real symbol of the object that every user of the names asks for.
extern "C" const int cLightLevels_Anchor;
#pragma comment(linker, "/include:_cLightLevels_Anchor")
