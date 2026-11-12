#pragma once
// The effects' plane collider, cTSPlaneCollider (lane YA, ran as RZ): a parallelogram in the effect's space that particles
// bounce off. cTSEffectsManager::Init (src/ts/cTSEffectsManager.cpp) builds it with the game's vtables (0xe8ce50, 0xe8ce70:
// QueryInterface / AddRef / Release are cRZUnknown's shared bodies) and keeps it under "plane"; its four own slots are ours,
// hooked at their RVAs (each body is referenced only from slots 3-6 of 0xe8ce50).
//
// The collider interface is cITSEffectsCollider's (src/ts/cTSCommonSystemObjects.h): slot 3 the state's size, slot 4 the
// state built for an emitter, slot 5 a particle's move tested against it, slot 6 a debug mesh of the plane.
#include "ts/EffectsTransform.h"

#include <cstdint>

namespace world_plane {

using fx::EffectsTransform;
using fx::Vector3;

// The state slot 4 builds (0x78 bytes): the plane through the parallelogram (unit normal, unless the edges are parallel, and
// d with normal . p + d = 0 on it), its corners in order around it, and a copy of the placement slot 6 draws through.
struct PlaneState {
    Vector3 normal;             // +0x00
    float d;                    // +0x0c
    Vector3 corner[4];          // +0x10 origin, origin + a, origin + a + b, origin + b
    EffectsTransform placement; // +0x40
};
static_assert(sizeof(PlaneState) == 0x78, "PlaneState layout");

// The parameters slot 4 reads from its first argument: the parallelogram's origin and its two edges at +0x1c.
struct PlaneParams {
    uint8_t unknown[0x1c];
    Vector3 origin; // +0x1c
    Vector3 edgeA;  // +0x28
    Vector3 edgeB;  // +0x34
};

// The slot bodies; `this` is not read by any of them.
class cTSPlaneCollider {
public:
    // 0xbdf690, slot 3: sizeof(PlaneState).
    uint32_t GetStateSize();
    // 0xbded70, slot 4 (ret 0x10): the parallelogram of `params` taken to the outer space by `transform` (each corner rotated
    // when the flag says so, scaled, translated; the normal only rotated), the normal from the edges' cross product, d from
    // the first corner, and `placement` copied into the state.
    void PrepareState(const PlaneParams *params, const EffectsTransform *transform, const EffectsTransform *placement,
                      PlaneState *state);
    // 0xbdea30, slot 5 (ret 0x18): a move from `from` to `to` that goes into the plane's front (its dot product with the
    // normal under -1e-5) and meets the plane raised by `radius` times the context's float at +0x4c within the move, at a
    // point inside the parallelogram (an even-odd count of the edges crossed by a ray in the plane's projection on its
    // dominant axis): `to` becomes that point, `normal` the plane's, and the result true; otherwise both untouched.
    bool Collide(const void *context, const PlaneState *state, float radius, const Vector3 *from, Vector3 *to, Vector3 *normal);
    // 0xbdf420, slot 6 (ret 0xc): a geometry node (class 0x7ba3838c) with one indexed mesh part (class 0x9bffc10d) holding
    // the corners placed by the state's placement as one group "layer"; `*out` gets the node, counted. Always true.
    bool Draw(uint32_t unused, const PlaneState *state, void **out);
};

} // namespace world_plane
