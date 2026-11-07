#include "game/original.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimXCommon.h"

using primX::Call;
using primX::HeldRef;

// GenericSimCall (opcode 0x01) cases, group B: cases that look an object up in the person list (the prologue's slot-10
// result, whose slot 16 takes (id, 1)) or in the entry list (the slot-9 result, whose slot 11 takes an id), and the
// attribute-check cases. Every case ends with the value the original leaves in eax (1 unless it says otherwise).

namespace {
constexpr uint32_t kDefaultResult = 1;

inline Key16 MakeKey(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return Key16{{a, b, c, d}};
}
// (low word of lo) | (hi << 16), both words sign-extended first as the original does.
inline uint32_t PackWords(int16_t lo, int16_t hi)
{
    return (static_cast<uint32_t>(static_cast<int32_t>(lo)) & 0xffffu) | (static_cast<uint32_t>(static_cast<int32_t>(hi)) << 16);
}
} // namespace

// Cases 19 and 20 share a tail after the id of the entry is known (Entry(id) finds it in the prologue's list).
// Returns once argument 2 has been set.
void GenericSimCall::AttributeTail(void *holder, int32_t id, bool refetchAttribute)
{
    if (id < 1 || id > 0x7cff) Throw(0x9a);
    void *const entry = Call<void *>(mL2c, 11, id);
    if (!entry) Throw(0x9a);
    const int32_t key = Call<int32_t>(entry, 42);
    const int32_t ref = Call<int32_t>(entry, 46);
    const int32_t attr = Call<int16_t>(mSelf->mField2c8, 15, 0x28u);
    void *const svc = GetCachedSingleton();
    void *const list = Call<void *>(svc, 8);
    void *const info = Call<void *>(list, 9, attr);
    if (ref != 0) {
        void *const sub = Call<void *>(holder, 9);
        if (Call<int32_t>(sub, 18) != ref && Call<uint8_t>(info, 31) != 0) {
            if (CheckAttribute(ref, attr)) SetArg(2, 1);
            return;
        }
    }
    // Case 19 asks the +0x2c8 object again for the word it passes; case 20 reuses the first answer.
    const int32_t second = refetchAttribute ? static_cast<int32_t>(Call<int16_t>(mSelf->mField2c8, 15, 0x28u)) : attr;
    if (CheckAttribute(key, second)) SetArg(2, 1);
}

// Case 19: argument 2 = -1; when the context's slot-23 object says yes (slot 24): the person-list object for argument 0
// (none throws 0x37) names the entry through its slot 16 with 0x3d; then the attribute tail.
uint32_t GenericSimCall::Case19()
{
    SetArg(2, 0xffffffffu);
    void *const holder = ContextSlot(23);
    if (Call<uint8_t>(holder, 24) == 0) return kDefaultResult;
    void *const person = Call<void *>(mL24, 16, static_cast<int32_t>(Arg(0)), 1u);
    if (!person) Throw(0x37);
    const int32_t id = Call<int16_t>(person, 16, 0x3du);
    AttributeTail(holder, id, true);
    return kDefaultResult;
}

// Cases 21, 22, 23: both arguments must name person-list objects (else throws 0x37; case 21 reads both arguments
// first, 22 and 23 check each right after reading it); then the context's slot-29 object, when
// there is one, gets the two ids as unsigned words: slot 9 with 4 (case 21), slot 9 with 2 (22), slot 8 (23).
uint32_t GenericSimCall::Case21()
{
    const int32_t a = Arg(0);
    const int32_t b = Arg(1);
    if (!Call<void *>(mL24, 16, a, 1u)) Throw(0x37);
    if (!Call<void *>(mL24, 16, b, 1u)) Throw(0x37);
    void *const target = ContextSlot(29);
    if (target) Call<void>(target, 9, uint32_t(uint16_t(a)), uint32_t(uint16_t(b)), 4u);
    return kDefaultResult;
}

