#pragma once
#include <cstdint>

#include "compgeom/CompGeom.h"

// nGZCompGeom's rigid transform, the quaternion helpers under it, and the boxes of a cuboid carried by a frame or a rigid
// transform (lane PH, scenegraph G0b; RVAs 1.18.0.187). The members are declared beside their types in CompGeom.h
// (RigidTransform3, BoundBox, Cuboid) and Frames.h (FrameMath::SetRotation); the free functions are here.
//
// NaNs: every product, sum and difference keeps the original's operand order (a NaN pair gives the first operand's NaN,
// quieted), as in Frames.cpp. The x87 dot product 0x18fbf0 has no order: of two NaNs the one with the larger significand.
namespace nGZCompGeom {

// 0x18fbf0 (__cdecl: a, b; the value on the x87 stack): (a1 b1 + a0 b0) + a2 b2 in x87 arithmetic, at the calling
// thread's x87 precision control, read on each call: at 53-bit (the CRT's default, the harness's) products of floats are
// exact in a double and the sums round to 53 bits, as doubles in SSE do; at 24-bit (the game's thread that created the
// Direct3D device: D3D9 sets it without D3DCREATE_FPU_PRESERVE) every product and sum is rounded to a 24-bit significand
// with the x87's exponent range. 64-bit precision is not modelled (computed as 53). Its callers store it with fstp to a
// float.
double __cdecl Dot87(const float *a, const float *b);

// 0x1a77f0 (__cdecl: out, q; returns out): the row-major 3x3 rotation matrix of the quaternion q (x, y, z, w), each element
// from products doubled by adding them to themselves, then summed in the original's order.
float *__cdecl QuaternionToMatrix(float *out, const float *q);

// 0x1a6ad0 (__cdecl: v, q; returns v): v turned by the inverse of the quaternion's rotation, in place (the transposed
// matrix of QuaternionToMatrix applied to v, each row ((a vx + b vy) + c vz) in the original's operand order).
float *__cdecl RotateByInverseQuaternion(float *v, const float *q);

// 0x1a6c40 (__cdecl: v, q; the result is in v): v turned by the quaternion's rotation, in place, with the operand order of
// every operation spelled out (what RigidTransform3::TransformPoint and RotateVector use). Not hooked here.
void RotateByQuaternionOrdered(float *v, const float *q);

// 0xae79e0 (__cdecl: a, b; lane VS): whether two rigid transforms nearly agree: the squared distance between the
// translations ((dx dx + dy dy) + dz dz, the differences a - b) below 1e-4, and the rotations' dot product (the products
// b a, summed ((y + x) + z) + w) within 1e-4 of +-1 (|(|dot| - 1)| < 1e-4). NaN anywhere gives false. Its one caller is the
// shape-reference node's rest-transform check (sg/cShapeRefNode.cpp, 0xa55430).
bool __cdecl RigidNearlyEqual(const RigidTransform3 *a, const RigidTransform3 *b);

} // namespace nGZCompGeom
