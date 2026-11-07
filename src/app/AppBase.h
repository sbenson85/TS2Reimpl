#pragma once
#include "rz/cGZAllocatorService.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <set>
#include <string>

// The bases and helpers of the application object (cTSAppFW, constructor 0xc26147; RVAs 1.18.0.187),
// The constructor and destructor chain are in app/AppCtor.h, the exception reporter in
// app/ExceptionReporter.h.
//
// cTSAppFW's layout (from the constructor): +0 cGZApp (cRZSystemService, with the cIGZApp face at
// +0x1c and its name string at +0x20), +0x40 cITSAppFW face, +0x44 cTSAppCommonImpl, +0x5c
// cIGZMessageTarget2 face, +0x60 a pointer cleared by the constructor, +0x64 a std::set<uint32_t>
// of ids, +0x6c.. flag bytes, +0x74 and +0x78.. mutex pointer and a list, +0x98 the exception
// reporter object (size at least 0x1b0), +0x244 a cRZString and +0x264 three flag bits.
namespace appbase {

// cGZApp: the application base. 0x40 bytes.
struct cGZApp {
    static constexpr uint32_t kVTable = 0xe9d118;       // primary (cIGZSystemService)
    static constexpr uint32_t kBaseVTable = 0xe9d150;   // the cRZUnknownMT face at +4
    static constexpr uint32_t kAppVTable = 0xe9d16c;    // cIGZApp face at +0x1c
    static constexpr int32_t kPriority = -2000000;      // 0xffe17b80

    cRZSystemService mSystem; // +0x00; its vtable words are replaced by the three above
    const void *mAppVTable;   // +0x1c
    cRZString mName;          // +0x20

    // 0xc59f5a (__thiscall, ret 8): cRZSystemService(id, kPriority), the three vtables, name = text.
    cGZApp *Construct(uint32_t serviceId, const char *text);
};

static_assert(sizeof(cGZApp) == 0x40, "cGZApp layout");
static_assert(offsetof(cGZApp, mAppVTable) == 0x1c, "cGZApp layout");
static_assert(offsetof(cGZApp, mName) == 0x20, "cGZApp layout");

// cTSAppCommonImpl: 0x18 bytes: vtable, two ordered containers (each a pointer to its head node and
// a size) and a word. The container types are not identified; only the empty state is made here:
// a 0x20-byte head node pointing at itself three times, with the "red/nil" flags word 0x0101 at +0xc.
struct cTSAppCommonImpl {
    static constexpr uint32_t kVTable = 0xeb5468;
    static constexpr uint32_t kHeadNodeSize = 0x20;

    const void *mVTable; // +0x00
    void *mHeadA;        // +0x04 head node of the first container
    uint32_t mSizeA;     // +0x08
    void *mHeadB;        // +0x0c head node of the second container
    uint32_t mSizeB;     // +0x10
    uint32_t mWord14;    // +0x14

    // 0xca4cb0 (__thiscall): sets the vtable, makes both head nodes (the first before the second).
    cTSAppCommonImpl *Construct();
};

static_assert(sizeof(cTSAppCommonImpl) == 0x18, "cTSAppCommonImpl layout");

// 0x1c3cb0 (__thiscall, ret 4): whether the string equals the C string (same length and bytes).
// Only al is defined.
uint8_t __fastcall StringEqualsCString(const std::string *self, void *edx, const char *text);

// 0x1c02fb (__thiscall): constructs an empty std::set<uint32_t> in raw memory (head node allocated),
// returns it. 0x1c02e4, the same without the return, is a library match (no entry).
std::set<uint32_t> *__fastcall ConstructIdSet(std::set<uint32_t> *self, void *edx);

// 0x1c04a4 (__thiscall, ret 4): inserts the id; returns whether it was new (al only).
uint8_t __fastcall InsertId(std::set<uint32_t> *self, void *edx, uint32_t id);

// The exception reporter object's small members (0x1bd3d8, 0x1bd467, 0x1bd48e, 0x1baff8, 0x1bd019).
struct cExceptionReporter {
    static constexpr uint32_t kFilter = 0x1bd687; // the unhandled-exception filter it installs

    // 0x1bd3d8 (__thiscall, ret 4): installs (enable true) or removes (false) the process-wide
    // unhandled-exception filter through SetUnhandledExceptionFilter, remembering the previous
    // filter at +0x1a8 and the state in the byte at +0x19b; does nothing when already in that state.
    // Returns 1 (al).
    uint8_t SetEnabled(uint32_t enable);

    // 0x1bd467 (__thiscall, ret 8): the two strings at +0x24 and +0x44 are assigned from the
    // std::strings inside the two cRZStrings given (each at +4).
    void SetNames(const cRZString *first, const cRZString *second);

    // 0x1bd48e (__thiscall, ret 4): the byte at +0x19a.
    void SetFlag19a(uint8_t value);

    // 0x1baff8 (__thiscall, ret 8): the byte at +0x194 + index.
    void SetLevel(uint32_t index, uint8_t value);
};

// 0x1bd019 (__cdecl, no arguments): whether IsDebuggerPresent can be had from kernel32 and says yes.
// The first call looks the function up (LoadLibraryA, GetProcAddress, FreeLibrary) and keeps the
// pointer in a game global (0x10cb074) and the answer in another (0x10cb078); later calls only read
// the answer. Only al is defined.
uint8_t __cdecl DebuggerPresentCached();

} // namespace appbase
