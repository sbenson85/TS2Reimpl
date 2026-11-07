// The graphic system's cached manager getters (lane OW): src/gz/GraphicServices.h.
#include "gz/GraphicServices.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h" // cIDeviceDX
#include "ifaces/cIGraphicSystemDX.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "rz/ServiceGetters.h"

namespace nGZGraphic4 {

namespace {

using ifaces::cIGraphicSystemDX;
using ifaces::cIPixelFormatsManager;

constexpr uint32_t kGraphicSystemFirstId = 0x85522803, kGraphicSystemSecondId = 0xa55227bb;
constexpr uint32_t kDraw2DServiceId = 0x89027355, kDraw2DServiceIid = 0x4c9282f2, kDraw2DSlot = 13;

// The service's answer for the graphic system (a held reference; null when the service gave none).
void *QueryGraphicSystem()
{
    void *graphicSystem = nullptr;
    if (void *const service = GetServiceObject()) {
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(service, kGraphicSystemFirstId, kGraphicSystemSecondId, &graphicSystem);
    }
    return graphicSystem;
}

template <void **Cache, void *(cIGraphicSystemDX::*Getter)()> void *CachedGraphicManager()
{
    void **const cache = Cache;
    if (*cache == nullptr) {
        void *graphicSystem = QueryGraphicSystem();
        // ORIGINAL-BUG: the answer is used unchecked, so a service that gave none crashes here.
        *cache = (As<cIGraphicSystemDX>(graphicSystem)->*Getter)();
        ReleaseHeldObject(graphicSystem);
    }
    return *cache;
}

} // namespace

void *__cdecl GetDraw2DCached()
{
    void **const cache = &ts2_rva_10ca8fc;
    if (*cache == nullptr) {
        void *service = nullptr;
        if (void *const framework = GetServiceObject()) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, 5>(framework, kDraw2DServiceId, kDraw2DServiceIid, &service);
        }
        // ORIGINAL-BUG: the answer is used unchecked, so a service that gave none crashes here.
        *cache = VirtualCall<void *(__thiscall *)(void *), kDraw2DSlot>(service);
        ReleaseHeldObject(service);
    }
    return *cache;
}

cIDeviceDX *__cdecl GetCurrentDeviceCached()
{
    void **const cache = &ts2_rva_10ca87c;
    if (*cache == nullptr) {
        void *graphicSystem = QueryGraphicSystem();
        *cache = graphicSystem; // stored before the holder lets go, and not kept alive by it
        ReleaseHeldObject(graphicSystem);
    }
    cIGraphicSystemDX *const system = As<cIGraphicSystemDX>(*cache);
    const int32_t index = system->GetCurrentDevice();
    if (index < 0) return nullptr;
    return static_cast<cIDeviceDX *>(system->GetDevice(uint32_t(index)));
}

void *__cdecl GetCurrentDeviceEnum()
{
    cIDeviceDX *const device = GetCurrentDeviceCached();
    return device ? device->GetDeviceEnumConst() : nullptr;
}

void *__cdecl GetPixelFormatForColorType(uint32_t colorType)
{
    cIPixelFormatsManager *const formats = As<cIPixelFormatsManager>(GetPixelFormatsManagerCached());
    switch (colorType) {
    case 2: return formats->GetARGB(8, 8, 0, 0, 0);
    case 4: return formats->GetARGB(16, 0, 5, 5, 5);
    case 5: return formats->GetARGB(16, 0, 5, 6, 5);
    case 6: return formats->GetARGB(16, 1, 5, 5, 5);
    case 7: return formats->GetARGB(16, 4, 4, 4, 4);
    case 8: return formats->GetARGB(32, 0, 8, 8, 8);
    case 9: return formats->GetARGB(32, 8, 8, 8, 8);
    default: return nullptr;
    }
}

void *__cdecl GetPixelFormatsManagerCached() { return CachedGraphicManager<&ts2_rva_10ca830, &cIGraphicSystemDX::GetPixelFormatsManager>(); }
void *__cdecl GetImageManipCached() { return CachedGraphicManager<&ts2_rva_10ca990, &cIGraphicSystemDX::GetImageManip>(); }
void *__cdecl GetVertexFormatsManagerCached() { return CachedGraphicManager<&ts2_rva_10caa24, &cIGraphicSystemDX::GetVertexFormatsManager>(); }
void *__cdecl GetVertexDeclarationsManagerCached() { return CachedGraphicManager<&ts2_rva_10caa28, &cIGraphicSystemDX::GetVertexDeclarationsManager>(); }
void *__cdecl GetShaderProgramsManagerCached() { return CachedGraphicManager<&ts2_rva_10caa3c, &cIGraphicSystemDX::GetShaderProgramsManager>(); }

} // namespace nGZGraphic4
