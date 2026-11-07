#pragma once
// The device call log's Direct3D side (TS2HOST_DEVLOG; docs/ui-testing.md). The host hands the game a Direct3DCreate9
// whose IDirect3D9 and IDirect3DDevice9 are wrappers: every call is forwarded unchanged to the real object and logged
// as a canonical text record (host/shared/devlog.h) with its arguments. Nothing the game sees changes, except that
// IDirect3DDevice9::GetDirect3D returns the wrapper (as it would return the original's object).
//
// Resources (buffers, textures, surfaces, shaders, declarations) are the real objects, passed through untouched.
// Records name them by what is in them, so names agree between runs:
//   textures                     T:/C:<12 hex>         hash of the creation parameters and every upload so far
//   static buffers               V:/I:<12 hex>         hash of the creation parameters (format 2)
//   dynamic buffers              Vd<n>:/Id<n>:<12 hex> the n-th dynamic buffer created with those parameters
// Format 2 (devlog 2): every buffer's bytes are shadowed (static ones up to kShadowBudget in all), and a draw names
// what it draws rather than where it lies: the hash of each stream's vertices drawn and of the indices relative to
// the smallest one, without the base vertex, start index or stream offset. Where those come from a pool or a ring
// buffer (the game's static vertex pools, its dynamic rings), positions differ between runs that draw the same thing.
// Uploads to shadowed buffers show the bytes and the hash, not the offset or the DISCARD/NOOVERWRITE flags (which
// follow the ring's position). A buffer beyond the budget is drawn and uploaded with positions, as in format 1.
//   shaders, declarations        v:/p:/d:<12 hex>      hash of the byte code or the element list
//   surfaces                     S<n>: (render target), Z<n>: (depth), O<n>: (off-screen) by parameters and order;
//                                BB<n> back buffers; a texture's level as <texture name>/<width>x<height>
//   state blocks, queries        b<n>, q<n>            creation order
//   anything created behind the wrapper's back         <kind>?:<12 hex> by its description, or "?"
// Uploads are seen at Unlock: the Lock/Unlock (LockRect/UnlockRect) slots of each resource class's vtable are patched
// once (the first object of the class created through the wrapper), and the locked bytes are hashed before the real
// Unlock. Volume textures' contents are not tracked.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

namespace devlog {
class Recorder;
}

namespace d3dlog {

// Wraps `real` (taking over the caller's reference): the result logs to `recorder`, which must outlive it.
IDirect3D9 *Wrap(IDirect3D9 *real, devlog::Recorder &recorder);

// Puts the patched vtable slots back and forgets every tracked resource (tests).
void ResetForTests();

} // namespace d3dlog
