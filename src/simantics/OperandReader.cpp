#include "simantics/OperandReader.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "game/original.h"
#include "simantics/PrimSCommon.h"
#include "world/WorldHeights.h"

#include <cstring>
#include <emmintrin.h>
#include <xmmintrin.h>

using primS::Call;
using primS::Held;
using primS::Raise;

namespace {

[[noreturn]] void RaiseNoFrame()
{
    throw cTSTreeStack::kStackErrorNoFrame;
}

// ---------------------------------------------------------------------------------------------------------
// Memory access in the opaque subobject / person data / table
// ---------------------------------------------------------------------------------------------------------

inline uint8_t *At(void *base, size_t off)
{
    return static_cast<uint8_t *>(base) + off;
}

inline int16_t I16(const void *base, size_t off)
{
    int16_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}

inline int32_t I32(const void *base, size_t off)
{
    int32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}

inline uint8_t *PtrAt(const void *base, size_t off)
{
    uint8_t *v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}

// (end - begin) of a vector of 2/4/12-byte elements, an arithmetic shift as the compiler did it.
inline int32_t ByteSpan(const uint8_t *begin, const uint8_t *end)
{
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(begin));
}

// ---------------------------------------------------------------------------------------------------------
// The stack
// ---------------------------------------------------------------------------------------------------------

// The element of the top stack frame: the embedded stack counter at +0x18 (slot 10 gives the depth), the frame
// vector at +0x1c/+0x20 (slot 41 of the frame gives the element). A missing frame throws the game's enum.
uint8_t *StackElement(uint8_t *s)
{
    const int32_t index = Call<int32_t>(s + 0x18, 10) - 1;
    if (index < 0) RaiseNoFrame();
    uint8_t *const *frames = reinterpret_cast<uint8_t *const *>(PtrAt(s, 0x1c));
    const uint32_t count = static_cast<uint32_t>(ByteSpan(PtrAt(s, 0x1c), PtrAt(s, 0x20)) >> 2);
    if (static_cast<uint32_t>(index) >= count) RaiseNoFrame();
    return Call<uint8_t *>(frames[index], 41);
}

// The stack object of the element (its 16-bit id at +0xa, 1-based into the table at +0x1c0), or null when the id is
// out of range or the entry empty.
uint8_t *StackObjectOrNull(uint8_t *s, const uint8_t *element)
{
    const int32_t id = I16(element, 0xa);
    if (id <= 0) return nullptr;
    uint8_t *table = PtrAt(s, 0x1c0);
    uint8_t *const *begin = reinterpret_cast<uint8_t *const *>(PtrAt(table, 0x1a24));
    const uint32_t count = static_cast<uint32_t>(ByteSpan(PtrAt(table, 0x1a24), PtrAt(table, 0x1a28)) >> 2);
    if (static_cast<uint32_t>(id) > count) return nullptr;
    return begin[id - 1];
}

// "P": the object the stack object's slot 280 gives. Any step failing throws `err`.
uint8_t *StackPerson(uint8_t *s, int16_t err)
{
    uint8_t *element = StackElement(s);
    uint8_t *object = StackObjectOrNull(s, element);
    if (!object) Raise(err);
    uint8_t *p = Call<uint8_t *>(object, 280);
    if (!p) Raise(err);
    return p;
}

// Owner 22 and 50 look the person up the same way but do not check it (ORIGINAL-BUG at the use sites).
uint8_t *StackPersonUnchecked(uint8_t *s, uint8_t *element)
{
    uint8_t *object = StackObjectOrNull(s, element);
    if (!object) return nullptr;
    return Call<uint8_t *>(object, 280);
}

constexpr uint32_t kIidPerson = 0x5a69d3d5;
constexpr uint32_t kIidList = 0x5b267c89;
constexpr uint32_t kIidInterface = 0x5a6216f6;

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------

