// cRZThreadSignal's signalling slots (lane LO, foundations F1; src/rz/cRZThreadSignal.h). Size profile, as the
// original's. Every event call goes through its import slot (game/import_slots.h).
#include "rz/cRZThreadSignal.h"

#include "game/import_slots.h"
#include "rz/cRZMutex.h"

#include <cstring>

#define NOMINMAX
#include <windows.h>

namespace {
constexpr IatSlot kIatSetEvent = &ts2_rva_d9a10c, kIatPulseEvent = &ts2_rva_d9a3dc, kIatResetEvent = &ts2_rva_d9a3d8,
                  kIatWaitForSingleObject = &ts2_rva_d9a184;
}

bool cRZThreadSignal::Signal()
{
    cRZThreadSignalState *state = mState;
    if (state->mManualReset == 0) return ImportSlot<decltype(&::PulseEvent), kIatPulseEvent>()(state->mEvent) != 0;
    ImportSlot<decltype(&::SetEvent), kIatSetEvent>()(state->mEvent);
    return false;
}

bool cRZThreadSignal::Pulse() { return ImportSlot<decltype(&::PulseEvent), kIatPulseEvent>()(mState->mEvent) != 0; }

uint32_t cRZThreadSignal::Wait(uint32_t microseconds)
{
    const DWORD milliseconds = microseconds == 0xffffffffu ? INFINITE : microseconds / 1000u;
    const DWORD result = ImportSlot<decltype(&::WaitForSingleObject), kIatWaitForSingleObject>()(mState->mEvent, milliseconds);
    if (result == WAIT_FAILED) return 0;
    return result == WAIT_TIMEOUT ? 2 : 1;
}

bool cRZThreadSignal::IsValid() { return mState->mEvent != nullptr; }

bool cRZThreadSignal::Reset() { return ImportSlot<decltype(&::ResetEvent), kIatResetEvent>()(mState->mEvent) != 0; }

// The table an image-free program stores in place of 0xe61b7c (standalone/owned_statics.cpp aliases the owned static to
// it): our members slot for slot, each the function the hooked game runs for that image slot. Slots 0 and 7 are bodies
// the image folds with cRZMutex's slots 0 and 6 (0x9ccb85 Delete, 0x9ccb69 GetHandle: the same object shape, the
// handle first in the block at +4), so ours there are cRZMutex's members for those bodies, as in the image.
namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

extern "C" const void *ts2_own_vt_e61b7c[8] = {
    Member(&cRZMutex::Delete),                  // 0x9ccb85 (folded)
    Member(&cRZThreadSignal::Signal),           // 0x9ccb93
    Member(&cRZThreadSignal::Pulse),            // 0x9ccb57
    Member(&cRZThreadSignal::Wait),             // 0x9ccbb9
    Member(&cRZThreadSignal::IsValid),          // 0x9ccb7a
    Member(&cRZThreadSignal::DeletingDestructor), // 0x9ccb38
    Member(&cRZThreadSignal::Reset),            // 0x9ccbfb
    Member(&cRZMutex::GetHandle),               // 0x9ccb69 (folded)
};
