#pragma once
// nGZSceneGraph::cRenderGroupManager (docs/subsystems/scenegraph.md, row DLb): the render-group registry, the map of
// render-group names to bits a renderable node's layer mask uses (cRenderableNodeImpl, cShape, the neighbourhood
// occupant manager). A name gets the next bit the first time it is asked for; at most 32 names. Names compare without
// case. The constructor registers "Practical" as bit 0.
//
// Standalone shape: COM class made by the factory 0xaa37f0 (the COM director's creator; its only reference), which is
// hooked and makes ours, so the constructor 0xaa3700, the destructor 0xaa37a0 and every slot are in effect through it.
// The registry the game asks for is RenderGroupRegistry() 0xaeb370 (hooked): the scene graph service's slot 7, cached.
//
// Layout (0x1c bytes, vtable 0xe7585c, 7 slots):
//   +0 vtable, +4 reference count, +8 std::vector<std::string> the names by bit, +0x14 std::map<std::string, bit>
//   (case-insensitive)
#include "sg/cRenderGroupNameIterator.h" // cIRenderGroupRegistry

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nGZSceneGraph {

// The map's order: _stricmp.
struct RenderGroupNameLess {
    bool operator()(const std::string &a, const std::string &b) const;
};

class cRenderGroupManager final : public cIRenderGroupRegistry {
public:
    static constexpr uint32_t kIid = 0xc9737f2c;
    static constexpr uint32_t kMaxGroups = 32;

    cRenderGroupManager();                    // 0xaa3700
    virtual ~cRenderGroupManager() = default; // 0xaa37a0; slot 6 the deleting destructor 0xaa37d0
    cRenderGroupManager(const cRenderGroupManager &) = delete;
    cRenderGroupManager &operator=(const cRenderGroupManager &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xaa38d0 (ids 1 and kIid)
    uint32_t AddRef() override;                              // 1: shared 0x148fb0
    uint32_t Release() override;                             // 2: shared 0x4b1d00 (the deleting destructor at 0)
    // 3: 0xaa3900: the name's bit, a new name taking the next one; 0 once 32 names are registered (even for a known
    // name).
    uint32_t BitOfName(const char *name) override;
    uint32_t Count() override;                // 4: 0xaa38b0: the names' vector size
    const char *NameOfBit(uint32_t bit) override; // 5: 0xaa3860: null past the end or for an empty name

    uint32_t mRefCount = 0;                                     // +4
    std::vector<std::string> mNames;                            // +8
    std::map<std::string, uint32_t, RenderGroupNameLess> mBits; // +0x14
};
static_assert(sizeof(cRenderGroupManager) == 0x1c, "cRenderGroupManager size");

// RenderGroupRegistry() (declared in cRenderGroupNameIterator.h, defined in cRenderGroupManager.cpp) is 0xaeb370
// (cdecl, hooked): the scene graph service's (0xe5074b6b / 0x25074b4d) slot 7, asked once and cached (not counted).
// The service is not checked for null.
// The cache (the game's cell 0x10f905c, ours now): null until the first call.
extern cIRenderGroupRegistry *gRenderGroupRegistry;

struct cRenderGroupManagerAdapters {
    static cIRenderGroupRegistry *__cdecl Create(); // 0xaa37f0: a new manager (null when the allocation fails)
};

} // namespace nGZSceneGraph