uint32_t GenericSimCall::Case22()
{
    const int32_t a = Arg(0);
    if (!Call<void *>(mL24, 16, a, 1u)) Throw(0x37);
    const int32_t b = Arg(1);
    if (!Call<void *>(mL24, 16, b, 1u)) Throw(0x37);
    void *const target = ContextSlot(29);
    if (target) Call<void>(target, 9, uint32_t(uint16_t(a)), uint32_t(uint16_t(b)), 2u);
    return kDefaultResult;
}

uint32_t GenericSimCall::Case23()
{
    const int32_t a = Arg(0);
    if (!Call<void *>(mL24, 16, a, 1u)) Throw(0x37);
    const int32_t b = Arg(1);
    if (!Call<void *>(mL24, 16, b, 1u)) Throw(0x37);
    void *const target = ContextSlot(29);
    if (target) Call<void>(target, 8, uint32_t(uint16_t(a)), uint32_t(uint16_t(b)));
    return kDefaultResult;
}

// Case 25: argument 0 == 0: slot 40 of the object table with 0. Otherwise the object for argument 0 (none throws 0xa)
// is asked for its interface 0x5a69d3d5 (failing throws 0x1c) and that goes to slot 40.
uint32_t GenericSimCall::Case25()
{
    if (Arg(0) == 0) {
        Call<void>(mL1c, 40, 0u);
        return kDefaultResult;
    }
    void *const obj = Call<void *>(mL1c, 29, static_cast<int32_t>(Arg(0)));
    if (!obj) Throw(0xa);
    HeldRef iface;
    if ((Call<uint32_t>(obj, 0, 0x5a69d3d5u, &iface.mObject) & 0xff) != 0) {
        Call<void>(mL1c, 40, iface.mObject);
    } else {
        Throw(0x1c);
    }
    return kDefaultResult;
}

// Case 27: argument 0 = how many of the context's slot-7 list (walked with slot 19 first / slot 20 next) have an item
// whose slot-4 object answers 1 to slot 8.
uint32_t GenericSimCall::Case27()
{
    uint16_t count = 0;
    void *const list = ContextSlot(7);
    if (list) {
        void *item = Call<void *>(list, 19);
        while (item) {
            void *const inner = Call<void *>(item, 4);
            if (Call<int32_t>(inner, 8) == 1) ++count;
            item = Call<void *>(list, 20);
        }
    }
    SetArg(0, count);
    return kDefaultResult;
}

// Cases 54..58: the person-list object for argument 0 must exist (else throws 0x37); then the context's slot-4 object,
// when there is one, is read and/or written for that id as unsigned word.
//   54: (x = 4, y = 3) filled by slot 17; slot 50 with (x + 1, y)
//   55: (x = 4, y = 3) filled by slot 17; slot 50 with (x, y + 1)
//   56: (x = 1) filled by slot 19; slot 51 with x + 1
//   57: slot 50 with (4, 3), slot 51 with 1
//   58: x = 4, y = 3, z = 1 filled by slots 17 and 19; arguments 1..3 = the low words (0, 0, 0 when there is no slot-4 object)
void GenericSimCall::RequirePerson(int32_t id)
{
    if (!Call<void *>(mL24, 16, id, 1u)) Throw(0x37);
}

uint32_t GenericSimCall::Case54()
{
    const int32_t id = Arg(0);
    RequirePerson(id);
    void *const target = ContextSlot(4);
    if (target) {
        int32_t x = 4, y = 3;
        Call<void>(target, 17, uint32_t(uint16_t(id)), &x, &y);
        Call<void>(target, 50, uint32_t(uint16_t(id)), static_cast<uint32_t>(x + 1), static_cast<uint32_t>(y));
    }
    return kDefaultResult;
}

