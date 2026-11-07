#pragma once
// The pixel converter 0x8c6b10 and its 62 per-format loops (0x8c38e0 .. 0x8c5610), docs/subsystems/rendering.md R5b step 3.
// cGZDrawSystem::ConvertPixels (slot 6) and cGZBuffer's BltUnclipped call the converter; it walks a rectangle of pixels of
// one format into a rectangle of another (formats 4..9 only).
//
// Formats (from the loops' formulas, src/gz/GZConvertPixels.cpp): 4 is 1555 whose alpha bit is ignored (read as opaque,
// written set), 6 is 1555 that keeps it, 5 is 565, 7 is 4444 (16 bits each); 8 is 32-bit colour whose alpha byte is ignored
// (read as opaque, written 0xff), 9 is 32-bit colour that keeps it.
#include <cstddef>
#include <cstdint>

// What the converter hands a loop: one by-value argument block (cdecl, so 0x58 bytes on the stack, the caller pops it). The
// loop reads the width, height, destination and its stride, source and its stride and the key; the format ids, bytes per
// pixel and the six words between are carried along and unused by the loops.
struct GZConvertArgs {
    int32_t width;
    int32_t height;
    uint8_t *dst;
    int32_t dstStride;
    int32_t width2;
    int32_t height2;
    uint32_t dstFormat;
    uint32_t dstBytes;
    const uint8_t *src;
    int32_t srcStride;
    int32_t width3;
    int32_t height3;
    uint32_t srcFormat;
    uint32_t srcBytes;
    uint32_t unused[6];
    uint32_t key;    // the transparent colour, already and-ed with the source format's mask
    uint32_t hasKey; // 1 when `key` applies
};
static_assert(sizeof(GZConvertArgs) == 0x58, "GZConvertArgs: 22 words");

using GZConvertLoopFn = void(__cdecl *)(GZConvertArgs);

struct GZConvertLoop {
    uint32_t rva;
    GZConvertLoopFn fn;
};
constexpr size_t kGZConvertLoopCount = 62;
// The loops, by original address. Ours at that address are the hooks of src/hook/tables/GZConvertPixels.cpp.
extern const GZConvertLoop kGZConvertLoops[kGZConvertLoopCount];

// 0x8c6b10: false for a format outside 4..9 (either side); otherwise the rows are converted (bottom-up with both strides
// negated when the source and destination ranges overlap and the destination lies below the source in memory), true.
// The rectangle must not be empty: a width or height of zero or less is the callers' to rule out.
bool __cdecl GZConvertPixels(uint8_t *dst, int32_t dstStride, uint32_t dstFormat, const uint8_t *src, int32_t srcStride,
                             uint32_t srcFormat, int32_t width, int32_t height, const uint32_t *key);

// The hooks' entry for loop `Index` of kGZConvertLoops.
template <size_t Index>
void __cdecl GZConvertLoopHook(GZConvertArgs args)
{
    kGZConvertLoops[Index].fn(args);
}
