// nGZSceneGraph::cShaderInit (lane RZ). See the header.
#include "sg/cShaderInit.h"

#include "game/original.h"
#include "sg/cShader.h"
#include "sg/SGVectorBlocks.h"
#include "sg/cTextureManager.h"
#include "simantics/cTSBehaviorTree.h"

#include <corecrt.h>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace nGZSceneGraph {

static_assert(offsetof(ShaderInitData, mDescs) == offsetof(ShaderInitView, mDescBegin), "Build reads the descriptors here");
static_assert(offsetof(ShaderInitData, mRanges) + 8 == offsetof(ShaderInitView, mRanges), "Build reads range pairs 1..12 from here");
static_assert(offsetof(ShaderInitData, mObjects) == 4 && offsetof(ShaderInitData, mNames) == 0x10, "data");

namespace {

// The object vectors are the game's std::vector of counted pointers (the image's instantiations: assign 0x20c700 is its
// _Assign_counted_range, grow 0x255dd0 its _Emplace_reallocate; both shared with other classes' vectors). Ours is the
// toolchain's std::vector over the same {first, last, end} triple, with an element that counts on copy, counts the new
// value before releasing the old on assignment, and releases on destruction (the image's element code inlined in both).
class ObjectRef {
public:
    ObjectRef(const ObjectRef &other) : mObject(other.mObject)
    {
        if (mObject) mObject->AddRef();
    }
    ObjectRef &operator=(const ObjectRef &other)
    {
        ReplaceObjectRef(other.mObject);
        return *this;
    }
    ~ObjectRef() noexcept(false)
    {
        if (mObject) mObject->Release();
    }

private:
    void ReplaceObjectRef(cIShaderObject *object)
    {
        cIShaderObject *const old = mObject;
        if (object == old) return;
        if (object) object->AddRef();
        mObject = object;
        if (old) old->Release();
    }
    cIShaderObject *mObject;
};
using ObjectStdVec = std::vector<ObjectRef>;
static_assert(sizeof(ObjectRef) == sizeof(cIShaderObject *) && sizeof(ObjectStdVec) == sizeof(ObjectVec), "same triple");

ObjectStdVec &AsStd(ObjectVec &vec) { return reinterpret_cast<ObjectStdVec &>(vec); }
const ObjectStdVec &AsStd(const ObjectVec &vec) { return reinterpret_cast<const ObjectStdVec &>(vec); }

// Give `pointer` a new counted value: counted first, stored, the old one released (as the setters of the image).
void ReplaceObject(cIShaderObject *&field, cIShaderObject *object)
{
    cIShaderObject *const old = field;
    if (object == old) return;
    if (object) object->AddRef();
    field = object;
    if (old) old->Release();
}

// Append to a vector of counted pointers (the vector's copy is counted). The callers hold the object across the call; the
// value is the caller's pointer seen as an element, as the image passes it.
void Append(ObjectVec &vec, cIShaderObject *object)
{
    AsStd(vec).push_back(reinterpret_cast<const ObjectRef &>(object));
}
void AppendHeld(ObjectVec &vec, cIShaderObject *object)
{
    if (object) object->AddRef();
    Append(vec, object);
    if (object) object->Release();
}

int32_t CountOf(const DescriptorVec &v) { return int32_t((reinterpret_cast<intptr_t>(v.mEnd) - reinterpret_cast<intptr_t>(v.mBegin)) / int32_t(sizeof(ShaderDescriptor))); }

} // namespace

namespace {
void ReleaseIfAny(cIShaderObject *object)
{
    if (object) object->Release();
}
} // namespace

// 0xaa7500
void DescriptorVec::DestroyRange(ShaderDescriptor *first, ShaderDescriptor *last, DescriptorVec *)
{
    for (; first != last; ++first) {
        for (int32_t i = 15; i >= 0; --i) cTextureHandle::At(&first->mTextures[i].mHandle)->Destroy();
        first->mNames.Destroy();
        ReleaseIfAny(first->mObject28);
        ReleaseIfAny(first->mObject24);
        ReleaseIfAny(first->mObject20);
        ReleaseIfAny(first->mObject1c);
        reinterpret_cast<RefPtrVector *>(&first->mPartsB)->Destroy();
        reinterpret_cast<RefPtrVector *>(&first->mPartsA)->Destroy();
        ReleaseIfAny(first->mObject00);
    }
}

