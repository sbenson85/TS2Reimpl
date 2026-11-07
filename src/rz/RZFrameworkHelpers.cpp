#include "rz/RZFrameworkHelpers.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/CommandParserTables.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma warning(disable : 4996) // _vsnprintf_l is what the game calls

namespace {

using ThisCallObject = uint32_t(__thiscall *)(void *);
using ThisCallSlot3 = void(__thiscall *)(void *, void *);

constexpr uint32_t kUnknownVTable = 0xda2fd0;        // cRZUnknown
constexpr uint32_t kFormatBufferSize = sizeof(ts2_rva_10cb840); // the 2 KB formatting buffer
constexpr uint32_t kWinProc4VTable = 0xe89534;       // cRZWinProc4

// std::string layout (MSVC 14.40): 16-byte buffer at +0, size at +0x10, capacity at +0x14.
// The message string starts at +4, after the vtable.
constexpr uint32_t kMessageOffset = 4;
constexpr uint32_t kMessageSizeOffset = 0x10;
constexpr uint32_t kMessageCapacityOffset = 0x14;
constexpr uint32_t kSmallCapacity = 15;

// Reads the next pointer of a list node (node + 4).
void *NodeNext(const void *node)
{
    return *reinterpret_cast<void *const *>(static_cast<const char *>(node) + 4);
}

} // namespace

RZUnknownSlot *RZUnknownSlot::Assign(void *object)
{
    void *old = mObject;
    if (object != old) {
        if (object) VirtualCall<ThisCallObject, 1>(object); // AddRef
        mObject = object;
        if (old) VirtualCall<ThisCallObject, 2>(old);          // Release
    }
    return this;
}

uint32_t RZGetGlobal10C9378()
{
    return uint32_t(uintptr_t(ts2_rva_10c9378));
}

bool RZUnknownQueryInterface::QueryInterfaceUnknown(uint32_t riid, void **ppv)
{
    if (riid != 1) return false;
    *ppv = this;
    VirtualCall<ThisCallObject, 1>(this); // AddRef
    return true;
}

RZStringSlot *RZStringSlot::Assign(const std::string &other)
{
    mString = other;
    return this;
}

void *RZFormattedErrorCtor(void *dst, const char *format, ...)
{
    auto *base = static_cast<char *>(dst);
    *reinterpret_cast<const void **>(base) = &ts2_rva_da5060; // nGZCommandParser::cError (an owned static)

    // The message string is initialised in place, as the original does: 16 zeroed bytes of buffer,
    // size 0, capacity 15. It does not run the std::string constructor.
    std::memset(base + kMessageOffset, 0, 16);
    *reinterpret_cast<uint32_t *>(base + kMessageOffset + kMessageSizeOffset) = 0;
    *reinterpret_cast<uint32_t *>(base + kMessageOffset + kMessageCapacityOffset) = kSmallCapacity;

    char *buffer = ts2_rva_10cb840;
    va_list args;
    va_start(args, format);
    _vsnprintf_l(buffer, kFormatBufferSize, format, nullptr, args);
    va_end(args);

    auto *message = reinterpret_cast<std::string *>(base + kMessageOffset);
    message->assign(buffer, std::strlen(buffer));
    return dst;
}

void RZListNodeVisitor::VisitNodesThenHead(void *head)
{
    if (!head) return;
    void *node = NodeNext(head);
    if (node != head) {
        // The next pointer is read before the call, so the callee may change or free the node.
        do {
            void *next = NodeNext(node);
            VirtualCall<ThisCallSlot3, 3>(this, node);
            node = next;
        } while (node != head);
    }
    VirtualCall<ThisCallSlot3, 3>(this, head);
}

RZUnknownLayout *RZUnknownLayout::Construct()
{
    mRefCount = 0;
    mVTable = GameAddress(kUnknownVTable);
    return this;
}

void RZWinProc4Vtable::SetVTable()
{
    mVTable = GameAddress(kWinProc4VTable);
}

RZWinProc4Vtable *RZWinProc4Vtable::Construct()
{
    mVTable = GameAddress(kWinProc4VTable);
    mRefCount = 0;
    return this;
}

uint32_t RZEnclosedFields::GetDwordMinus12() const
{
    return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const char *>(this) - 12);
}

uint8_t RZEnclosedFields::GetByteMinus8() const
{
    return *(reinterpret_cast<const uint8_t *>(this) - 8);
}
