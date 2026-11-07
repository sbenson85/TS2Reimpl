// nTSSG::cTSSGSystem: slots 84 and 85, a data file read through an unreplaced reader (lane JD). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "sg/SGLocalString.h"

#include <array>
#include <cstdint>
#include <list>
#include <vector>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// The reader both slots build on the stack: a scene-graph class nobody has replaced yet (0xf4 bytes in the original's
// frame), known only through the functions below.
struct alignas(8) FileReader {
    uint8_t bytes[0x100];
};
using ReaderFn = void(__thiscall *)(FileReader *);
void Construct(FileReader *reader) { OriginalFunction<ReaderFn, 0x3a6a30>()(reader); }
void Destruct(FileReader *reader) { OriginalFunction<ReaderFn, 0x3a6b10>()(reader); }
// Opens the file named by `path` (a cRZString; the second argument 0); false when it is not there.
bool Open(FileReader *reader, const cRZString *path)
{
    return OriginalFunction<uint8_t(__thiscall *)(FileReader *, const cRZString *, uint32_t), 0x3a8690>()(reader, path, 0) != 0;
}
// True when the file's header is current (its version fields both 0x41).
bool IsCurrent(FileReader *reader) { return OriginalFunction<uint8_t(__thiscall *)(FileReader *), 0x3a8660>()(reader) != 0; }

// The two record lists slot 84 reads: std::list nodes of 0xcc and 0xec bytes whose records own a vector of 76-byte
// entries at +0xa4 (their only member with a destructor).
template <size_t kSize> struct Record {
    uint8_t head[0xa4];
    std::vector<std::array<uint8_t, 76>> entries;
    uint8_t tail[kSize - 0xa4 - sizeof(std::vector<std::array<uint8_t, 76>>)];
};
using RecordA = Record<0xc4>;
using RecordB = Record<0xe4>;
static_assert(sizeof(RecordA) == 0xc4 && sizeof(RecordB) == 0xe4);
using ListFn = void(__thiscall *)(FileReader *);

// A reader built and destroyed around a scope.
struct ScopedReader {
    FileReader reader;
    ScopedReader() { Construct(&reader); }
    ~ScopedReader() { Destruct(&reader); }
    ScopedReader(const ScopedReader &) = delete;
    ScopedReader &operator=(const ScopedReader &) = delete;
};

bool OpenPath(FileReader *reader, const char *path)
{
    nTSSG::SGLocalString name(path);
    return Open(reader, &name.value);
}

} // namespace

namespace nTSSG {

void cTSSGSystem::LoadDataFile(const char *path)
{
    ScopedReader file;
    if (!OpenPath(&file.reader, path)) return;
    if (void *target = Call<41, void *>(this)) Call<8>(target);
    OriginalFunction<ListFn, 0x3a8720>()(&file.reader);
    OriginalFunction<ListFn, 0x3a92c0>()(&file.reader);
    std::list<RecordA> first;
    OriginalFunction<void(__thiscall *)(FileReader *, std::list<RecordA> *), 0x3a90b0>()(&file.reader, &first);
    OriginalFunction<void(__thiscall *)(FileReader *, std::list<RecordA> *), 0x3a7220>()(&file.reader, &first);
    std::list<RecordB> second;
    OriginalFunction<void(__thiscall *)(FileReader *, std::list<RecordB> *), 0x3a87c0>()(&file.reader, &second);
    OriginalFunction<void(__thiscall *)(FileReader *, std::list<RecordB> *), 0x3a71e0>()(&file.reader, &second);
    OriginalFunction<ListFn, 0x3a87f0>()(&file.reader);
}

int32_t cTSSGSystem::CheckDataFile(const char *path)
{
    ScopedReader file;
    if (!OpenPath(&file.reader, path)) return 2;
    return IsCurrent(&file.reader) ? -1 : 1;
}

} // namespace nTSSG
