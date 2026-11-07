#include "gz/cIStreamStringBehaviourAdaptor.h"

#include "game/vtable.h"
#include "rz/cRZOStream.h"

#include <new>

namespace {
using NoArgRet32 = uint32_t(__thiscall *)(void *);
constexpr int kAddRefSlot = 1;
constexpr int kQueryInterfaceSlot = 0;
} // namespace

cIStreamStringBehaviourAdaptor *cIStreamStringBehaviourAdaptor::Construct(void *inner)
{
    mRefCount = 0;
    mSkipByteSwap = 1;
    mOwner = nullptr;
    mVTable = VTable();
    mInner = inner;
    VirtualCall<NoArgRet32, kAddRefSlot>(inner);
    return this;
}

bool cIStreamStringBehaviourAdaptor::QueryInterface(uint32_t id, void **out)
{
    if (id == kInnerId)
        return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), kQueryInterfaceSlot>(mInner, id, out);
    return cRZIStream::QueryInterface(id, out);
}

uint32_t cIStreamStringBehaviourAdaptor::Skip(uint32_t count)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kSkipSlot>(mInner, count);
}

uint32_t cIStreamStringBehaviourAdaptor::GetVoid(void *dst, uint32_t size)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, void *, uint32_t), kRawReadSlot>(mInner, dst, size);
}

uint32_t cIStreamStringBehaviourAdaptor::GetError()
{
    return VirtualCall<NoArgRet32, kGetErrorSlot>(mInner);
}

cIStreamStringBehaviourAdaptor *cIStreamStringBehaviourAdaptor::DeletingDestructor(uint32_t flags)
{
    if (mInner) VirtualCall<NoArgRet32, kReleaseSlot>(mInner);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cIStreamStringBehaviourAdaptor));
    return this;
}

// The owned vtable (docs/standalone-shape.md, "Owned statics"): the face an image-free program stores. Each slot runs
// the member of the same name; the slots the adaptor inherits run cRZIStream's (and the bodies cRZIStream shares with
// cRZOStream run cRZOStream's, on the same layout).
namespace rzface {

class cIStreamStringBehaviourAdaptor final {
    ::cIStreamStringBehaviourAdaptor *Self() { return reinterpret_cast<::cIStreamStringBehaviourAdaptor *>(this); }
    ::cRZIStream *Base() { return reinterpret_cast<::cRZIStream *>(this); }
    ::cRZOStream *Out() { return reinterpret_cast<::cRZOStream *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return ++Self()->mRefCount; }                                                  // 1 (shared 0x148fb0)
    virtual uint32_t Release() { return Out()->Release(); }                                                    // 2 (shared 0x1b5fc4)
    virtual uint32_t Skip(uint32_t count) { return Self()->Skip(count); }                                      // 3
    virtual bool GetSint8Slot4(void *dst) { return Base()->GetSint8(dst); }
    virtual bool GetSint8Slot5(void *dst) { return Base()->GetSint8(dst); }
    virtual bool GetSint16Slot6(void *dst) { return Base()->GetSint16(dst); }
    virtual bool GetSint16Slot7(void *dst) { return Base()->GetSint16(dst); }
    virtual bool GetSint32Slot8(void *dst) { return Base()->GetSint32(dst); }
    virtual bool GetSint32Slot9(void *dst) { return Base()->GetSint32(dst); }
    virtual bool GetSint64Slot10(void *dst) { return Base()->GetSint64(dst); }
    virtual bool GetSint64Slot11(void *dst) { return Base()->GetSint64(dst); }
    virtual bool GetFloat32(void *dst) { return Base()->GetFloat32(dst); }                                     // 12
    virtual bool GetFloat64(void *dst) { return Base()->GetFloat64(dst); }                                     // 13
    virtual uint8_t GetRZCharStr(char *buf, uint32_t size) { return Base()->GetRZCharStr(buf, size); }         // 14
    virtual uint8_t GetGZStr(cIGZString *out) { return Base()->GetGZStr(out); }                                // 15
    virtual uint32_t GetGZSerializable(void *serializable) { return Base()->GetGZSerializable(serializable); } // 16
    virtual uint32_t GetVoid(void *dst, uint32_t size) { return Self()->GetVoid(dst, size); }                  // 17
    virtual uint32_t GetError() { return Self()->GetError(); }                                                 // 18
    virtual void *SetOwner(void *owner) { return Out()->SetOwner(owner); }                                     // 19 (shared 0x1b6617)
    virtual void *GetOwner() { return Self()->mOwner; }                                                        // 20 (shared 0x145770)
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }             // 21
    virtual bool SetByteOrder(uint32_t order) { return Out()->SetByteOrder(order); }                           // 22 (shared 0x1b5f3c)
    virtual bool True23() { return true; }                                                                     // 23 (shared 0x144910)
};

} // namespace rzface

const void *cIStreamStringBehaviourAdaptor::OwnVTable()
{
    rzface::cIStreamStringBehaviourAdaptor face;
    return *reinterpret_cast<const void *const *>(&face);
}