uint32_t GenericSimCall::Case55()
{
    const int32_t id = Arg(0);
    RequirePerson(id);
    void *const target = ContextSlot(4);
    if (target) {
        int32_t x = 4, y = 3;
        Call<void>(target, 17, uint32_t(uint16_t(id)), &x, &y);
        Call<void>(target, 50, uint32_t(uint16_t(id)), static_cast<uint32_t>(x), static_cast<uint32_t>(y + 1));
    }
    return kDefaultResult;
}

uint32_t GenericSimCall::Case56()
{
    const int32_t id = Arg(0);
    RequirePerson(id);
    void *const target = ContextSlot(4);
    if (target) {
        int32_t x = 1;
        Call<void>(target, 19, uint32_t(uint16_t(id)), &x);
        Call<void>(target, 51, uint32_t(uint16_t(id)), static_cast<uint32_t>(x + 1));
    }
    return kDefaultResult;
}

uint32_t GenericSimCall::Case57()
{
    const int32_t id = Arg(0);
    RequirePerson(id);
    void *const target = ContextSlot(4);
    if (target) {
        Call<void>(target, 50, uint32_t(uint16_t(id)), 4u, 3u);
        Call<void>(target, 51, uint32_t(uint16_t(id)), 1u);
    }
    return kDefaultResult;
}

uint32_t GenericSimCall::Case58()
{
    const int32_t id = Arg(0);
    RequirePerson(id);
    void *const target = ContextSlot(4);
    if (target) {
        int32_t x = 4, y = 3, z = 1;
        Call<void>(target, 17, uint32_t(uint16_t(id)), &x, &y);
        Call<void>(target, 19, uint32_t(uint16_t(id)), &z);
        SetArg(1, uint16_t(x));
        SetArg(2, uint16_t(y));
        SetArg(3, uint16_t(z));
    } else {
        SetArg(1, 0);
        SetArg(2, 0);
        SetArg(3, 0);
    }
    return kDefaultResult;
}

// Cases 82 and 83: build a four-dword key from arguments 7,6 / 5,4 / 3,2 (each pair: low word | high word << 16, last
// dword 0); the person-list object for argument 1 (none throws 0x37) names, through its slot 16 with 0x3d, an entry of the
// prologue's list (a value in 1..0x7cff, unsigned compare; no entry throws 0x9a). Case 82 calls slot 10 of the entry
// with the key, case 83 slot 12 (its answer is dropped).
void *GenericSimCall::KeyedEntry(Key16 &key)
{
    // The original reads the words in this order (the last pair first, each pair low word first); the order of C++ call arguments
    // is unspecified, so each read is its own statement (lane Z4: ours had read each pair backwards).
    const int16_t a3 = Arg(3);
    const int16_t a2 = Arg(2);
    const uint32_t third = PackWords(a3, a2);
    const int16_t a5 = Arg(5);
    const int16_t a4 = Arg(4);
    const uint32_t second = PackWords(a5, a4);
    const int16_t a7 = Arg(7);
    const int16_t a6 = Arg(6);
    const uint32_t first = PackWords(a7, a6);
    key = MakeKey(first, second, third, 0);
    void *const person = Call<void *>(mL24, 16, static_cast<int32_t>(Arg(1)), 1u);
    if (!person) Throw(0x37);
    void *entry = nullptr;
    const int32_t id = Call<int16_t>(person, 16, 0x3du);
    if (static_cast<uint32_t>(id) >= 1 && static_cast<uint32_t>(id) <= 0x7cff) entry = Call<void *>(mL2c, 11, id);
    if (!entry) Throw(0x9a);
    return entry;
}

uint32_t GenericSimCall::Case82()
{
    Key16 key;
    void *const entry = KeyedEntry(key);
    Call<void>(entry, 10, &key);
    return kDefaultResult;
}

uint32_t GenericSimCall::Case83()
{
    Key16 key;
    void *const entry = KeyedEntry(key);
    // The branch result is the entry's answer (1 when its low byte is nonzero, else 0); lane Z4: ours dropped it and always gave 1.
    return (Call<uint32_t>(entry, 12, &key) & 0xff) != 0 ? 1u : 0u;
}
