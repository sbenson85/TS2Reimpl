#include "simantics/PrimW.h"

#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"

#include <cstring>

namespace {

// A call through slot `slot` of `obj`'s vtable, __thiscall, the arguments pushed as written.
template <typename R, typename... A> inline R Call(const void *obj, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(obj, slot)(const_cast<void *>(obj), args...);
}

inline uint16_t Word(const uint8_t *p, size_t off)
{
    uint16_t v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

inline uint32_t Dword(const uint8_t *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

[[noreturn]] inline void Raise(int16_t code)
{
    throw code;
}

// The small parameter accessors the original calls (0x57e7a0, 0x57e7d0, 0x57e540): two 5-bit numbers and a 3-bit
// type spread over byte +4 and byte +0xd.
inline uint16_t NumberA(const uint8_t *p)
{
    return static_cast<uint16_t>(((p[0xd] & 3) << 3) | ((p[4] >> 4) & 3));
}

inline uint16_t NumberB(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0xd] >> 2) & 0x1f);
}

inline uint8_t TypeBits(const uint8_t *p)
{
    return static_cast<uint8_t>((p[4] >> 4) & 7);
}

// An operand of the node: scope byte and id word.
struct Operand {
    uint8_t mScope;
    uint16_t mId;
};

// A reference the original keeps in its frame. While the frame is in the state that has an unwind action, a throw
// releases it (0x145d50); the explicit releases the code writes out move the frame to a state without one.
struct Held {
    void *mObject = nullptr;
    bool mArmed = false;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept
    {
        if (mArmed) reinterpret_cast<RZAutoRef *>(&mObject)->ReleaseUnguarded();
    }
    // The explicit release (null check, then slot 2).
    void ReleaseNow()
    {
        mArmed = false;
        if (mObject) Call<void>(mObject, 2);
    }
};

// The operand read: slot 7 of the secondary subobject (this + 4), seven stack arguments, the value in ax.
inline int16_t Read(PrimWObject *self, Operand op, SimDataRef *dest = nullptr)
{
    void *sub = &self->mSecondary;
    return Call<int16_t>(sub, 7, static_cast<uint32_t>(op.mScope), static_cast<uint32_t>(op.mId), dest, static_cast<float **>(nullptr),
                         static_cast<uint32_t *>(nullptr), 0u, 0u);
}

// The person for a selector: kinds 0..3 through the context holder's slot 6 object (slots 36..39, the id for 1..3),
// kind 4 the context holder's own slot 17; any other kind gives none.
void *ResolvePerson(PrimWObject *self, uint32_t kind, uint32_t id)
{
    void *holder = self->mContextHolder;
    switch (kind) {
    case 0: return Call<void *>(Call<void *>(holder, 6), 36);
    case 1: return Call<void *>(Call<void *>(holder, 6), 37, id);
    case 2: return Call<void *>(Call<void *>(holder, 6), 38, id);
    case 3: return Call<void *>(Call<void *>(holder, 6), 39, id);
    case 4: return Call<void *>(holder, 17);
    default: return nullptr;
    }
}

// An iterator getter (begin/end): the hidden result slot is a local, the value is read through the returned pointer.
inline uint32_t Iter(void *inv, int slot)
{
    uint32_t hidden = 0;
    return *Call<const uint32_t *>(inv, slot, &hidden);
}

// The position of iterator `it` from the begin iterator (getter `beginSlot`), in elements.
inline int32_t IndexOf(void *inv, int beginSlot, uint32_t it)
{
    uint32_t hidden = 0;
    const uint32_t *begin = Call<const uint32_t *>(inv, beginSlot, &hidden);
    return static_cast<int32_t>(it - *begin) >> 2;
}

inline void *ElementAt(uint32_t it)
{
    return *reinterpret_cast<void **>(it);
}

constexpr uint32_t kHelperClass = 0x4aba86f3;
constexpr uint32_t kHelperInterface = 0x4aba86dd;

