#include "simantics/PrimR.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"
#include "simantics/PrimRCommon.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>

// The thread-safe-statics helpers of our CRT, used on the game's guard variable (as in rz/cGZAllocatorService.cpp).
extern "C" __declspec(thread) int _Init_thread_epoch;

using namespace primr;

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x12
// ---------------------------------------------------------------------------------------------------------

namespace {

// The function-local static the handler keeps: a hash set of object type ids at 0x10dbf54 (guard 0x10dbf74, the
// element count at +8). Its insert (0x4209f0) is library code: the toolchain's set of 32-bit numbers over the same bytes,
// on operator new as the original's. Its constructor, find and end are still the original's.
constexpr uint32_t kSet = 0x10dbf54;
constexpr uint32_t kSetGuard = 0x10dbf74;
constexpr uint32_t kSetCount = 0x10dbf5c;

using TypeSet = std::unordered_set<uint32_t>;
static_assert(sizeof(TypeSet) == 0x20, "the original's set is 0x20 bytes");

using SetCtor = void *(__thiscall *)(void *);
using SetFind = void **(__thiscall *)(void *, void *, const uint32_t *);
using SetEnd = void **(__thiscall *)(void *, void *);

void EnsureSetConstructed()
{
    int *guard = GameAddressOpaque<int>(kSetGuard); // opaque: ICE with the constant form (game.h)
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                OriginalFunction<SetCtor, 0x57c410>()(GameAddress<void>(kSet));
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            // The exit-time destructor of the set (a function of the game's, registered with the CRT).
            atexit(OriginalAddress<void(__cdecl *)(), 0xd94670>());
            _Init_thread_footer(guard);
        }
    }
}

// The text file stream (cRZOFStream 0x1b4dd2 / 0x1b52d8, 0x18 bytes; rz/RZFileStreams.h) the debug log is written to,
// opened for appending.
struct LogStream {
    alignas(4) uint8_t mBytes[0x18];
    explicit LogStream(cRZString *path) { Stream()->Construct(*path->AsInterface(), 0, 0, 0, 1); }
    LogStream(const LogStream &) = delete;
    ~LogStream() { Stream()->Destruct(); }
    cRZOFStream *Stream() { return reinterpret_cast<cRZOFStream *>(mBytes); }
};

void WriteLog(void *stream, const char *text)
{
    OriginalFunction<void *(__cdecl *)(void *, const char *), 0x57cb60>()(stream, text);
}

} // namespace