// 0xaa7a90
ShaderDescriptor *DescriptorVec::FillDefault(ShaderDescriptor *first, uint32_t count, DescriptorVec *)
{
    for (; count != 0; --count, ++first) {
        first->mObject00 = nullptr;
        first->mPartsA = {};
        first->mPartsB = {};
        first->mObject1c = first->mObject20 = first->mObject24 = first->mObject28 = nullptr;
        first->mNames.Construct();
        for (ShaderTextureSlot &t : first->mTextures) t.mHandle = cTextureHandle::kNone; // the value is left as it was
        first->mByteC4 = first->mByteC5 = first->mByteC6 = first->mByteC7 = 0;
    }
    return first;
}

namespace {

constexpr uint32_t kMaxDescriptors = 0xffffffffu / sizeof(ShaderDescriptor); // 0x147ae14

// The capacity a vector of `cap` grows to for `count`: half again, at least count, at most the maximum.
uint32_t GrownCapacity(uint32_t cap, uint32_t count)
{
    if (cap > kMaxDescriptors - (cap >> 1)) return kMaxDescriptors;
    const uint32_t grown = cap + (cap >> 1);
    return grown < count ? count : grown;
}

void CopyCounted(cIShaderObject *&field, cIShaderObject *object)
{
    field = object;
    if (object) object->AddRef();
}

void AssignObjects(ObjectVec &to, const ObjectVec &from)
{
    AsStd(to) = AsStd(from); // nothing for the same vector
}

} // namespace

// 0xaa7c80
ShaderTextureSlot *ShaderTextureSlot::CopyConstruct(const ShaderTextureSlot *from)
{
    cTextureHandle::At(&mHandle)->CopyFrom(cTextureHandle::At(&from->mHandle));
    mValue = from->mValue;
    return this;
}

ShaderDescriptor *ShaderDescriptor::CopyConstruct(const ShaderDescriptor *from)
{
    CopyCounted(mObject00, from->mObject00);
    reinterpret_cast<ShaderPartVec *>(&mPartsA)->CopyConstruct(reinterpret_cast<const ShaderPartVec *>(&from->mPartsA));
    reinterpret_cast<ShaderPartVec *>(&mPartsB)->CopyConstruct(reinterpret_cast<const ShaderPartVec *>(&from->mPartsB));
    CopyCounted(mObject1c, from->mObject1c);
    CopyCounted(mObject20, from->mObject20);
    CopyCounted(mObject24, from->mObject24);
    CopyCounted(mObject28, from->mObject28);
    mNames.CopyConstruct(&from->mNames);
    for (uint32_t i = 0; i < 16; ++i) mTextures[i].CopyConstruct(&from->mTextures[i]);
    mByteC4 = from->mByteC4;
    mByteC5 = from->mByteC5;
    mByteC6 = from->mByteC6;
    mByteC7 = from->mByteC7;
    return this;
}

// 0xaa7db0
ShaderDescriptor *ShaderDescriptor::Assign(const ShaderDescriptor *from)
{
    if (from != this) ReplaceObject(mObject00, from->mObject00);
    AssignObjects(mPartsA, from->mPartsA);
    AssignObjects(mPartsB, from->mPartsB);
    if (from != this) {
        ReplaceObject(mObject1c, from->mObject1c);
        ReplaceObject(mObject20, from->mObject20);
        ReplaceObject(mObject24, from->mObject24);
        ReplaceObject(mObject28, from->mObject28);
    }
    mNames.Assign(&from->mNames);
    for (uint32_t i = 0; i < 16; ++i) {
        cTextureHandle::At(&mTextures[i].mHandle)->Assign(cTextureHandle::At(&from->mTextures[i].mHandle));
        mTextures[i].mValue = from->mTextures[i].mValue;
    }
    mByteC4 = from->mByteC4;
    mByteC5 = from->mByteC5;
    mByteC6 = from->mByteC6;
    mByteC7 = from->mByteC7;
    return this;
}

