// The size-built foundations helpers (/O1 modules): 0x141533, 0xc260bc, 0xc9bff9. Declarations: rz/FoundationsHelpers.h.
#include "rz/FoundationsHelpers.h"

#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject (0x1427b5)

namespace {

constexpr uint32_t kSettingsDelegateId = 0x9b3798f7, kSettingsDelegateIid = 0xfb3798e8;

} // namespace

KeyRefPair *KeyRefPair::Construct(const uint32_t *key, void *const *ref)
{
    mKey[0] = key[0];
    mKey[1] = key[1];
    mRef = *ref;
    if (mRef) VirtualCall<uint32_t(__thiscall *)(void *), 1>(mRef);
    return this;
}

ServiceRefHolder *ServiceRefHolder::AcquireSettingsDelegate()
{
    mObject = nullptr;
    if (void *service = GetServiceObject()) {
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(service, kSettingsDelegateId, kSettingsDelegateIid, &mObject);
    }
    return this;
}

namespace nGZLog {

LevelMap::iterator *__fastcall FindLogTableEntry(LevelMap *self, void *, LevelMap::iterator *out, const LogString &key)
{
    *out = self->find(key);
    return out;
}

} // namespace nGZLog