namespace {

// 0x53d9c0: the element pointer is stored as it is.
void BindRef(SimDataRef *r, int16_t *ptr, int32_t index, int32_t length)
{
    const uint16_t len16 = static_cast<uint16_t>(length);
    const bool usable = ptr != nullptr && static_cast<int16_t>(len16) > 0;
    r->mLength = usable ? static_cast<int16_t>(len16) : 0;
    const int16_t checkLen = usable ? static_cast<int16_t>(len16) : 0;
    const int16_t idx = static_cast<int16_t>(index);
    r->mIndex = (ptr != nullptr && idx > 0 && idx < checkLen) ? idx : 0;
    r->mPtr = ptr;
    r->mMin = INT16_MIN;
    r->mMax = INT16_MAX;
}

// 0x52ccc0: the run's start is given; a valid index selects the element.
void MakeRef(SimDataRef *r, int16_t *ptr, int32_t index, int32_t length)
{
    const uint16_t len16 = static_cast<uint16_t>(length);
    const bool usable = ptr != nullptr && static_cast<int16_t>(len16) > 0;
    const int16_t checkLen = usable ? static_cast<int16_t>(len16) : 0;
    r->mLength = usable ? static_cast<int16_t>(len16) : 0;
    const int16_t idx = static_cast<int16_t>(index);
    if (ptr != nullptr && idx > 0 && idx < checkLen) {
        r->mIndex = idx;
        r->mPtr = ptr + idx;
    } else {
        r->mIndex = 0;
        r->mPtr = ptr;
    }
    r->mMin = INT16_MIN;
    r->mMax = INT16_MAX;
}

} // namespace

void OperandRef::Bind(int16_t *ptr, int32_t index, int32_t length)
{
    BindRef(this, ptr, index, length);
}

SimDataRef *OperandRef::MakeFromBase(int16_t *ptr, int32_t index, int32_t length)
{
    MakeRef(this, ptr, index, length);
    return this;
}

int32_t __cdecl RoundHalfAway(float value)
{
    // comiss 0, x; jbe: taken when 0 <= x or unordered, then add 0.5 in double, else subtract it.
    const __m128d d = _mm_cvtps_pd(_mm_set_ss(value));
    const __m128d half = _mm_set_sd(0.5);
    const __m128d r = !(value < 0.0f) ? _mm_add_sd(d, half) : _mm_sub_sd(d, half);
    return _mm_cvttsd_si32(r);
}

namespace {

// The byte table of 0x53f7d0: ids (index = id - 1) whose slot-7 argument is id - 1 instead of id.
bool DecrementsId(int32_t idMinusOne)
{
    static const uint8_t kKeep[93] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1,
                                      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};
    return static_cast<uint32_t>(idMinusOne) <= 0x5c && kKeep[idMinusOne] == 0;
}

} // namespace

void __cdecl BindObjectData(void *object, int32_t id, SimDataRef *dest)
{
    int16_t si = static_cast<int16_t>(id);
    SimDataRef *d = dest;
    if (si >= 0xc9) {
        int16_t *p = Call<int16_t *>(object, 7, static_cast<int32_t>(si));
        d->mLength = p ? 2 : 0;
        d->mIndex = 0;
        d->mPtr = p;
        d->mMin = INT16_MIN;
        d->mMax = INT16_MAX;
        return;
    }
    if (DecrementsId(static_cast<int32_t>(si) - 1)) si = static_cast<int16_t>(si - 1);
    int32_t wide = si;
    int16_t *p = Call<int16_t *>(object, 7, wide);
    int16_t index = 0;
    if (p) {
        d->mLength = 0x6c;
        if (si > 0 && si < 0x6c) index = si;
    } else {
        d->mLength = 0;
    }
    d->mIndex = index;
    d->mPtr = p;
    d->mMin = INT16_MIN;
    d->mMax = INT16_MAX;
}

// ---------------------------------------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------------------------------------

namespace {

using CachedFn = void *(__cdecl *)();
using U32Fn = uint32_t(__cdecl *)(uint32_t value, int32_t a, int32_t b);
using PtrFn = uint32_t(__cdecl *)(uint32_t *value);

// Owner 14, 15 and 17: the float attribute of the interface the QueryInterface found.
uint32_t FloatAttribute(RZAutoRef &ref, int32_t id, uint32_t *floatDest)
{
    const float f = Call<float>(ref.mObject, 13, id);
    const uint32_t value = static_cast<uint16_t>(RoundHalfAway(f));
    if (floatDest) *floatDest = Call<uint32_t>(ref.mObject, 15, id);
    return value;
}

} // namespace