// 0xaa77c0
ShaderDescriptor *DescriptorVec::CopyRange(const ShaderDescriptor *first, const ShaderDescriptor *last, ShaderDescriptor *dest, DescriptorVec *)
{
    for (; first != last; ++first, ++dest) dest->CopyConstruct(first);
    return dest;
}

// 0xaa7930
ShaderDescriptor *DescriptorVec::CopyCount(const ShaderDescriptor *first, uint32_t count, ShaderDescriptor *dest, DescriptorVec *)
{
    for (; count != 0; --count, ++first, ++dest) dest->CopyConstruct(first);
    return dest;
}

// 0xaa72f0
ShaderDescriptor *DescriptorVec::AssignCount(const ShaderDescriptor *first, uint32_t count, ShaderDescriptor *dest)
{
    for (; count != 0; --count, ++first, ++dest) dest->Assign(first);
    return dest;
}

// 0xaa75e0
void DescriptorVec::Reallocate(uint32_t count, const void *)
{
    if (count > kMaxDescriptors) std::_Xlength_error("vector too long");
    const uint32_t oldSize = uint32_t(CountOf(*this));
    const uint32_t oldCap = uint32_t((reinterpret_cast<intptr_t>(mCap) - reinterpret_cast<intptr_t>(mBegin)) / int32_t(sizeof(ShaderDescriptor)));
    const uint32_t bytes = GrownCapacity(oldCap, count) * uint32_t(sizeof(ShaderDescriptor));
    ShaderDescriptor *const block = vectorblocks::Allocate<ShaderDescriptor>(bytes);
    FillDefault(block + oldSize, count - oldSize, this);
    CopyRange(mBegin, mEnd, block, this);
    if (mBegin) {
        DestroyRange(mBegin, mEnd, this);
        vectorblocks::Free(mBegin, mCap);
    }
    mBegin = block;
    mEnd = block + count;
    mCap = reinterpret_cast<ShaderDescriptor *>(reinterpret_cast<uint8_t *>(block) + bytes);
}

// 0xaa7130
void DescriptorVec::Assign(const ShaderDescriptor *first, uint32_t count)
{
    const uint32_t cap = uint32_t((reinterpret_cast<intptr_t>(mCap) - reinterpret_cast<intptr_t>(mBegin)) / int32_t(sizeof(ShaderDescriptor)));
    if (count <= cap) {
        const uint32_t size = uint32_t(CountOf(*this));
        if (count <= size) {
            ShaderDescriptor *const newEnd = AssignCount(first, count, mBegin);
            DestroyRange(newEnd, mEnd, this);
            mEnd = newEnd;
            return;
        }
        for (ShaderDescriptor *p = mBegin; p != mEnd; ++p, ++first) p->Assign(first);
        mEnd = CopyCount(first, count - size, mEnd, this);
        return;
    }
    if (count > kMaxDescriptors) std::_Xlength_error("vector too long");
    const uint32_t newCap = GrownCapacity(cap, count);
    if (mBegin) {
        DestroyRange(mBegin, mEnd, this);
        vectorblocks::Free(mBegin, mCap);
        mBegin = mEnd = mCap = nullptr;
    }
    const uint32_t bytes = newCap * uint32_t(sizeof(ShaderDescriptor));
    ShaderDescriptor *const block = vectorblocks::Allocate<ShaderDescriptor>(bytes);
    mBegin = mEnd = block;
    mCap = reinterpret_cast<ShaderDescriptor *>(reinterpret_cast<uint8_t *>(block) + bytes);
    mEnd = CopyCount(first, count, block, this);
}

