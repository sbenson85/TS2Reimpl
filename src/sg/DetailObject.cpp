// nGZSceneGraph::GetDetailObject (lane TI). See the header.
#include "sg/DetailObject.h"

#include "game/vtable.h"

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kServiceFirstId = 0xe5074b6b, kServiceSecondId = 0x25074b4d;
constexpr unsigned kQuerySlot = 5, kDetailSlot = 9;

// The original's frame has one unwind action, ScopedRef::ReleaseObject on the holder, registered after the slot 5 call.
struct ReleaseHolder {
    ScopedRef *holder;
    ~ReleaseHolder() { holder->ReleaseObject(); }
};

} // namespace

void *__cdecl GetDetailObject()
{
    void **const cache = &ts2_rva_10f9028;
    if (*cache == nullptr) {
        ScopedRef held{nullptr};
        if (void *const service = GetServiceObject()) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, kQuerySlot>(service, kServiceFirstId, kServiceSecondId, &held.mObject);
        }
        ReleaseHolder release{&held};
        *cache = VirtualCall<void *(__thiscall *)(void *), kDetailSlot>(held.mObject);
    }
    return *cache;
}

} // namespace nGZSceneGraph
