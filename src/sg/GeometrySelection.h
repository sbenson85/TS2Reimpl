// nGZSceneGraph::GeometrySelection (lane 261111g, scenegraph row group SU): the 16-byte list of a geometry data
// container's element or group indices that the builders make on the stack (0xa84dc0's caller family 0xa8cbc0 /
// 0xa8fdb0, cCompactorBuilder::Compact, cTangentSpaceBuilder::Build, three nTSSG callers). No RTTI and no vtable: the
// name is ours. The game has no out-of-line destructor for it (each caller frees the vector and releases the
// container inline). Members hooked in src/hook/tables/SGUtilities.cpp.
#pragma once

#include "sg/cGeometryDataContainerClass.h"

#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

struct GeometrySelection {
    // The "any" values of the filters: the identity's is its own constant, every other field's 0x7fffffff.
    static constexpr uint32_t kAnyIdentity = 0xbc530258u;
    static constexpr uint32_t kAny = 0x7fffffffu;

    cIGeometryDataContainer *mContainer = nullptr; // +0, counted
    std::vector<uint32_t> mIndices;                // +4

    // 0xaed500 (thiscall, ret 0x14): zeroed, then SelectElements.
    GeometrySelection(cIGeometryDataContainer *container, uint32_t identity, uint32_t blockFormat, uint32_t setFormat,
                      uint32_t repetition);
    // 0xaed570 (thiscall, ret 0x10): zeroed, then SelectGroups.
    GeometrySelection(cIGeometryDataContainer *container, uint32_t linkIndex, const char *name, uint32_t primType);
    GeometrySelection(const GeometrySelection &) = delete;
    GeometrySelection &operator=(const GeometrySelection &) = delete;

    // 0xaedbb0 (thiscall, ret 0x14): `container` held (counted before the old one is released, when it differs), the
    // list emptied, then the index of every element (slot 31 asked again after each, slot 32's header read from the
    // out buffer) whose identity, block format, set format and repetition match (each unless "any").
    void SelectElements(cIGeometryDataContainer *container, uint32_t identity, uint32_t blockFormat, uint32_t setFormat,
                        uint32_t repetition);
    // 0xaedcd0 (thiscall, ret 0x10): the same over the groups (slots 44 / 45): link index (unless "any"), name (case
    // blind; any when null) and primitive type (unless "any").
    void SelectGroups(cIGeometryDataContainer *container, uint32_t linkIndex, const char *name, uint32_t primType);

    // 0xaeda10 / 0xaedb10 (thiscall, ret 8; lane 261111l): the header of the selection's `i`th element (slot 32) or
    // group (slot 45), written to `out` by the container; `out` is returned whatever the slot answers. Nothing checks
    // `i` against the list.
    cGeometryElementHeader *ElementHeader(cGeometryElementHeader *out, uint32_t i);
    cGeometryGroupInfo *GroupInfo(cGeometryGroupInfo *out, uint32_t i);

    // The hooks' entry points at the constructors' RVAs (each returns this).
    GeometrySelection *ConstructElements(cIGeometryDataContainer *container, uint32_t identity, uint32_t blockFormat,
                                         uint32_t setFormat, uint32_t repetition);
    GeometrySelection *ConstructGroups(cIGeometryDataContainer *container, uint32_t linkIndex, const char *name,
                                       uint32_t primType);
};
static_assert(sizeof(GeometrySelection) == 0x10, "GeometrySelection size");

} // namespace nGZSceneGraph