// 0xaa7cf0
void DescriptorVec::Destroy()
{
    if (!mBegin) return;
    DestroyRange(mBegin, mEnd, this);
    vectorblocks::Free(mBegin, mCap);
    mBegin = nullptr;
    mEnd = nullptr;
    mCap = nullptr;
}

// 0xc631d0
uint32_t NamePool::At(uint32_t i) const { return reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(w[3]))[i] + w[0]; }

// 0xaa7bc0
ShaderInitData *ShaderInitData::Construct()
{
    mAttribute = 0;
    mObjects = {};
    mNames.Construct();
    mDescs = {};
    std::memset(mRanges, 0xff, sizeof(mRanges));
    return this;
}

// 0xaa7b90
cShaderInit *cShaderInit::Construct()
{
    *reinterpret_cast<const void *const **>(this) = kOwnVTable;
    mRefCount = 0;
    mData.Construct();
    return this;
}

// 0xaa82a0
cShaderInit *cShaderInit::Create()
{
    void *block = ::operator new(sizeof(cShaderInit));
    return block ? static_cast<cShaderInit *>(block)->Construct() : nullptr;
}

// 0xaa7d70
void cShaderInit::DestructBody()
{
    *reinterpret_cast<const void *const **>(this) = kOwnVTable;
    mData.mDescs.Destroy();
    mData.mNames.Destroy();
    reinterpret_cast<RefPtrVector *>(&mData.mObjects)->Destroy();
}

// Slot 26, 0xaa7f60
cShaderInit *cShaderInit::DeleteBody(uint32_t flags)
{
    DestructBody();
    if (flags & 1) ::operator delete(this, sizeof(cShaderInit));
    return this;
}

// 0xaa8460. The image's branch for a pass inside the vector comes first; the branches for "the new size is smaller than the
// old" cannot be taken here (the pass is at or past the end), nor can "equal".
ShaderDescriptor *cShaderInit::Element(uint32_t pass)
{
    DescriptorVec &vec = mData.mDescs;
    const uint32_t have = uint32_t(CountOf(vec));
    if (have <= pass) {
        const uint32_t wanted = pass + 1;
        const uint32_t room = uint32_t((reinterpret_cast<intptr_t>(vec.mCap) - reinterpret_cast<intptr_t>(vec.mBegin)) / int32_t(sizeof(ShaderDescriptor)));
        if (room < wanted) {
            const uint32_t scratch = pass;
            vec.Reallocate(wanted, &scratch);
        } else {
            vec.mEnd = DescriptorVec::FillDefault(vec.mEnd, wanted - have, &vec);
        }
    }
    return vec.mBegin + pass;
}

// Slot 0, 0xaa8430
bool cShaderInit::QueryInterfaceBody(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != kIidCounted) return false;
    *out = this;
    AddRef();
    return true;
}

// Slot 1 (0x148fb0)
uint32_t cShaderInit::AddRefBody() { return ++mRefCount; }

// Slot 2 (0x292890)
uint32_t cShaderInit::ReleaseBody()
{
    if (--mRefCount != 0) return mRefCount;
    Delete(1);
    return 0;
}

// Slot 4 (0x14a260)
uint32_t cShaderInit::SetAttributeBody(uint32_t value)
{
    mData.mAttribute = value;
    return value;
}

// Slot 21 (0x142840)
ShaderInitData *cShaderInit::GetDataBody() { return &mData; }

// Slot 3, 0xaa8180
uint32_t cShaderInit::CloneBody(uint32_t iid, void **out)
{
    cShaderInit *const copy = Create();
    copy->AddRef();
    copy->mData.mAttribute = mData.mAttribute;
    AssignObjects(copy->mData.mObjects, mData.mObjects);
    copy->mData.mNames.Assign(&mData.mNames);
    if (&copy->mData.mDescs != &mData.mDescs) copy->mData.mDescs.Assign(mData.mDescs.mBegin, uint32_t(CountOf(mData.mDescs)));
    std::memcpy(copy->mData.mRanges, mData.mRanges, sizeof(mData.mRanges));
    copy->QueryInterface(iid, out);
    return copy->Release();
}

