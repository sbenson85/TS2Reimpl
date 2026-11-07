#include "simantics/SimGeometry.h"
#include "simantics/PrimR.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "game/game.h"
#include "game/original.h"
#include "simantics/PrimRCommon.h"

#include "rz/GameAllocator.h"

#include <cstring>
#include <functional>
#include <unordered_set>

using namespace primr;

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x0e
// ---------------------------------------------------------------------------------------------------------

namespace {

// The container helpers the handler walks its object list with (std::list-like iterators, a hash set of numbers,
// a vector); all of them are the original's (not replaced yet). An iterator is one pointer.
using IterOut = uint32_t *;
inline uint32_t ListBegin(void *list)
{
    uint32_t out;
    OriginalFunction<IterOut(__thiscall *)(void *, IterOut), 0x5a98e0>()(list, &out);
    return out;
}
inline uint32_t ListEnd(void *list) // 0x5a9bd0 (also used on the visited set)
{
    uint32_t out;
    OriginalFunction<IterOut(__thiscall *)(void *, IterOut), 0x5a9bd0>()(list, &out);
    return out;
}
inline bool IterNotEqual(uint32_t it, uint32_t other) // 0x57cbb0
{
    return OriginalFunction<uint8_t(__thiscall *)(uint32_t *, const uint32_t *), 0x57cbb0>()(&it, &other) != 0;
}
inline bool IterEqual(uint32_t it, uint32_t other) // 0x57cb80
{
    return OriginalFunction<uint8_t(__thiscall *)(uint32_t *, const uint32_t *), 0x57cb80>()(&it, &other) != 0;
}
inline void **IterValue(uint32_t *it) // 0x57cca0: the address of the element
{
    return OriginalFunction<void **(__thiscall *)(uint32_t *), 0x57cca0>()(it);
}
inline void IterAdvance(uint32_t *it) // 0x57cd00 (post-increment, the old value goes to the first argument)
{
    uint32_t old;
    OriginalFunction<IterOut(__thiscall *)(uint32_t *, IterOut, int), 0x57cd00>()(it, &old, 0);
}

// The set of object numbers the person may use: the toolchain's std::unordered_set of 32-bit numbers (the original's
// 0x57c3d0 builds it, 0x55db30 finds in it, 0x38eef0 and 0x1fcdb0 free its two blocks; those are library code,
// db/library-matches.jsonl, an instantiation with the identical layout). The own interface's slot 150 (still the
// original's code: cTSPerson 0x516fd0) fills it through the pointer we pass, with the game's insert (0x420bd0: 0xc-byte
// nodes, so a set, not a map) and rehash (0x42d860), which allocate and free through the allocator service (0x10c95d4
// slots 3 and 4). So its allocator is the game's: with std::allocator the game's rehash freed our operator-new bucket
// vector through the general allocator and crashed (lane WE, smoke b49f2793).
using VisitedNumbers = std::unordered_set<int32_t, std::hash<int32_t>, std::equal_to<int32_t>, GameAllocator<int32_t>>;
static_assert(sizeof(VisitedNumbers) == 0x20, "the original's set is 0x20 bytes");

struct VisitedSet {
    VisitedNumbers mSet;
    bool Contains(int32_t key) const { return mSet.find(key) != mSet.end(); }
};

// A reference to a list element (0x57c330): copies the pointer and AddRefs it.
struct Candidate {
    RZAutoRef mRef{nullptr};
    explicit Candidate(void **element) { OriginalFunction<void(__thiscall *)(void *, void **), 0x57c330>()(&mRef, element); }
    Candidate(const Candidate &) = delete;
    ~Candidate() noexcept { mRef.ReleaseUnguarded(); }
    void *Get() const { return mRef.mObject; }
};

} // namespace