uint32_t PrimRObject::Prim12(void *frame, const PrimParamView *param)
{
    const int32_t mode = static_cast<int16_t>(Word(param, 4));
    if (mode < 0 || mode > 2) Raise(9);

    int16_t id;
    if (mode == 0) id = Call<int16_t>(this, 47);
    else if (mode == 1) id = Call<int16_t>(frame, 12);
    else id = 0;

    void *table = Call<void *>(mContext, 26);
    void *object = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!object) return 0;

    // The ids of the object types that are never deleted this way, filled on first use.
    EnsureSetConstructed();
    void *set = GameAddress<void>(kSet);
    if (*GameAddress<int>(kSetCount) == 0) {
        static const uint32_t kTypes[] = {0xcc7b08f8, 0x6c7b0905, 0xcc203477, 0x865a6812, 0x81e6bef9, 0xd20c0aee, 0x39ccf441};
        for (const uint32_t type : kTypes) static_cast<TypeSet *>(set)->insert(type);
    }

    void *typeOwner = Call<void *>(object, 61);
    const uint32_t type = Call<uint32_t>(typeOwner, 28);
    uint8_t endIterator[4];
    uint8_t foundIterator[4];
    void **end = OriginalFunction<SetEnd, 0x5a9bd0>()(set, endIterator);
    void **found = OriginalFunction<SetFind, 0x57bf70>()(set, foundIterator, &type);
    if (*found != *end) return 0;

    uint8_t position[12];
    Call<void>(object, 41, position);
    void *manager = Call<void *>(this, 63);
    // 0x57e520: bit 1 of param byte +6, inverted.
    const uint32_t keep = ((Byte(param, 6) >> 1) & 1) ^ 1;
    Call<void>(manager, 18, static_cast<uint32_t>(static_cast<uint16_t>(id)), keep);

    if (Call<uint8_t>(object, 116)) {
        uint8_t second[12];
        Call<void>(object, 41, second);
        OriginalFunction<void(__thiscall *)(void *, void *, void *, uint32_t), 0x5416a0>()(this, object, position, 0);
    }

    if (Call<uint8_t>(mContext, 31)) {
        // The developer's deletion log ("OETreeDelete.xls" next to the folder the application gives).
        LocalString line;
        LocalString path;
        appinit::CommonImplSlot4(0x4d4896ba, reinterpret_cast<uint32_t>(&path.mString), 1, 0, 0xffffffffu);
        const char *fileName = GameAddress<const char>(0xdc42c8);
        path.mString.mString.append(fileName, std::char_traits<char>::length(fileName));
        LogStream stream(&path.mString);
        stream.Stream()->GetError();

        void *treeObject = Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 3)));
        void *stackObject = Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 12)));
        LocalString treeName;
        Call<void>(frame, 37, &treeName.mString);

        const char *treeText;
        if (treeObject) {
            const uint32_t treeOwnerName = Call<uint32_t>(treeObject, 92);
            const uint32_t myName = Call<uint32_t>(this, 92);
            const uint32_t node = static_cast<uint16_t>(Call<int16_t>(frame, 14));
            const int32_t treeId = Call<int16_t>(frame, 9);
            treeText = treeName.mString.ToChar();
            const int32_t objectId = Call<int16_t>(object, 47);
            const uint32_t objectName = Call<uint32_t>(object, 92);
            void *clock = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x2c8);
            const uint32_t tick = Call<uint32_t>(clock, 18);
            line.mString.Sprintf(GameAddress<const char>(0xdd7c40), tick, objectName, objectId, treeText, treeId, node, myName, treeOwnerName);
        } else {
            const uint32_t myName = Call<uint32_t>(this, 92);
            const uint32_t node = static_cast<uint16_t>(Call<int16_t>(frame, 14));
            const int32_t treeId = Call<int16_t>(frame, 9);
            treeText = treeName.mString.ToChar();
            const int32_t objectId = Call<int16_t>(object, 47);
            const uint32_t objectName = Call<uint32_t>(object, 92);
            void *clock = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x2c8);
            const uint32_t tick = Call<uint32_t>(clock, 18);
            line.mString.Sprintf(GameAddress<const char>(0xdd7cd0), tick, objectName, objectId, treeText, treeId, node, myName);
        }
        WriteLog(stream.mBytes, line.mString.ToChar());

        if (stackObject) {
            const int32_t stackId = Call<int16_t>(stackObject, 47);
            const uint32_t stackName = Call<uint32_t>(stackObject, 92);
            line.mString.Sprintf(GameAddress<const char>(0xdd7d30), stackName, stackId);
        } else {
            line.mString.mString.assign(GameAddress<const char>(0xd9e644));
        }
        WriteLog(stream.mBytes, line.mString.ToChar());
    }

    // The object is handed to the holder's slot 29 (looks like the removal queue) when it has that interface.
    Held removable;
    if (Call<uint8_t>(object, 0, 0x5a69d3d5u, &removable.mRef.mObject)) {
        void *holder = Call<void *>(mContext, 6);
        Call<void>(holder, 29, removable.Get());
    }

    // ORIGINAL-BUG: the answer of this query is not checked; when the object lacks the interface and is this
    // object itself, the call below reads through a null pointer.
    Held owner;
    Call<uint8_t>(object, 0, 0x99c7f5au, &owner.mRef.mObject);
    bool deletesSelf = false;
    if (id == Call<int16_t>(this, 47)) {
        void *sub = static_cast<uint8_t *>(owner.Get()) + 4;
        deletesSelf = Call<uint8_t>(sub, 33) != 0;
    }
    // `owner`, then `removable`, are released here.
    if (deletesSelf && (Byte(param, 6) & 1) == 0) return 2;
    return 1;
}
