// A light set's ambient map (lane 261111n, scenegraph leftovers): 0xb409f0, which copies an image resource's pixels into a
// cImageData for cLightingManager's room settings message (DoMessage 0x2d44b0). See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZPersistBufferResource.h"
#include "rz/ServiceGetters.h"
#include "sg/cImageData.h"

#include <cstdint>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::DivS;
using nGZSceneGraph::cIImageData;
using nGZSceneGraph::cImageData;

constexpr uint32_t kImageResourceType = 0x856ddbac; // the image resources' type (cGZPersistBufferResource)

// A counted interface released when it goes (on an exception too).
struct Counted {
    void *mObject = nullptr;
    Counted() = default;
    Counted(const Counted &) = delete;
    Counted &operator=(const Counted &) = delete;
    ~Counted()
    {
        if (mObject) RawSlot<2, void>(mObject); // receiver: cIGZUnknown (Release)
    }
};

cIImageData *Target(const void *const *slot) { return static_cast<cIImageData *>(const_cast<void *>(*slot)); }

// A colour channel: the byte over 255.
float Channel(uint32_t colour, int shift) { return DivS(float(int32_t((colour >> shift) & 0xffu)), 255.0f); }

} // namespace

uint8_t __cdecl LoadAmbientMapImage(uint32_t group, const void *instance, const void **slot)
{
    const uint32_t key[4] = {kImageResourceType, group, uint32_t(reinterpret_cast<uintptr_t>(instance)), 0};
    // The persist system's resource manager (slot 3; the service is not checked).
    void *const manager = Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
    Counted resource;
    uint8_t loaded = 0;
    // receiver: the resource manager (slot 3 GetResource: key, iid, out, flags 9, 0)
    if (manager && Call<3, uint8_t>(manager, static_cast<const void *>(key), cIGZPersistBufferResource::kIid,
                                    &resource.mObject, 9u, 0u)) {
        Counted image;
        auto *const persist = static_cast<cIGZPersistBufferResource *>(resource.mObject);
        if (persist->QueryBuffer(cIGZBuffer::kIid, &image.mObject)) {
            auto *const buffer = static_cast<cIGZBuffer *>(image.mObject);
            const uint32_t width = buffer->GetWidth();
            const uint32_t height = buffer->GetHeight();
            if (!*slot) {
                auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
                com->GetClassObject(cImageData::kClassId, cIImageData::kIid, const_cast<void **>(slot));
            }
            // ORIGINAL-BUG: the image is used unchecked (its creation may fail).
            Target(slot)->Init(int32_t(width), int32_t(height), 1, 2, 1.0f, 1);
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    cIImageData *const target = Target(slot); // read before the pixel, as the original
                    const uint32_t colour = buffer->GetPixel(x, y);
                    const float rgb[3] = {Channel(colour, 16), Channel(colour, 8), Channel(colour, 0)};
                    target->SetPixelOpaque(int32_t(x), int32_t(y), rgb, 0);
                }
            }
            loaded = 1;
        }
    }
    if (!loaded) {
        if (const void *const old = *slot) {
            *slot = nullptr;
            RawSlot<2, void>(old); // receiver: the image (cIGZUnknown Release)
        }
    }
    return loaded;
}

} // namespace nTSSG
