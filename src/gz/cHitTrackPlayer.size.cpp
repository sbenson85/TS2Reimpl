#include "gz/cHitTrackPlayer.h"

#include "game/vtable.h"

namespace {

// The original computes (this != 0 ? this + offset : 0) with neg/sbb/and.
void *SubobjectOrNull(void *self, uint32_t offset)
{
    return self ? static_cast<char *>(self) + offset : nullptr;
}

} // namespace

bool cHitTrackPlayer::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == 0x00000001 || riid == 0xbb1be367) { // cIGZUnknown, and a second interface: this, not masked
        *ppvObj = this;
    } else {
        uint32_t offset;
        switch (riid) {
        case 0x206077d2: offset = 0x0c; break; // cIGZSnd
        case 0x306077d2: offset = 0x08; break; // cIGZSnd3D
        case 0xfd584467: offset = 0x04; break; // cIGZSndRegisterSet
        case 0x5b34f0af: offset = 0x14; break; // cIGZSndUpdateTarget
        case 0xaa11a64f: offset = 0x10; break; // cIGZSndViewUpdateTarget
        case 0xdd583e2d: offset = 0x18; break; // cIGZSndMultipleSources
        case 0xe9d00a9a: offset = 0x1c; break; // cIGZSndCompletionNotification
        default: return false;
        }
        *ppvObj = SubobjectOrNull(this, offset);
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

uint32_t cHitTrackPlayer::AddRef()
{
    return ++mRefCount;
}

uint32_t cHitTrackPlayer::Release()
{
    if (mRefCount > 1) return --mRefCount; // unsigned compare: a count of -1 takes the decrement path
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 20>(this, 1); // deleting destructor
    return 0;
}

namespace {

// Fields of a secondary-base subobject: `self` is object + base offset, as the original receives it.
template <typename T> T &FieldAt(const void *self, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(const_cast<void *>(self)) + offset);
}

// Calls slot `id` of the vtable at +4 (cIGZSndRegisterSet) with one argument. The original does
// `add ecx,-8` from the +12 subobject, so the callee gets object+4 and its eax is returned unchanged.
uint32_t ForwardToRegisterSetSlot4(const void *sndSubobject, uint32_t id)
{
    void *registerSet = static_cast<char *>(const_cast<void *>(sndSubobject)) - 8;
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 4>(registerSet, id);
}

} // namespace

// Primary vtable slots: this = object.

void *cHitTrackPlayer::GetMultipleSourcesInterface()
{
    return SubobjectOrNull(this, 0x18); // cIGZSndMultipleSources
}

bool cHitTrackPlayer::InvokeChainedSlot5()
{
    // this+0xb4 holds the address of a cell, and the cell holds the target object's pointer.
    void *cell = FieldAt<void *>(this, 0xb4);
    void *target = *static_cast<void **>(cell);
    VirtualCall<void(__thiscall *)(void *), 5>(target);
    return true;
}

void cHitTrackPlayer::SetFlagAtD4()
{
    FieldAt<uint8_t>(this, 0xd4) = 1;
}

bool cHitTrackPlayer::CallSndSlot7(uint32_t, uint32_t)
{
    // Two stack arguments are popped here but not passed on: the cIGZSnd slot 7 callee takes none.
    void *snd = reinterpret_cast<char *>(this) + 0x0c;
    VirtualCall<void(__thiscall *)(void *), 7>(snd);
    return false;
}

// cIGZSnd subobject slots: this = object+0x0c.

bool cHitTrackPlayer::SndFlagAt40()
{
    return FieldAt<uint32_t>(this, 0x40) != 0; // object+0x4c
}

bool cHitTrackPlayer::SndFlagAt5C()
{
    return FieldAt<uint32_t>(this, 0x5c) != 0; // object+0x68
}

uint32_t cHitTrackPlayer::SndQuery13()
{
    return ForwardToRegisterSetSlot4(this, 0x13);
}

uint32_t cHitTrackPlayer::SndQuery15()
{
    return ForwardToRegisterSetSlot4(this, 0x15);
}

uint32_t cHitTrackPlayer::SndQuery16()
{
    return ForwardToRegisterSetSlot4(this, 0x16);
}

uint32_t cHitTrackPlayer::SndQuery17()
{
    return ForwardToRegisterSetSlot4(this, 0x17);
}

uint32_t cHitTrackPlayer::SndQuery49()
{
    return ForwardToRegisterSetSlot4(this, 0x49);
}

bool cHitTrackPlayer::SndTestId2A()
{
    return ForwardToRegisterSetSlot4(this, 0x2a) != 0;
}

uint32_t cHitTrackPlayer::SndForwardSlot5(uint32_t a, uint32_t, uint32_t)
{
    // The original pushes 0, 0, 0 and then its first argument: the callee gets (a, 0, 0, 0), and the
    // forwarder's second and third arguments are dropped.
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 5>(this, a, 0, 0, 0);
}

bool cHitTrackPlayer::SndNotifyEmbedded(uint32_t arg)
{
    // The embedded object at object+0x20 (this+0x14) has its own vtable; its slot 17 takes one argument.
    void *embedded = reinterpret_cast<char *>(this) + 0x14;
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 17>(embedded, arg);
    return true;
}