// Slots 5, 6, 7, 14, 15
void cShaderInit::SetObject00Body(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    ReplaceObject(Element(pass)->mObject00, object);
}
void cShaderInit::SetObject1cBody(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    ReplaceObject(Element(pass)->mObject1c, object);
}
void cShaderInit::SetObject20Body(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    ReplaceObject(Element(pass)->mObject20, object);
}
void cShaderInit::SetObject24Body(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    ReplaceObject(Element(pass)->mObject24, object);
}
void cShaderInit::SetObject28Body(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    ReplaceObject(Element(pass)->mObject28, object);
}

// Slots 8, 9, 10: the object is counted before the pass is looked up (the lookup may grow the vector).
void cShaderInit::AddPartABody(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    if (object) object->AddRef();
    ObjectVec &vec = Element(pass)->mPartsA;
    Append(vec, object);
    if (object) object->Release();
}
void cShaderInit::AddPartBBody(int32_t pass, cIShaderObject *object)
{
    if (pass < 0) return;
    if (object) object->AddRef();
    ObjectVec &vec = Element(pass)->mPartsB;
    Append(vec, object);
    if (object) object->Release();
}
void cShaderInit::AddObjectBody(cIShaderObject *object)
{
    AppendHeld(mData.mObjects, object);
}

// Slots 11, 12
void cShaderInit::AddPassNameBody(int32_t pass, const void *name)
{
    if (pass < 0) return;
    Element(pass)->mNames.Add(static_cast<const cIShaderName *>(name));
}
void cShaderInit::AddNameBody(const void *name)
{
    mData.mNames.Add(static_cast<const cIShaderName *>(name));
}

// Slot 13, 0xaa8690
void cShaderInit::SetTextureBody(int32_t pass, uint32_t index, uint32_t source, uint32_t value)
{
    if (pass < 0) return;
    ShaderDescriptor *const desc = Element(pass);
    uint32_t made;
    const cTextureHandle *const temp = cTextureHandle::At(&made)->InitFromName(reinterpret_cast<const char *>(uintptr_t{source}));
    cTextureHandle::At(&desc->mTextures[index].mHandle)->Assign(temp);
    cTextureHandle::At(&made)->Destroy();
    desc->mTextures[index].mValue = value;
}

// Slots 16..19
void cShaderInit::SetByteC4Body(int32_t pass, uint8_t value)
{
    if (pass < 0) return;
    Element(pass)->mByteC4 = value;
}
void cShaderInit::SetByteC5Body(int32_t pass, uint8_t value)
{
    if (pass < 0) return;
    Element(pass)->mByteC5 = value;
}
void cShaderInit::SetByteC6Body(int32_t pass, uint8_t value)
{
    if (pass < 0) return;
    Element(pass)->mByteC6 = value;
}
void cShaderInit::SetByteC7Body(int32_t pass, uint8_t value)
{
    if (pass < 0) return;
    Element(pass)->mByteC7 = value;
}

// Slot 20, 0xaa8510
void cShaderInit::SetRangeBody(uint32_t pair, uint32_t first, uint32_t second)
{
    mData.mRanges[2 * pair] = first;
    mData.mRanges[2 * pair + 1] = second;
}

// Slots 22, 23: the vector is found again after every refusal (a part's answer could in principle change it).
bool cShaderInit::PartAIsBody(int32_t pass, uint32_t iid, void **out)
{
    if (pass < 0 || uint32_t(pass) >= uint32_t(CountOf(mData.mDescs))) return false;
    cIShaderObject **p = mData.mDescs.mBegin[pass].mPartsA.mBegin;
    if (p == mData.mDescs.mBegin[pass].mPartsA.mEnd) return false;
    for (;;) {
        if ((*p)->QueryInterface(iid, out)) return true;
        ++p;
        if (p == mData.mDescs.mBegin[pass].mPartsA.mEnd) return false;
    }
}
bool cShaderInit::PartBIsBody(int32_t pass, uint32_t iid, void **out)
{
    if (pass < 0 || uint32_t(pass) >= uint32_t(CountOf(mData.mDescs))) return false;
    cIShaderObject **p = mData.mDescs.mBegin[pass].mPartsB.mBegin;
    if (p == mData.mDescs.mBegin[pass].mPartsB.mEnd) return false;
    for (;;) {
        if ((*p)->QueryInterface(iid, out)) return true;
        ++p;
        if (p == mData.mDescs.mBegin[pass].mPartsB.mEnd) return false;
    }
}

