#include "rz/cRZThreadSignal.h"

#include "game/import_slots.h"

#include <new>
#include <windows.h>

namespace {
using CreateEventFn = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
using CloseHandleFn = BOOL(WINAPI *)(HANDLE);
}

cRZThreadSignalState *cRZThreadSignalState::Construct()
{
    mManualReset = 0;
    mName.Construct();
    mLock.Construct();
    mEvent = nullptr;
    return this;
}

void cRZThreadSignalState::Destruct()
{
    mLock.Destruct();
    mName.Destruct();
}

void cRZThreadSignal::Destruct()
{
    cRZThreadSignalState *state = mState;
    mVTable = &ts2_rva_e61b7c;
    reinterpret_cast<CloseHandleFn>(ts2_rva_d9a17c)(state->mEvent);
    state->Destruct();
    ::operator delete(state, sizeof(cRZThreadSignalState));
}

cRZThreadSignal *cRZThreadSignal::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZThreadSignal));
    return this;
}

cRZThreadSignal *cRZThreadSignal::Construct(uint32_t manualReset)
{
    mVTable = &ts2_rva_e61b7c;
    mState = nullptr;
    auto *state = static_cast<cRZThreadSignalState *>(::operator new(sizeof(cRZThreadSignalState)));
    state->Construct();
    mState = state;
    state->mManualReset = manualReset;
    // The game's own import slot, so a harness that fakes imports sees the same call on both sides.
    state->mEvent = reinterpret_cast<CreateEventFn>(ts2_rva_d9a244)(nullptr, manualReset != 0, FALSE, nullptr);
    return this;
}
