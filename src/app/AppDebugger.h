#pragma once
#include "rz/cGZAllocatorService.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <set>

// The log/debug setup of AddApplicationServicesHere (0xc267fb): 0xc29d5a configures the log service
// (nGZLog::cService, asked for from the framework by 0x1bf8fb), registers the two std adapters and the
// caption back end with it, creates the cheat console service, and loads the log-settings cheat files.
// RVAs are 1.18.0.187; slot N of a vtable is at byte offset 4N.
namespace appdebugger {

// ---- 0x1bf8fb: the log service holder ----
//
// A one-pointer local that holds the log service. 0x1bf8fb (__thiscall, returns this) clears it and, when
// the framework exists, asks it (slot 5) for the service with ids 0x09b69c4e and 0x09b69c46. Releasing
// it is the allocator holder's release (0x1be5d1, Release inside catch(...) built /EHa).
struct LogServiceHolder {
    static constexpr uint32_t kServiceId = 0x09b69c4e;
    static constexpr uint32_t kInterfaceId = 0x09b69c46;

    void *mService;

    LogServiceHolder *Acquire(); // 0x1bf8fb
    void Release();              // 0x1be5d1 (the same body as AllocatorServiceHolder::Release)
};
static_assert(sizeof(LogServiceHolder) == 4, "LogServiceHolder layout");

// ---- cRZOStream / cRZTextOStream constructors (0x10-byte base objects) ----
//
// 0x1b4f60 (cRZOStream): count 0, "native order" byte 1, +0xc 0, vtable 0xda29bc. 0x1b4fab
// (cRZTextOStream): that, then the vtable 0xda2b4c. Both __thiscall, return this. (The reimplemented
// cRZOStream in rz/cRZOStream.h covers the first 12 bytes; this base object is 0x10.)
struct OStreamBase {
    static constexpr uint32_t kOStreamVTable = 0xda29bc;
    static constexpr uint32_t kTextOStreamVTable = 0xda2b4c;
    static constexpr uint32_t kSize = 0x10;

    const void *mVTable;  // +0x00
    uint32_t mRefCount;   // +0x04 0
    uint8_t mNativeOrder; // +0x08 1
    uint8_t pad09[3];
    uint32_t mField0c;    // +0x0c 0

    OStreamBase *ConstructOStream();     // 0x1b4f60
    OStreamBase *ConstructTextOStream(); // 0x1b4fab
};
static_assert(sizeof(OStreamBase) == 0x10, "OStreamBase layout");

// ---- nGZLog::cRZStdAdapter (0x34 bytes): a text output stream over a standard handle ----
//
// 0xc9c2e2 (__thiscall, ret 8): (target, name). AddApplicationServicesHere makes target 0 named "StdOut"
// and target 1 named "StdErr".
struct cRZStdAdapter {
    static constexpr uint32_t kVTable = 0xeb47d8;
    static constexpr uint32_t kSize = 0x34;

    OStreamBase mBase;     // +0x00 (the vtable is replaced by kVTable)
    int32_t mTarget;       // +0x10
    cRZString mName;       // +0x14

    cRZStdAdapter *Construct(int32_t target, const char *name); // 0xc9c2e2
};
static_assert(sizeof(cRZStdAdapter) == 0x34, "cRZStdAdapter layout");
static_assert(offsetof(cRZStdAdapter, mName) == 0x14, "cRZStdAdapter layout");

// 0x1b9cad (__cdecl, one argument): stores GetCurrentThreadId() through the pointer.
void __cdecl StoreCurrentThreadId(uint32_t *out);

// ---- cTSWinCheatConsole (0xe0 bytes; service id 0x67780399, priority 0) ----
//
// 0xca9bf0 (__thiscall, returns this). The service-id set (+0x38) holds 0x8d504495 afterwards; the
// owner thread id is at +0x88. Only the fields the constructor writes are named.
struct cTSWinCheatConsole {
    static constexpr uint32_t kSize = 0xe0;
    static constexpr uint32_t kVTable = 0xeb5b44;          // +0x00
    static constexpr uint32_t kVTable4 = 0xeb5b7c;         // +0x04
    static constexpr uint32_t kVTable8 = 0xeb5b94;         // +0x08
    static constexpr uint32_t kStreamVTable = 0xeb5ba8;    // +0x0c (the cRZTextOStream part)
    static constexpr uint32_t kSystemVTable = 0xeb5c0c;    // +0x1c
    static constexpr uint32_t kUnknownVTable = 0xeb5c44;   // +0x20
    static constexpr uint32_t kServiceId = 0x67780399;
    static constexpr uint32_t kInitialId = 0x8d504495;     // the one id in the set
    static constexpr uint32_t kQueryIdA = 0xc51b11cb;      // QueryInterface ids AddApplicationServices uses
    static constexpr uint32_t kQueryIdB = 0x287fb697;

