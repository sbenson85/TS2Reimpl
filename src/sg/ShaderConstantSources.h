#pragma once
// The shader program constants' sources (scenegraph M3a): what cApplyShaderProgramConstants::Apply writes into a float
// or integer binding's registers. RVAs are 1.18.0.187.
//
// One body for both files (the image has one template made twice): a switch on the binding's source (0..0x2a; any other
// writes nothing). The integer file gets the same values truncated to int32 (cvttss2si), except the words the image
// copies as they are: the matrix sources' bytes (6..0xe, 0x12, 0x13), a frame's rotation words (0..5, 0xf) and a bone's
// twelve words (0x10). The light indices of sources 0x28 and 0x29 are counts, as floats in the float file and as they
// are in the integer one.
//
// Sources, by number (dst: the first register's four words; count: the binding's registers):
//   0..5   a frame's 3x4 rows (rotation row, offset word) per register: the item's transform, its inverse, the context's
//          inverse frame (+0x110), the context's frame (+0xdc), the item's transform followed by the frame, and that
//          product's inverse
//   6..0xd 4x4 matrices: the projection's inverse, the projection, +0x1c4, the item's transform times the projection,
//          the event source's placement times the (outer) context's view (+0x184), the inverse of the item's transform
//          times the projection, the view, the view's inverse
//   0xe    the projection with element 9 offset by -value / 32768 times element 13
//   0xf    each bone's frame rows (three registers a bone); 0x10 each bone's rotation rows 0, 1 and offset
//   0x11   one scalar of the item's per register, at the binding's dimension
//   0x12   the detail object's matrix (slot 45); 0x13 that times the projected light direction's texture matrix
//   0x14   (0, frac(time), frame number, time); 0x15 / 0x17 the detail object's slot 46 / 47 vector
//   0x16   the 12 bytes before the item's holder table (the ambient colour)
//   0x18   the binding's four values; 0x19 the item's values at the binding's offset (channels 1..4) or those
//   0x1a   the colour event 0x8c798726 index 0; 0x1b a depth range's (-1 / (far - near), far / (far - near), 0, 1)
//   0x1c   the modulators' colours; 0x1d..0x2a the item's lights (directional, point and spot runs) in several layouts
#include "sg/SGShaderAttributes.h"
#include "sg/cApplyShaderProgramConstants.h"

#include <cstdint>

namespace nGZSceneGraph {

// 0xaba8f0 (__cdecl; SEH frame on an aligned stack): the float file's registers for `binding`.
void __cdecl FillFloatConstants(float *dst, const cApplyShaderProgramConstants::Binding *binding,
                                const cShaderApplyContext *ctx, const cShaderApplyItem *item);
// 0xab7cd0 (__cdecl): the integer file's registers for `binding`: the same sources as int32.
void __cdecl FillIntConstants(int32_t *dst, const cApplyShaderProgramConstants::Binding *binding,
                              const cShaderApplyContext *ctx, const cShaderApplyItem *item);

} // namespace nGZSceneGraph
