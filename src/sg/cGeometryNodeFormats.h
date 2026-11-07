// The vertex-format helpers the scene graph's geometry and shader setup share (cdecl free functions, hooked at their
// RVAs): the format a device can take, a one-stream variant of a format, and the copy of vertices from one format's
// layout into another's.
#pragma once

#include <cstdint>

namespace nGZGraphic4 {
class cIDeviceDX;
class cIVertexFormat;
} // namespace nGZGraphic4

namespace nGZSceneGraph {

// 0x18daa0: `format` rebuilt for `device` into *out (counted; the old value is not released): the streams are packed
// into the device's stream limit (enumeration limit 0xa; the last stream takes every stream past it), four-byte colours
// become the colour type when the device lacks processing cap 4. False when the device has no streams (nothing built)
// or a built stream is wider than its limit 0xe (the format stays in *out).
bool __cdecl MakeDeviceVertexFormat(nGZGraphic4::cIVertexFormat **out, nGZGraphic4::cIDeviceDX *device,
                                    nGZGraphic4::cIVertexFormat *format);

// 0x18da20 (lane VS): whether a geometry element's identity is one of the fifteen vertex element names the geometry
// build knows (position, normal, tangent, binormal, the bone indices and weights, and nine more). The answer is in al only (the original leaves the rest of eax as its compares did).
bool __cdecl IsKnownVertexElement(uint32_t identity);

// 0x18d990: the elements of `format`'s stream `fromStream` as a format of their own, all in stream `toStream`, into
// *out (counted; the old value is not released). Nothing happens when the stream is not in the format.
void __cdecl MakeStreamVertexFormat(nGZGraphic4::cIVertexFormat **out, nGZGraphic4::cIVertexFormat *format,
                                    int32_t toStream, int32_t fromStream);

// 0xc627e0: `count` vertices copied from `source` (vertex `sourceStart` on, laid out as `sourceFormat`'s stream
// `sourceStream`) to `dest` (vertex `destStart` on, as `destFormat`'s stream `destStream`), four bytes at a time, each
// destination element taken from the source element of the same usage and index. Gives the bytes written.
uint32_t __cdecl CopyVertexStream(const void *source, uint32_t sourceStart, nGZGraphic4::cIVertexFormat *sourceFormat,
                                  uint32_t sourceStream, void *dest, uint32_t destStart,
                                  nGZGraphic4::cIVertexFormat *destFormat, uint32_t destStream, uint32_t count);

} // namespace nGZSceneGraph
