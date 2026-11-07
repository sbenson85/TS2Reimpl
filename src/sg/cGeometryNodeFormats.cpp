// The scene graph's vertex-format helpers (src/sg/cGeometryNodeFormats.h). The calls follow the original's order and
// count: the formats manager is fetched again for every call made to it, and a format is asked its stream and element
// counts again on every turn of the loops that read them (the tests compare the call streams).
#include "sg/cGeometryNodeFormats.h"

#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cVertexFormat.h"

#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIDeviceEnum;
using nGZGraphic4::cIVertexFormat;
using nGZGraphic4::cIVertexFormatsManager;
using nGZGraphic4::cVertexElement;

namespace {

constexpr uint32_t kLimitStreams = 0xa;      // the device's vertex stream count
constexpr uint32_t kLimitStreamBytes = 0xe;  // the widest stream it takes, in bytes per vertex
constexpr uint32_t kCapColourBytes = 4;      // the processing cap that takes four-byte colours as they are
constexpr uint32_t kTypeFourBytes = 4, kTypeColour = 5;

cIVertexFormatsManager *Formats()
{
    return static_cast<cIVertexFormatsManager *>(nGZGraphic4::GetVertexFormatsManagerCached());
}

cIDeviceEnum *Enumeration(cIDeviceDX *device) { return static_cast<cIDeviceEnum *>(device->GetDeviceEnum()); }

int32_t Signed(uint32_t v) { return static_cast<int32_t>(v); }

} // namespace

bool __cdecl IsKnownVertexElement(uint32_t identity)
{
    switch (identity) {
    case 0x114113c3: case 0x114113cd:
    case 0x3b83078b:                  // normal
    case 0x3bd70105:                  // bone weights
    case 0x5b830781:                  // position
    case 0x5cf2cfe1: case 0xdcf2cfdc:
    case 0x69d92b93:                  // binormal
    case 0x89d92ba0:                  // tangent
    case 0xbb8307ab: case 0xdb830795:
    case 0xcb6f3a6a: case 0xcb7206a1: case 0xeb720693:
    case 0xfbd70111:                  // bone indices
        return true;
    default:
        return false;
    }
}

bool __cdecl MakeDeviceVertexFormat(cIVertexFormat **out, cIDeviceDX *device, cIVertexFormat *format)
{
    const uint32_t streams = Enumeration(device)->GetLimit(kLimitStreams);
    if (streams == 0) return false;
    const int32_t last = Signed(streams - 1);
    const bool colours = Enumeration(device)->HasProcessingCap(kCapColourBytes) != 0;
    Formats()->BeginFormat();
    int32_t target = 0;
    for (int32_t s = 0; s < Signed(format->GetStreamCount()); ++s) {
        for (int32_t i = 0; i < Signed(format->GetElementCount(s)); ++i) {
            cVertexElement e;
            format->GetElement(&e, s, i);
            if (e.type == kTypeFourBytes && !colours) e.type = kTypeColour;
            Formats()->AddElement(target, e.type, e.usage, e.index);
        }
        if (target < last) ++target;
    }
    Formats()->EndFormat(out);
    const uint32_t widest = Enumeration(device)->GetLimit(kLimitStreamBytes);
    for (int32_t s = 0; s < Signed((*out)->GetStreamCount()); ++s)
        if ((*out)->GetStreamSize(s) > widest) return false;
    return true;
}

void __cdecl MakeStreamVertexFormat(cIVertexFormat **out, cIVertexFormat *format, int32_t toStream, int32_t fromStream)
{
    if (fromStream >= Signed(format->GetStreamCount())) return;
    Formats()->BeginFormat();
    for (int32_t i = 0; i < Signed(format->GetElementCount(fromStream)); ++i) {
        cVertexElement e;
        format->GetElement(&e, fromStream, i);
        Formats()->AddElement(toStream, e.type, e.usage, e.index);
    }
    Formats()->EndFormat(out);
}

uint32_t __cdecl CopyVertexStream(const void *source, uint32_t sourceStart, cIVertexFormat *sourceFormat, uint32_t sourceStream,
                                  void *dest, uint32_t destStart, cIVertexFormat *destFormat, uint32_t destStream, uint32_t count)
{
    const uint32_t stride = destFormat->GetStreamSize(destStream);
    const uint32_t words = stride >> 2;
    // Where each destination word comes from in a source vertex. ORIGINAL-BUG: nothing checks that the elements' sizes
    // fit the stride (an element past it writes past the table).
    std::vector<uint32_t> from(words);
    uint32_t *const table = from.data();
    const uint32_t elements = destFormat->GetElementCount(destStream);
    uint32_t k = 0;
    for (uint32_t i = 0; i < elements; ++i) {
        cVertexElement e, match;
        destFormat->GetElement(&e, destStream, i);
        sourceFormat->FindElement(&match, sourceStream, e.usage, e.index);
        const uint32_t size = destFormat->GetElementSize(destStream, i);
        uint32_t offset = sourceFormat->GetElementOffset(sourceStream, match.position);
        for (uint32_t n = size >> 2; n != 0; --n, offset += 4) table[k++] = offset;
    }
    // ORIGINAL-BUG: the source vertices are stepped by the destination's stride, not the source format's.
    const uint8_t *const src = static_cast<const uint8_t *>(source);
    uint8_t *to = static_cast<uint8_t *>(dest) + destStart * stride;
    uint32_t at = sourceStart * stride;
    for (uint32_t v = 0; v < count; ++v, to += stride, at += stride) {
        uint32_t *const words32 = reinterpret_cast<uint32_t *>(to);
        for (uint32_t w = 0; w < words; ++w) words32[w] = *reinterpret_cast<const uint32_t *>(src + at + table[w]);
    }
    return stride * count;
}

} // namespace nGZSceneGraph
