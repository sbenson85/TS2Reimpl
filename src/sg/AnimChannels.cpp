#include "sg/AnimChannels.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>

namespace nGZSceneDBAnim {

namespace {

using Fn_u32_this = uint32_t(__thiscall *)(void *);
using Fn_ptr_this = void *(__thiscall *)(void *);
using Fn_float_this = float(__thiscall *)(void *);

// The context's slot 62 takes no arguments and returns a key; the key goes to slot 18 of a setter (or of a keyed
// reference) with two more arguments, the address of a key field and a data object. The original pushes those two
// before calling slot 62, then the key: a reading of it as two slot 62 arguments breaks the stack without frame
// pointers (found with the TS2_ESPCHECK build, 2026-10-16).
float ContextKey(void *context)
{
    return VirtualCall<Fn_float_this, 62>(context);
}

// Calls slot 1 (AddRef) of the object, the way the original does: a __thiscall through the object's vtable.
void CallAddRef(void *object)
{
    VirtualCall<Fn_u32_this, 1>(object);
}

// The interface of a "V" reference (see the header): (object + 4) + vbtable[index], where the vbptr is at object + 4.
void *VBase(void *object, int index)
{
    uint8_t *base = static_cast<uint8_t *>(object) + 4;
    const int32_t *vbtable = *reinterpret_cast<int32_t *const *>(base);
    return base + vbtable[index];
}

// Release (slot 2) of a "V" reference, on the interface at vbtable[1].
void ReleaseV(void *object)
{
    void *unknown = VBase(object, 1);
    VirtualCall<Fn_u32_this, 2>(unknown);
}

// Release (slot 2) of a "D" reference.
void ReleaseD(void *object)
{
    VirtualCall<Fn_u32_this, 2>(object);
}

// The destructors' pattern: read the field, and when it is not null clear it and then release the object.
void ClearAndReleaseV(void *&field)
{
    void *object = field;
    if (object != nullptr) {
        field = nullptr;
        ReleaseV(object);
    }
}

void ClearAndReleaseD(void *&field)
{
    void *object = field;
    if (object != nullptr) {
        field = nullptr;
        ReleaseD(object);
    }
}

// The member destructors that follow: the field is read again (it is null by now, except for the ones the
// destructor body did not clear) and its object released without clearing the field.
void ReleaseFieldV(void *const &field)
{
    void *object = field;
    if (object != nullptr) ReleaseV(object);
}

void ReleaseFieldD(void *const &field)
{
    void *object = field;
    if (object != nullptr) ReleaseD(object);
}

// Pushes a dead object on a class's free list: the object's first word takes the old head.
void PushFreeList(uint32_t headRva, void *object)
{
    if (object == nullptr) return;
    void **head = GameAddress<void *>(headRva);
    *static_cast<void **>(object) = *head;
    *head = object;
}

float Clamp01(float v)
{
    // minss/comiss semantics: below zero gives 0, above one gives 1, and NaN passes through.
    if (0.0f > v) return 0.0f;
    return (1.0f < v) ? 1.0f : v;
}

} // namespace

// ---- cChannelUber -------------------------------------------------------------------------------------------

// Each QueryInterface below has the same body with its own riid set: a null ppv returns false, a riid in the set
// stores this into *ppv, AddRefs through slot 1 and returns true. Another riid returns false. Only al is defined;
// the other bits of eax on the false paths are left over from the arguments.

// Body (0xa266d0): riid compares against 1 and 0xec70498e.
bool cChannelUber::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != 0xec70498eu) return false;
    *ppv = this;
    CallAddRef(this);
    return true;
}

// Slot 1 of the derived classes is 0x146a50, a tail jump to 0x148fb0 (the base's own slot 1).
uint32_t cChannelUber::AddRef()
{
    return ++refCount_;
}

// Slot 2 (0x2386b0; the derived classes' 0x9efd40 jumps to it). Decrements, and at zero deletes the object through
// slot 3 with the flag 1 and returns 0. A count that was already 0 wraps without deleting.
uint32_t cChannelUber::Release()
{
    const uint32_t count = refCount_ - 1;
    refCount_ = count;
    if (count == 0) {
        VirtualCall<void *(__thiscall *)(void *, uint32_t), 3>(this, 1);
        return 0;
    }
    return count;
}

