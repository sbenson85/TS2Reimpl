#pragma once
// The memory-texture routines cImageManipDX's copy and normal-map slots hand to (lane OW, rendering): the textures that
// live in memory (nRZGraphic4::cMemBlockAsTexture, still game code) are copied level by level, converted between pixel
// formats and given their smaller mip levels by pure CPU code; the normal map is computed on the CPU too.
//
// Textures and surfaces here are engine objects reached by their interfaces (ifaces::cITexture, cILockableSurface); the
// memory ones are the only ones the two entry points are ever given (the callers check for the memory-texture interface
// first). A level routine takes two lockable surfaces (the target first), locks them (target for writing, source read
// only) and releases the locks before returning. Everything is cdecl, as the original's: the level routines and
// kernels are handed around as function pointers.
//
// The pieces of 0x184710 (CopyMemoryTextures) in the order it uses them:
//   * ChooseConverter (0x185a30) picks the routine that copies a source level into a target level: CopyLevel (the same
//     pixel format id), ConvertRgbToArgb (a 24-bit 8/8/8 source into a 32-bit 8/8/8 target, when the caller allows it)
//     or none;
//   * the DXT level decoders (0x185b90 / 0x185da0 / 0x185fb0) turn a compressed level into A8R8G8B8;
//   * ReduceLevelTwoTap (0x1854e0) and ReduceLevelFourTap (0x185770) make the next smaller level of a surface from
//     its rows: the reducer halves each row, the combiner halves the rows (src/gz/MipKernels.h), the row buffers are the
//     caller's scratch;
//   * ComputeNormalMapLevel (0x184ec0) is the per-level normal map kernel.
#include <cstdint>

namespace nGZGraphic4::memchain {

using LevelConverter = void(__cdecl *)(void *target, void *source);
// A row reducer / row combiner as the level reducers call them (the pixel kernels of MipKernels.h, whatever their layout):
// (destination row, source row or table of rows, count).
using RowKernel = void(__cdecl *)(void *destination, const void *source, int count);

void __cdecl CopyLevel(void *target, void *source);          // 0x184290
void __cdecl ConvertRgbToArgb(void *target, void *source);   // 0x1844b0
LevelConverter __cdecl ChooseConverter(void *targetTexture, void *sourceTexture, bool allowConversion); // 0x185a30
void __cdecl DecodeDxt1Level(void *target, void *source);    // 0x185b90
void __cdecl DecodeDxt3Level(void *target, void *source);    // 0x185da0
void __cdecl DecodeDxt5Level(void *target, void *source);    // 0x185fb0

// 0x1854e0: `outRows` rows of `outWidth` pixels into `target` from `sourceRows` rows of `sourceWidth` pixels of
// `source`, two source rows to an output row. A source one row high gives one output row (the row reduced or copied).
// `rowBytes` is the size of one pixel (a source one pixel wide is copied for that many bytes: the engine passes the
// pixel size here), `scratch` holds two rows of outWidth * rowBytes.
void __cdecl ReduceLevelTwoTap(void *target, int outWidth, int outRows, void *source, int sourceWidth, int sourceRows,
                               uint8_t *scratch, int rowBytes, RowKernel reducer, RowKernel combiner);
// 0x185770: the same with four source rows to an output row (the 1-3-3-1 kernel; the rows beyond the edges mirror), the
// scratch four rows.
void __cdecl ReduceLevelFourTap(void *target, int outWidth, int outRows, void *source, int sourceWidth,
                                uint32_t sourceRows, uint8_t *scratch, int rowBytes, RowKernel reducer,
                                RowKernel combiner);

// 0x184ec0: the normal map of a width x height 8-bit height map at `source` into `target` (A8R8G8B8, alpha 0xff):
// rows and columns wrap. Red and green are the two diagonal differences scaled by the reciprocal length, blue the
// amplitude term; all through the engine's reciprocal square root approximation, in single precision.
void __cdecl ComputeNormalMapLevel(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int width,
                                   int height, float amplitude);

// 0x184710: copies the levels of one memory texture into another, converting formats where the pair allows and making
// the target's further levels by halving (`filtered`: the 1-3-3-1 kernel, else the two-tap one). False when the shapes
// do not fit (non power of two sizes, an unsupported format, levels that do not line up).
bool __cdecl CopyMemoryTextures(void *source, void *target, bool filtered);
// 0x183500: the normal map of an 8-bit luminance memory texture into an A8R8G8B8 / X8R8G8B8 one of the same size and
// level count (every level on its own); false when the textures do not fit.
bool __cdecl ComputeMemoryNormalMap(void *source, void *target, float amplitude);

} // namespace nGZGraphic4::memchain
