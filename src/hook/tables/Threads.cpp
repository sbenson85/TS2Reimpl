#include "hook/hooks.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZThread.h"
#include "rz/cRZThreadSignal.h"

namespace {

// Lane LO (foundations F1): cRZThread's slots and thread routines, the SleepEx helper, cRZThreadSignal's signalling
// slots. The constructors and SetName are in ServicesB.cpp, the signal's constructor and destructors in nGZLog.cpp.
// cRZCallbackThread's slots and thread body; cRZCallbackTimer whole (slots 2, 4, 8 and 12 are folded bodies, not ours).
using StartStartable = uint32_t (cRZThread::*)(void *);
using StartRoutine = uint32_t (cRZThread::*)(cRZThread::Routine, void *);

const HookSpec kHooks[] = {
    {"cRZThread_Release", 0x1b9ebb, 14, 0xe088ac1a, reinterpret_cast<void *>(&cRZThread::Release)},
    {"cRZThread_StartStartable", 0x1b9c32, 32, 0xb54290f7, MemberAddress(static_cast<StartStartable>(&cRZThread::Start))},
    {"cRZThread_StartRoutine", 0x1b9bc1, 32, 0xc7359cec, MemberAddress(static_cast<StartRoutine>(&cRZThread::Start))},
    {"cRZThread_Exit", 0x1b9c9c, 17, 0x3bebf6d7, MemberAddress(&cRZThread::Exit)},
    {"cRZThread_Suspend", 0x1b9fb1, 19, 0x3fb58a2c, MemberAddress(&cRZThread::Suspend)},
    {"cRZThread_Resume", 0x1b9ec9, 19, 0xfa45672d, MemberAddress(&cRZThread::Resume)},
    {"cRZThread_Sleep", 0x1b9f77, 30, 0xb9e46338, MemberAddress(&cRZThread::Sleep)},
    {"cRZThread_Wake", 0x1b9ff1, 26, 0xfa6e19f8, MemberAddress(&cRZThread::Wake)},
    {"cRZThread_Join", 0x1b9df1, 32, 0xb19cf95e, MemberAddress(&cRZThread::Join)},
    {"cRZThread_GetPriority", 0x1b9cd2, 32, 0xf7d6b891, MemberAddress(&cRZThread::GetPriority)},
    {"cRZThread_SetPriority", 0x1b9edc, 32, 0x87fc4418, MemberAddress(&cRZThread::SetPriority)},
    {"cRZThread_IsRunning", 0x1b9db2, 32, 0xf44ed833, MemberAddress(&cRZThread::IsRunning)},
    {"cRZThread_DeletingDestructor", 0x1b9b7f, 31, 0x6654f45a, MemberAddress(&cRZThread::DeletingDestructor)},
    {"cRZThread_GetExitCode", 0x1b9cba, 24, 0xe7cd3650, MemberAddress(&cRZThread::GetExitCode)},
    {"cRZThread_GetThreadId", 0x1b9d42, 20, 0x0bd19da0, MemberAddress(&cRZThread::GetThreadId)},
    {"cRZThread_GetHandle", 0x1b9d30, 18, 0x0df1acfa, MemberAddress(&cRZThread::GetHandle)},
    {"cRZThread_SleepMicroseconds", 0x1b9f95, 28, 0xad21e5ab, reinterpret_cast<void *>(&cRZThread::SleepMicroseconds)},
    {"cRZThread_ExitWith", 0x1b9fd2, 29, 0xa65b11bc, reinterpret_cast<void *>(&cRZThread::ExitWith)},
    {"cRZThread_RunStartable", 0x1b9d82, 32, 0x7a3e6bed, reinterpret_cast<void *>(&cRZThread::RunStartable)},
    {"cRZThread_RunRoutine", 0x1b9d56, 32, 0xc0f538a8, reinterpret_cast<void *>(&cRZThread::RunRoutine)},
    {"cRZThreadSignal_Signal", 0x9ccb93, 32, 0x11627f04, MemberAddress(&cRZThreadSignal::Signal)},
    {"cRZThreadSignal_Pulse", 0x9ccb57, 18, 0xb4abb36e, MemberAddress(&cRZThreadSignal::Pulse)},
    {"cRZThreadSignal_Wait", 0x9ccbb9, 32, 0xb1d6319d, MemberAddress(&cRZThreadSignal::Wait)},
    {"cRZThreadSignal_IsValid", 0x9ccb7a, 11, 0xb362e498, MemberAddress(&cRZThreadSignal::IsValid)},
    {"cRZThreadSignal_Reset", 0x9ccbfb, 18, 0x0472c0ec, MemberAddress(&cRZThreadSignal::Reset)},
    {"cRZCallbackThread_QueryInterface", 0x9cfc6f, 32, 0xb88ea9ea, MemberAddress(&cRZCallbackThread::QueryInterface)},
    {"cRZCallbackThread_AddRef", 0x9cfc40, 18, 0x8caec38b, MemberAddress(&cRZCallbackThread::AddRef)},
    {"cRZCallbackThread_Release", 0x9cfc93, 32, 0xb39fccab, MemberAddress(&cRZCallbackThread::Release)},
    {"cRZCallbackThread_AddCallback", 0x9cfb5d, 32, 0x1f3318fa, MemberAddress(&cRZCallbackThread::AddCallback)},
    {"cRZCallbackThread_RemoveCallback", 0x9cfcb4, 32, 0x9516e3a3, MemberAddress(&cRZCallbackThread::RemoveCallback)},
    {"cRZCallbackThread_SetPriority", 0x9cfd09, 8, 0xaa8f5df9, MemberAddress(&cRZCallbackThread::SetPriority)},
    {"cRZCallbackThread_GetPriority", 0x9cfc67, 8, 0xa33911d2, MemberAddress(&cRZCallbackThread::GetPriority)},
    {"cRZCallbackThread_GetThreadId", 0x9cfc52, 21, 0x86ec377c, MemberAddress(&cRZCallbackThread::GetThreadId)},
    {"cRZCallbackThread_Run", 0x9cfd11, 32, 0x6fb1b948, reinterpret_cast<void *>(&cRZCallbackThread::Run)},
    {"cRZCallbackTimer_Construct", 0x9ccf32, 32, 0x5d0074f7, MemberAddress(&cRZCallbackTimer::Construct)},
    {"cRZCallbackTimer_Destruct", 0x9ccfa9, 32, 0xc29fe563, MemberAddress(&cRZCallbackTimer::Destruct)},
    {"cRZCallbackTimer_DeletingDestructor", 0x9cd02c, 31, 0xbc98f0bb, MemberAddress(&cRZCallbackTimer::DeletingDestructor)},
    {"cRZCallbackTimer_SetField10", 0x9cd1f3, 32, 0x37fe6797, MemberAddress(&cRZCallbackTimer::SetField10)},
    {"cRZCallbackTimer_SetPeriod", 0x9cd19d, 32, 0xc27ec157, MemberAddress(&cRZCallbackTimer::SetPeriod)},
    {"cRZCallbackTimer_StartRepeating", 0x9cd2b7, 32, 0x1300b469, MemberAddress(&cRZCallbackTimer::StartRepeating)},
    {"cRZCallbackTimer_StartOnce", 0x9cd249, 32, 0x276d9cbb, MemberAddress(&cRZCallbackTimer::StartOnce)},
    {"cRZCallbackTimer_Stop", 0x9cd32b, 32, 0x339a6ac2, MemberAddress(&cRZCallbackTimer::Stop)},
    {"cRZCallbackTimer_SetCallback", 0x9cd18a, 19, 0xcdf871d8, MemberAddress(&cRZCallbackTimer::SetCallback)},
    {"cRZCallbackTimer_SetUseService", 0x9cd04b, 32, 0xcf71f8f3, MemberAddress(&cRZCallbackTimer::SetUseService)},
    {"cRZCallbackTimer_OnTimer", 0x9cd0ec, 32, 0x4f1bcb9a, reinterpret_cast<void *>(&cRZCallbackTimer::OnTimer)},
};
REGISTER_HOOKS(kHooks);

} // namespace
