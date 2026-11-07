#include "ui/TSUICAS.h"
#include "game/vtable.h"
#include "rz/QueryInterfaces.h"
#include "rz/RZFrameworkHelpers.h"
#include "ui/UIWinProcs.h"

namespace {

template <typename T> T &At(void *self, intptr_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(self) + offset);
}

// Slot 1 (AddRef) of `self`'s own vtable, as the original calls it. The result is not used.
void AddRef(void *self)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(self);
}

// The refcount increment (`inc dword [ecx+0xc]`) that the component chain's AddRefs reach: 0x5c51e0 -> 0x15e520,
// 0x5c51b2 -> 0x5c51e0 and 0x5f1d11 -> 0x5ec802 -> 0x5c51b2. The originals call these directly, not through a
// vtable, so the increment is reproduced inline here. The result is not used.
void IncrementRefCount(void *self)
{
    ++At<uint32_t>(self, 0xc);
}

// this + offset, or 0 when this is 0 (the original's neg/sbb/and).
void *OffsetOrNull(void *self, uintptr_t offset)
{
    return self != nullptr ? static_cast<uint8_t *>(self) + offset : nullptr;
}

// The body shared by the component chain: a null ppv returns false; otherwise stores this + offset through ppv,
// increments the refcount at +0xc and returns true. Callers have already compared the riid.
bool StoreOffsetAndIncrement(void *self, uintptr_t offset, void **ppv)
{
    if (ppv == nullptr) return false;
    *ppv = OffsetOrNull(self, offset);
    IncrementRefCount(self);
    return true;
}

} // namespace

namespace nTSUICAS {

// 0x602f64 (46 bytes, ret 8). The original's eax after the fall-through call is the callee's; only al is the result.
bool cTSUICASComponentPetCoatLayerEntry::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 0xf15b2506u)
        return reinterpret_cast<RZUnknownQueryInterface *>(this)->QueryInterfaceUnknown(riid, ppv);
    *ppv = this;
    AddRef(this);
    return true;
}

// 0x602f92 (46 bytes, ret 8). Like the entry's QueryInterface, but the fall-through goes to the thumbnail's
// QueryInterface (0x75dc5b, with the null edx of its __fastcall form) instead of the RZUnknown one.
bool cTSUICASComponentPetCoatLayerThumbnail::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 0x115b2117u) return qi::TSWinProcGridThumbnailQueryInterface(this, nullptr, riid, ppv);
    *ppv = this;
    AddRef(this);
    return true;
}

// 0x603b9f (42 bytes, ret 8): Assign(null) on +0x18, then +0x1c (RZUnknownSlot, both verified), then the
// verified cTSWinProcGridThumbnail::Vf7 on this with the same two arguments in the same order.
bool cTSUICASComponentPetCoatLayerThumbnail::Vf7(uint32_t a, uint32_t b)
{
    reinterpret_cast<RZUnknownSlot *>(reinterpret_cast<uint8_t *>(this) + 0x18)->Assign(nullptr);
    reinterpret_cast<RZUnknownSlot *>(reinterpret_cast<uint8_t *>(this) + 0x1c)->Assign(nullptr);
    return reinterpret_cast<cTSWinProcGridThumbnail *>(this)->Vf7(a, b);
}

// 0x759042 (61 bytes, ret 8). `neg ecx; lea eax,[edx+8]; sbb ecx,ecx; and ecx,eax` stores this + 8 when this is
// non-zero and 0 otherwise. The AddRef is on this itself. Only al is the result.
bool cTSFragmentFilterBase::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 0x50b4ee7fu)
        return reinterpret_cast<RZUnknownQueryInterface *>(this)->QueryInterfaceUnknown(riid, ppv);
    const uintptr_t self = reinterpret_cast<uintptr_t>(this);
    *ppv = reinterpret_cast<void *>(self != 0 ? self + 8 : 0);
    AddRef(this);
    return true;
}

// 0x6049a5 (52 bytes, ret 4). The stack argument is not read.
void cTSUICASComponentPetBin::Vf184(uint32_t)
{
    if (At<uint8_t>(this, 0x2c1) != 0 && At<uint32_t>(this, 0x2dc) == 2) {
        void *object = At<void *>(this, 0x184);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 15>(object, 0x801u);
    }
    void *subobject = reinterpret_cast<uint8_t *>(this) + 0x168;
    VirtualCall<void(__thiscall *)(void *), 7>(subobject);
}