    const void *mVTable;          // +0x00
    const void *mVTable4;         // +0x04
    const void *mVTable8;         // +0x08
    OStreamBase mStream;          // +0x0c
    cRZSystemService mSystem;     // +0x1c .. +0x37
    std::set<uint32_t> mIds;      // +0x38 .. +0x3f
    uint32_t mField40;            // +0x40 0
    uint32_t mField44;            // +0x44 10
    uint32_t mFields48[4];        // +0x48..+0x57 0
    uint8_t mByte58;              // +0x58 1
    uint8_t pad59[3];
    uint32_t mFields5c[9];        // +0x5c..+0x7f 0
    uint16_t mWord80;             // +0x80 0
    uint8_t pad82[2];             // not written
    uint32_t mField84;            // +0x84 100
    uint32_t mOwnerThread;        // +0x88
    cRZString mStringA;           // +0x8c
    cRZCriticalSection mLock;     // +0xac
    uint32_t mFieldsB4[3];        // +0xb4..+0xbf 0
    cRZString mStringB;           // +0xc0

    cTSWinCheatConsole *Construct(); // 0xca9bf0
};
static_assert(sizeof(cTSWinCheatConsole) == 0xe0, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mSystem) == 0x1c, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mIds) == 0x38, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mByte58) == 0x58, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mWord80) == 0x80, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mOwnerThread) == 0x88, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mStringA) == 0x8c, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mLock) == 0xac, "cTSWinCheatConsole layout");
static_assert(offsetof(cTSWinCheatConsole, mStringB) == 0xc0, "cTSWinCheatConsole layout");

// ---- the allocator service's cached pointer (game statics 0x10c95d4 / guard 0x10c95d8) ----
//
// The same cache FreeThroughAllocatorService (0x197100) fills on first use: the allocator service from
// GetAllocatorService, in a function-local static.
//
// 0x1e7190 (__stdcall, ret 8; the pushed `this` register is not used): allocates count * 0x20 bytes
// through the cached service's slot 3. The second argument is unused. Used for the nodes of
// std::list<std::string> (0x20 bytes: next, prev, a std::string).
void *__stdcall AllocateListNodes(uint32_t count, uint32_t unused);
// 0x152b80 (__cdecl, three arguments; the first and third are unused): frees the second through the
// cached service's slot 4 (a std::string buffer in the callers' calls).
void __cdecl FreeStringBuffer(void *unusedA, void *mem, uint32_t unusedC);
// 0x1d5ea0 (__cdecl, two arguments: the list header and its sentinel node): destroys a
// std::list<std::string>'s nodes (the sentinel stays). The last node's next is cleared, then every node
// from the sentinel's next on gets its string emptied (buffer freed through 0x152b80 when it is
// heap-allocated) and is freed through 0x197100.
struct ListHeader {
    void *mHead; // the sentinel node
    uint32_t mSize;
};
void __cdecl DestroyStringListNodes(ListHeader *list, void *sentinel);

// ---- the log-settings cheat files ----
//
// The game's global object at 0x1107510 is a stream-like object with a pimpl: [global] is the object, [global+4]
// the impl whose fields +4, +8 are two pointers (the services found by 0xcb0760) and whose std::string
// at +0xc is the file name (0xcb0660).
struct LogCheatImpl {
    uint32_t mField0;   // +0x00
    void *mServiceA;    // +0x04 the log service (0xcb0640)
    void *mServiceB;    // +0x08 the other service (0xcb0650)
    std::string mName;  // +0x0c
};
struct LogCheatGlobal {
    static constexpr uint32_t kRva = 0x1107510;
    void *mObject;        // +0x00 the stream-like object
    LogCheatImpl *mImpl;  // +0x04
};

// 0xcb0640 / 0xcb0650 (__thiscall, ret 4): store the argument in the impl's +4 / +8.
void __fastcall SetCheatServiceA(LogCheatGlobal *self, void *edx, void *service);
void __fastcall SetCheatServiceB(LogCheatGlobal *self, void *edx, void *service);
// 0xcb0660 (__thiscall, ret 4): assigns the C string to the impl's name.
void __fastcall SetCheatName(LogCheatGlobal *self, void *edx, const char *name);
// 0xcb0760 (__thiscall, no arguments): asks the framework (slot 5) for two services: the log service (ids
// 0x09b69c4e / 0x09b69c46, stored at +4 by 0xcb0640) and another one (ids 0x0b6b619a / 0xeb6b6192, stored
// at +8 by 0xcb0650), then releases the local references (the log service with the /EHa catch of 0x1be5d1,
// the other with RZAutoRef::ReleaseObject).
void __fastcall LoadCheatServices(LogCheatGlobal *self, void *edx);
// 0xcb0690 (__cdecl): reads the string option 0x4d4896ba from the application's common implementation
// into a local, sets the global's name to it, loads the services.
void __cdecl PrepareCheatGlobal();
// 0xcaf560 (__thiscall, ret 4): calls slot 12 of the stream-like object's virtual base with (name, 0).
void __fastcall OpenCheatFile(LogCheatGlobal *self, void *edx, const char *name);
// 0xcaf060 (__cdecl, one argument: a cRZString): when the file exists, PrepareCheatGlobal, then
// OpenCheatFile on the global with the string's text.
void __cdecl LoadCheatFile(const cRZString *path);
// 0xcaf090 (__cdecl, no arguments): lists the application's paths (CommonImpl slot 5, id 0xcc9f7591),
// loads "LogSettings.cheat" from the first of them that has it, then "UserLogSettings.cheat" the same way,
// and frees the list.
void __cdecl LoadLogSettingsCheats();

// ---- 0xc29d5a: the log setup (__cdecl, no arguments) ----
void __cdecl SetupLogging();

} // namespace appdebugger
