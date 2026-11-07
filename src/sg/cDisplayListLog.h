#pragma once
// The display list's log (cDisplayList::mLog, a cRZOFStream that OpenLog opens in text mode) as the scene and draw
// slots write to it: cRZTextOStream's slots. Shared by cDisplayList.cpp and cDisplayListDraw.cpp; the item log and the
// shader context's description are cDisplayListLog.cpp.
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZString;

namespace nGZSceneGraph {

struct DrawItem;

class cIDisplayListLog : public cIGZUnknown {
public:
    virtual void Flush() = 0;                                // 3
    virtual uint32_t SetSint8(int8_t value) = 0;             // 4
    virtual uint32_t SetUint8(uint8_t value) = 0;            // 5
    virtual uint32_t SetSint16(int16_t value) = 0;           // 6
    virtual uint32_t SetUint16(uint16_t value) = 0;          // 7
    virtual uint32_t SetSint32(int32_t value) = 0;           // 8
    virtual uint32_t SetUint32(uint32_t value) = 0;          // 9: the number as text
    virtual uint32_t SetSint64(int64_t value) = 0;           // 10
    virtual uint32_t SetUint64(uint64_t value) = 0;          // 11
    virtual uint32_t SetFloat32(float value) = 0;            // 12
    virtual uint32_t SetFloat64(double value) = 0;           // 13
    virtual uint8_t SetRZCharStr(const char *text) = 0;      // 14: the text
};

// 0xaa1640 (__cdecl; DrawPass's call): the first `count` items, each as labelled lines: its index and sort key, the
// material variant, the material's name and index, the geometry, the user data, the sub key's index and flags, the
// layer and depth, the node and its slot 88, the shader context (DescribeShaderContext), a table of its lights (one
// column per light: type, light, node's slot 88, the light's parameters; padded to line up), its ambient colour.
void __cdecl LogDrawItems(cIDisplayListLog *log, const DrawItem *items, int32_t count);

// 0xaf4c90 (__cdecl): the shader context's three state words described into `out`: the vertex format (the formats
// manager's own description), the vertex, texture and morph counts, the four flags and the lights per type.
void __cdecl DescribeShaderContext(cIGZString *out, const uint32_t state[3]);

} // namespace nGZSceneGraph
