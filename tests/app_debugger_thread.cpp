// Differential tests for the cRZThread destructor chain (src/rz/cRZThreadRelease.*):
//   0x1b9b47 the state's destructor body, 0x1b9e7b the state's Release, 0x1b9b71 the thread's destructor body.
//
// The state's lock gets a recording vtable (lock/unlock only), so the order "lock, count, unlock, destroy" is
// visible. The original closes the thread handle through the game's import (IAT 0xd9a17c), replaced here by a
// function that logs and closes the real event; ours calls kernel32 directly, so both sides get a real event
// and the test looks at whether it is still open afterwards. Frees are recorded across each side's own call
// with a FreeRecorder.
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/cRZThreadRelease.h"

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace appdebugger;

constexpr uint32_t kIatCloseHandle = 0xd9a17c;
int gCloseCalls = 0;

BOOL WINAPI FakeCloseHandle(HANDLE h)
{
    ++gCloseCalls;
    return CloseHandle(h);
}

bool Open(HANDLE h)
{
    DWORD flags;
    return h && GetHandleInformation(h, &flags);
}

struct Side {
    cRZThreadState *state;
    HANDLE handle;
    const char *nameBuffer; // the name's heap buffer, or null
    void *lockBlock;
};

Side MakeState(uint32_t count, bool withHandle, const std::string &name)
{
    auto *state = static_cast<cRZThreadState *>(::operator new(sizeof(cRZThreadState)));
    std::memset(static_cast<void *>(state), 0xab, sizeof(*state));
    state->Construct();
    state->mRefCount = count;
    state->mName.mString = name;
    HANDLE handle = withHandle ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
    state->mField4 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle));
    Side side{state, handle, name.size() > 15 ? state->mName.mString.c_str() : nullptr, state->mLock.mBlock};
    return side;
}

void *gLockVTable[adt::kMaxSlots];

bool Freed(const difftest::FreeRecorder &rec, const void *p)
{
    return rec.Freed(p);
}

} // namespace

DIFF_TEST(AppDebugger_ThreadStateRelease, 0x1b9e7b)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(cRZThreadState *)>(0x1b9e7b);
    adt::Def(adt::kLock, 1, 0, "", [](void *, const uint32_t *) { return 1u; });
    adt::Def(adt::kLock, 2, 0, "", [](void *, const uint32_t *) { return 0u; });
    auto table = adt::BuildVTable(adt::kLock);
    adt::gKnownVTables[table.data()] = adt::kLock;
    void **iat = ctx.image.At<void *>(kIatCloseHandle);
    void *savedIat = *iat;
    *iat = reinterpret_cast<void *>(&FakeCloseHandle);
    int destroyed = 0;
    const std::string longName(40, 'n');
    for (uint32_t count : {0u, 1u, 2u, 3u, 100u, 0x80000000u, 0xffffffffu}) {
        for (bool withHandle : {false, true}) {
            for (const std::string &name : {std::string(), std::string("short"), longName}) {
                uint32_t result[2];
                std::vector<std::string> logs[2];
                bool freed[2], closed[2];
                uint32_t countAfter[2] = {0, 0};
                for (int side = 0; side < 2; ++side) {
                    Side s = MakeState(count, withHandle, name);
                    s.state->mLock.mVTable = table.data();
                    gCloseCalls = 0;
                    adt::ResetSide();
                    difftest::FreeRecorder rec;
                    result[side] = side == 0 ? orig(s.state) : ReleaseThreadState(s.state, nullptr);
                    rec.Stop();
                    bool gone = Freed(rec, s.state);
                    freed[side] = gone;
                    // With a FreeRecorder the name buffer and lock block can be checked here too (exact, unlike HeapValidate).
                    if (gone && s.nameBuffer && !Freed(rec, s.nameBuffer)) ctx.Fail("side %d: the name buffer was not freed", side);
                    if (gone && !Freed(rec, s.lockBlock)) ctx.Fail("side %d: the lock block was not freed", side);
                    if (!gone && (Freed(rec, s.lockBlock) || (s.nameBuffer && Freed(rec, s.nameBuffer)))) ctx.Fail("side %d: a member block was freed without destruction", side);
                    logs[side] = adt::gLog;
                    closed[side] = withHandle && !Open(s.handle);
                    if (!gone) countAfter[side] = s.state->mRefCount;
                    if (gone) {
                        if (side == 0 && withHandle && gCloseCalls != 1) ctx.Fail("original closed %d times", gCloseCalls);
                        if (side == 0 && !withHandle && gCloseCalls != 0) ctx.Fail("original closed a null handle");
                    } else {
                        s.state->mLock.mVTable = GameAddress(cRZCriticalSection::kVTable);
                        s.state->mLock.Destruct();
                        s.state->mName.Destruct();
                        if (s.handle) CloseHandle(s.handle);
                        ::operator delete(s.state);
                    }
                    if (gone && s.handle && Open(s.handle)) CloseHandle(s.handle);
                }
                ++ctx.cases;
                if (result[0] != result[1] || freed[0] != freed[1] || closed[0] != closed[1] || countAfter[0] != countAfter[1])
                    ctx.Fail("count %u handle %d name %zu: result %u/%u freed %d/%d closed %d/%d count after %u/%u", count, withHandle, name.size(), result[0], result[1], freed[0], freed[1], closed[0], closed[1], countAfter[0], countAfter[1]);
                if (logs[0] != logs[1]) ctx.Fail("count %u: calls differ: %s", count, adt::FirstDifference(logs[0], logs[1]).c_str());
                if (logs[0].size() != 2 || logs[0][0] != "lock.1()" || logs[0][1] != "lock.2()") ctx.Fail("count %u: lock calls %s", count, adt::Joined(logs[0]).c_str());
                destroyed += freed[0];
                if (count > 1 && (freed[0] || result[0] != count - 1)) ctx.Fail("count %u: expected a decrement", count);
                if (count <= 1 && (!freed[0] || result[0] != 0)) ctx.Fail("count %u: expected destruction", count);
            }
        }
    }
    *iat = savedIat;
    adt::gKnownVTables.erase(table.data());
    if (destroyed < 10) ctx.Fail("destruction path reached %d times", destroyed);
}

