// nTSSG::cPortalLight (the light the diffusion manager builds for a portal, 0x6c bytes; lane 261110l, scenegraph
// LD1b): the helpers cDiffusionManager's slot 11 calls. See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "sg/cIDiffusionLightExtension.h"
#include "sg/SGNodeInterfaces.h"

#include <cmath>
#include <cstring>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kAngleOffset = 0xda20ec; // the float taken from the angle first

} // namespace

void cPortalLight::PlaceCorners()
{
    const double angle = static_cast<double>(SubS(mAngle, *GameAddress<const float>(kAngleOffset)));
    const float s = static_cast<float>(std::sin(angle));
    const float c = static_cast<float>(std::cos(angle));
    // The box's two points turned by the angle (comiss / cmova below: the first of a pair unless the other is smaller,
    // a NaN taking the first).
    const float t0 = SubS(MulS(mSourceMin[0], c), MulS(mSourceMin[1], s));
    const float t1 = AddS(MulS(mSourceMin[1], c), MulS(mSourceMin[0], s));
    const float t2 = SubS(MulS(mSourceMax[0], c), MulS(mSourceMax[1], s));
    const float t3 = AddS(MulS(mSourceMax[1], c), MulS(mSourceMax[0], s));
    mCorners[0] = AddS(mPosition[0], t0 > t2 ? t2 : t0);
    mCorners[1] = AddS(mPosition[1], t1 > t3 ? t3 : t1);
    mCorners[2] = AddS(mPosition[0], t2 > t0 ? t2 : t0);
    mCorners[3] = AddS(mPosition[1], t3 > t1 ? t3 : t1);
}

void cPortalLight::FillFromExtension(nGZSceneGraph::cICompositionTreeNode *start)
{
    if (!start) return;
    cIDiffusionLightExtension *held = nullptr;
    nGZSceneGraph::cICompositionTreeNode *node = start;
    for (;;) {
        for (nGZSceneGraph::cIExtension *extension = node->GetFirstExtension(); extension;
             extension = extension->GetNext()) {
            DropHeld(reinterpret_cast<void *&>(held));
            if (extension->QueryInterface(cIDiffusionLightExtension::kIid, reinterpret_cast<void **>(&held))) break;
        }
        nGZSceneGraph::cICompositionTreeNode *const parent = node->GetParent(); // also asked after a find
        if (held || !parent) break;
        node = parent;
    }
    if (!held) return;
    // The original passes its reference on to a second one: the first released, then the second counted (the same
    // object; the node keeps its extension alive meanwhile).
    ReleaseThroughBase(held);
    AddRefThroughBase(held);
    std::memcpy(mSourceMin, held->GetSourceMin(), 8);
    std::memcpy(mSourceMax, held->GetSourceMax(), 8);
    const float intensity = held->GetSourceIntensity();
    const float *const colour = held->GetSourceColor();
    mColor[0] = MulS(colour[0], intensity);
    mColor[1] = MulS(colour[1], intensity);
    mColor[2] = MulS(colour[2], intensity);
    mDirectional = held->GetSourceIsDirectional();
    mFalloff = held->GetSourceFalloff();
    mDirectionalFalloff = held->GetSourceDirectionalFalloff();
    mWallIntensityScale = held->GetSourceWallIntensityScale();
    mFloorIntensityScale = held->GetSourceFloorIntensityScale();
    mInterFloorFalloff = held->GetSourceInterFloorFalloff();
    ReleaseThroughBase(held);
}

} // namespace nTSSG
