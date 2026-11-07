#include "ts/cTSObject.h"
#include "game/vtable.h"
#include "simantics/TreeSim.h"

#include <cstdint>

namespace {

// Field access relative to `base` (the primary object or a subobject, as the original code uses it).
template <typename T> T &At(const void *base, intptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using TwoArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using ThreeArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using NoArgPtr = void *(__thiscall *)(void *);
using OneArgPtr = void *(__thiscall *)(void *, uint32_t);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgVoid = void(__thiscall *)(void *, uint32_t);
using TwoArgVoid = void(__thiscall *)(void *, uint32_t, uint32_t);
using FloatArgVoid = void(__thiscall *)(void *, uint32_t, float, uint32_t);
using FiveArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SixArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using TwoArgFloatRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, float);

// `setne al` after a test: the original writes only al, so the rest of eax keeps the callee's result.
uint32_t WithAlFlag(uint32_t eax, bool flag)
{
    return (eax & ~0xffu) | (flag ? 1u : 0u);
}

// The predicate tests: `test ax,ax` looks at the low word, `test al,mask` at the low byte.
uint32_t WordNonZeroIntoAl(uint32_t eax)
{
    return WithAlFlag(eax, (eax & 0xffffu) != 0);
}

// Shared tail of vf177 and vf178: calls slot 104 on this, and if its al is zero calls slot 208 on this.
void CallSlot104Then208(const void *self)
{
    uint32_t r = VirtualSlot<NoArgRet32>(self, 104)(const_cast<void *>(self));
    if (!static_cast<uint8_t>(r)) VirtualSlot<NoArgVoid>(self, 208)(const_cast<void *>(self));
}

} // namespace

uint32_t cTSObject::vf31(uint32_t a)
{
    uint32_t &flags = At<uint32_t>(this, 0x230);
    uint32_t old = flags;
    // Only the low five bits change. eax ends as old ^ a on both paths: the original computes it first.
    uint32_t x = old ^ a;
    if (x & 0x1f) flags = ((old ^ a) & 0xffffffe0u) ^ a;
    return x;
}

uint32_t cTSObject::vf37(uint32_t a)
{
    int32_t v = static_cast<int16_t>(a & 0xffff);
    At<int32_t>(this, 0x1d8) = v;
    return static_cast<uint32_t>(v);
}

uint32_t cTSObject::vf119()
{
    uint32_t r = VirtualCall<NoArgRet32, 152>(this);
    return WithAlFlag(r, (static_cast<uint8_t>(r) & 0x14) != 0);
}

uint32_t cTSObject::vf124()
{
    uint32_t r = VirtualCall<OneArgRet32, 13>(this, 0x6a);
    return WordNonZeroIntoAl(r);
}

uint32_t cTSObject::vf125()
{
    void *obj = VirtualCall<NoArgPtr, 91>(this);
    uint32_t r = VirtualCall<OneArgRet32, 8>(obj, 0x1b);
    return WordNonZeroIntoAl(r);
}

uint32_t cTSObject::vf127()
{
    uint32_t r = VirtualCall<NoArgRet32, 153>(this);
    return WithAlFlag(r, (static_cast<uint8_t>(r) & 0x5f) != 0);
}

void cTSObject::vf177(uint32_t a)
{
    VirtualCall<TwoArgRet32, 170>(this, a, 1);
    CallSlot104Then208(this);
}

void cTSObject::vf178(uint32_t a, float f)
{
    VirtualCall<FloatArgVoid, 171>(this, a, f, 1);
    CallSlot104Then208(this);
}

void cTSObject::vf221()
{
    At<uint8_t>(this, 0x293) = 0;
    At<uint8_t>(this, 0x291) = 1;
    void *sub = At<void *>(this, 0x40);
    void *obj = VirtualCall<NoArgPtr, 18>(sub);
    if (!obj) return;
    // The original reads obj's vtable before calling slot 47, and calls through that copy.
    void *const *objVt = *static_cast<void *const *const *>(obj);
    uint32_t word = static_cast<uint16_t>(VirtualCall<NoArgRet32, 47>(this));
    reinterpret_cast<OneArgVoid>(objVt[20])(obj, word);
}

