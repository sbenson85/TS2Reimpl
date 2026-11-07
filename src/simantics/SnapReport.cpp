#include "simantics/SnapReport.h"

#include "game/vtable.h"
#include "rz/StaticGetters2.h"

#include <cstring>

namespace {

constexpr uint32_t kServiceLookupSlot = 32;  // slot of the cached singleton C: (code, message out)
constexpr uint32_t kObjectLookupSlot = 194;  // 0x308 / 4
constexpr uint32_t kObjectLookup2Slot = 195; // 0x30c / 4
constexpr int kAddRefSlot = 1;
constexpr size_t kNodeValueOffset = 0x10;

enum Kind : uint32_t { kKindService = 0, kKindObjectWithCode = 1, kKindObjectOnly = 2 };

} // namespace

uint8_t SnapErrorReport::LookupMessage(void **message)
{
    switch (mKind) {
    case kKindService: {
        void *service = GetCachedSingletonC();
        return static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void **), kServiceLookupSlot>(service, mCode, message));
    }
    case kKindObjectWithCode:
        if (!mObject) return 0;
        return static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void **), kObjectLookupSlot>(mObject, mCode, message));
    case kKindObjectOnly:
        if (!mObject) return 0;
        return static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *, void **), kObjectLookup2Slot>(mObject, message));
    default:
        return 0;
    }
}

SnapErrorReport *SnapStateReports::BuildReport(SnapErrorReport *out) const
{
    if (mReportCount == 0) {
        // The empty report: only the code, the object and the kind are written.
        out->mCode = 0;
        out->mObject = nullptr;
        out->mKind = 0;
        return out;
    }
    const uint8_t *const head = static_cast<const uint8_t *>(mReportsHead);
    const uint8_t *const first = *reinterpret_cast<const uint8_t *const *>(head); // the head's left link: the first node
    const SnapErrorReport *const value = reinterpret_cast<const SnapErrorReport *>(first + kNodeValueOffset);
    out->mCode = value->mCode;
    std::memcpy(out->mDetail, value->mDetail, sizeof(out->mDetail));
    out->mObject = value->mObject;
    if (out->mObject) VirtualSlot<uint32_t(__thiscall *)(void *)>(out->mObject, kAddRefSlot)(out->mObject);
    out->mKind = value->mKind;
    return out;
}
