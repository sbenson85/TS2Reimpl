#include "rz/cRZTextFile.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cstring>
#include <new>

namespace {

// Slots of cIGZFile calls the original makes through the vtable.
constexpr int kSlotLength = 7;
constexpr int kSlotSeekToPosition = 12;
constexpr int kSlotRead = 14;

// The first two bytes of a file, read as a little-endian uint16: FE FF (big-endian UTF-16) and
// FF FE (little-endian UTF-16).
constexpr uint16_t kBomBigEndian = 0xfffe;
constexpr uint16_t kBomLittleEndian = 0xfeff;

} // namespace

// Construction and destruction ------------------------------------------------------------------

cRZTextFile *cRZTextFile::Construct(const cIGZString &path)
{
    cRZFile::Construct(path);
    InitText();
    return this;
}

cRZTextFile *cRZTextFile::Construct(const char *path)
{
    cRZFile::Construct(path);
    InitText();
    return this;
}

// What both constructors do after the cRZFile part.
void cRZTextFile::InitText()
{
    mVTable = GameAddress(kVTable);
    mCharSize = 0;
    mByteOrderMark = false;
    // The original clears the whole std::string/u16string storage, then sets size and capacity
    // (the empty-string state); placement new below does the same from zeroed memory.
    std::memset(&mLine, 0, sizeof(mLine));
    new (&mLine) std::string();
    std::memset(&mWide, 0, sizeof(mWide));
    new (&mWide) std::u16string();
    mText.Construct();
    SetBuffering(kBufferSize, kBufferSize);
}

void cRZTextFile::Destruct()
{
    mText.Destruct();
    mWide.~basic_string();
    mLine.~basic_string();
    cRZFile::Destruct();
}

cRZTextFile *cRZTextFile::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// Open ------------------------------------------------------------------------------------------

bool cRZTextFile::Open(uint32_t access, uint32_t creation, uint32_t sharing)
{
    if (!cRZFile::Open(access, creation, sharing)) return false;
    // A file shorter than two bytes has no mark (and the read below is skipped).
    if (VirtualCall<uint32_t(__thiscall *)(void *), kSlotLength>(this) < 2) return true;
    // Read as a 16-bit value: the original zeroes a 4-byte local and compares only its low word.
    uint16_t mark = 0;
    if (!VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), kSlotRead>(this, &mark, 2)) return true;
    if (mark == kBomBigEndian || mark == kBomLittleEndian) {
        mCharSize = 2;
        mByteOrderMark = true;  // the read position stays after the mark
    } else {
        VirtualCall<int32_t(__thiscall *)(void *, int32_t), kSlotSeekToPosition>(this, 0);
    }
    return true;
}
