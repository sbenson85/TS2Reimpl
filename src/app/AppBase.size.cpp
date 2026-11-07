#include "app/AppBase.h"

#include "game/game.h"
#include "game/import.h"
#include "rz/RZFrameworkHelpers.h"

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <new>

namespace appbase {
namespace {

constexpr uint32_t kIatSetUnhandledExceptionFilter = 0xd9a1b4;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124, kIatFreeLibrary = 0xd9a120;
constexpr uint32_t kKernelName = 0xda31e4, kDebuggerFunctionName = 0xda31f4;
constexpr uint32_t kDebuggerFunctionCache = 0x10cb074, kDebuggerPresentCache = 0x10cb078;

// A head node as the game makes them for its empty ordered containers: three pointers to itself and
// the "black, nil" flag word.
void *MakeHeadNode()
{
    auto *node = static_cast<void **>(::operator new(cTSAppCommonImpl::kHeadNodeSize));
    node[0] = node;
    node[1] = node;
    node[2] = node;
    *reinterpret_cast<uint16_t *>(&node[3]) = 0x0101;
    return node;
}

} // namespace

// ---- 0xc59f5a ----

cGZApp *cGZApp::Construct(uint32_t serviceId, const char *text)
{
    mSystem.Construct(serviceId, static_cast<uint32_t>(kPriority));
    mSystem.mVTable = GameAddress<void>(kVTable);
    mSystem.mBaseVTable = GameAddress<void>(kBaseVTable);
    mAppVTable = GameAddress<void>(kAppVTable);
    mName.Construct(text);
    return this;
}

// ---- 0xca4cb0 ----

cTSAppCommonImpl *cTSAppCommonImpl::Construct()
{
    mVTable = GameAddress<void>(kVTable);
    mHeadA = nullptr;
    mSizeA = 0;
    mHeadA = MakeHeadNode();
    mHeadB = nullptr;
    mSizeB = 0;
    try {
        mHeadB = MakeHeadNode();
    } catch (...) {
        // The original's unwind action empties and frees the first container (0xca4da0: erase the
        // tree under the head, then a sized delete of the head). It is empty here. This path needs an
        // allocation failure and is not exercised by the tests.
        ::operator delete(mHeadA, kHeadNodeSize);
        throw;
    }
    mWord14 = 0;
    return this;
}

// ---- 0x1c3cb0 ----

uint8_t __fastcall StringEqualsCString(const std::string *self, void *, const char *text)
{
    size_t length = std::strlen(text);
    return self->size() == length && std::memcmp(self->data(), text, length) == 0;
}

// ---- 0x1c02fb / 0x1c04a4 ----

std::set<uint32_t> *__fastcall ConstructIdSet(std::set<uint32_t> *self, void *)
{
    return new (self) std::set<uint32_t>();
}

uint8_t __fastcall InsertId(std::set<uint32_t> *self, void *, uint32_t id)
{
    return self->insert(id).second;
}

// ---- the exception reporter's small members ----

uint8_t cExceptionReporter::SetEnabled(uint32_t enable)
{
    auto *bytes = reinterpret_cast<uint8_t *>(this);
    using SetFilter = void *(__stdcall *)(void *);
    // Only the low byte of the argument is tested.
    if (static_cast<uint8_t>(enable) != 0) {
        if (bytes[0x19b] == 0) {
            bytes[0x19b] = 1;
            *reinterpret_cast<void **>(bytes + 0x1a8) = ImportFunction<SetFilter, kIatSetUnhandledExceptionFilter>()(GameAddress<void>(kFilter));
        }
    } else if (bytes[0x19b] != 0) {
        void *previous = *reinterpret_cast<void **>(bytes + 0x1a8);
        bytes[0x19b] = 0;
        ImportFunction<SetFilter, kIatSetUnhandledExceptionFilter>()(previous);
        *reinterpret_cast<void **>(bytes + 0x1a8) = nullptr;
    }
    return 1;
}

void cExceptionReporter::SetNames(const cRZString *first, const cRZString *second)
{
    auto *bytes = reinterpret_cast<uint8_t *>(this);
    reinterpret_cast<RZStringSlot *>(bytes + 0x24)->Assign(first->mString);
    reinterpret_cast<RZStringSlot *>(bytes + 0x44)->Assign(second->mString);
}

void cExceptionReporter::SetFlag19a(uint8_t value)
{
    reinterpret_cast<uint8_t *>(this)[0x19a] = value;
}

void cExceptionReporter::SetLevel(uint32_t index, uint8_t value)
{
    reinterpret_cast<uint8_t *>(this)[0x194 + index] = value;
}

// ---- 0x1bd019 ----

uint8_t __cdecl DebuggerPresentCached()
{
    void **function = GameAddress<void *>(kDebuggerFunctionCache);
    uint8_t *answer = GameAddress<uint8_t>(kDebuggerPresentCache);
    if (*function == nullptr) {
        HMODULE module = ImportFunction<HMODULE(__stdcall *)(const char *), kIatLoadLibraryA>()(GameAddress<const char>(kKernelName));
        if (module) {
            *function = ImportFunction<void *(__stdcall *)(HMODULE, const char *), kIatGetProcAddress>()(module, GameAddress<const char>(kDebuggerFunctionName));
            if (*function) {
                if (reinterpret_cast<uint32_t(__stdcall *)()>(*function)()) *answer = 1;
            }
            ImportFunction<BOOL(__stdcall *)(HMODULE), kIatFreeLibrary>()(module);
        }
    }
    return *answer;
}

} // namespace appbase