uint32_t SimOperandSource::Read(int32_t ownerArg, uint32_t idArg, SimDataRef *destArg, uint32_t *floatDest, uint32_t *zeroed,
                                uint32_t clearFlag, uint32_t arg7)
{
    uint8_t *const s = reinterpret_cast<uint8_t *>(this);
    uint8_t *const object = s - 4;

    // The destination: the caller's, or a private one that nobody reads.
    SimDataRef local;
    SimDataRef *dest = destArg ? destArg : &local;
    SimDataRef *d = dest;
    dest->Reset();
    if (zeroed) *zeroed = 0;
    if (floatDest) *floatDest = 0;
    bool clearable = true;

    // Called on every read, whatever the owner. Its result is kept in the frame (`ctx26`).
    uint8_t *const context = PtrAt(s, 0x3c);
    void *const ctx26 = Call<void *>(context, 26);

    const int16_t id16 = static_cast<int16_t>(idArg);
    const uint16_t idu = static_cast<uint16_t>(idArg);
    const int32_t idSigned = id16;
    const int16_t owner = static_cast<int16_t>(ownerArg);
    uint32_t value = 0;
    SimDataRef tmp;

    // The error of a stack-object table lookup is 0xa for some owners and 0x15 for others (cases below).
    switch (owner) {
    case -1: Raise(0x5b);

    case 0: {
        if (id16 < 0) Raise(6);
        const uint16_t len = static_cast<uint16_t>(I16(s, 0x88));
        if (id16 >= static_cast<int16_t>(len)) Raise(6);
        BindRef(dest, reinterpret_cast<int16_t *>(PtrAt(s, 0x84)) + id16, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 1: {
        uint8_t *p = StackPerson(s, 0xa);
        if (id16 < 0) Raise(6);
        const uint16_t len = static_cast<uint16_t>(I16(p, 0x8c));
        if (id16 >= static_cast<int16_t>(len)) Raise(6);
        BindRef(dest, reinterpret_cast<int16_t *>(PtrAt(p, 0x88)) + id16, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 2: {
        if (id16 < 0) Raise(6);
        const uint16_t len = static_cast<uint16_t>(I16(s, 0x90));
        if (id16 >= static_cast<int16_t>(len)) Raise(6);
        // The original inlines the same binding as 0x53d9c0.
        BindRef(dest, reinterpret_cast<int16_t *>(PtrAt(s, 0x8c)) + id16, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 3: {
        if (idu >= 0x72) Raise(7);
        BindRef(dest, reinterpret_cast<int16_t *>(s + 0xa2) + id16, idArg, 0x72);
        value = static_cast<uint16_t>(dest->Get());
        // Reading attribute 0x19 tells the context's slot-26 object about this object's value at +0x1c8.
        if (idu == 0x19) Call<void>(ctx26, 72, static_cast<int32_t>(I16(s, 0x1c8)));
        break;
    }
    case 4: {
        if (idu >= 0x72) Raise(7);
        uint8_t *p = StackPerson(s, 0xa);
        BindRef(dest, reinterpret_cast<int16_t *>(p + 0xa6) + id16, idArg, 0x72);
        value = static_cast<uint16_t>(dest->Get());
        if (idu == 0x19) Call<void>(ctx26, 72, static_cast<int32_t>(static_cast<int16_t>(Call<uint16_t>(p, 47))));
        break;
    }
    case 5: {
        uint8_t *p = StackPerson(s, 0xa);
        if (id16 < 0) Raise(6);
        const uint16_t len = static_cast<uint16_t>(I16(p, 0x94));
        if (id16 >= static_cast<int16_t>(len)) Raise(6);
        BindRef(dest, reinterpret_cast<int16_t *>(PtrAt(p, 0x90)) + id16, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 6: {
        if (idu > 0x3b) Raise(0x3f);
        void *o = Call<void *>(context, 3);
        int16_t *p = Call<int16_t *>(o, 17, idArg);
        BindRef(dest, p, idArg, 0x3c);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 7:
        value = idu;
        break;
    case 8: {
        if (idu > 7) Raise(2);
        BindRef(dest, reinterpret_cast<int16_t *>(s + 0x92) + id16, idArg, 8);
        value = static_cast<uint16_t>(dest->Get());
        clearable = false;
        break;
    }
    case 9: {
        if (id16 < 0) Raise(8);
        uint8_t *e1 = StackElement(s);
        const uint32_t count = Call<uint8_t>(e1, 26);
        if (static_cast<uint32_t>(idSigned) >= count) Raise(8);
        uint8_t *e2 = StackElement(s);
        uint8_t *e3 = StackElement(s);
        const uint32_t len = Call<uint8_t>(e2, 26);
        int16_t *p = Call<int16_t *>(e3, 35, idSigned);
        BindRef(dest, p, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        clearable = false;
        break;
    }
    case 10: {
        if (static_cast<uint16_t>(idArg) != 0) Raise(0xf);
        // Slot 23 of the secondary subobject gives a depth; slot 22 the object one below the top... of that depth.
        const uint16_t depth = Call<uint16_t>(s, 23);
        const uint32_t which = static_cast<uint16_t>(depth + static_cast<uint16_t>(~idArg));
        void *entry = Call<void *>(s, 22, which);
        int16_t *p = Call<int16_t *>(entry, 11);
        d->mLength = p != nullptr ? 1 : 0;
        d->mIndex = 0;
        d->mPtr = p;
        d->mMin = INT16_MIN;
        d->mMax = INT16_MAX;
        value = static_cast<uint16_t>(dest->Get());
        clearable = false;
        break;
    }
    case 11: {
        if (idu > 7) Raise(2);
        const uint16_t t = static_cast<uint16_t>(I16(s, 0x92 + id16 * 2));
        if (t > 7) Raise(2);
        BindRef(dest, reinterpret_cast<int16_t *>(s + 0x92) + static_cast<int16_t>(t), t, 8);
        value = static_cast<uint16_t>(dest->Get());
        clearable = false;
        break;
    }
    case 13: {
        uint8_t *p = StackPerson(s, 0xa);
        if (idu > 7) Raise(2);
        BindRef(dest, reinterpret_cast<int16_t *>(p + 0x96) + id16, idArg, 8);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 14: {
        if (idu > 0xf) Raise(0x1a);
        if (As<ifaces::cTSObject>(object)->ObjectKind() != 2) Raise(0x1c);
        Held held;
        if (Call<uint8_t>(object, 0, kIidPerson, &held.mRef.mObject)) value = FloatAttribute(held.mRef, idSigned, floatDest);
        break;
    }
    case 15: {
        if (idu > 0xf) Raise(0x1a);
        uint8_t *p = StackPerson(s, 0x15);
        if (Call<int32_t>(p, 98) != 2) Raise(0x1c);
        Held held;
        if (Call<uint8_t>(p, 0, kIidPerson, &held.mRef.mObject)) value = FloatAttribute(held.mRef, idSigned, floatDest);
        break;
    }
    case 16: {
        uint8_t *p = StackPerson(s, 0x15);
        void *q = Call<void *>(p, 68, idSigned);
        if (!q) Raise(0x1b);
        value = Call<uint16_t>(q, 4);
        break;
    }
    case 17: {
        if (idu > 7) Raise(2);
        const int16_t t = I16(s, 0x92 + id16 * 2);
        if (t < 0 || t >= 0x10) Raise(0x1a);
        uint8_t *p = StackPerson(s, 0x15);
        if (Call<int32_t>(p, 98) != 2) Raise(0x1c);
        Held held;
        if (Call<uint8_t>(p, 0, kIidPerson, &held.mRef.mObject)) {
            // The temps value is read again for each call.
            const float f = Call<float>(held.mRef.mObject, 13, static_cast<int32_t>(I16(s, 0x92 + id16 * 2)));
            value = static_cast<uint16_t>(RoundHalfAway(f));
            if (floatDest) *floatDest = Call<uint32_t>(held.mRef.mObject, 15, static_cast<int32_t>(I16(s, 0x92 + id16 * 2)));
        }
        break;
    }
    case 19: {
        if (idu > 0xe6) Raise(0x21);
        uint8_t *p = StackPerson(s, 0x15);
        if (Call<int32_t>(p, 98) != 2) Raise(0x1c);
        Held held;
        if (Call<uint8_t>(p, 0, kIidPerson, &held.mRef.mObject)) {
            int16_t *ptr = Call<int16_t *>(held.mRef.mObject, 12);
            MakeRef(&tmp, ptr, idArg, 0xe7);
            *dest = tmp;
            value = static_cast<uint16_t>(dest->Get());
        }
        break;
    }
    case 20: {
        void *q = Call<void *>(object, 68, idSigned);
        if (!q) Raise(0x1b);
        value = Call<uint16_t>(q, 4);
        break;
    }
    case 21: {
        uint8_t *p = StackPerson(s, 0xa);
        uint8_t *q = Call<uint8_t *>(p, 64);
        uint32_t out = 0;
        if (!Call<uint8_t>(q, 4, idSigned, &out)) Raise(0x23);
        BindObjectData(q, idArg, dest);
        value = Call<uint16_t>(q, 5, idSigned);
        break;
    }
    case 22: {
        if (id16 < 0) Raise(8);
        uint8_t *e1 = StackElement(s);
        const uint32_t count = Call<uint8_t>(e1, 26);
        if (static_cast<uint32_t>(idSigned) >= count) Raise(8);
        uint8_t *e2 = StackElement(s);
        const uint32_t q = Call<uint16_t>(e2, 30, idSigned);
        uint8_t *e3 = StackElement(s);
        uint8_t *p = StackPersonUnchecked(s, e3);
        // ORIGINAL-BUG: p is not checked; without a stack object the next read faults at address 0x8c.
        if (static_cast<int16_t>(q) >= I16(p, 0x8c)) Raise(6);
        uint8_t *e4 = StackElement(s);
        int16_t *ptr = nullptr;
        if (static_cast<int16_t>(q) >= 0 && static_cast<int16_t>(q) < I16(p, 0x8c))
            ptr = reinterpret_cast<int16_t *>(PtrAt(p, 0x88)) + static_cast<int16_t>(q);
        const uint32_t len = Call<uint8_t>(e4, 26);
        BindRef(dest, ptr, q, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 23: {
        const uint32_t v = static_cast<uint16_t>(I16(s, 0x92));
        const CachedFn cached = OriginalFunction<CachedFn, 0x212e61>();
        switch (id16) {
        case 0: {
            const float f = Call<float>(object, 185, v);
            // single-precision arithmetic as the compiler emitted it
            float t = f - 0.25f;
            t = t * 100.0f;
            t = t * 4.0f;
            const uint16_t r = static_cast<uint16_t>(_mm_cvttss_si32(_mm_set_ss(t)));
            if (static_cast<int16_t>(r) < 0) value = 0;
            else value = static_cast<int16_t>(r) > 0x64 ? 0x64 : r;
            break;
        }
        case 1:
            value = (static_cast<uint16_t>(OriginalFunction<U32Fn, 0x7decd0>()(v, 2, 0)) == 0) ? 1 : 0;
            break;
        case 2: {
            void *service = cached();
            void *y = Call<void *>(service, 39);
            uint8_t buf[32] = {};
            Call<void>(y, 77, buf, v, 2);
            value = static_cast<uint16_t>(I16(buf, 8));
            break;
        }
        case 3:
            value = static_cast<uint16_t>(worldheights::RoomAreaInTiles(static_cast<int32_t>(v), 2));
            break;
        case 4: {
            void *service = cached();
            void *y = Call<void *>(service, 39);
            uint8_t buf[32] = {};
            Call<void>(y, 77, buf, v, 2);
            uint32_t key = Call<uint32_t>(service, 58, buf);
            value = static_cast<uint8_t>(OriginalFunction<PtrFn, 0x7e24f0>()(&key));
            break;
        }
        case 5: {
            // Count the entries of the table's slot-31 list whose slot-25(0x10000) is false, slot-13(0x3b) is 0x16 and
            // slot-131 equals the value. The list's sentinel is read again at each step.
            uint8_t *list = Call<uint8_t *>(PtrAt(s, 0x1c0), 31);
            uint8_t *node = PtrAt(PtrAt(list, 0), 0);
            uint32_t count = 0;
            if (node != PtrAt(list, 0)) {
                do {
                    uint8_t *entry = PtrAt(node, 8);
                    if (Call<uint8_t>(entry, 25, 0x10000u) == 0) {
                        if (Call<uint16_t>(PtrAt(node, 8), 13, 0x3bu) == 0x16) {
                            if (Call<uint16_t>(PtrAt(node, 8), 131) == static_cast<uint16_t>(v)) ++count;
                        }
                    }
                    node = PtrAt(node, 0);
                } while (node != PtrAt(list, 0));
            }
            value = static_cast<uint16_t>(count);
            break;
        }
        case 6:
            value = static_cast<uint8_t>(worldheights::RoomMostlyFlagged(static_cast<int32_t>(v)));
            break;
        case 7:
            value = (static_cast<uint16_t>(OriginalFunction<U32Fn, 0x7decd0>()(v, 2, 1)) == 0) ? 1 : 0;
            break;
        default:
            Raise(0x29);
        }
        break;
    }
    case 24: {
        void *a = Call<void *>(context, 10);
        uint8_t *e = StackElement(s);
        int16_t *p = nullptr;
        value = Call<uint16_t>(a, 14, static_cast<uint32_t>(static_cast<uint16_t>(I16(e, 0xa))), idArg, &p);
        d->mLength = p ? 0xb : 0;
        d->mIndex = (p && id16 > 0 && id16 < d->mLength) ? id16 : 0;
        d->mPtr = p;
        d->mMin = INT16_MIN;
        d->mMax = INT16_MAX;
        break;
    }
    case 25: {
        if (id16 < 0) Raise(8);
        uint8_t *e1 = StackElement(s);
        const uint32_t count = Call<uint8_t>(e1, 27);
        if (static_cast<uint32_t>(idSigned) >= count) Raise(8);
        uint8_t *e2 = StackElement(s);
        uint8_t *e3 = StackElement(s);
        const uint32_t len = Call<uint8_t>(e2, 27);
        int16_t *p = Call<int16_t *>(e3, 36, idSigned);
        BindRef(dest, p, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        clearable = false;
        break;
    }
    case 26: {
        uint8_t *e1 = StackElement(s);
        void *x = Call<void *>(e1, 20);
        void *y = Call<void *>(x, 37);
        // The two output slots start as the frame's argument slots: the context result and the id.
        uint32_t slotValue = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx26));
        int32_t slotIndex = static_cast<int32_t>(idArg);
        if (!Call<uint8_t>(y, 11, idArg, &slotValue, &slotIndex)) Raise(0x2e);
        Held held;
        uint8_t *e2 = StackElement(s);
        void *x2 = Call<void *>(e2, 20);
        Call<void>(x2, 29, slotValue, kIidList, &held.mRef.mObject);
        if (!held.mRef.mObject || slotIndex < 0) Raise(0x2e);
        const int32_t n = Call<int32_t>(held.mRef.mObject, 9);
        if (slotIndex >= n) Raise(0x2e);
        value = Call<uint16_t>(held.mRef.mObject, 11, slotIndex);
        break;
    }
    case 31: {
        uint8_t *p = StackPerson(s, 0x15);
        if (Call<int32_t>(p, 98) != 2) Raise(0x1c);
        if (idu > 7) Raise(2);
        const uint16_t t = static_cast<uint16_t>(I16(s, 0x92 + id16 * 2));
        if (t > 0xe6) Raise(0x21);
        Held held;
        if (Call<uint8_t>(p, 0, kIidPerson, &held.mRef.mObject)) {
            int16_t *ptr = Call<int16_t *>(held.mRef.mObject, 12);
            MakeRef(&tmp, ptr, t, 0xe7);
            *dest = tmp;
            value = static_cast<uint16_t>(dest->Get());
        }
        break;
    }
    case 32: {
        void *a = Call<void *>(context, 10);
        uint8_t *e = StackElement(s);
        void *obj = Call<void *>(a, 16, static_cast<int32_t>(I16(e, 0xa)), 1);
        if (!obj) Raise(0x37);
        if (idu > 0xe6) Raise(0x21);
        int16_t *ptr = Call<int16_t *>(obj, 18, arg7);
        MakeRef(&tmp, ptr, idArg, 0xe7);
        *dest = tmp;
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 33:
        Raise(0x3a);
    case 34: {
        if (static_cast<uint16_t>(idArg - 1) > 0xe) Raise(0x3c);
        void *x = Call<void *>(context, 6);
        value = Call<uint16_t>(x, 24, idSigned);
        break;
    }
    case 35: {
        uint8_t *p = StackPerson(s, 0xa);
        void *q = Call<void *>(p, 91);
        value = Call<uint16_t>(q, 8, idSigned) != 0 ? 1 : 0;
        break;
    }
    case 36: {
        if (id16 < 0) Raise(0x40);
        void *a1 = Call<void *>(object, 61);
        const int32_t count = Call<int32_t>(a1, 46);
        if (idSigned >= count) Raise(0x40);
        void *a2 = Call<void *>(object, 61);
        void *a3 = Call<void *>(object, 61);
        const uint16_t len = Call<uint16_t>(a2, 46);
        int16_t *ptr = Call<int16_t *>(a3, 47);
        MakeRef(&tmp, ptr, idArg, len);
        *dest = tmp;
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 37: {
        uint8_t *p = StackPerson(s, 0xa);
        if (id16 < 0) Raise(0x40);
        void *a1 = Call<void *>(p, 61);
        const int32_t count = Call<int32_t>(a1, 46);
        if (idSigned >= count) Raise(0x40);
        void *a2 = Call<void *>(p, 61);
        void *a3 = Call<void *>(p, 61);
        const uint32_t len = Call<uint32_t>(a2, 46);
        int16_t *ptr = Call<int16_t *>(a3, 47);
        MakeRef(&tmp, ptr, idArg, len);
        *dest = tmp;
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 38: {
        void *a = Call<void *>(context, 10);
        uint8_t *e = StackElement(s);
        void *obj = Call<void *>(a, 16, static_cast<int32_t>(I16(e, 0xa)), 1);
        if (!obj) Raise(0x37);
        if (!Call<void *>(obj, 10)) return 0;
        {
            void *x = Call<void *>(obj, 10);
            if (!Call<void *>(x, 4)) Raise(0x23);
        }
        {
            void *x = Call<void *>(obj, 10);
            void *y = Call<void *>(x, 4);
            uint32_t out = idArg;
            if (!Call<uint8_t>(y, 4, idSigned, &out)) Raise(0x23);
        }
        void *x = Call<void *>(obj, 10);
        void *y = Call<void *>(x, 4);
        value = Call<uint16_t>(y, 5, idSigned);
        break;
    }
    case 39: {
        if (idu > 9) Raise(0x56);
        MakeRef(&tmp, reinterpret_cast<int16_t *>(s + 0x1ac), idArg, 10);
        *dest = tmp;
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 40: {
        uint8_t *p = StackPerson(s, 0xa);
        if (idu > 9) Raise(0x56);
        MakeRef(&tmp, reinterpret_cast<int16_t *>(p + 0x1b0), idArg, 10);
        *dest = tmp;
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 41: {
        if (id16 < 0) Raise(0x6c);
        const int32_t len = ByteSpan(PtrAt(s, 0x1a0), PtrAt(s, 0x1a4)) >> 1;
        if (static_cast<uint32_t>(idSigned) >= static_cast<uint32_t>(len)) Raise(0x6c);
        BindRef(dest, reinterpret_cast<int16_t *>(PtrAt(s, 0x1a0)) + id16, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 42: {
        uint8_t *p = StackPerson(s, 0x15);
        if (id16 < 0) Raise(0x6c);
        const int16_t n = static_cast<int16_t>(Call<uint16_t>(p, 266));
        if (id16 >= n) Raise(0x6c);
        int16_t *ptr = Call<int16_t *>(p, 269, idArg);
        const uint32_t len = Call<uint16_t>(p, 266);
        BindRef(dest, ptr, idArg, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 43: {
        if (id16 < 0) Raise(0x6c);
        const int32_t count = ByteSpan(PtrAt(s, 0x194), PtrAt(s, 0x198)) / 12;
        if (static_cast<uint32_t>(idSigned) >= static_cast<uint32_t>(count)) Raise(0x6c);
        const uint16_t v = *reinterpret_cast<const uint16_t *>(PtrAt(s, 0x1a0) + idSigned * 2);
        if (static_cast<int16_t>(v) < 0) Raise(0x6d);
        const uint8_t *range = PtrAt(s, 0x194) + idSigned * 12;
        int16_t *begin = reinterpret_cast<int16_t *>(PtrAt(range, 0));
        const int32_t n = ByteSpan(PtrAt(range, 0), PtrAt(range, 4)) >> 1;
        const int32_t vi = static_cast<int16_t>(v);
        if (static_cast<uint32_t>(vi) >= static_cast<uint32_t>(n)) Raise(0x6d);
        const int16_t len = static_cast<int16_t>(static_cast<uint16_t>(n));
        dest->mLength = len;
        if (static_cast<int16_t>(v) > 0 && static_cast<int16_t>(v) < len) {
            dest->mIndex = static_cast<int16_t>(v);
            dest->mPtr = begin + vi;
        } else {
            dest->mIndex = 0;
            dest->mPtr = static_cast<int16_t>(v) < len ? begin + vi : nullptr;
        }
        dest->mMin = INT16_MIN;
        dest->mMax = INT16_MAX;
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 44: {
        uint8_t *p = StackPerson(s, 0x15);
        const int16_t n = static_cast<int16_t>(Call<uint16_t>(p, 266));
        if (id16 < 0) Raise(0x6c);
        if (id16 >= n) Raise(0x6c);
        const uint16_t a = Call<uint16_t>(p, 273, idArg);
        const uint16_t b = Call<uint16_t>(p, 274, idArg);
        if (static_cast<int16_t>(b) < 0) Raise(0x6d);
        if (static_cast<int16_t>(b) >= static_cast<int16_t>(a)) Raise(0x6d);
        int16_t *ptr = Call<int16_t *>(p, 271, idArg);
        BindRef(dest, ptr, b, a);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 45: {
        if (id16 < 0) Raise(0x6c);
        const int32_t count = ByteSpan(PtrAt(s, 0x194), PtrAt(s, 0x198)) / 12;
        if (static_cast<uint32_t>(idSigned) >= static_cast<uint32_t>(count)) Raise(0x6c);
        const uint16_t t = static_cast<uint16_t>(I16(s, 0x92));
        if (static_cast<int16_t>(t) < 0) Raise(0x6d);
        const uint8_t *range = PtrAt(s, 0x194) + idSigned * 12;
        int16_t *begin = reinterpret_cast<int16_t *>(PtrAt(range, 0));
        const int32_t n = ByteSpan(PtrAt(range, 0), PtrAt(range, 4)) >> 1;
        const int32_t ti = static_cast<int16_t>(t);
        if (static_cast<uint32_t>(ti) >= static_cast<uint32_t>(n)) Raise(0x6d);
        BindRef(dest, begin + ti, t, n);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 46: {
        uint8_t *p = StackPerson(s, 0x15);
        if (id16 < 0) Raise(0x6c);
        const int16_t n = static_cast<int16_t>(Call<uint16_t>(p, 266));
        if (id16 >= n) Raise(0x6c);
        if (I16(s, 0x92) <= -1) Raise(0x6d);
        const int16_t m = static_cast<int16_t>(Call<uint16_t>(p, 273, idArg));
        const uint16_t t = static_cast<uint16_t>(I16(s, 0x92));
        if (static_cast<int16_t>(t) >= m) Raise(0x6d);
        int16_t *ptr = Call<int16_t *>(p, 267, idArg, static_cast<uint32_t>(t));
        const uint32_t len = Call<uint16_t>(p, 273, idArg);
        BindRef(dest, ptr, t, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 47: {
        uint8_t *e1 = StackElement(s);
        void *x = Call<void *>(e1, 20);
        void *y = Call<void *>(x, 37);
        uint32_t slotValue = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx26));
        uint32_t slotOwner = static_cast<uint32_t>(ownerArg);
        if (!Call<uint8_t>(y, 13, idArg, &slotValue, &slotOwner)) Raise(0x2e);
        Held held;
        uint8_t *e2 = StackElement(s);
        void *x2 = Call<void *>(e2, 20);
        Call<void>(x2, 29, slotValue, kIidList, &held.mRef.mObject);
        if (!held.mRef.mObject) Raise(0x2e);
        if (I16(s, 0x92 + static_cast<int16_t>(slotOwner) * 2) < 0) Raise(0x2e);
        const int32_t n = Call<int32_t>(held.mRef.mObject, 9);
        const int32_t index = I16(s, 0x92 + static_cast<int16_t>(slotOwner) * 2);
        if (index >= n) Raise(0x2e);
        value = Call<uint16_t>(held.mRef.mObject, 11, index);
        break;
    }
    case 48: {
        if (idu > 7) Raise(2);
        void *q = Call<void *>(object, 68, static_cast<int32_t>(I16(s, 0x92 + id16 * 2)));
        if (!q) Raise(0x1b);
        value = Call<uint16_t>(q, 4);
        break;
    }
    case 49: {
        if (idu > 7) Raise(2);
        uint8_t *p = StackPerson(s, 0x15);
        void *q = Call<void *>(p, 68, static_cast<int32_t>(I16(s, 0x92 + id16 * 2)));
        if (!q) Raise(0x1b);
        value = Call<uint16_t>(q, 4);
        break;
    }
    case 50: {
        if (id16 < 0) Raise(8);
        uint8_t *e1 = StackElement(s);
        const uint32_t count = Call<uint8_t>(e1, 26);
        if (static_cast<uint32_t>(idSigned) >= count) Raise(8);
        uint8_t *e2 = StackElement(s);
        const uint32_t q = Call<uint16_t>(e2, 30, idSigned);
        uint8_t *e3 = StackElement(s);
        uint8_t *p = StackPersonUnchecked(s, e3);
        // ORIGINAL-BUG: p is not checked; without a stack object the next call faults.
        const int16_t n = static_cast<int16_t>(Call<uint16_t>(p, 23));
        if (static_cast<int16_t>(q) >= n) Raise(6);
        uint8_t *e4 = StackElement(s);
        int16_t *ptr = nullptr;
        if (static_cast<int16_t>(q) >= 0 && static_cast<int16_t>(q) < I16(p, 0x94))
            ptr = reinterpret_cast<int16_t *>(PtrAt(p, 0x90)) + static_cast<int16_t>(q);
        const uint32_t len = Call<uint8_t>(e4, 26);
        BindRef(dest, ptr, q, len);
        value = static_cast<uint16_t>(dest->Get());
        break;
    }
    case 51: {
        uint8_t *p = StackPerson(s, 0xa);
        uint8_t *q = Call<uint8_t *>(p, 64);
        if (Call<uint8_t>(p, 104)) {
            Held held;
            if (Call<uint8_t>(p, 0, kIidInterface, &held.mRef.mObject)) {
                void *a = Call<void *>(p, 61);
                void *b = Call<void *>(a, 27);
                q = Call<uint8_t *>(b, 4);
            }
        }
        uint32_t out = 0;
        if (!Call<uint8_t>(q, 4, idSigned, &out)) Raise(0x23);
        BindObjectData(q, idArg, dest);
        value = Call<uint16_t>(q, 5, idSigned);
        break;
    }
    case 12:
    case 18:
    case 27:
    case 28:
    case 29:
    case 30:
    default:
        Raise(9);
    }

    // Owners that read a value (not the writable ones) clear the destination again when the caller asked for the
    // value only.
    if (static_cast<uint8_t>(clearFlag) != 0 && clearable) {
        dest->Reset();
        if (floatDest) *floatDest = 0;
    }
    return value & 0xffff;
}