uint32_t cTSObject::vf245(uint32_t a)
{
    (void)a; // The caller's argument is popped by `ret 4` and never read.
    At<uint8_t>(this, 0x70) = 0;
    uint32_t &flags = At<uint32_t>(this, 0x230);
    flags = (flags & ~0x100u) | 0x100u;
    void *sub = At<void *>(this, 0x1c4);
    int32_t word = static_cast<int16_t>(At<uint16_t>(this, 0x1cc));
    return VirtualCall<ThreeArgRet32, 74>(sub, static_cast<uint32_t>(word), 4, 1);
}

void cTSObject::vf314(float f)
{
    At<float>(this, 0x220) = f;
}

void cTSObject::vf198(uint32_t argument)
{
    ReloadReferences(false);
    At<uint32_t>(this, 0x190) = argument;
    int16_t *const data = &At<int16_t>(this, 0xa6); // the object data words
    data[3] = -1;
    data[0x43] = static_cast<int16_t>(VirtualCall<NoArgRet32, 66>(this));
    data[0x37] = static_cast<int16_t>(VirtualCall<NoArgRet32, 67>(this));
    data[0x17] = 2;
    data[7] = static_cast<int16_t>(VirtualCall<NoArgRet32, 352>(this));

    void *const owner = At<void *>(this, 0x240);
    void *const sizer = VirtualCall<NoArgPtr, 4>(owner);
    const uint32_t reserve = VirtualCall<OneArgRet32, 3>(sizer, 2);
    cTSTreeSim *const treeSim = &At<cTSTreeSim>(this, 4);
    treeSim->PrepareStack(reserve, &At<uint16_t>(this, 0x96));

    int16_t pairs[4] = {0, -1, 0, -1};
    VirtualCall<void(__thiscall *)(void *, int16_t *, uint32_t, uint32_t), 197>(this, pairs, 1, 0);
    VirtualCall<NoArgVoid, 41>(treeSim);
    At<uint8_t>(this, 0x86) = 1;
}