// 0xa266a0: runs the destructor body; the flag 1 frees the block with operator delete(p, 0xc4).
cChannelUber *cChannelUber::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// 0xa264a0. First the references are cleared and released in field order, then the member destructors release
// the fields again (only +0xc0 was not cleared before, so only that one has an object left to release).
void cChannelUber::Destruct()
{
    vtable_ = GameAddress<void>(kVTable);
    ClearAndReleaseV(ref8_);
    ClearAndReleaseV(ref14_);
    target_ = nullptr;
    raw1c_ = nullptr;
    ClearAndReleaseD(ref20_);
    ClearAndReleaseV(ref28_);
    ClearAndReleaseD(ref2c_);
    ClearAndReleaseD(ref48_);
    ClearAndReleaseD(ref4c_);
    ClearAndReleaseD(ref64_);
    ClearAndReleaseD(ref68_);
    rawA8_ = nullptr;
    raw88_ = nullptr;
    ReleaseFieldD(refC0_);
    ReleaseFieldD(ref68_);
    ReleaseFieldD(ref64_);
    ReleaseFieldD(ref4c_);
    ReleaseFieldD(ref48_);
    ReleaseFieldD(ref2c_);
    ReleaseFieldV(ref28_);
    ReleaseFieldD(ref20_);
    ReleaseFieldV(ref14_);
    ReleaseFieldV(ref8_);
}

// 0xa267b0. Locals: x = [ebp-0x14] and y = [ebp-0x10], both 0 at first; y is the result.
float cChannelUber::Evaluate(float *blend)
{
    float x = 0.0f;
    float y = 0.0f;
    if (target_ == nullptr) return y;

    void *context = VirtualCall<Fn_ptr_this, 12>(target_);
    if (context != nullptr) {
        y = VirtualCall<Fn_float_this, 44>(context);
        x = y;
    } else {
        // No context: ask the target's interface (vbase 1) for the 0x6a6fd04d interface and read the value from the
        // object its slot 8 returns (reached through that object's vbase 2).
        void *found = nullptr;
        void *unknown = VBase(target_, 1);
        const bool ok = VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(unknown, 0x6a6fd04du, &found);
        if (ok) {
            if (VirtualCall<Fn_ptr_this, 8>(found) != nullptr) {
                void *inner = VirtualCall<Fn_ptr_this, 8>(found);
                void *reader = VBase(inner, 2);
                y = VirtualCall<Fn_float_this, 44>(reader);
                x = y;
            }
        }
        if (found != nullptr) ReleaseV(found);
    }

    const int32_t mode = mode_;
    if ((mode != 4 && mode != 5 && mode != 6) || context == nullptr) return y;

    using SetFn = void(__thiscall *)(void *, float, const void *, void *);

    void *setter = ref2c_;
    if (setter != nullptr) {
        // The setter's vtable is read before the context is asked for the value.
        void *const *setterVTable = *static_cast<void *const *const *>(setter);
        const float key = ContextKey(context);
        reinterpret_cast<SetFn>(setterVTable[18])(setter, key, &key44_, ref28_);
        const float curve = VirtualCall<float(__thiscall *)(void *, int), 6>(ref28_, 0);
        const float c = Clamp01(curve);
        float result;
        if (flag30_ != 0) {
            const float weight = x;
            if (blend != nullptr) *blend = (1.0f - c) * weight;
            result = c * weight;
        } else {
            result = (1.0f - c) * x;
        }
        y = result;
        x = result;
    }

    if (mode_ == 6) {
        void *setter2 = ref4c_;
        if (setter2 != nullptr) {
            void *const *setterVTable = *static_cast<void *const *const *>(setter2);
            const float key = ContextKey(context);
            reinterpret_cast<SetFn>(setterVTable[18])(setter2, key, &key60_, ref28_);
            const float curve = VirtualCall<float(__thiscall *)(void *, int), 6>(ref28_, 0);
            const float c = Clamp01(curve);
            y = c * x;
            if (blend != nullptr) *blend = c * *blend;
            return y;
        }
    }
    return y;
}

// ---- Pooled classes: the free-list "operator delete" and the destructors ----------------------------------------

// Body (0x9efd10): riid compares against 1, 0xac704952 and 0xec70498e.
bool cChannelUberQ::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != 0xac704952u && riid != 0xec70498eu) return false;
    *ppv = this;
    CallAddRef(this);
    return true;
}

void __cdecl cChannelUberQ::PoolFree(void *object)
{
    PushFreeList(kPoolHeadRva, object);
}

void *cChannelUberQ::ReturnThis()
{
    return this;
}

// 0x9eef80: the +0xc4 reference and then the +0x150 one are cleared and released, the member destructors release
// them again (both are null now), and the base destructor body runs.
void cChannelUberQ::Destruct()
{
    vtable_ = GameAddress<void>(kVTable);
    ClearAndReleaseV(refC4_);
    ClearAndReleaseD(ref150_);
    ReleaseFieldD(ref150_);
    ReleaseFieldV(refC4_);
    cChannelUber::Destruct();
}