DIFF_TEST(AppDebugger_ThreadStateDestroy, 0x1b9b47)
{
    auto orig = ctx.Original<void(__thiscall *)(cRZThreadState *)>(0x1b9b47);
    void **iat = ctx.image.At<void *>(kIatCloseHandle);
    void *savedIat = *iat;
    *iat = reinterpret_cast<void *>(&FakeCloseHandle);
    const std::string longName(40, 'n');
    for (bool withHandle : {false, true}) {
        for (const std::string &name : {std::string(), longName}) {
            bool closed[2];
            uint32_t vtable[2];
            for (int side = 0; side < 2; ++side) {
                Side s = MakeState(5, withHandle, name);
                gCloseCalls = 0;
                difftest::FreeRecorder rec;
                if (side == 0) orig(s.state);
                else DestroyThreadState(s.state, nullptr);
                rec.Stop();
                closed[side] = withHandle && !Open(s.handle);
                vtable[side] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.state->mName.mVTable));
                if (s.nameBuffer && !Freed(rec, s.nameBuffer)) ctx.Fail("side %d: the name buffer was not freed", side);
                if (!Freed(rec, s.lockBlock)) ctx.Fail("side %d: the lock block was not freed", side);
                if (Freed(rec, s.state)) ctx.Fail("side %d: the block itself must stay", side);
                if (s.handle && Open(s.handle)) CloseHandle(s.handle);
                ::operator delete(s.state);
            }
            ++ctx.cases;
            if (closed[0] != closed[1] || vtable[0] != vtable[1] || closed[0] != withHandle) ctx.Fail("handle %d: closed %d/%d, name vtable %x/%x", withHandle, closed[0], closed[1], vtable[0], vtable[1]);
        }
    }
    *iat = savedIat;
}

DIFF_TEST(AppDebugger_ThreadDestroy, 0x1b9b71)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(cRZThread *)>(0x1b9b71);
    for (uint32_t count : {1u, 2u, 5u}) {
        uint32_t result[2], countAfter[2] = {0, 0};
        const void *vtable[2];
        for (int side = 0; side < 2; ++side) {
            Side s = MakeState(count, false, "t");
            cRZThread thread;
            thread.mVTable = reinterpret_cast<const void *>(0x12345678);
            thread.mState = s.state;
            result[side] = side == 0 ? orig(&thread) : DestroyThread(&thread, nullptr);
            vtable[side] = thread.mVTable;
            if (count > 1) {
                countAfter[side] = s.state->mRefCount;
                ::operator delete(s.state);
            }
        }
        ++ctx.cases;
        if (result[0] != result[1] || vtable[0] != vtable[1] || countAfter[0] != countAfter[1] || vtable[1] != GameAddress(cRZThread::kVTable))
            ctx.Fail("count %u: result %u/%u vtable %p/%p count after %u/%u", count, result[0], result[1], vtable[0], vtable[1], countAfter[0], countAfter[1]);
    }
}