uint32_t PrimRObject::Prim0E(void *frame, const PrimParamView *param)
{
    // The primitive's "kind" (param word +4) picks the object function to search for and a data cell to rate with.
    int32_t cell;     // -1: none
    int32_t function; // the function number of the objects searched for
    switch (static_cast<int16_t>(Word(param, 4))) {
    case 0: cell = 0x1f; function = 0x12; break;
    case 1: cell = 0x20; function = 0x13; break;
    case 2: cell = 0x21; function = 0x14; break;
    case 3: cell = 0x24; function = 0x15; break;
    case 6: cell = 0x25; function = 0x18; break;
    case 7: cell = 0x26; function = 0x19; break;
    case 8: cell = 0x1a; function = 0x1a; break;
    case 10: cell = 0x3d; function = 0xe; break;
    case 11: cell = 0x27; function = 0x1c; break;
    case 12: cell = 0x40; function = 0x10; break;
    case 13: cell = 0x41; function = 0x11; break;
    case 14: cell = 0xf; function = 0x1d; break;
    case 15: cell = -1; function = 0x1f; break;
    case 23: cell = -1; function = 0x2a; break;
    case 25: cell = -1; function = 0x2c; break;
    case 27: cell = -1; function = 0x35; break;
    default: return 0;
    }

    const int16_t personId = As<ifaces::cTSObject>(this)->GetArgument(0u);
    As<ifaces::cTSObject>(this)->GetArgument(1u); // (the answer is not used)
    void *operandObject = this;
    uint32_t number = 0xffffffffu;
    void *table = Call<void *>(mContext, 26);
    const uint8_t f6 = Byte(param, 6);
    if (f6 & 8) {
        const int16_t id = Read(this, Byte(param, 7), Word(param, 8), nullptr, nullptr);
        operandObject = Call<void *>(table, 29, static_cast<int32_t>(id));
        if (!operandObject) Raise(0x17);
    }
    if ((f6 & 4) || (f6 & 0x20)) {
        void *source = (f6 & 8) ? operandObject : this;
        number = static_cast<uint16_t>(Call<int16_t>(source, 131));
    }

    void *positionService = nullptr;
    float position[3] = {-1.0f, -1.0f, 0.0f};
    if (f6 & 0x10) {
        void *service = OriginalFunction<void *(__cdecl *)(), 0x212e61>()();
        void *inner = Call<void *>(service, 39);
        positionService = Call<void *>(inner, 40, 5u);
        if (positionService) {
            if (!operandObject) Raise(0x17);
            uint8_t scratch[12];
            const float *p = Call<const float *>(operandObject, 42, scratch);
            std::memcpy(position, p, sizeof(position));
        }
    }

    float best = *GameAddress<float>(0xda1eb8);
    void *registry = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x1c4);
    void *list = Call<void *>(registry, 32, static_cast<uint32_t>(function));
    uint32_t bestIt = ListEnd(list);

    VisitedSet visited;
    bool haveInterface = false;
    Held ownInterface;
    if (Call<uint8_t>(this, 0, 0x5a69d3d5u, &ownInterface.mRef.mObject)) {
        if (function != 0x35) {
            const int16_t mine = Call<int16_t>(this, 131);
            haveInterface = Call<uint8_t>(ownInterface.Get(), 150, &visited.mSet, static_cast<int32_t>(mine)) != 0;
        }
        Call<void>(ownInterface.Get(), 59, 0u);
    }

    void *stackList = nullptr;
    if (Byte(param, 0xa) & 1) {
        const int16_t owner = As<ifaces::cTSObject>(this)->GetArgument(0u);
        void *object = Call<void *>(table, 29, static_cast<int32_t>(owner));
        if (!object) Raise(8);
        const uint16_t second = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(1u));
        stackList = Call<void *>(object, 265, static_cast<uint32_t>(second));
        if (!stackList) Raise(0x6c);
    }

    const float one = *GameAddress<float>(0xd9c698);
    uint32_t it = ListBegin(list);
    if (IterNotEqual(it, ListEnd(list))) {
        do {
            {
                Candidate candidate(IterValue(&it));
                void *object = candidate.Get();
                // `accepts` returns true when the candidate passes every filter; false skips it.
                [&] {
                    if (!object) return;
                    if (Call<uint8_t>(object, 25, 0x10000u)) return;
                    if (Call<uint8_t>(object, 25, 0x20000u)) return;
                    if (Call<uint8_t>(object, 13, 0x38u) & 5) return;
                    const uint32_t flags6b = Call<uint32_t>(object, 13, 0x6bu);
                    if ((flags6b & 0xffff) != 0) {
                        if (function != 0x1f) return;
                        if ((flags6b & 0xff00) != 0x200) return;
                    }
                    if (haveInterface) {
                        const int32_t id = Call<int16_t>(object, 131);
                        if (!visited.Contains(id)) return;
                    }
                    if (function == 0x2a || function == 0x2c) {
                        if (static_cast<int8_t>(f6) >= 0) {
                            if (Call<int16_t>(object, 13, 0x19u) > 0) return;
                        }
                    } else {
                        if (Call<uint8_t>(object, 129)) return;
                        if (Call<uint8_t>(object, 190, static_cast<uint32_t>(static_cast<uint8_t>(~f6) >> 7))) return;
                        if (Call<uint8_t>(object, 135)) return;
                    }

                    void *type = Call<void *>(object, 91);
                    void *entry = Call<void *>(type, 11, static_cast<uint32_t>(function));
                    if (!entry) return;
                    if (Call<int16_t>(entry, 6) == 0 && Call<int16_t>(entry, 8) == 0) return;

                    if (cell == -1) {
                        Call<void>(this, 14, 0x42u, 0x32u);
                    } else {
                        const uint16_t value = static_cast<uint16_t>(Call<int16_t>(object, 13, static_cast<uint32_t>(cell)));
                        Call<void>(this, 14, 0x42u, static_cast<uint32_t>(value));
                        if (Call<int16_t>(this, 13, 0x42u) == 0) return;
                    }

                    if (Call<int16_t>(entry, 8) != 0) {
                        // The secondary subobject's slot 15 checks the two objects can meet (eight arguments).
                        const uint32_t operandNumber = static_cast<uint16_t>(Call<int16_t>(operandObject, 47));
                        const uint32_t entryNumber = static_cast<uint16_t>(Call<int16_t>(entry, 8));
                        const uint32_t objectNumber = static_cast<uint16_t>(Call<int16_t>(object, 47));
                        const uint32_t objectLink = Call<uint32_t>(object, 89);
                        void *sub = &mSecondary;
                        if (!Call<uint8_t>(sub, 15, objectLink, objectNumber, entryNumber, operandNumber, 4u, 0u, 0u, 0u)) return;
                        if (function == 0x2a || function == 0x35) {
                            // ORIGINAL-BUG: the own-interface query's answer is not checked here either.
                            if (Call<uint8_t>(ownInterface.Get(), 46)) {
                                const uint16_t value = static_cast<uint16_t>(Call<int16_t>(object, 13, 0x70u));
                                Call<void>(this, 14, 0x42u, static_cast<uint32_t>(value));
                            }
                        }
                        if (Call<int16_t>(this, 13, 0x42u) == 0) return;
                    }

                    // Whether the candidate is acceptable by the primitive's selection flags.
                    bool accepted;
                    if (f6 & 1) {
                        uint8_t scratch[4];
                        Call<void>(object, 101, scratch);
                        uint8_t first[12], dest[12];
                        simgeom::TileFromBytes(reinterpret_cast<int32_t *>(first), scratch);
                        simgeom::TileCentre(reinterpret_cast<float *>(dest), reinterpret_cast<const int32_t *>(first));
                        void *service = OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()();
                        void *inner = Call<void *>(service, 39);
                        accepted = static_cast<uint16_t>(Call<int16_t>(inner, 80, dest, 0u, 1u)) == 0;
                    } else if (f6 & 2) {
                        uint8_t scratch[4];
                        Call<void>(object, 101, scratch);
                        uint8_t first[12], dest[12];
                        simgeom::TileFromBytes(reinterpret_cast<int32_t *>(first), scratch);
                        simgeom::TileCentre(reinterpret_cast<float *>(dest), reinterpret_cast<const int32_t *>(first));
                        void *service = OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()();
                        void *inner = Call<void *>(service, 39);
                        accepted = static_cast<uint16_t>(Call<int16_t>(inner, 80, dest, 0u, 1u)) != 0;
                    } else if (Byte(param, 6) & 4) {
                        accepted = static_cast<uint16_t>(Call<int16_t>(object, 131)) == static_cast<uint16_t>(number);
                    } else if (f6 & 0x20) {
                        accepted = static_cast<uint16_t>(Call<int16_t>(object, 131)) != static_cast<uint16_t>(number);
                    } else if (f6 & 0x40) {
                        accepted = Call<int16_t>(object, 131) == personId;
                    } else if (Byte(param, 0xa) & 1) {
                        int32_t id = static_cast<uint16_t>(Call<int16_t>(object, 131));
                        uint32_t endOut, beginOut, foundOut, end2Out;
                        OriginalFunction<IterOut(__thiscall *)(void *, IterOut), 0x5a9c60>()(stackList, &endOut);
                        OriginalFunction<IterOut(__thiscall *)(void *, IterOut), 0x5a9970>()(stackList, &beginOut);
                        OriginalFunction<IterOut(__cdecl *)(IterOut, uint32_t, uint32_t, int32_t *), 0x57bef0>()(&foundOut, beginOut, endOut, &id);
                        OriginalFunction<IterOut(__thiscall *)(void *, IterOut), 0x5a9c60>()(stackList, &end2Out);
                        accepted = !IterEqual(foundOut, end2Out);
                    } else {
                        accepted = true;
                    }
                    if (!accepted) return;

                    // The score: the rating cell, discounted by distance and weighted by the person's opinion.
                    const int32_t rating = Call<int16_t>(this, 13, 0x42u);
                    void *measurer = (f6 & 8) ? operandObject : this;
                    const float distance = Call<float>(measurer, 128, object, *GameAddress<float>(0x1014404));
                    float scale = distance / *GameAddress<float>(0x1014400);
                    scale = scale + one;
                    float score = one / scale;
                    score = score * static_cast<float>(rating);
                    if (ownInterface.Get()) {
                        const float first = Call<float>(ownInterface.Get(), 79, object);
                        const float second = Call<float>(ownInterface.Get(), 78, object, 0u);
                        float combined = (first + one) * score;
                        score = second * combined;
                    }
                    if (!(score > best)) return;
                    if ((f6 & 0x10) && positionService) {
                        uint8_t scratch[12];
                        const float *other = Call<const float *>(object, 42, scratch);
                        if (Call<uint8_t>(positionService, 19, position, other, 0u, 0u, 0u)) return;
                    }
                    best = score;
                    bestIt = it;
                }();
            }
            IterAdvance(&it);
        } while (IterNotEqual(it, ListEnd(list)));
    }

    if (IterNotEqual(bestIt, ListEnd(list))) {
        if (*IterValue(&bestIt)) {
            void *chosen = *IterValue(&bestIt);
            const uint16_t number47 = static_cast<uint16_t>(Call<int16_t>(chosen, 47));
            Call<void>(frame, 13, static_cast<uint32_t>(number47));
            return 1;
        }
    }
    return 0;
}