// Reads the length of the inventory's list through its begin/end getters (the original computes the count and never
// uses it; the getters are still called, begin first).
inline void CallListGetters(void *inv, int beginSlot, int endSlot)
{
    uint32_t hiddenBegin = 0, hiddenEnd = 0;
    (void)Call<const uint32_t *>(inv, beginSlot, &hiddenBegin);
    (void)Call<const uint32_t *>(inv, endSlot, &hiddenEnd);
}

} // namespace

uint32_t PrimWObject::Prim33(void *frame, const PrimParamView *param)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(param);
    const Operand opA{p[5], Word(p, 6)};
    const Operand opB{p[0xe], Word(p, 0xf)};
    const Operand opC{p[0x11], Word(p, 0x12)};
    const Operand opD{p[0xa], Word(p, 0xb)};

    const uint32_t opcode = p[8];
    uint32_t kind = p[4] & 7;
    int32_t flag = (p[4] & 8) >> 3;
    uint16_t id;
    if (flag == 1 || opcode != 9) {
        id = static_cast<uint16_t>(Read(this, opA));
    } else {
        // Opcode 9 in mode 0 reuses the selector the last remembered entry was found with.
        kind = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(mSelKind)));
        flag = static_cast<int16_t>(mSelFlag);
        id = mSelId;
    }

    void *inv = ResolvePerson(this, kind, id);
    if (!inv) Raise(0x57);

    // The token: the dword at +9, or (for some operations) the id of the stack object's owner data.
    uint32_t token = 0;
    const uint32_t tokenField = Dword(p, 9);
    if (tokenField == 0) {
        if ((flag == 0 && (opcode == 0 || opcode == 2 || opcode == 3)) ||
            (flag == 1 && (opcode == 0 || opcode == 2 || opcode == 4 || opcode == 6 || opcode == 7))) {
            void *table = Call<void *>(this, 63);
            const int16_t stackId = Call<int16_t>(frame, 12);
            void *owner = Call<void *>(table, 29, static_cast<int32_t>(stackId));
            if (!owner) Raise(0x15);
            void *ownerData = Call<void *>(owner, 61);
            token = Call<uint32_t>(ownerData, 28);
        }
    } else {
        token = tokenField;
    }
    const uint32_t cat = p[0xd];

    // ---- mode 0 -------------------------------------------------------------------------------------------------
    if (flag == 0) {
        CallListGetters(inv, 28, 29);
        switch (opcode) {
        case 0: {
            const int16_t r = Call<int16_t>(inv, 16, token, static_cast<uint32_t>(NumberA(p)), 0u);
            if (r < 0) return 0;
            break;
        }
        case 1: {
            const int16_t r = Read(this, opB);
            const uint32_t index = static_cast<uint16_t>(r - 1);
            if (Call<uint8_t>(inv, 20, index) == 0) return 0;
            break;
        }
        case 2: {
            const uint32_t a = NumberA(p);
            const uint32_t b = NumberB(p);
            if (Call<uint8_t>(inv, 18, token, a, b, 0u) == 0) return 0;
            break;
        }
        case 3: {
            SimDataRef dest;
            dest.Reset();
            const uint16_t r = static_cast<uint16_t>(Read(this, opB, &dest));
            if (!dest.HasData()) Raise(0x13);
            const uint32_t a = NumberA(p);
            const uint32_t b = NumberB(p);
            const uint32_t flagBit = (p[4] & 0x80) != 0;
            uint32_t found = 0;
            Call<void>(inv, 25, &found, token, static_cast<uint32_t>(r), a, b, flagBit, 0u);
            if (found == Iter(inv, 29)) return 0;
            const int32_t position = IndexOf(inv, 28, found);
            if (!dest.Set(static_cast<int16_t>(position) + 1)) Raise(0x14);
            break;
        }
        case 4: {
            const int16_t r1 = Read(this, opB);
            const int16_t r2 = Read(this, opC);
            const uint32_t index = static_cast<uint16_t>(r1 - 1);
            uint32_t it = 0;
            Call<void>(inv, 21, &it, index);
            if (it == Iter(inv, 29)) return 0;
            Call<void>(ElementAt(it), 20, static_cast<uint32_t>(static_cast<uint16_t>(r2)));
            break;
        }
        case 5: {
            const int16_t r1 = Read(this, opB);
            SimDataRef dest;
            dest.Reset();
            uint16_t value = static_cast<uint16_t>(Read(this, opC, &dest));
            const uint32_t index = static_cast<uint16_t>(r1 - 1);
            uint32_t it = 0;
            Call<void>(inv, 21, &it, index);
            if (it == Iter(inv, 29)) return 0;
            if (Call<uint8_t>(ElementAt(it), 19, &value) == 0) return 0;
            if (!dest.HasData()) Raise(0x59);
            if (!dest.Set(static_cast<int32_t>(value))) Raise(0x14);
            break;
        }
        case 6: {
            const int16_t r1 = Read(this, opB);
            const uint16_t index = static_cast<uint16_t>(r1 - 1);
            uint32_t it = 0;
            Call<void>(inv, 21, &it, static_cast<uint32_t>(index));
            if (it == Iter(inv, 29)) return 0;
            Call<void>(mCursor, 29, ElementAt(it));
            mSelKind = static_cast<uint16_t>(kind);
            mSelId = id;
            mSelFlag = static_cast<uint16_t>(flag);
            mSelIndex = index;
            const uint32_t value = Call<uint32_t>(ElementAt(it), 6);
            mEntryHigh = static_cast<uint16_t>(value >> 16);
            mEntryLow = static_cast<uint16_t>(value);
            mEntryWord = Call<uint16_t>(ElementAt(it), 8);
            mEntryWord2 = Call<uint16_t>(ElementAt(it), 18);
            break;
        }
        case 7: {
            const int16_t r1 = Read(this, opB);
            const uint32_t index = static_cast<uint16_t>(r1 - 1);
            SimDataRef dest;
            dest.Reset();
            (void)Read(this, opC, &dest);
            const uint16_t *at = nullptr;
            Call<void>(mCursor, 24, &at, index);
            uint32_t hidden = 0;
            const uint16_t *const *end = Call<const uint16_t *const *>(mCursor, 26, &hidden);
            if (at == *end) return 0;
            if (!dest.HasData()) Raise(0x59);
            if (!dest.Set(static_cast<int32_t>(*at))) Raise(0x14);
            break;
        }
        case 8: {
            const int16_t r1 = Read(this, opB);
            const uint32_t index = static_cast<uint16_t>(r1 - 1);
            const int16_t r2 = Read(this, opC);
            uint32_t at = 0;
            Call<void>(mCursor, 24, &at, index);
            uint32_t hidden = 0;
            const uint32_t *end = Call<const uint32_t *>(mCursor, 26, &hidden);
            if (at == *end) return 0;
            Call<void>(mCursor, 22, index, static_cast<uint32_t>(static_cast<uint16_t>(r2)));
            break;
        }
        case 9: {
            uint32_t it = 0;
            Call<void>(inv, 21, &it, static_cast<uint32_t>(mSelIndex));
            if (it == Iter(inv, 29)) return 0;
            void *entry = ElementAt(it);
            const int32_t cursorValue = Call<int32_t>(mCursor, 27);
            const int32_t entryValue = Call<int32_t>(entry, 27);
            if (entryValue != cursorValue) {
                if (Call<int32_t>(mCursor, 27) == 0) Raise(0x9c);
                if (Call<int32_t>(entry, 27) == 0) Raise(0x9d);
                return 0;
            }
            Call<void>(entry, 29, mCursor);
            break;
        }
        case 10: {
            SimDataRef dest;
            dest.Reset();
            (void)Read(this, opC, &dest);
            if (!dest.HasData()) Raise(0x59);
            const uint32_t value = Call<uint32_t>(inv, 31);
            if (!dest.Set(static_cast<int32_t>(value))) Raise(0x14);
            break;
        }
        case 0xb: {
            Held helper;
            helper.mArmed = true;
            void *framework = GetDirectorFrameworkObject();
            const bool got = Call<uint8_t>(framework, 3, kHelperClass, kHelperInterface, &helper.mObject) != 0;
            if (got) {
                if (Call<int32_t>(mCursor, 27) != 0) {
                    if (Call<int32_t>(mCursor, 27) != 0) Raise(0x9c);
                    helper.ReleaseNow();
                    return 0;
                }
                Call<void>(helper.mObject, 29, mCursor);
                Call<void>(helper.mObject, 9, static_cast<uint32_t>(mEntryWord));
                const int16_t r = Call<int16_t>(inv, 15, helper.mObject);
                if (r < 0) {
                    helper.ReleaseNow();
                    return 0;
                }
            }
            helper.ReleaseNow();
            break;
        }
        case 0xc: {
            SimDataRef dest;
            dest.Reset();
            const uint16_t r = static_cast<uint16_t>(Read(this, opB, &dest));
            if (!dest.HasData()) Raise(0x13);
            const uint32_t a = NumberA(p);
            const uint32_t b = NumberB(p);
            const uint32_t flagBit = (p[4] & 0x80) != 0;
            uint32_t found = 0;
            Call<void>(inv, 24, &found, static_cast<uint32_t>(r), a, b, flagBit, 0u);
            if (found == Iter(inv, 29)) return 0;
            const int32_t position = IndexOf(inv, 28, found);
            if (!dest.Set(static_cast<int16_t>(position) + 1)) Raise(0x14);
            break;
        }
        case 0xd: {
            SimDataRef dest;
            dest.Reset();
            (void)Read(this, opC, &dest);
            if (!dest.HasData()) Raise(0x59);
            const uint32_t a = NumberA(p);
            const uint32_t b = NumberB(p);
            const uint32_t value = Call<uint32_t>(inv, 30, a, b, 0u);
            if (!dest.Set(static_cast<int32_t>(value))) Raise(0x14);
            break;
        }
        case 0xe: {
            const int16_t r1 = Read(this, opB);
            const int16_t r2 = Read(this, opC);
            const int16_t r3 = Read(this, opD);
            const uint32_t first = static_cast<uint16_t>(r1 - 1);
            const uint32_t index = static_cast<uint16_t>(r3 - 1);
            uint32_t it = 0;
            Call<void>(inv, 21, &it, index);
            if (it == Iter(inv, 29)) return 0;
            Call<void>(ElementAt(it), 22, first, static_cast<uint32_t>(static_cast<uint16_t>(r2)));
            break;
        }
        case 0xf: {
            const int16_t r1 = Read(this, opD);
            const int16_t r2 = Read(this, opB);
            const uint32_t second = static_cast<uint16_t>(r2 - 1);
            const uint32_t first = static_cast<uint16_t>(r1 - 1);
            SimDataRef dest;
            dest.Reset();
            (void)Read(this, opC, &dest);
            uint32_t it = 0;
            Call<void>(inv, 21, &it, first);
            if (it == Iter(inv, 29)) return 0;
            const uint16_t value = Call<uint16_t>(ElementAt(it), 21, second);
            if (!dest.HasData()) Raise(0x59);
            if (!dest.Set(static_cast<int32_t>(value))) Raise(0x14);
            break;
        }
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13: {
            void *table = Call<void *>(mContextHolder, 26);
            const int16_t stackId = Call<int16_t>(frame, 12);
            void *stackObject = Call<void *>(table, 29, static_cast<int32_t>(stackId));
            // The result of the cached service getter is not used; the call fills its cache.
            (void)CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
            const int32_t slot46 = Call<int32_t>(inv, 46);
            if (slot46 == 0) return 0;
            switch (opcode - 0x10) {
            case 0:
            case 3: {
                if (!stackObject) return 0;
                const uint32_t isNew = opcode == 0x13;
                const uint16_t w = Call<uint16_t>(stackObject, 47, static_cast<uint32_t>(slot46), 0u, isNew);
                const int32_t newEntry = Call<int32_t>(table, 87, static_cast<uint32_t>(w));
                if (newEntry != 0) {
                    const uint32_t number = NumberA(p);
                    Held helper;
                    helper.mArmed = true;
                    void *framework = GetDirectorFrameworkObject();
                    (void)Call<uint8_t>(framework, 3, kHelperClass, kHelperInterface, &helper.mObject);
                    void *ownerData = Call<void *>(stackObject, 61);
                    const uint32_t ownerId = Call<uint32_t>(ownerData, 28);
                    // ORIGINAL-BUG: the helper object is used without a null check (the query's answer is ignored).
                    Call<void>(helper.mObject, 7, ownerId);
                    Call<void>(helper.mObject, 9, number);
                    Call<void>(helper.mObject, 28, newEntry);
                    const int16_t r = Call<int16_t>(inv, 15, helper.mObject);
                    if (r >= 0) {
                        helper.ReleaseNow();
                        return 1;
                    }
                    Call<void>(table, 90, newEntry, static_cast<uint32_t>(slot46), 0u);
                    helper.ReleaseNow();
                }
                return 0;
            }
            case 1: {
                const int16_t r = Read(this, opB);
                const uint16_t index = static_cast<uint16_t>(r - 1);
                uint32_t it = 0;
                Call<void>(inv, 21, &it, static_cast<uint32_t>(index));
                if (it == Iter(inv, 29)) return 0;
                Held entry;
                entry.mObject = ElementAt(it);
                if (entry.mObject) Call<void>(entry.mObject, 1);
                entry.mArmed = true;
                if (!entry.mObject) {
                    entry.ReleaseNow();
                    return 0;
                }
                void *ctx = Call<void *>(mContextHolder, 26);
                int16_t made = 0;
                if (Call<int32_t>(entry.mObject, 27) == 0) {
                    void *singleton = GetCachedSingleton();
                    void *lookup = Call<void *>(singleton, 24);
                    const uint32_t entryId = Call<uint32_t>(entry.mObject, 6);
                    void *thing = Call<void *>(lookup, 14, entryId);
                    if (thing != nullptr) {
                        thing = Call<void *>(thing, 27);
                        if (thing != nullptr && Call<void *>(thing, 4) != nullptr) {
                            void *typed = Call<void *>(thing, 4);
                            const int32_t type = Call<int32_t>(typed, 3, 9u);
                            if (type != 0x10 && type != 0x11 && type != 2) made = Call<int16_t>(ctx, 16, thing, 0u, 0u, 0u);
                        }
                    }
                } else {
                    uint16_t block[4] = {1, static_cast<uint16_t>(kind), id, index};
                    const int32_t what = Call<int32_t>(entry.mObject, 27);
                    made = Call<int16_t>(ctx, 88, what, static_cast<uint32_t>(slot46), 0u, block);
                }
                if (made == 0 || Call<int32_t>(ctx, 29, static_cast<int32_t>(made)) == 0) {
                    entry.ReleaseNow();
                    return 0;
                }
                Call<void>(frame, 13, static_cast<uint32_t>(static_cast<uint16_t>(made)));
                entry.ReleaseNow();
                return 1;
            }
            case 2: {
                const uint32_t kind2 = p[0xa] & 7;
                const int32_t flag2 = (p[0xa] & 8) >> 3;
                if (flag2 != 0) return 0;
                const uint16_t id2 = static_cast<uint16_t>(Read(this, opC));
                void *target = ResolvePerson(this, kind2, id2);
                if (!target) Raise(0x57);
                const int16_t r = Read(this, opB);
                return Call<uint8_t>(inv, 48, target, static_cast<int32_t>(r) - 1, 1u, 1u) != 0 ? 1 : 0;
            }
            default: break;
            }
            break;
        }
        default: return 0;
        }
        return 1;
    }

    // ---- mode 1 -------------------------------------------------------------------------------------------------
    CallListGetters(inv, 42, 43);
    switch (opcode) {
    case 0: {
        const uint32_t type = TypeBits(p);
        const int16_t r = Read(this, opC);
        if (Call<uint8_t>(inv, 33, token, cat, type, static_cast<uint32_t>(static_cast<uint16_t>(r))) == 0) return 0;
        break;
    }
    case 1: {
        const uint32_t type = TypeBits(p);
        const int16_t r1 = Read(this, opB);
        const int16_t r2 = Read(this, opC);
        const uint32_t index = static_cast<uint16_t>(r1 - 1);
        uint32_t it = 0;
        Call<void>(inv, 39, &it, index);
        if (it == Iter(inv, 43)) return 0;
        const uint16_t v = Call<uint16_t>(ElementAt(it), 21, 1u, type, static_cast<uint32_t>(static_cast<uint16_t>(r2)));
        const uint32_t w = Call<uint32_t>(ElementAt(it), 6, static_cast<uint32_t>(v));
        // ORIGINAL-BUG: slot 33 takes four arguments (token, category, type, count; ret 0x10) but the original pushes
        // only this one; the other three are whatever lies on its stack (the callee also pops 12 bytes too many, which
        // the frame pointer hides). Zeros are passed here.
        (void)Call<uint8_t>(inv, 33, w, 0u, 0u, 0u);
        break;
    }
    case 2: {
        const int16_t r = Read(this, opC);
        if (Call<uint8_t>(inv, 34, token, cat, static_cast<uint32_t>(static_cast<uint16_t>(r))) == 0) return 0;
        break;
    }
    case 3: {
        const int16_t r1 = Read(this, opB);
        const int16_t r2 = Read(this, opC);
        const uint32_t index = static_cast<uint16_t>(r1 - 1);
        uint32_t it = 0;
        Call<void>(inv, 39, &it, index);
        if (it == Iter(inv, 43)) return 0;
        const uint16_t v = Call<uint16_t>(ElementAt(it), 21, 1u, static_cast<uint32_t>(static_cast<uint16_t>(r2)));
        const uint32_t w = Call<uint32_t>(ElementAt(it), 6, static_cast<uint32_t>(v));
        // ORIGINAL-BUG: slot 34 takes three arguments (token, category, count; ret 0xc), the original pushes one.
        (void)Call<uint8_t>(inv, 34, w, 0u, 0u);
        break;
    }
    case 4: {
        if (Call<uint8_t>(inv, 35, token, cat) == 0) return 0;
        break;
    }
    case 5: {
        const int16_t r = Read(this, opB);
        const int16_t position = static_cast<int16_t>(r - 1);
        if (position < 0) Raise(0x74);
        if (Call<uint8_t>(inv, 36, static_cast<uint32_t>(static_cast<uint16_t>(position))) == 0) return 0;
        break;
    }
    case 6: {
        SimDataRef first;
        first.Reset();
        (void)Read(this, opC, &first);
        SimDataRef second;
        second.Reset();
        (void)Read(this, opB, &second);
        uint32_t it = 0;
        Call<void>(inv, 37, &it, token, cat);
        if (it == Iter(inv, 43)) return 0;
        if (!first.HasData()) Raise(0x59);
        if (!second.HasData()) Raise(0x59);
        const uint16_t v = Call<uint16_t>(ElementAt(it), 21, 0u);
        if (!first.Set(static_cast<int32_t>(v))) Raise(0x14);
        const int32_t position = IndexOf(inv, 42, it);
        if (!second.Set(position + 1)) Raise(0x14);
        break;
    }
    case 7: {
        uint32_t it = 0;
        Call<void>(inv, 37, &it, token, cat);
        if (it == Iter(inv, 43)) return 0;
        Call<void>(mCursor, 29, ElementAt(it));
        mSelKind = static_cast<uint16_t>(kind);
        mSelId = id;
        mSelFlag = static_cast<uint16_t>(flag);
        mSelIndex = static_cast<uint16_t>(IndexOf(inv, 42, it));
        mEntryHigh = static_cast<uint16_t>(Call<uint32_t>(ElementAt(it), 6) >> 16);
        mEntryLow = static_cast<uint16_t>(Call<uint32_t>(ElementAt(it), 6));
        mEntryWord = Call<uint16_t>(ElementAt(it), 8);
        mEntryWord2 = Call<uint16_t>(ElementAt(it), 18);
        break;
    }
    case 8: {
        const int16_t r1 = Read(this, opB);
        const uint16_t index = static_cast<uint16_t>(r1 - 1);
        uint32_t it = 0;
        Call<void>(inv, 39, &it, static_cast<uint32_t>(index));
        if (it == Iter(inv, 43)) return 0;
        Call<void>(mCursor, 29, ElementAt(it));
        mSelKind = static_cast<uint16_t>(kind);
        mSelId = id;
        mSelFlag = static_cast<uint16_t>(flag);
        mSelIndex = index;
        mEntryHigh = static_cast<uint16_t>(Call<uint32_t>(ElementAt(it), 6) >> 16);
        mEntryLow = static_cast<uint16_t>(Call<uint32_t>(ElementAt(it), 6));
        mEntryWord = Call<uint16_t>(ElementAt(it), 8);
        mEntryWord2 = Call<uint16_t>(ElementAt(it), 18);
        break;
    }
    case 9: {
        SimDataRef first;
        first.Reset();
        const int16_t r1 = Read(this, opB, &first);
        const uint16_t index = static_cast<uint16_t>(r1 - 1);
        SimDataRef second;
        second.Reset();
        (void)Read(this, opC, &second);
        uint32_t it = 0;
        Call<void>(inv, 38, &it, cat, static_cast<uint32_t>(index));
        if (it == Iter(inv, 43)) return 0;
        if (!first.HasData()) Raise(0x59);
        if (!second.HasData()) Raise(0x78);
        if (!second.HasNext()) Raise(0x78);
        const int32_t position = IndexOf(inv, 42, it);
        if (!first.Set(static_cast<int16_t>(position) + 1)) Raise(0x14);
        const uint32_t value = Call<uint32_t>(ElementAt(it), 6);
        second.Set(static_cast<int32_t>(value & 0xffff));
        second.Advance();
        second.Set(static_cast<int32_t>(value >> 16));
        break;
    }
    case 10: {
        SimDataRef dest;
        dest.Reset();
        (void)Read(this, opC, &dest);
        if (!dest.HasData()) Raise(0x59);
        uint32_t hiddenBegin = 0, hiddenEnd = 0;
        const uint32_t *begin = Call<const uint32_t *>(inv, 42, &hiddenBegin);
        const uint32_t *end = Call<const uint32_t *>(inv, 43, &hiddenEnd);
        const int32_t count = static_cast<int32_t>(*end - *begin) >> 2;
        if (!dest.Set(count)) Raise(0x14);
        break;
    }
    case 0xb: {
        const uint32_t kind2 = p[0xa] & 7;
        const int32_t flag2 = (p[0xa] & 8) >> 3;
        if (flag2 != 1) return 0;
        const uint16_t id2 = static_cast<uint16_t>(Read(this, opC));
        void *target = ResolvePerson(this, kind2, id2);
        if (!target) Raise(0x57);
        const int16_t r = Read(this, opB);
        return Call<uint8_t>(target, 49, inv, static_cast<int32_t>(r) - 1) != 0 ? 1 : 0;
    }
    default: return 0;
    }
    return 1;
}
