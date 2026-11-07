#include "sg/cRenderGroupManager.h"

#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef

#include <cstring>
#include <new>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kSceneGraphServiceId = 0xe5074b6b;
constexpr uint32_t kSceneGraphServiceIid = 0x25074b4d;
constexpr uint32_t kGetRegistrySlot = 7;
constexpr uint32_t kFrameworkGetSystemServiceSlot = 5;

// The scene graph service as far as this file uses it.
class cISceneGraphService : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual cIRenderGroupRegistry *Registry() = 0; // 7
};

class cIFrameworkServices : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual bool GetSystemService(uint32_t id, uint32_t iid, void **out) = 0; // 5
};
static_assert(kFrameworkGetSystemServiceSlot == 5 && kGetRegistrySlot == 7, "the slots the interfaces above declare");

} // namespace

bool RenderGroupNameLess::operator()(const std::string &a, const std::string &b) const
{
    return _stricmp(a.c_str(), b.c_str()) < 0;
}

// ---- the manager --------------------------------------------------------------------------------------------------

cRenderGroupManager::cRenderGroupManager() { BitOfName("Practical"); }

bool cRenderGroupManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != kIid)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cRenderGroupManager::AddRef() { return ++mRefCount; }

uint32_t cRenderGroupManager::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

uint32_t cRenderGroupManager::BitOfName(const char *name)
{
    if (mBits.size() >= kMaxGroups) return 0;
    {
        const auto known = mBits.find(std::string(name));
        if (known != mBits.end()) return known->second;
    }
    const uint32_t bit = static_cast<uint32_t>(mBits.size());
    mBits[std::string(name)] = bit;
    if (mNames.size() <= bit) mNames.resize(bit + 1);
    mNames[bit] = name;
    return bit;
}

uint32_t cRenderGroupManager::Count() { return static_cast<uint32_t>(mNames.size()); }

const char *cRenderGroupManager::NameOfBit(uint32_t bit)
{
    if (bit < mNames.size() && !mNames[bit].empty()) return mNames[bit].c_str();
    return nullptr;
}

// ---- the registry -------------------------------------------------------------------------------------------------

cIRenderGroupRegistry *gRenderGroupRegistry = nullptr;

cIRenderGroupRegistry *RenderGroupRegistry()
{
    if (!gRenderGroupRegistry) {
        ScopedRef service{nullptr};
        if (auto *const framework = static_cast<cIFrameworkServices *>(GetServiceObject()))
            framework->GetSystemService(kSceneGraphServiceId, kSceneGraphServiceIid, &service.mObject);
        gRenderGroupRegistry = static_cast<cISceneGraphService *>(service.mObject)->Registry();
        service.ReleaseObject();
    }
    return gRenderGroupRegistry;
}

cIRenderGroupRegistry *__cdecl cRenderGroupManagerAdapters::Create()
{
    return new (std::nothrow) cRenderGroupManager;
}

} // namespace nGZSceneGraph
