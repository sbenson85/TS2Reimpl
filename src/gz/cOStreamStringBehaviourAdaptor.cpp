#include "gz/cOStreamStringBehaviourAdaptor.h"

#include "game/vtable.h"

#include <new>

namespace {
using NoArgRet32 = uint32_t(__thiscall *)(void *);
constexpr int kFlushSlot = 3;
constexpr int kSetVoidSlot = 17;
constexpr int kGetErrorSlot = 18;
} // namespace

cOStreamStringBehaviourAdaptor *cOStreamStringBehaviourAdaptor::Construct(void *inner)
{
    mRefCount = 0;
    mNativeOrder = 1;
    mOwner = nullptr;
    mVTable = VTable();
    mInner = inner;
    if (inner) VirtualCall<NoArgRet32, kAddRefSlot>(inner);
    return this;
}

uint32_t cOStreamStringBehaviourAdaptor::Flush()
{
    return VirtualCall<NoArgRet32, kFlushSlot>(mInner);
}

uint32_t cOStreamStringBehaviourAdaptor::SetVoid(const void *src, uint32_t n)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, const void *, uint32_t), kSetVoidSlot>(mInner, src, n);
}

uint32_t cOStreamStringBehaviourAdaptor::GetError()
{
    return VirtualCall<NoArgRet32, kGetErrorSlot>(mInner);
}

cOStreamStringBehaviourAdaptor *cOStreamStringBehaviourAdaptor::DeletingDestructor(uint32_t flags)
{
    if (mInner) VirtualCall<NoArgRet32, kReleaseSlot>(mInner);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cOStreamStringBehaviourAdaptor));
    return this;
}

// The owned vtable (docs/standalone-shape.md, "Owned statics"): slots the adaptor does not override run cRZOStream's.
namespace rzface {

class cOStreamStringBehaviourAdaptor final {
    ::cOStreamStringBehaviourAdaptor *Self() { return reinterpret_cast<::cOStreamStringBehaviourAdaptor *>(this); }
    ::cRZOStream *Base() { return reinterpret_cast<::cRZOStream *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Base()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return ++Base()->mRefCount; }                                                 // 1 (shared 0x148fb0)
    virtual uint32_t Release() { return Base()->Release(); }                                                  // 2 (shared 0x1b5fc4)
    virtual uint32_t Flush() { return Self()->Flush(); }                                                      // 3
    virtual uint32_t SetSint8Slot4(uint8_t value) { return Base()->SetSint8(value); }
    virtual uint32_t SetSint8Slot5(uint8_t value) { return Base()->SetSint8(value); }
    virtual uint32_t SetSint16(int16_t value) { return Base()->SetSint16(value); }                            // 6
    virtual uint32_t SetUint16(uint16_t value) { return Base()->SetUint16(value); }                           // 7
    virtual uint32_t SetSint32Slot8(int32_t value) { return Base()->SetSint32(value); }
    virtual uint32_t SetSint32Slot9(int32_t value) { return Base()->SetSint32(value); }
    virtual uint32_t SetSint64Slot10(int64_t value) { return Base()->SetSint64(value); }
    virtual uint32_t SetSint64Slot11(int64_t value) { return Base()->SetSint64(value); }
    virtual uint32_t SetFloat32(float value) { return Base()->SetFloat32(value); }                            // 12
    virtual uint32_t SetFloat64(double value) { return Base()->SetFloat64(value); }                           // 13
    virtual uint8_t SetRZCharStr(const char *text) { return Base()->SetRZCharStr(text); }                     // 14
    virtual uint8_t SetGZStr(cIGZString *text) { return Base()->SetGZStr(text); }                             // 15
    virtual uint32_t SetGZSerializable(void *serializable) { return Base()->SetGZSerializable(serializable); } // 16
    virtual uint32_t SetVoid(const void *src, uint32_t n) { return Self()->SetVoid(src, n); }                // 17
    virtual uint32_t GetError() { return Self()->GetError(); }                                                // 18
    virtual void *SetOwner(void *owner) { return Base()->SetOwner(owner); }                                   // 19 (shared 0x1b6617)
    virtual void *GetOwner() { return Base()->mOwner; }                                                       // 20 (shared 0x145770)
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }            // 21
    virtual bool SetByteOrder(uint32_t order) { return Base()->SetByteOrder(order); }                         // 22 (shared 0x1b5f3c)
    virtual bool True23() { return true; }                                                                    // 23 (shared 0x144910)
};

} // namespace rzface

const void *cOStreamStringBehaviourAdaptor::OwnVTable()
{
    rzface::cOStreamStringBehaviourAdaptor face;
    return *reinterpret_cast<const void *const *>(&face);
}
