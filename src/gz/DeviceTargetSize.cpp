#include "gz/DeviceTargetSize.h"

#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cSurfaceDX.h"

namespace nGZGraphic4 {

cDeviceDisplaySize *__cdecl GetDeviceTargetSize(cDeviceDisplaySize *out)
{
    cIDeviceDX *const device = GetCurrentDeviceCached();
    const cDisplayMode mode = static_cast<cIDeviceEnum *>(device->GetDeviceEnumConst())->GetAdapterDisplayMode();
    out->mWidth = mode.mWidth;
    out->mHeight = mode.mHeight;
    out->mAspect = mode.mAspect;
    out->mFormat = mode.mFormat;
    if (device->IsDeviceReady()) {
        cISurface *const back = static_cast<cISurface *>(device->GetBackBuffer(0));
        out->mWidth = back->GetWidth();
        out->mHeight = back->GetHeight();
        out->mFormat = static_cast<cIPixelFormat *>(back->GetPixelFormat());
    }
    return out;
}

} // namespace nGZGraphic4
