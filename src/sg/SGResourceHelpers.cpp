#include "sg/SGResourceHelpers.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "rz/StringKeyParser.h"
#include "sg/SGResourceBlockers.h"

namespace SGResourceHelpers {

namespace {

// The bytes of `self` as the original addresses them (this = object + 0x30).
uint8_t *Bytes(const void *self)
{
    return reinterpret_cast<uint8_t *>(const_cast<void *>(self));
}

// The virtual base subobject: the vbptr at -0x14 points at a table whose entry 1 is the displacement from the
// vbptr to the base, so the base is at this + entry1 - 0x14.
void *VirtualBase(void *self)
{
    uint8_t *bytes = Bytes(self);
    uint8_t *vbptr = *reinterpret_cast<uint8_t **>(bytes - 0x14);
    const int32_t displacement = *reinterpret_cast<const int32_t *>(vbptr + 4);
    return bytes + displacement - 0x14;
}

// Releases the holder on the way out, as the original's cleanup region does: the holder is released on every exit
// after its key copy begins, including when a C++ exception leaves through the calls in that region.
struct HolderRelease {
    RZAutoRef *holder;
    ~HolderRelease()
    {
        holder->ReleaseObject();
    }
};

// The out object of the virtual base's slot 4 (state 1 of ResetAndSubmit). Its unwind action in the original is a
// noexcept function (FuncInfo flags 5, no try) that releases the object at +0 if it is non-null: a C++ exception
// leaving it terminates, as it does here. Disarmed before the normal release, which the original does inline.
struct OutRelease {
    void **ref;
    ~OutRelease()
    {
        if (ref && *ref) VirtualCall<void(__thiscall *)(void *), 2>(*ref);
    }
    void Disarm()
    {
        ref = nullptr;
    }
};

// Closes the argument cell on the way out, as the original's cleanup does: Close runs after Assign on every exit from
// it. Close is noexcept in the original (its frame is), so a C++ exception leaving it terminates here too.
struct ArgCellClose {
    ServiceCell *cell;
    ~ArgCellClose()
    {
        cell->Close();
    }
};

// Service slot 5 with the identifiers the holders of 0xa3ca00 and 0xa3cbf0 use; fills the holder (when a service exists).
void AcquireHolder(RZAutoRef *holder)
{
    if (void *service = GetServiceObject()) {
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(service, 0x056b906e, 0x656b8efc, &holder->mObject);
    }
}

// The flag-A block both slots start with (the same as ResetAndSubmit's first block): a holder, the key copied through
// the primary's slot 3, the holder's slot 10 with the key, and the release.
void SubmitFirstHolder(void *object)
{
    RZAutoRef holder{nullptr};
    AcquireHolder(&holder);
    HolderRelease release{&holder};
    void *const *holderVtable = *reinterpret_cast<void *const *const *>(holder.mObject);
    uint8_t key[16]; // fully written by primary slot 3
    void *keyPtr = VirtualCall<void *(__thiscall *)(void *, void *), 3>(object, key);
    reinterpret_cast<void(__thiscall *)(void *, void *)>(holderVtable[10])(holder.mObject, keyPtr);
}

// The last block of both slots: a second holder, the key through primary slot 3, and the holder's slot 9 with (key,
// object), which pops both (ret 8). The holder is released after the call, with its byte result kept.
uint8_t SubmitWithSecondHolder(void *object)
{
    RZAutoRef holder{nullptr};
    AcquireHolder(&holder);
    HolderRelease release{&holder};
    void *const *holderVtable = *reinterpret_cast<void *const *const *>(holder.mObject);
    uint8_t key[16]; // fully written by primary slot 3
    void *keyPtr = VirtualCall<void *(__thiscall *)(void *, void *), 3>(object, key);
    // The original pushes the object before the key copy, so it is the second argument here as well.
    return reinterpret_cast<uint8_t(__thiscall *)(void *, void *, void *)>(holderVtable[9])(holder.mObject, keyPtr, object);
}

// The middle of both slots: the virtual base's slot 4 gives an out object X and a pointer R. The preset is the dword at R + 4
// (read before the next call), and the key text is the base's slot 17 (no arguments). ParseKey(out, key, preset, null) fills
// res. X is released afterwards, and also when the key or ParseKey throws (its unwind action, state 2 in the original).
// The original zeroes res before slot 4.
void ParseBaseKey(void *base, StringKeyParser::Result *res)
{
    *res = StringKeyParser::Result{};
    // Slot 4 fills a 12-byte out object whose first word is X (the original reserves [ebp-0x1c..-0x14] for it and uses
    // only the first word); a lone pointer let the callee write past it (found with the TS2_ESPCHECK build, /RTCs).
    struct OutObject {
        void *mObject = nullptr;
        uint32_t mRest[2] = {};
    } out;
    void *&x = out.mObject;
    void *ref = VirtualCall<void *(__thiscall *)(void *, OutObject *), 4>(base, &out);
    const uint32_t preset = *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(ref) + 4);
    {
        // The original's state 3 (the inline release below) has an unidentified unwind funclet (0x8444f7); not reproduced.
        OutRelease outRelease{&x};
        const char *key = static_cast<const char *>(VirtualCall<void *(__thiscall *)(void *), 17>(base));
        StringKeyParser::ParseKey(res, key, preset, nullptr);
        outRelease.Disarm();
    }
    if (x) VirtualCall<void(__thiscall *)(void *), 2>(x);
}

} // namespace

