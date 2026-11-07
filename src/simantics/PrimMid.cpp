#include "simantics/PrimMid.h"
#include "ifaces/cTSObject.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cmath>
#include <cstring>

namespace {

// A call through slot `slot` of `obj`'s vtable, __thiscall, the arguments pushed as written.
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

inline uint32_t Dword(const void *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof(v));
    return v;
}

[[noreturn]] void Raise(int16_t code)
{
    throw code;
}

// The operand read the handlers use: slot 7 of the secondary subobject (this + 4), seven stack arguments
// (scope, id, two outputs, three zeros), the value in ax.
inline int16_t Read(PrimMidObject *self, uint32_t scope, uint32_t id, SimDataRef *dest, float **floatDest)
{
    void *sub = &self->mSecondary;
    return Call<int16_t>(sub, kPrimReadSlot, scope, id, dest, floatDest, static_cast<uint32_t *>(nullptr), 0u, 0u);
}

// Param helpers the original calls as tiny functions (all just field tests): 0x57e570/0x57e590 (the id: the word
// at +0xa / +8 when bit 0 of the byte at +8 / +6 is set, else 0xb), 0x57e5c0/0x57e5d0 (the scope: the byte at +9 /
// +7 under the same bit, else 3).
inline uint16_t IdFromWord8(const void *p)
{
    return (Byte(p, 6) & 1) ? Word(p, 8) : 0xb;
}
inline uint16_t ScopeFromByte7(const void *p)
{
    return (Byte(p, 6) & 1) ? Byte(p, 7) : 3;
}
inline uint16_t IdFromWordA(const void *p)
{
    return (Byte(p, 8) & 1) ? Word(p, 0xa) : 0xb;
}
inline uint16_t ScopeFromByte9(const void *p)
{
    return (Byte(p, 8) & 1) ? Byte(p, 9) : 3;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------

float SimFixed::ToFloat() const
{
    // cvtdq2ps, then mulss by 2^-16 (0x37800000).
    return static_cast<float>(mRaw) * 1.52587890625e-05f;
}

float Atan2Float(float y, float x)
{
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}

int32_t SimFixedPoint::Octant(const SimFixedPoint *to) const
{
    const float scale = 1.52587890625e-05f; // 2^-16
    const float dy = static_cast<float>(to->mY) * scale - static_cast<float>(mY) * scale;
    const float dx = static_cast<float>(to->mX) * scale - static_cast<float>(mX) * scale;
    // The sign flip is a sign-bit xor taken only when the value is strictly below zero.
    const float ady = (0.0f > dy) ? -dy : dy;
    const float adx = (0.0f > dx) ? -dx : dx;
    const float tan22 = 0.41421357f; // 0x3ed413cd

    int32_t octant;
    if (ady * tan22 > adx) {
        octant = 2;
    } else {
        octant = 3 + (adx * tan22 > ady ? 1 : 0);
    }
    if (0.0f > dy) {
        if (octant == 2) octant = 6;
        else if (octant == 3) octant = 5;
    }
    if (0.0f > dx) {
        if (octant == 3) octant = 1;
        else if (octant == 4) octant = 0;
        else if (octant == 5) octant = 7;
    }
    return octant;
}

int __cdecl RankCompare(const int32_t *a, const int32_t *b)
{
    const float *table = *GameAddress<const float *>(0x10dbf50);
    const double diff = static_cast<double>(table[*a] - table[*b]);
    if (-0.01 > diff) return -1;
    return diff > 0.01 ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x7e
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Prim7E(void *frame, const PrimParamView *param)
{
    void *handler = Call<void *>(mOwner, 67);
    if (!handler) return 0;
    return Call<uint32_t>(handler, 7, static_cast<void *>(this), frame, param);
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x00, Idle
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Idle(void *frame, const PrimParamView *param)
{
    const int index = static_cast<int16_t>(param->mWord4);
    if (index < 0) Raise(8);
    // The frame's slot 26 gives how many counters it has (a byte).
    if (index >= Call<uint8_t>(frame, 26)) Raise(8);
    int16_t *counter = Call<int16_t *>(frame, 35, static_cast<int32_t>(index));

    void *context = mContext;
    void *ticker = Call<void *>(context, 3);
    const uint8_t mode = Call<uint8_t>(ticker, 14);
    if ((mode & 6) == 0) {
        if (*counter == 0) return 1;
        Call<uint8_t>(this, 196); // asked, and both answers return 2
        return 2;
    }

    void *table = Call<void *>(context, 26);
    int32_t id = Call<int16_t>(this, 47);
    if (Call<int32_t>(table, 73, id) == 0) {
        *counter = 0;
        id = Call<int16_t>(this, 47);
        Call<void>(table, 71, id, static_cast<int32_t>(-1));
    }
    if (*counter < 0) *counter = 0;
    if (*counter == 0) return 1;
    if (!Call<uint8_t>(this, 196)) {
        *counter = static_cast<int16_t>(*counter - 1);
        return 2;
    }
    id = Call<int16_t>(this, 47);
    Call<void>(table, 71, id, static_cast<int32_t>(*counter) - 1);
    return 2;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x0b, DistanceTo
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::DistanceTo(void *frame, const PrimParamView *param)
{
    const int index = static_cast<int16_t>(param->mWord4);
    if (index < 0 || index >= 8) Raise(2);

    const int16_t first = Read(this, ScopeFromByte7(param), IdFromWord8(param), nullptr, nullptr);

    void *table = Call<void *>(mContext, 26);
    void *a = Call<void *>(table, 29, static_cast<int32_t>(first));
    if (!a) Raise(0x17);
    const int16_t second = Call<int16_t>(frame, 12);
    void *b = Call<void *>(table, 29, static_cast<int32_t>(second));
    if (!b) Raise(0x17);

    const float arg = *GameAddress<float>(0x1014404);
    const bool scaled = ((Byte(param, 0xa) >> 1) & 1) != 0;
    uint16_t value;
    if (scaled) {
        const float distance = Call<float>(a, 128, b, arg);
        value = static_cast<uint16_t>(static_cast<int32_t>(distance * 100.0f));
    } else {
        const float distance = Call<float>(a, 128, b, arg);
        value = static_cast<uint16_t>(static_cast<int32_t>(distance));
    }
    As<ifaces::cTSObject>(this)->SetArgument(static_cast<uint32_t>(param->mWord4), static_cast<uint32_t>(value));
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x0c, DirectionTo
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::DirectionTo(void *frame, const PrimParamView *param)
{
    SimDataRef dest;
    dest.Reset();
    Read(this, param->mWord6, param->mWord4, &dest, nullptr);

    const int16_t firstId = Read(this, ScopeFromByte9(param), IdFromWordA(param), nullptr, nullptr);

    void *table = Call<void *>(mContext, 26);
    void *a = Call<void *>(table, 29, static_cast<int32_t>(firstId));
    if (!a) Raise(0x17);
    const int16_t secondId = Call<int16_t>(frame, 12);
    void *b = Call<void *>(table, 29, static_cast<int32_t>(secondId));
    if (!b) Raise(0x15);

    if ((Byte(param, 0xc) >> 1) & 1) {
        // A bearing: position 33 is the object's fixed-point position (x, y).
        const uint8_t *pb = Call<const uint8_t *>(b, 33);
        const float by = reinterpret_cast<const SimFixed *>(pb + 4)->ToFloat();
        const uint8_t *pa = Call<const uint8_t *>(a, 33);
        const float ay = reinterpret_cast<const SimFixed *>(pa + 4)->ToFloat();
        const float dy = by - ay;
        const uint8_t *pb2 = Call<const uint8_t *>(b, 33);
        const float bx = reinterpret_cast<const SimFixed *>(pb2)->ToFloat();
        const uint8_t *pa2 = Call<const uint8_t *>(a, 33);
        const float ax = reinterpret_cast<const SimFixed *>(pa2)->ToFloat();
        const float dx = bx - ax;
        const float angle = Atan2Float(dx, dy);
        const float degrees = angle * 57.295776f + 90.0f;
        const int32_t whole = static_cast<int32_t>(degrees);
        if (!dest.Set((static_cast<int32_t>(static_cast<int16_t>(whole)) + 0x168) % 0x168)) Raise(0x14);
    } else {
        const SimFixedPoint *to = Call<const SimFixedPoint *>(b, 33);
        const SimFixedPoint *from = Call<const SimFixedPoint *>(a, 33);
        const int16_t octant = static_cast<int16_t>(from->Octant(to));
        if (!dest.Set((static_cast<int32_t>(octant) + 8) % 8)) Raise(0x14);
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x14
// ---------------------------------------------------------------------------------------------------------

namespace {

// Param word +4 -> the index asked of the object (0x18 and anything outside 0..0x1b: none).
int32_t Prim14Index(int16_t selector)
{
    static const int8_t kIndex[0x1c] = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x0e, 0x1c, 0x10, 0x11,
                                        0x1d, 0x1f, 0x20, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, -1,   0x2c, 0x2e, 0x35};
    const uint32_t s = static_cast<uint32_t>(static_cast<int32_t>(selector));
    if (s > 0x1b) return -1;
    return kIndex[s];
}

} // namespace

uint32_t PrimMidObject::Prim14(void *frame, const PrimParamView *param)
{
    void *context = mContext;
    void *table = Call<void *>(context, 26);
    const int16_t id = Call<int16_t>(frame, 12);
    void *person = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!person) Raise(0x15);
    Call<void>(person, 64);

    const int32_t index = Prim14Index(static_cast<int16_t>(param->mWord4));
    if (index < 0) return 0;

    const uint16_t flags = param->mWord6;
    uint32_t value = Call<uint16_t>(person, 93, index);
    if ((flags >> 2) & 1) value = Call<uint16_t>(person, 94, index);

    void *extra = nullptr;
    if ((flags >> 1) & 1) extra = Call<void *>(frame, 35, 0);

    if (static_cast<uint16_t>(value) != 0) {
        // The fourth argument is the frame pointer with its low byte replaced by bit 0 of the flags: the
        // compiler reused the frame's argument slot for the flag byte.
        const uint32_t flag = (reinterpret_cast<uintptr_t>(frame) & ~0xffu) | (flags & 1u);
        if (Call<uint8_t>(this, 248, person, extra, value, flag, 4u)) return 3;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x19, Budget
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Budget(void * /*frame*/, const PrimParamView *param)
{
    uint8_t scope;
    switch (Byte(param, 4)) {
    case 0: scope = 7; break;
    case 1: scope = 9; break;
    case 2: scope = 0x19; break;
    default: scope = Byte(param, 5); break;
    }
    const uint32_t id = Word(param, 6);
    const uint32_t account = Byte(param, 0xa);
    if (account > 10) Raise(0x38);

    const int32_t flags = static_cast<int16_t>(Word(param, 8));
    int32_t amount;
    if ((flags >> 3) & 1) {
        const int32_t high = As<ifaces::cTSObject>(this)->GetArgument(2);
        const int32_t low = As<ifaces::cTSObject>(this)->GetArgument(3);
        amount = low + high * 10000;
    } else {
        void *sub = &mSecondary;
        const int32_t read = Call<int16_t>(sub, kPrimReadSlot, static_cast<uint32_t>(scope), id, 0u, 0u, 0u, 0u, 0u);
        uint32_t multiplier = Word(param, 0xb);
        if ((flags >> 2) & 1) multiplier = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(2));
        if (static_cast<uint16_t>(multiplier) == 0) multiplier = 1;
        amount = static_cast<int32_t>(static_cast<int16_t>(multiplier)) * read;
    }

    void *funds = Call<void *>(mContext, 3);
    const int32_t balance = Call<int32_t>(funds, 29);
    const int32_t change = (((flags >> 1) & 1) == 0) ? amount : -amount;
    if (change >= 0 && balance < change) return 0;

    if ((Byte(param, 8) & 1) == 0) {
        void *target = Call<void *>(mContext, 3);
        Call<void>(target, 44, account, change);
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x20
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimMidObject::Prim20(void * /*frame*/, const PrimParamView *param)
{
    int32_t wanted = static_cast<int32_t>(Dword(param, 4));
    const uint8_t mode = Byte(param, 0xb);

    void *sub = &mSecondary;
    const int16_t read = Call<int16_t>(sub, kPrimReadSlot, static_cast<uint32_t>(Byte(param, 0xa)),
                                       static_cast<uint32_t>(Word(param, 8)), 0u, 0u, 0u, 0u, 0u);

    if (static_cast<uint32_t>(wanted) == 0x4c7cab2bu) {
        const uint8_t *found = Call<const uint8_t *>(this, 289);
        wanted = static_cast<int32_t>(Dword(found, 0xa));
    }

    if (mode & 4) {
        void *list = Call<void *>(mContext, 24);
        if (Call<uint32_t>(list, 14, wanted) == 0) return 0;
        As<ifaces::cTSObject>(this)->SetArgument(1u, static_cast<uint32_t>(wanted >> 16));
        As<ifaces::cTSObject>(this)->SetArgument(0u, static_cast<uint32_t>(wanted));
        Call<void>(this, 313, wanted);
        return 1;
    }

    void *entry;
    if (mode & 2) {
        void *owner = Call<void *>(mContext, 10);
        void *slot = Call<void *>(owner, 16, static_cast<int32_t>(read), 0u);
        if (!slot) return 0;
        entry = Call<void *>(slot, 10);
    } else {
        void *table = Call<void *>(this, 63);
        void *found = Call<void *>(table, 29, static_cast<int32_t>(read));
        if (!found) return 0;
        entry = Call<void *>(found, 61);
    }
    if (!entry) return 0;

    void *probe = entry;
    if (mode & 1) {
        void *alt = Call<void *>(entry, 32);
        if (!alt) probe = nullptr;
        else probe = alt;
    }
    if (probe && static_cast<int32_t>(Call<uint32_t>(probe, 28)) == wanted) return 1;
    void *inner = Call<void *>(entry, 27);
    return static_cast<int32_t>(Call<uint32_t>(inner, 28)) == wanted ? 1 : 0;
}
