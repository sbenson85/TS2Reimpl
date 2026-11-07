// The output back end's small slots, the text formatter's name setter and the service's OnTick (nGZLog; first
// reimplemented as src/gz/GZLog.size.cpp, merged here by lane LZ).
#include "rz/nGZLog.h"

#include "game/vtable.h"

namespace nGZLog {

namespace {

constexpr uint32_t kIdUnknown = 1;
constexpr uint32_t kIdOutputBackEnd = 0x49b84afb;

} // namespace

bool cLogOutputBackEnd::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case kIdUnknown:
    case kIdOutputBackEnd:
        *out = this;
        VirtualCall<void(__thiscall *)(void *), kSlotAddRef>(this);
        return true;
    default:
        return false;
    }
}

uint32_t cLogOutputBackEnd::Release()
{
    // A signed compare: a count with the top bit set takes the delete path.
    if (static_cast<int32_t>(mRefCount) > 1) {
        --mRefCount;
        return mRefCount;
    }
    VirtualCall<void(__thiscall *)(void *, uint32_t), kSlotDelete>(this, 1);
    return 0;
}

bool cLogOutputBackEnd::AcceptFirst(uint32_t arg)
{
    if (!mEnabled || !mFormatter) return false;
    if (mFilter && !VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), kFilterSlotAcceptFirst>(mFilter, arg)) return false;
    return true;
}

bool cLogOutputBackEnd::Accept(uint32_t record)
{
    if (!mEnabled || !mFormatter) return false;
    if (mFilter && !VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), kFilterSlotAccept>(mFilter, record)) return false;
    return true;
}

std::string *cLogOutputBackEnd::SetName(const char *name)
{
    return &mText.mString.assign(name);
}

const char *cLogOutputBackEnd::Name() const
{
    return mText.mString.c_str();
}

void cLogOutputBackEnd::SetSynchronous(uint8_t value)
{
    mFlag = value;
}

bool cLogOutputBackEnd::FlushDue()
{
    // The count goes up before the division, so a trapping division (interval 0) leaves it advanced. Only al
    // (remainder == 0) is defined.
    const uint32_t old = mCounter;
    mCounter = old + 1;
    return static_cast<int32_t>(old) % mInterval == 0;
}

uint32_t cLogOutputBackEnd::GroupLevel(uint32_t group)
{
    void *filter = mFilter;
    if (!filter) return 0;
    void *levels = nullptr;
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void **), 0>(filter, kGroupLevelsIid, &levels);
    // ORIGINAL-BUG: the face is not checked: a filter without it faults here.
    const uint32_t result = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kGroupLevelsSlotGroupLevel>(levels, group);
    if (levels) VirtualCall<void(__thiscall *)(void *), 2>(levels);
    return result;
}

std::string *cLogFormatterText::SetName(const char *name)
{
    return &mName.mString.assign(name);
}

bool cLogService::OnTick(uint32_t arg)
{
    // Through the cIService face (slots 26 and 29), as the game dispatches them.
    cIService *service = this;
    service->SetFrame(arg);
    service->FlushOutputs();
    return true;
}

} // namespace nGZLog