uint32_t cSignBitBeforeThis::SignBit()
{
    // The full 32-bit eax is the result (callers use all of it); shr by 31 leaves only bit 31.
    const uint32_t word = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(this) - 8);
    return word >> 31;
}

void *cSGSecondary::VirtualBaseIfObject()
{
    // lea eax,[ecx-0x30]; test; a null object returns that null in eax without reading the vbptr.
    if (Bytes(this) - 0x30 == nullptr) return nullptr;
    return VirtualBase(this);
}

uint8_t cSGSecondary::SubmitKeyToService()
{
    uint8_t *self = Bytes(this);
    if (self[-8] == 0) return 0;

    void *base = VirtualBase(this);
    if (!VirtualCall<uint8_t(__thiscall *)(void *), 23>(base)) return 1;

    self[-7] = 0;

    // The holder is a local, as in the original's frame. The cleanup region starts after the service call, so
    // the holder is released only once the key copy has begun.
    RZAutoRef holder{nullptr};
    if (void *service = GetServiceObject()) {
        // Slot 5 takes the two identifiers and an output pointer; the callee pops them.
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(service, 0x056b906e, 0x656b8efc, &holder.mObject);
    }

    HolderRelease release{&holder};
    void *object = self - 0x30;
    void *resource = *reinterpret_cast<void **>(self - 0xc);
    // With no service, or when slot 5 leaves the holder null, the original reads the holder's vtable from null here
    // and faults; this does the same. The service is set at boot, so the state is not expected.
    void *const *holderVtable = *reinterpret_cast<void *const *const *>(holder.mObject);
    uint8_t key[16]; // fully written by primary slot 3
    void *keyPtr = VirtualCall<void *(__thiscall *)(void *, void *), 3>(object, key);
    return reinterpret_cast<uint8_t(__thiscall *)(void *, void *, void *, void *)>(holderVtable[14])(holder.mObject, object,
                                                                                                    keyPtr, resource);
}

