#include "simantics/PrimMid.h"
#include "ifaces/cTSObject.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"

#include <cstdlib>
#include <cstring>

namespace {

template <typename R, typename... A> inline R Call(const void *obj, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(obj, slot)(const_cast<void *>(obj), args...);
}

inline uint8_t Byte(const void *p, size_t off)
{
    return static_cast<const uint8_t *>(p)[off];
}

inline uint16_t Word(const void *p, size_t off)
{
    uint16_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof(v));
    return v;
}

[[noreturn]] void Raise(int16_t code)
{
    throw code;
}

// The reference the original keeps in its frame: released (slot 2) when the handler returns or throws.
struct Held {
    RZAutoRef mRef{nullptr};
    ~Held() noexcept { mRef.ReleaseUnguarded(); }
};

// The same for a reference to an object that is released through its virtual base (RZAutoRef::ReleaseVirtualBase).
struct HeldVirtual {
    RZAutoRef mRef{nullptr};
    ~HeldVirtual() noexcept { mRef.ReleaseVirtualBase(); }
};

int __cdecl CompareIds(const void *a, const void *b)
{
    return RankCompare(static_cast<const int32_t *>(a), static_cast<const int32_t *>(b));
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x21
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Prim21(void * /*frame*/, const PrimParamView *param)
{
    const uint32_t check = Word(param, 0xa);
    const uint32_t mode = Word(param, 8);

    int32_t ids[10] = {5, 6, 7, 8, 9, 0xd, 0xe, 0xf, 0, 1};
    Held held; // the original sets the reference up (null) before reading the table
    void *table = Call<void *>(mContext, 26);

    constexpr uint32_t kInterface = 0x5a69d3d5;
    if (mode == 1) {
        void *sub = &mSecondary;
        void *named = Call<void *>(sub, 20);
        const int16_t id = Call<int16_t>(named, 12);
        void *target = Call<void *>(table, 29, static_cast<int32_t>(id));
        if (target) {
            held.mRef.Reset();
            Call<uint32_t>(target, 0, kInterface, &held.mRef.mObject);
        }
    } else if (mode != 2) {
        held.mRef.Reset();
        Call<uint32_t>(this, 0, kInterface, &held.mRef.mObject);
    }

    // Nothing was found: the same error as an interface that is not in the state the handler needs.
    if (!held.mRef.mObject) Raise(0xa);
    void *state = Call<void *>(held.mRef.mObject, 8);
    if (Call<int32_t>(state, 98) != 2) Raise(0xa);
    if (check > 2) Raise(0x1e);

    // The float table the comparator ranks by (a game global: the comparator is a plain function).
    *GameAddress<const float *>(0x10dbf50) = Call<const float *>(held.mRef.mObject, 15, 0);
    std::qsort(ids, 8, sizeof(int32_t), &CompareIds);
    for (int i = 0; i < 5; ++i) As<ifaces::cTSObject>(this)->SetArgument(static_cast<uint32_t>(i), static_cast<uint32_t>(static_cast<uint16_t>(ids[i])));
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x30
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Prim30(void *frame, const PrimParamView *param)
{
    int16_t id;
    if (static_cast<int16_t>(param->mWord4) != 0) id = Call<int16_t>(frame, 12);
    else id = Call<int16_t>(this, 47);

    void *table = Call<void *>(this, 63);
    void *object = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (object) {
        Held held;
        void *director = GetDirectorFrameworkObject();
        if (Call<uint8_t>(director, 3, 0xcbc79579u, 0x0bc7957du, &held.mRef.mObject)) {
            Call<void>(held.mRef.mObject, 5, 0x2dcae8efu);
            Call<void>(held.mRef.mObject, 12, 0xac5b5b4fu, static_cast<int32_t>(id));
            void *notified = OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()();
            if (notified) Call<void>(notified, 12, held.mRef.mObject);
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x7a, OnTimer
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::OnTimer(void *frame, const PrimParamView *param)
{
    void *context = mContext;
    Call<void *>(context, 24); // asked, the result is not used
    void *table = Call<void *>(context, 26);
    const int16_t id = Call<int16_t>(frame, 12);
    void *timers = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!timers) Raise(0x17);

    const uint32_t kind = Byte(param, 0x13);
    const uint32_t timerId = Word(param, 7);
    const bool bit3 = ((Byte(param, 9) >> 3) & 1) != 0;
    int32_t count;
    if (bit3) count = As<ifaces::cTSObject>(this)->GetArgument(1);
    else count = Word(param, 4);

    if (kind == 0) {
        int16_t values[3] = {0, 0, 0};
        uint32_t n = 3;
        const void *pointer = values;
        if (Byte(param, 9) & 1) {
            n = Call<uint8_t>(frame, 26);
            pointer = Call<const void *>(frame, 35, 0);
        } else {
            void *sub = &mSecondary;
            values[0] = Call<int16_t>(sub, kPrimReadSlot, static_cast<uint32_t>(Byte(param, 0xa)), static_cast<uint32_t>(Word(param, 0xb)),
                                      0u, 0u, 0u, 0u, 0u);
            values[1] = Call<int16_t>(sub, kPrimReadSlot, static_cast<uint32_t>(Byte(param, 0xd)), static_cast<uint32_t>(Word(param, 0xe)),
                                      0u, 0u, 0u, 0u, 0u);
            values[2] = Call<int16_t>(sub, kPrimReadSlot, static_cast<uint32_t>(Byte(param, 0x10)), static_cast<uint32_t>(Word(param, 0x11)),
                                      0u, 0u, 0u, 0u, 0u);
        }
        uint32_t flags = 0;
        if ((Byte(param, 9) >> 1) & 1) flags |= 1;
        const int32_t who = Call<int16_t>(this, 47);
        const uint32_t result = Call<uint32_t>(timers, 301, count, timerId, flags, who, n, pointer);
        As<ifaces::cTSObject>(this)->SetArgument(0u, result);
        return 1;
    }
    if (kind == 1) {
        uint32_t flags = 0;
        if ((Byte(param, 9) >> 1) & 1) flags |= 1;
        if ((Byte(param, 9) >> 2) & 1) flags |= 2;
        const int32_t who = As<ifaces::cTSObject>(this)->GetArgument(0);
        Call<void>(timers, 302, count, timerId, flags, who);
        return 1;
    }
    const int32_t who = As<ifaces::cTSObject>(this)->GetArgument(0);
    Call<void>(timers, 303, who);
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x23
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Prim23(void *frame, const PrimParamView *param)
{
    void *table = Call<void *>(mContext, 26);
    void *state = Call<void *>(mContext, 3);

    if (Call<int32_t>(frame, 18) == 0) {
        // The frame has not started: begin it.
        if (static_cast<uint16_t>(Call<uint16_t>(state, 15, 0x34)) != 0) return 1;
        uint32_t index = Word(param, 4);
        if (Byte(param, 8) & 0x20) index = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0));
        Call<void>(state, 16, 0x34, index);
        Call<void>(state, 16, 0xb, 1);
        Call<void>(frame, 19, 1);
        return 2;
    }
    if (Call<int32_t>(frame, 18) != 1) return 1;

    Call<void>(frame, 19, 2);
    const int16_t id = Call<int16_t>(frame, 12);
    void *found = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!found) Raise(0x17);

    const uint8_t flags = Byte(param, 8);
    if (((flags >> 6) & 1) == 0) {
        if (Call<int32_t>(state, 21) != 2) Call<void>(state, 20, 2, 0);
    }

    if (void *service = GetCachedSingletonB()) {
        if (!Call<uint8_t>(found, 188)) {
            Call<void>(service, 44, 0);
            return 1;
        }
        if (flags & 8) {
            const int16_t again = Call<int16_t>(frame, 12);
            Call<void>(service, 37, static_cast<int32_t>(again));
        } else if (flags & 1) {
            const int16_t again = Call<int16_t>(frame, 12);
            Call<void>(service, 44, static_cast<int32_t>(again));
            return 2;
        } else {
            Call<void>(service, 44, 0);
        }
    }

    // The object is looked up once more (no null check: the original uses the result at once).
    const int16_t third = Call<int16_t>(frame, 12);
    void *object = Call<void *>(table, 29, static_cast<int32_t>(third));
    HeldVirtual first;
    first.mRef.ResetVirtualBase();
    if (Call<uint8_t>(object, 12, 0xfa1c39f7u, 0xfa1c39edu, &first.mRef.mObject)) Call<void>(first.mRef.mObject, 118);

    void *lister = OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
    void *middle = Call<void *>(lister, 53);
    void *source = Call<void *>(middle, 37);

    HeldVirtual second;
    // The source's virtual base subobject: the vbptr at +4 points to a table whose second entry is its offset
    // from that vbptr.
    uint8_t *sourceBytes = static_cast<uint8_t *>(source);
    const uint8_t *vbTable = *reinterpret_cast<uint8_t *const *>(sourceBytes + 4);
    const int32_t vbOffset = *reinterpret_cast<const int32_t *>(vbTable + 4);
    void *base = sourceBytes + 4 + vbOffset;
    second.mRef.ResetVirtualBase();
    if (Call<uint8_t>(base, 0, 0xeaf75329u, &second.mRef.mObject)) {
        void *target = second.mRef.mObject;
        const int32_t count = Call<int32_t>(target, 20);
        if (count > 0) {
            const uint8_t mode = Byte(param, 7);
            target = second.mRef.mObject;
            if (mode == 1) Call<void>(target, 5, 0);
            else if (mode == 2) Call<void>(target, 5, count / 2);
            else Call<void>(target, 5, count - 1);
        }
    }
    return 2;
}