// 0x9ef0d0. The flag 1 frees the block: with the flag 4 as well the sized delete is a no-op (0x140fe0 is a bare ret),
// otherwise the object goes back on the class's free list.
cChannelUberQ *cChannelUberQ::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) {
        if (!(flags & 4)) PoolFree(this);
    }
    return this;
}

// Body (0x9f0840): riid compares against 1, 0xcc70496e and 0xec70498e.
bool cChannelUberXYZ::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != 0xcc70496eu && riid != 0xec70498eu) return false;
    *ppv = this;
    CallAddRef(this);
    return true;
}

void __cdecl cChannelUberXYZ::PoolFree(void *object)
{
    PushFreeList(kPoolHeadRva, object);
}

// 0x9efe60.
void cChannelUberXYZ::Destruct()
{
    vtable_ = GameAddress<void>(kVTable);
    ClearAndReleaseV(refC4_);
    ReleaseFieldV(refC4_);
    cChannelUber::Destruct();
}

// 0x9eff80 (see cChannelUberQ::DeletingDestructor).
cChannelUberXYZ *cChannelUberXYZ::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) {
        if (!(flags & 4)) PoolFree(this);
    }
    return this;
}

// Body (0x9f0c70): riid compares against 1, 0x6ce2e072 and 0xec70498e.
bool cChannelUberAttribute::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != 0x6ce2e072u && riid != 0xec70498eu) return false;
    *ppv = this;
    CallAddRef(this);
    return true;
}

void __cdecl cChannelUberAttribute::PoolFree(void *object)
{
    PushFreeList(kPoolHeadRva, object);
}

// 0x9f08f0: the reference is at +0xc8 (the first dword is a count).
void cChannelUberAttribute::Destruct()
{
    vtable_ = GameAddress<void>(kVTable);
    ClearAndReleaseV(ref_);
    ReleaseFieldV(ref_);
    cChannelUber::Destruct();
}

// 0x9f0a10 (see cChannelUberQ::DeletingDestructor).
cChannelUberAttribute *cChannelUberAttribute::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) {
        if (!(flags & 4)) PoolFree(this);
    }
    return this;
}

// 0x9f0a50. `time` is not used. The values come from one of three places depending on the mode; the context (slot 12
// of the target) is fetched first and used by mode 4 only.
bool cChannelUberAttribute::Fetch(float, float *dest)
{
    if (target_ == nullptr) return false;
    if (count_ == 0) return false;

    void *context = VirtualCall<Fn_ptr_this, 12>(target_);
    const uint32_t index = static_cast<uint32_t>(mode_ - 1);
    if (index > 8) return false;

    const void *source;
    switch (index) {
    case 8: // mode 9: needs ref14_
        if (ref14_ == nullptr) return false;
        source = values_;
        break;
    case 0: // mode 1
    case 4: // mode 5
        source = values_;
        break;
    case 3: { // mode 4: the context turns the data object into a key for the d reference, which accepts or rejects it
        void *keyed = ref20_;
        void *const *keyedVTable = *static_cast<void *const *const *>(keyed);
        const float key = ContextKey(context);
        using KeyedFn = bool(__thiscall *)(void *, float, const void *, void *);
        const bool accepted = reinterpret_cast<KeyedFn>(keyedVTable[18])(keyed, key, &key34_, ref_);
        if (!accepted) return false;
        source = VirtualCall<Fn_ptr_this, 1>(ref_);
        break;
    }
    default:
        return false;
    }

    std::memcpy(dest, source, static_cast<size_t>(count_) << 2);
    if (flag8c_ != 0) {
        // Negates component index90_ (a sign-bit flip, which also applies to NaNs).
        uint32_t bits;
        std::memcpy(&bits, &dest[index90_], sizeof(bits));
        bits ^= 0x80000000u;
        std::memcpy(&dest[index90_], &bits, sizeof(bits));
    }
    return true;
}

// 0x9f0bb0 (ret 0xc). The record pointer is advanced by 0x28 before it is handed on as the destination.
float cChannelUberAttribute::Slot7(float time, uint8_t *record, float *blend)
{
    float value = Evaluate(blend);
    if (value > 0.0f) {
        if (!Fetch(time, reinterpret_cast<float *>(record + 0x28))) value = 0.0f;
    }
    return value;
}

// 0x9f0b60 (ret 8).
float cChannelUberAttribute::Slot8(float time, uint8_t *record)
{
    float value = Evaluate(nullptr);
    if (value > 0.0f) {
        if (!Fetch(time, reinterpret_cast<float *>(record + 0x28))) value = 0.0f;
    }
    return value;
}