// 0x5f8e0a (65 bytes, ret 4). The owner pointer is read before the slot is released, as the original does (it
// loads edi first). The flag == 0 path returns the slot from RZUnknownSlot::Assign (eax); the other path returns
// the eax of the owner's slot 19 call.
uint32_t cTSUICASComponentModifiers::Vf656_4(uint32_t flag)
{
    void *const slot = reinterpret_cast<uint8_t *>(this) + 0x54;
    if (flag == 0) {
        RZUnknownSlot *assigned = reinterpret_cast<RZUnknownSlot *>(slot)->Assign(nullptr);
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(assigned));
    }
    void *owner = At<void *>(this, -0x110);
    void *old = At<void *>(slot, 0);
    if (old != nullptr) {
        At<void *>(slot, 0) = nullptr;
        VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
    }
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *), 19>(owner, flag, 0x6c4f3599u, slot);
}

// 0x5fe25b (ret 8). The null check comes before the riid, as in the original. The fall-through is a call (not a
// tail jump) to cTSFragmentFilterBase::QueryInterface; its result is the result here.
bool cTSHairFilter::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 0xd0b524ccu) return reinterpret_cast<cTSFragmentFilterBase *>(this)->QueryInterface(riid, ppv);
    *ppv = this;
    AddRef(this);
    return true;
}

// 0x5f8da6 (ret 8). Riids other than 0xcca4d188 go to the component chain.
bool cTSUICASComponentModifiers::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0xcca4d188u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

// 0x6051a0 (ret 8). The store is of this itself (no offset). Other riids go to cTSUICASComponentBin::QueryInterface.
bool cTSUICASComponentPetBin::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x91802019u) return reinterpret_cast<cTSUICASComponentBin *>(this)->QueryInterface(riid, ppv);
    return StoreOffsetAndIncrement(this, 0, ppv);
}

// 0x5f1f44 (ret 8). Other riids go to cTSUICASComponentBin::QueryInterface (0x5fcc7a).
bool cTSUICASComponentSimBin::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x4c6c9c85u) return reinterpret_cast<cTSUICASComponentBin *>(this)->QueryInterface(riid, ppv);
    return StoreOffsetAndIncrement(this, 0x2c8, ppv);
}

// 0x5fcc7a (ret 8). Other riids go to the component chain.
bool cTSUICASComponentBin::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x2caa06e1u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

// The component-chain functions below each have the shape of cTSUICASComponentBin::QueryInterface: one riid
// stores + 0x290, the rest go to cTSUICASComponentQueryInterface.
bool cTSUICASComponentBabyMaker::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0xec76abc6u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentArchetypes::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0xecb8974au) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentOverlays::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x6ca51defu) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentSkinTone::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x0d123d4eu) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentPetCoatColors::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x713ba27bu) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentBodyOptions::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x719a9d02u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentClothing::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x8c6bd3edu) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentPersonality::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x0c6bf669u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentGenetics::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x8c6c07d7u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentAccessories::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x4c6c0893u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentHair::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x0ced66f0u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

bool cTSUICASComponentPetGenetics::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x11390e39u) return cTSUICASComponentQueryInterface(this, nullptr, riid, ppv);
    return StoreOffsetAndIncrement(this, 0x290, ppv);
}

// 0x75907f (ret 8). The riid is compared before the null check, as the original does; the store is this + 0x168
// and the refcount increment is inline (see IncrementRefCount). Other riids go to cTSUIWinHandler::QueryInterface.
bool __fastcall cTSUICASComponentQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (riid != 0x4bb92321u) return reinterpret_cast<cTSUIWinHandler *>(self)->QueryInterface(riid, ppv);
    return StoreOffsetAndIncrement(self, 0x168, ppv);
}

} // namespace nTSUICAS

// 0x755d3c (ret 8). The riid is compared before the null check. The AddRef here is virtual (slot 1 of this's own
// vtable), unlike the component chain's inline increment. Other riids tail-jump to qi::GZWinQueryInterface.
bool cTSUIWinHandler::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x8bd4f230u) return qi::GZWinQueryInterface(this, nullptr, riid, ppv);
    if (ppv == nullptr) return false;
    *ppv = OffsetOrNull(this, 0xf8);
    AddRef(this);
    return true;
}