void cTSObject::vf244(uint32_t event, bool runOwnTree, bool propagate, uint32_t argument)
{
    using EventTreeFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using CallTreeFn = void(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using SendFn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    using ByIdFn = void *(__thiscall *)(void *, int32_t);

    if (runOwnTree) {
        void *const owner = VirtualCall<NoArgPtr, 89>(this);
        const uint16_t tree = static_cast<uint16_t>(VirtualCall<EventTreeFn, 31>(owner, event, 0, 0));
        if (tree != 0) {
            void *const treeSim = &At<uint8_t>(this, 4);
            VirtualCall<CallTreeFn, 15>(treeSim, owner, 0, tree, At<uint16_t>(this, 0x1cc), 4, argument, 0, 0);
        }
    }
    if (!propagate) return;
    const int32_t count = static_cast<int32_t>(VirtualCall<NoArgRet32, 66>(this));
    for (int32_t i = 0; i < count; ++i) {
        void *const contained = VirtualCall<OneArgPtr, 68>(this, static_cast<uint32_t>(i));
        if (!contained || static_cast<uint16_t>(VirtualCall<NoArgRet32, 4>(contained)) == 0) continue;
        void *const context = At<void *>(this, 0x40);
        void *const objects = VirtualCall<NoArgPtr, 26>(context);
        // The original reads the list's vtable before asking the contained object for its id again.
        void *const *const objectsVTable = *static_cast<void *const *const *>(objects);
        const int16_t id = static_cast<int16_t>(VirtualCall<NoArgRet32, 4>(contained));
        void *const target = reinterpret_cast<ByIdFn>(objectsVTable[29])(objects, id);
        if (target) VirtualCall<SendFn, 244>(target, event, 1, 1, argument);
    }
}

uint32_t cTSObject::vf249(void *frame, void *node)
{
    using ResumeFn = uint32_t(__thiscall *)(void *, void *, void *, void *);
    using RunNodeFn = uint32_t(__thiscall *)(void *, void *, uint32_t);
    using ByteFn = uint8_t(__thiscall *)(void *);
    using SetByteFn = void(__thiscall *)(void *, uint32_t);

    void *const services = At<void *>(this, 0x48);
    cTSTreeSim *const treeSim = &At<cTSTreeSim>(this, 4);
    if (!VirtualCall<NoArgPtr, 6>(services)) return treeSim->ResumeFrame(frame, node);
    const uint32_t flags = At<uint32_t>(this, 0x230);
    if (!(flags & 0x40) && (flags & 0x80)) {
        void *const helper = VirtualCall<NoArgPtr, 6>(services);
        return VirtualCall<ResumeFn, 3>(helper, this, frame, node);
    }
    if (VirtualCall<ByteFn, 22>(frame) != 0) {
        VirtualCall<SetByteFn, 23>(frame, 0);
        return VirtualCall<RunNodeFn, 13>(treeSim, frame, 0);
    }
    if (VirtualCall<ByteFn, 18>(node) != 0) {
        VirtualCall<SetByteFn, 19>(node, 0);
        const uint32_t result = VirtualCall<RunNodeFn, 13>(treeSim, frame, 0);
        VirtualCall<SetByteFn, 19>(node, 1);
        return result;
    }
    return 0;
}

uint32_t cTSObject::vf346()
{
    uint32_t r = VirtualCall<OneArgRet32, 93>(this, 0xf);
    return WordNonZeroIntoAl(r);
}

// Only al is defined on this path (the original's eax keeps the caller's upper bits), so these return 0 or 1.
uint32_t cTSObject::vf121()
{
    return At<int16_t>(this, 0x122) > 0 ? 1u : 0u;
}

uint32_t cTSObject::vf183(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t r = VirtualCall<OneArgRet32, 13>(this, 9);
    // cwde: the low word of the result, sign-extended into eax, is the first argument of slot 182.
    uint32_t w = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r & 0xffffu)));
    return VirtualCall<SixArgRet32, 182>(this, w, a1, a2, a3, a4, a5);
}

uint32_t cTSObject::vf211()
{
    return VirtualCall<FiveArgRet32, 238>(this, 0x22, 0, 0, 0, 0);
}

uint32_t cTSObject::vf212()
{
    return VirtualCall<FiveArgRet32, 238>(this, 0x21, 0, 0, 0, 0);
}

void cTSObject::vf213(uint32_t a)
{
    At<uint8_t>(this, 0x2e8) = static_cast<uint8_t>(a);
}

uint32_t cTSObject::vf214()
{
    return At<uint8_t>(this, 0x2e8);
}

uint32_t cTSObject::vf219()
{
    return At<int16_t>(this, 0x84) > 0 ? 1u : 0u;
}

void cTSObject::vf222(uint32_t a)
{
    At<uint8_t>(this, 0x293) = static_cast<uint8_t>(a);
}

uint32_t cTSObject::vf224(uint32_t a)
{
    return VirtualCall<TwoArgRet32, 14>(this, 9, a);
}

uint32_t cTSObject::vf315(uint32_t a1, uint32_t a2, float a3)
{
    VirtualCall<TwoArgFloatRet32, 324>(this, a1, a2, a3);
    return a1;
}

uint32_t cTSObject::vf316(uint32_t a)
{
    return VirtualCall<OneArgRet32, 325>(this, a);
}

uint32_t cTSObject::vf328()
{
    uint32_t r = VirtualCall<NoArgRet32, 153>(this);
    return WithAlFlag(r, (static_cast<uint8_t>(r) & 0x50) != 0);
}