// ---- cEventKeyConst --------------------------------------------------------------------------------------------

// Body (0xa29200): the original tests riid - 1, then riid - 1 - 0x5b0aaa73, then that less 0x37ccf9, so the
// accepted riids are 1, 0x5b0aaa74 and 0x5b0aaa74 + 0x37ccf9 = 0x5b42776d.
bool cEventKeyConst::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != 0x5b0aaa74u && riid != 0x5b42776du) return false;
    *ppv = this;
    CallAddRef(this);
    return true;
}

void cEventKeyConst::Nop4(uint32_t) {}

void cEventKeyConst::Nop8(uint32_t, uint32_t) {}

// 0xa29270. Builds a 0x20-byte cRZString (vtable 0xd9b470) holding "cEventKeyConst" (14 characters, capacity 15, in
// the small buffer at +4; the string's reference count at +0x1c), stores it in out->name and AddRefs it through its
// slot 1. The type id and count are constants.
cEventKeyConst::ClassNameResult *cEventKeyConst::GetTypeName(ClassNameResult *out)
{
    // The original's operator new throws on failure, so its null branch is dead.
    uint8_t *string = static_cast<uint8_t *>(::operator new(0x20));
    std::memset(string + 4, 0, 16);
    const uint32_t size = 0xe;
    const uint32_t capacity = 0xf;
    std::memcpy(string + 0x14, &size, sizeof(size));
    std::memcpy(string + 0x18, &capacity, sizeof(capacity));
    std::memcpy(string + 4, "cEventKeyConst", 15); // 14 characters and the terminator at +0x12
    const void *stringVTable = ::cRZString::VTable();
    std::memcpy(string, &stringVTable, sizeof(stringVTable));
    std::memset(string + 0x1c, 0, 4);
    out->name = string;
    CallAddRef(string);
    out->typeId = 0x3b427766;
    out->count = 5;
    return out;
}

float cEventKeyConst::GetFloat11()
{
    return static_cast<float>(word4_);
}

int32_t cEventKeyConst::GetSigned12()
{
    return word6_;
}

uint32_t cEventKeyConst::GetDword13()
{
    uint32_t value;
    std::memcpy(&value, dwordA_, sizeof(value));
    return value;
}

cEventKeyConst *cEventKeyConst::Construct()
{
    vtable_ = GameAddress<void>(kVTable);
    return this;
}

void cEventKeyConst::Destruct()
{
    vtable_ = GameAddress<void>(kVTable);
}

// ---- the Q and XYZ evaluators (slots 7 and 8, lane IC) ----

namespace {
using SampleRotation = bool(__thiscall *)(cChannelUberQ *, uint32_t, uint8_t *);   // 0x9ef110
using SamplePosition = bool(__thiscall *)(cChannelUberXYZ *, uint32_t, uint8_t *); // 0x9effc0
} // namespace

float cChannelUberQ::EvaluateBlended(uint32_t time, uint8_t *out, float *blend)
{
    float weight = 1.0f;
    if (mode_ != 2) weight = Evaluate(blend);
    if (weight > 0.0f || mode_ == 2) {
        if (!OriginalFunction<SampleRotation, 0x9ef110>()(this, time, out + 0xc)) weight = 0.0f;
    }
    return weight;
}

float cChannelUberQ::EvaluateUnblended(uint32_t time, uint8_t *out)
{
    float weight = 1.0f;
    if (mode_ != 2) weight = Evaluate(nullptr);
    if (weight > 0.0f || mode_ == 2) {
        if (!OriginalFunction<SampleRotation, 0x9ef110>()(this, time, out + 0xc)) weight = 0.0f;
    }
    return weight;
}

float cChannelUberXYZ::EvaluateBlended(uint32_t time, uint8_t *out, float *blend)
{
    float weight = 1.0f;
    if (mode_ != 2) weight = Evaluate(blend);
    if (weight > 0.0f || mode_ == 2) {
        if (!OriginalFunction<SamplePosition, 0x9effc0>()(this, time, out + 0x1c)) weight = 0.0f;
    }
    return weight;
}

float cChannelUberXYZ::EvaluateUnblended(uint32_t time, uint8_t *out)
{
    float weight = 1.0f;
    if (mode_ != 2) weight = Evaluate(nullptr);
    if (weight > 0.0f || mode_ == 2) {
        if (!OriginalFunction<SamplePosition, 0x9effc0>()(this, time, out + 0x1c)) weight = 0.0f;
    }
    return weight;
}

} // namespace nGZSceneDBAnim