// Slot 24, 0xaa8380
bool cShaderInit::ObjectIsBody(uint32_t iid, void **out)
{
    cIShaderObject **p = mData.mObjects.mBegin;
    if (p == mData.mObjects.mEnd) return false;
    for (;;) {
        if ((*p)->QueryInterface(iid, out)) return true;
        ++p;
        if (p == mData.mObjects.mEnd) return false;
    }
}

// Slot 25, 0xaa8760
void *cShaderInit::TextureOfBody(int32_t pass, uint32_t index)
{
    if (pass < 0 || uint32_t(pass) >= uint32_t(CountOf(mData.mDescs))) return nullptr;
    return cTextureHandle::At(&mData.mDescs.mBegin[pass].mTextures[index].mHandle)->Get();
}

// The tables are filled by dynamic initialisers (a member function's address is no constant expression), before any code of
// the program runs, as the image's table is there before its code.
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)

namespace {
template <typename T> const void *Own(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

// Slot order as the image's (27 slots; vtable 0xe760a4).
const void *const cShaderInit::kOwnVTable[27] = {
    Own(&cShaderInit::QueryInterfaceBody),   // 0 0xaa8430
    Own(&cShaderInit::AddRefBody),           // 1 0x148fb0 (shared)
    Own(&cShaderInit::ReleaseBody),          // 2 0x292890 (shared)
    Own(&cShaderInit::CloneBody),            // 3 0xaa8180
    Own(&cShaderInit::SetAttributeBody),     // 4 0x14a260 (shared)
    Own(&cShaderInit::SetObject00Body),      // 5 0xaa85b0
    Own(&cShaderInit::SetObject1cBody),      // 6 0xaa8630
    Own(&cShaderInit::SetObject20Body),      // 7 0xaa85f0
    Own(&cShaderInit::AddPartABody),         // 8 0xaa80b0
    Own(&cShaderInit::AddPartBBody),         // 9 0xaa7f90
    Own(&cShaderInit::AddObjectBody),        // 10 0xaa8030
    Own(&cShaderInit::AddPassNameBody),      // 11 0xaa8160
    Own(&cShaderInit::AddNameBody),          // 12 0xaa8150
    Own(&cShaderInit::SetTextureBody),       // 13 0xaa8690
    Own(&cShaderInit::SetObject24Body),      // 14 0xaa8720
    Own(&cShaderInit::SetObject28Body),      // 15 0xaa8570
    Own(&cShaderInit::SetByteC4Body),        // 16 0xaa8700
    Own(&cShaderInit::SetByteC5Body),        // 17 0xaa8550
    Own(&cShaderInit::SetByteC6Body),        // 18 0xaa8670
    Own(&cShaderInit::SetByteC7Body),        // 19 0xaa8530
    Own(&cShaderInit::SetRangeBody),         // 20 0xaa8510
    Own(&cShaderInit::GetDataBody),          // 21 0x142840 (shared)
    Own(&cShaderInit::PartAIsBody),          // 22 0xaa83c0
    Own(&cShaderInit::PartBIsBody),          // 23 0xaa8310
    Own(&cShaderInit::ObjectIsBody),         // 24 0xaa8380
    Own(&cShaderInit::TextureOfBody),        // 25 0xaa8760
    Own(&cShaderInit::DeleteBody),           // 26 0xaa7f60
};

} // namespace nGZSceneGraph