uint8_t cSGSecondary::ResetAndSubmit(void *a1, void *a2)
{
    uint8_t *self = Bytes(this);
    void *object = self - 0x30;

    if (self[-8] != 0) {
        // State 0 (the holder's release) begins after the service call, as in the original's frame. The
        // release also runs when the key copy or the holder's slot 10 throws.
        RZAutoRef holder{nullptr};
        if (void *service = GetServiceObject()) {
            VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(service, 0x056b906e, 0x656b8efc, &holder.mObject);
        }
        HolderRelease release{&holder};
        // The original reads the holder's vtable before the key copy and calls slot 10 through that copy.
        void *const *holderVtable = *reinterpret_cast<void *const *const *>(holder.mObject);
        uint8_t key[16]; // fully written by primary slot 3
        void *keyPtr = VirtualCall<void *(__thiscall *)(void *, void *), 3>(object, key);
        reinterpret_cast<void(__thiscall *)(void *, void *)>(holderVtable[10])(holder.mObject, keyPtr);
    }

    // The cell at this - 0x10 (object + 0x20) is a ServiceCell: Reset is verified in StaticGetters2.
    reinterpret_cast<ServiceCell *>(self - 0x10)->Reset();

    void *base = VirtualBase(this);
    void *x = nullptr;
    void *ref = VirtualCall<void *(__thiscall *)(void *, void **), 4>(base, &x);
    const uint32_t arg = *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(ref) + 4);
    {
        // State 1 (x live) covers the primary slot 8 call; state 2 (the inline release) is not guarded.
        OutRelease outRelease{&x};
        VirtualCall<void(__thiscall *)(void *, uint32_t), 8>(object, arg);
        outRelease.Disarm();
    }
    if (x) VirtualCall<void(__thiscall *)(void *), 2>(x);

    VirtualCall<void(__thiscall *)(void *, void *), 9>(object, a1);
    VirtualCall<void(__thiscall *)(void *, void *), 10>(object, a2);
    self[-8] = 1;

    // The original stores the second holder in the first argument's slot (zeroed first); it is only ever a
    // local here, since the caller does not read its arguments after the call.
    RZAutoRef holder{nullptr};
    if (void *service = GetServiceObject()) {
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(service, 0x056b906e, 0x656b8efc, &holder.mObject);
    }
    uint8_t result = 0;
    {
        HolderRelease release{&holder};
        void *const *holderVtable = *reinterpret_cast<void *const *const *>(holder.mObject);
        uint8_t key[16]; // fully written by primary slot 3
        void *keyPtr = VirtualCall<void *(__thiscall *)(void *, void *), 3>(object, key);
        // Slot 9 takes two stack arguments, the key pointer and the object, and pops both (ret 8). The original pushes the
        // object before the key copy and leaves it under the key call's frame, so the same two dwords are on the stack
        // here. Slot 9 returns a byte (the original keeps bl = al, returned as al).
        result = reinterpret_cast<uint8_t(__thiscall *)(void *, void *, void *)>(holderVtable[9])(holder.mObject, keyPtr, object);
    }
    return result;
}

uint8_t cSGSecondary::OpenAssignAndSubmit(uint32_t a1, void *a2, void *a3)
{
    uint8_t *self = Bytes(this);
    void *object = self - 0x30;

    if (self[-8] != 0) SubmitFirstHolder(object);

    // a1 is a ServiceCell holding the value; it is opened with (a1, 1) and its Assign goes into the cell at this - 0x10.
    ServiceCell argCell{a1};
    ServiceCell *opened = argCell.Open(a1, 1);
    {
        ArgCellClose close{&argCell};
        reinterpret_cast<ServiceCell *>(self - 0x10)->Assign(&opened->mValue);
    }

    void *base = VirtualBase(this);
    VirtualCall<void(__thiscall *)(void *, void *), 15>(base, a3);

    // The flag is the inverse of IsInvalid (0 or 1 in al); an invalid cell or a null a3 returns 0 here.
    const uint8_t valid = reinterpret_cast<const SGResourceBlockers::cInvalidDwordTag *>(self - 0x10)->IsInvalid() ^ 1;
    self[-8] = valid;
    if (valid == 0 || a3 == nullptr) return 0;

    StringKeyParser::Result res{};
    ParseBaseKey(base, &res);
    VirtualCall<void(__thiscall *)(void *, void *), 4>(object, &res);
    VirtualCall<void(__thiscall *)(void *, void *), 9>(object, a2);
    return SubmitWithSecondHolder(object);
}

uint8_t cSGSecondary::AssignAndSubmit(uint32_t a1, void *a2)
{
    uint8_t *self = Bytes(this);
    void *object = self - 0x30;

    ServiceCell argCell{a1};
    ServiceCell *opened = argCell.Open(a1, 1);
    {
        ArgCellClose close{&argCell};
        reinterpret_cast<ServiceCell *>(self - 0x10)->Assign(&opened->mValue);
    }

    void *base = VirtualBase(this);
    VirtualCall<void(__thiscall *)(void *, void *), 15>(base, a2);
    if (self[-8] != 0) SubmitFirstHolder(object);

    const uint8_t valid = reinterpret_cast<const SGResourceBlockers::cInvalidDwordTag *>(self - 0x10)->IsInvalid() ^ 1;
    self[-8] = valid;
    if (valid == 0 || a2 == nullptr) return 0;

    StringKeyParser::Result res{};
    ParseBaseKey(base, &res);
    VirtualCall<void(__thiscall *)(void *, void *), 4>(object, &res);
    return SubmitWithSecondHolder(object);
}

} // namespace SGResourceHelpers
