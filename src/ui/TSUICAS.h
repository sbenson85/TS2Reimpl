#pragma once
#include <cstddef>
#include <cstdint>

// Create-a-Sim UI classes (RVAs 1.18.0.187). `this` is the object the original is called with: for a
// subobject slot, the subobject's address (the vtable slot's own `ecx`), not the start of the most derived
// object. Offsets below are from that `this`.

// cTSUIWinHandler (global namespace; its cGZWin base is at +0, cITSUIWinHandler at +0xf8).
class cTSUIWinHandler {
public:
    // QueryInterface, 0x755d3c (56 bytes, ret 8). riid 0x8bd4f230 stores this + 0xf8 (or 0 when this is 0) and
    // AddRefs this through slot 1 of its own vtable; a null ppv returns false first. Anything else tail-jumps to
    // qi::GZWinQueryInterface (0x9325d8) with the same arguments. Only al is the result.
    bool QueryInterface(uint32_t riid, void **ppv);
};

namespace nTSUICAS {

// The QueryInterface of a pet coat layer entry, 0x602f64 (46 bytes, ret 8). riid 0xf15b2506 stores this and
// AddRefs it (slot 1); anything else goes to RZUnknownQueryInterface::QueryInterfaceUnknown (0x1b945c) on this.
// A null ppv returns false before the riid is looked at.
class cTSUICASComponentPetCoatLayerEntry {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

class cTSUICASComponentPetCoatLayerThumbnail {
public:
    // QueryInterface, 0x602f92 (46 bytes, ret 8). Null ppv returns false. riid 0x115b2117 stores this and AddRefs
    // it; anything else goes to qi::TSWinProcGridThumbnailQueryInterface (0x75dc5b) with a null edx.
    bool QueryInterface(uint32_t riid, void **ppv);

    // Slot 7, 0x603b9f (42 bytes, ret 8): RZUnknownSlot::Assign(null) on +0x18 and +0x1c, then calls
    // cTSWinProcGridThumbnail::Vf7(a, b) (0x75de32) on this. Returns that call's al; only al is the result.
    bool Vf7(uint32_t a, uint32_t b);
};

// QueryInterface, 0x759042 (61 bytes, ret 8). riid 0x50b4ee7f stores this + 8 (or 0 when this is 0) and
// AddRefs this (slot 1, on this itself, not on the stored address); anything else goes to
// RZUnknownQueryInterface::QueryInterfaceUnknown (0x1b945c). A null ppv returns false.
class cTSFragmentFilterBase {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

class cTSUICASComponentPetBin {
public:
    // vf184, 0x6049a5 (52 bytes, ret 4). When the byte at +0x2c1 is set and the dword at +0x2dc is 2,
    // calls slot 15 of the object at +0x184 with 0x801. Then calls slot 7 of the subobject at +0x168. The argument
    // is not used; nothing is returned.
    void Vf184(uint32_t);

    // QueryInterface, 0x6051a0 (ret 8). riid 0x91802019 stores this (no offset) and increments the refcount at
    // +0xc; a null ppv returns false first. Anything else tail-jumps to cTSUICASComponentBin::QueryInterface.
    bool QueryInterface(uint32_t riid, void **ppv);
};

class cTSUICASComponentModifiers {
public:
    // Slot 4 of the subobject at +656 (vf656_4), 0x5f8e0a (65 bytes, ret 4). `flag` is the first stack argument.
    // flag == 0: RZUnknownSlot::Assign(null) on the slot at this + 0x54, which returns the slot (eax).
    // flag != 0: Releases the slot's old object (slot 2) and clears it, then calls slot 19 of the object whose
    // pointer is at this - 0x110, with (flag, 0x6c4f3599, slot address), and returns that call's eax.
    uint32_t Vf656_4(uint32_t flag);

    // QueryInterface, 0x5f8da6 (ret 8). riid 0xcca4d188 stores this + 0x290; see the component chain below.
    bool QueryInterface(uint32_t riid, void **ppv);
};

// The QueryInterface functions below are ret 8 and follow one of two shapes. "Stores + 0xNNN" means: a null ppv
// returns false; riid X stores this + 0xNNN (0 when this is 0) through ppv and increments the refcount at this +
// 0xc (the original calls 0x5c51e0, which is that increment); anything else tail-jumps to the next QueryInterface
// in the chain named in the comment, with the same arguments.

// cTSHairFilter::QueryInterface, 0x5fe25b (46 bytes, ret 8). A null ppv returns false before the riid is looked at.
// riid 0xd0b524cc stores this and AddRefs it (virtual slot 1); anything else calls cTSFragmentFilterBase::QueryInterface
// (0x759042) with the same arguments and returns its result.
class cTSHairFilter {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5f1f44 (riid 0x4c6c9c85, stores + 0x2c8, then AddRef as above; other riids go to cTSUICASComponentBin::QueryInterface).
class cTSUICASComponentSimBin {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5f3570 (riid 0xec76abc6, stores + 0x290; other riids go to the component chain, cTSUICASComponent below).
class cTSUICASComponentBabyMaker {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5f67bc (riid 0xecb8974a, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentArchetypes {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5fb48e (riid 0x6ca51def, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentOverlays {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5fcc7a (riid 0x2caa06e1, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentBin {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5ff548 (riid 0x0d123d4e, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentSkinTone {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x602f00 (riid 0x713ba27b, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentPetCoatColors {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x60610f (riid 0x719a9d02, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentBodyOptions {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5ecf40 (riid 0x8c6bd3ed, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentClothing {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5ee467 (riid 0x0c6bf669, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentPersonality {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5efa4c (riid 0x8c6c07d7, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentGenetics {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5f1471 (riid 0x4c6c0893, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentAccessories {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x5fe291 (riid 0x0ced66f0, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentHair {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// 0x600f33 (riid 0x11390e39, stores + 0x290; other riids go to the component chain).
class cTSUICASComponentPetGenetics {
public:
    bool QueryInterface(uint32_t riid, void **ppv);
};

// cTSUICASComponent::QueryInterface, 0x75907f (56 bytes, ret 8), the end of the component chain. The class is
// declared in UIWinProcs.h (another group's file) without this member, so the function is free, with the
// __fastcall-plus-dummy-edx form the qi:: functions use. riid 0x4bb92321 stores this + 0x168 (0 when this is 0),
// increments the refcount at this + 0xc, and returns true; anything else tail-jumps to cTSUIWinHandler::QueryInterface
// (0x755d3c). A null ppv returns false.
bool __fastcall cTSUICASComponentQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

} // namespace nTSUICAS
