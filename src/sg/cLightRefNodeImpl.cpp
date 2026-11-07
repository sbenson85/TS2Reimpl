#include "sg/cLightRefNodeImpl.h"

#include "gz/cGZAny.h"          // gz::AnyUint32
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPropertySet.h" // cIGZPropertySetString, GZServiceString
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"

#include <new>

namespace nGZSceneGraph {

namespace {

// The bounds inflation: the configuration service's property set (framework slot 5), its "lightBoundsInflate" read into
// a uint32 any (0 when the set has no such property). With no framework the original calls through a null set (so does
// this).
float ReadBoundsInflate()
{
    RZAutoRef settings{nullptr};
    auto *const framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    if (framework != nullptr)
        framework->GetSystemService(cLightRefNodeImpl::kSettingsService, cLightRefNodeImpl::kSettingsIid,
                                    &settings.mObject);
    gz::AnyUint32 value(0);
    {
        GZServiceString key("lightBoundsInflate");
        static_cast<cIGZPropertySetString *>(settings.mObject)->GetPropertyValue(&key, &value);
    }
    settings.ReleaseObject();
    // Unsigned to float through double, as the original converts it.
    return static_cast<float>(static_cast<double>(value.mValue));
}

} // namespace

cLightRefNodeImpl::cLightRefNodeImpl() : mBoundsInflate(ReadBoundsInflate()) {}

cLightRefNodeImpl::~cLightRefNodeImpl()
{
    if (mHeld74 != nullptr) mHeld74->Release();
    if (mLight != nullptr) mLight->Release();
}

cLightRefNodeImpl *cLightRefNodeImpl::Construct() { return ::new (this) cLightRefNodeImpl; }

void cLightRefNodeImpl::Destruct() { this->~cLightRefNodeImpl(); }

} // namespace nGZSceneGraph
