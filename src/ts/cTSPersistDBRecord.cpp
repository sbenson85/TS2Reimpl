#include "ts/cTSPersistDBRecord.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace {

// Field access relative to `base` (a subobject pointer, as the original code uses it).
template <typename T> T &At(const void *base, intptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using TwoArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

// The original sets only al after the callee's result is in eax: the result keeps its upper bits.
uint32_t WithAlOne(uint32_t eax)
{
    return (eax & ~0xffu) | 1u;
}

// The null primary object gives `this` = 0x38 after the subobject offset.
constexpr uintptr_t kNullPrimaryThis = 0x38;

} // namespace

uint32_t cTSPersistDBRecord::vf30(uint32_t a)
{
    At<uint32_t>(this, 0x4c) = a;
    return WithAlOne(a);
}

void *cTSPersistDBRecordITS::vf56_5()
{
    auto self = reinterpret_cast<uintptr_t>(this);
    return self == kNullPrimaryThis ? nullptr : reinterpret_cast<void *>(self - 0x18);
}

void *cTSPersistDBRecordITS::vf56_6()
{
    auto self = reinterpret_cast<uintptr_t>(this);
    return self == kNullPrimaryThis ? nullptr : reinterpret_cast<void *>(self + 4);
}

uint32_t cTSPersistDBRecordITS::vf56_15()
{
    uint32_t r = VirtualCall<NoArgRet32, 12>(this);
    return At<uint32_t>(this, -0x14) - r;
}

uint32_t cTSPersistDBRecordITS::vf56_16(uint32_t a)
{
    uint32_t sum = VirtualCall<NoArgRet32, 12>(this) + a;
    At<uint32_t>(this, -0xc) = sum;
    return sum;
}

uint32_t cTSPersistDBRecordITS::vf56_17()
{
    uint32_t r = VirtualCall<NoArgRet32, 12>(this);
    return At<uint32_t>(this, -0xc) - r;
}

uint32_t cTSPersistDBRecordIGZ::vf60_13(uint32_t a)
{
    // The original calls slot 16 on this - 4 (the cITSPersistDBRecord subobject) with the argument.
    void *owner = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(this) - 4);
    uint32_t r = VirtualCall<OneArgRet32, 16>(owner, a);
    return WithAlOne(r);
}

uint32_t cTSPersistDBRecordIGZ::vf60_14(uint32_t a)
{
    uint32_t sum = VirtualCall<NoArgRet32, 12>(this) + a;
    uint32_t r = VirtualCall<OneArgRet32, 13>(this, sum);
    return WithAlOne(r);
}

uint32_t cTSPersistDBRecordIGZ::vf60_15(uint32_t a1, uint32_t a2)
{
    if (a2 == 0) return 0;
    void *obj = At<void *>(this, 0x20);
    if (!obj) {
        *reinterpret_cast<uint32_t *>(a2) = 0;
        return 0;
    }
    return VirtualCall<TwoArgRet32, 0>(obj, a1, a2);
}

bool cTSPersistDBRecordIGZ::vf60_17(uint8_t mask)
{
    uint8_t flags = At<uint8_t>(this, 0x24);
    return static_cast<uint8_t>(flags & mask) == mask;
}

void cTSPersistDBRecordIGZ::vf60_18(uint8_t bits, uint8_t set)
{
    uint8_t &flags = At<uint8_t>(this, 0x24);
    if (set)
        flags |= bits;
    else
        flags = static_cast<uint8_t>(flags & ~bits);
}

// ---- second batch -------------------------------------------------------------------------------

namespace {

using PtrArg = uint32_t(__thiscall *)(void *, void *);
using PtrArgPtr = void *(__thiscall *)(void *, void *);
using ByteArgVoid = uint32_t(__thiscall *)(void *, uint32_t);

// Replaces the reference-counted object at `slot` with `fresh`, in the original's order: nothing when
// they are the same pointer, else AddRef the new one (if any), store it, Release the old one (if any).
void ReplaceCounted(void **slot, void *fresh)
{
    void *old = *slot;
    if (fresh == old) return;
    if (fresh) VirtualCall<NoArgRet32, 1>(fresh);
    *slot = fresh;
    if (old) VirtualCall<NoArgRet32, 2>(old);
}

// A new cRZString (0x20 bytes, refcount 0) holding [data, data + len), built as the original does:
// operator new, then the string constructor, the game's vtable and a zero count.
cRZString *NewString(const char *data, size_t len)
{
    auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    if (s) {
        new (&s->mString) std::string(data, len);
        s->mVTable = GameAddress(cRZString::kVTable);
        s->mRefCount = 0;
    }
    return s;
}

} // namespace

uint8_t cTSPersistDBRecordIS::vf16_18()
{
    return At<uint8_t>(this, -0x10 + 0x30);
}

void *cTSPersistDBRecordSS::vf32_7()
{
    auto self = reinterpret_cast<uintptr_t>(this);
    return self == 0x20 ? nullptr : reinterpret_cast<void *>(self - 0x10);
}

uint32_t cTSPersistDBRecordSS::vf32_10()
{
    void *its = reinterpret_cast<uint8_t *>(this) + 0x18;
    uint32_t r = VirtualCall<NoArgRet32, 12>(its);
    return At<uint32_t>(this, 0x14) + r;
}

uint32_t cTSPersistDBRecordSS::vf32_11()
{
    void *its = reinterpret_cast<uint8_t *>(this) + 0x18;
    uint32_t r = VirtualCall<NoArgRet32, 12>(its);
    return At<uint32_t>(this, 4) - r;
}

uint8_t cTSPersistDBRecordSS::vf32_13()
{
    return At<uint8_t>(this, -0x20 + 0x30);
}

uint32_t cTSPersistDBRecordSlots::vf29(uint32_t a)
{
    At<uint32_t>(this, 0x48) = a;
    return WithAlOne(a);
}

uint32_t cTSPersistDBRecordSlots::vf31(uint32_t a)
{
    At<uint32_t>(this, 0x50) = a;
    return WithAlOne(a);
}

uint32_t cTSPersistDBRecordITS2::AddRef()
{
    return ++At<uint32_t>(this, -0x38 + 4);
}

void *cTSPersistDBRecordITS2::vf56_3()
{
    return reinterpret_cast<uint8_t *>(this) - 0x38;
}

void *cTSPersistDBRecordITS2::vf56_4()
{
    return reinterpret_cast<cTSPersistDBRecordSS *>(reinterpret_cast<uint8_t *>(this) - 0x18)->vf32_7();
}

uint32_t cTSPersistDBRecordITS2::vf56_9()
{
    return reinterpret_cast<cTSPersistDBRecordSS *>(reinterpret_cast<uint8_t *>(this) - 0x18)->vf32_10();
}

uint32_t cTSPersistDBRecordITS2::vf56_10()
{
    return reinterpret_cast<cTSPersistDBRecordSS *>(reinterpret_cast<uint8_t *>(this) - 0x18)->vf32_11();
}

uint32_t cTSPersistDBRecordITS2::vf56_11()
{
    return At<uint32_t>(this, -4);
}

bool cTSPersistDBRecordITS2::vf56_20(void *outString)
{
    void *primary = reinterpret_cast<uint8_t *>(this) - 0x38;
    cRZString tmp;
    tmp.Construct();
    const char *text = reinterpret_cast<const char *>(At<uintptr_t>(this, -4) + At<uint32_t>(this, -0xc));
    tmp.mString.assign(text);
    // The position is read again after the assign, as in the original.
    uint32_t pos = static_cast<uint32_t>(tmp.mString.size()) + 1 + At<uint32_t>(this, -0xc);
    At<uint32_t>(this, -0xc) = pos;
    bool ok;
    if (pos > At<uint32_t>(this, -0x14)) {
        VirtualCall<ByteArgVoid, 24>(primary, 1);
        ok = false;
    } else {
        if (pos & 1) At<uint32_t>(this, -0xc) = pos + 1;
        VirtualCall<PtrArg, 17>(outString, &tmp);
        VirtualCall<ByteArgVoid, 24>(primary, 0);
        ok = true;
    }
    tmp.Destruct();
    return ok;
}

bool cTSPersistDBRecordITS2::vf56_22(void *srcString)
{
    // The length (slot 7) is fetched before the data pointer (slot 6).
    uint32_t len = VirtualCall<NoArgRet32, 7>(srcString);
    auto data = reinterpret_cast<const char *>(static_cast<uintptr_t>(VirtualCall<NoArgRet32, 6>(srcString)));
    cRZString *s = NewString(data, len);
    ReplaceCounted(&At<void *>(this, 0x20), s);
    return true;
}

bool cTSPersistDBRecordITS2::vf56_23(void *outString)
{
    if (At<void *>(this, 0x20) == nullptr && VirtualCall<NoArgRet32, 12>(this) != 0) {
        auto getText = [this]() {
            return reinterpret_cast<const char *>(static_cast<uintptr_t>(VirtualCall<NoArgRet32, 11>(this)));
        };
        if (*getText() != 0) {
            (void)getText(); // the original calls slot 11 twice in a row; the first result is dropped
            const char *p = getText();
            size_t len = std::strlen(p);
            size_t room = VirtualCall<NoArgRet32, 12>(this);
            size_t count = len < room ? len : room;
            auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            if (s) {
                new (&s->mString) std::string(getText(), count);
                s->mVTable = GameAddress(cRZString::kVTable);
                s->mRefCount = 0;
            }
            ReplaceCounted(&At<void *>(this, 0x20), s);
        }
    }
    void *cache = At<void *>(this, 0x20);
    if (!cache) return false;
    VirtualCall<PtrArg, 17>(outString, cache);
    return true;
}

void cTSPersistDBRecordITS2::vf56_25(const void *key)
{
    std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x10, key, 16);
}

void cTSPersistDBRecordITS2::vf56_26(uint32_t a)
{
    At<uint8_t>(this, 0xc) = static_cast<uint8_t>(a);
}

uint32_t cTSPersistDBRecordIGZ2::AddRef()
{
    return ++At<uint32_t>(this, -0x3c + 4);
}

void *cTSPersistDBRecordIGZ2::GetKey()
{
    return reinterpret_cast<uint8_t *>(this) + 0xc;
}

void cTSPersistDBRecordIGZ2::SetKey(const void *key)
{
    std::memcpy(reinterpret_cast<uint8_t *>(this) + 0xc, key, 16);
}

uint32_t cTSPersistDBRecordIGZ2::Close()
{
    void *primary = reinterpret_cast<uint8_t *>(this) - 0x3c;
    return VirtualCall<NoArgRet32, 23>(primary);
}

uint32_t cTSPersistDBRecordIGZ2::vf60_10()
{
    void *ss = reinterpret_cast<uint8_t *>(this) - 0x1c;
    return VirtualCall<NoArgRet32, 11>(ss);
}

uint32_t cTSPersistDBRecordIGZ2::vf60_12()
{
    void *its = reinterpret_cast<uint8_t *>(this) - 4;
    return VirtualCall<NoArgRet32, 17>(its);
}

void cTSPersistDBRecordIGZ2::vf60_16(void *obj)
{
    ReplaceCounted(&At<void *>(this, 0x20), obj);
}

// ---- third batch --------------------------------------------------------------------------------

namespace {

using Obj = cTSPersistDBRecordObject;

Obj *PrimaryOf(void *subobject, intptr_t offset)
{
    return reinterpret_cast<Obj *>(reinterpret_cast<uintptr_t>(subobject) - offset);
}

// Releases a reference-counted object (slot 2, no arguments), if there is one.
void ReleaseCounted(void *obj)
{
    if (obj) VirtualCall<NoArgRet32, 2>(obj);
}

void InstallVTables(Obj *o)
{
    o->mVTable = GameAddress(Obj::kVTableRecord);
    o->mVTableIn = GameAddress(Obj::kVTableIS);
    o->mVTableSer = GameAddress(Obj::kVTableSS);
    o->mVTableITS = GameAddress(Obj::kVTableITS);
    o->mVTableIGZ = GameAddress(Obj::kVTableIGZ);
}

} // namespace

Obj *Obj::Construct(uint32_t order)
{
    cRZSerializableStream::Construct(order);
    // The base class's cITSPersistDBRecord and cIGZPersistDBRecord vtables are written first and
    // replaced at once; only the final values are observable.
    InstallVTables(this);
    m40 = 0;
    m44 = 0;
    mKey[0] = mKey[1] = mKey[2] = mKey[3] = 0;
    mCache = nullptr;
    mSecondary = nullptr;
    mFlags = 0;
    return this;
}

Obj *Obj::DeletingDestructor(uint32_t flags)
{
    InstallVTables(this);
    ReleaseCounted(mSecondary);
    ReleaseCounted(mCache);
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

void *Obj::Create()
{
    auto *o = static_cast<Obj *>(::operator new(kSize));
    if (!o) return nullptr;
    o->Construct(1);
    return reinterpret_cast<uint8_t *>(o) + 0x38;
}

bool Obj::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIdITS && riid != kIdIGZ) return cRZSerializableStream::QueryInterface(riid, out);
    uintptr_t self = reinterpret_cast<uintptr_t>(this);
    uintptr_t sub = self + (riid == kIdITS ? 0x38 : 0x3c);
    *out = self ? reinterpret_cast<void *>(sub) : nullptr;
    VirtualCall<NoArgRet32, kAddRefSlot>(this);
    return true;
}

uint32_t Obj::AddRef()
{
    return ++mRefCount;
}

uint32_t Obj::Release()
{
    return cRZSerializableStream::Release();
}

bool Obj::SetVoid(const void *src, uint32_t n)
{
    return cRZSerializableStream::SetVoid(src, n);
}

bool Obj::SetByteOrder(uint32_t order)
{
    return cRZSerializableStream::SetByteOrder(order);
}

bool Obj::Reset()
{
    return cRZSerializableStream::Reset();
}

bool cTSPersistDBRecordIS2::QueryInterface(uint32_t riid, void **out)
{
    return PrimaryOf(this, 0x10)->QueryInterface(riid, out);
}

uint32_t cTSPersistDBRecordIS2::AddRef()
{
    return PrimaryOf(this, 0x10)->AddRef();
}

uint32_t cTSPersistDBRecordIS2::Release()
{
    return PrimaryOf(this, 0x10)->Release();
}

bool cTSPersistDBRecordIS2::GetVoid(void *dst, uint32_t n)
{
    return reinterpret_cast<cRZSerializableStreamIS *>(this)->GetVoid(dst, n);
}

Obj *cTSPersistDBRecordIS2::vf16_21(uint32_t flags)
{
    return PrimaryOf(this, 0x10)->DeletingDestructor(flags);
}

bool cTSPersistDBRecordIS2::vf16_22(uint32_t order)
{
    return PrimaryOf(this, 0x10)->SetByteOrder(order);
}

bool cTSPersistDBRecordIS2::vf16_23()
{
    return PrimaryOf(this, 0x10)->Reset();
}

bool cTSPersistDBRecordSS2::QueryInterface(uint32_t riid, void **out)
{
    return PrimaryOf(this, 0x20)->QueryInterface(riid, out);
}

uint32_t cTSPersistDBRecordSS2::AddRef()
{
    return PrimaryOf(this, 0x20)->AddRef();
}

uint32_t cTSPersistDBRecordSS2::Release()
{
    return PrimaryOf(this, 0x20)->Release();
}

void *cTSPersistDBRecordSS2::vf32_6()
{
    return reinterpret_cast<uint8_t *>(this) - 0x20;
}

bool cTSPersistDBRecordSS2::Write(void *out)
{
    return reinterpret_cast<cRZSerializableStreamSS *>(this)->Write(out);
}

bool cTSPersistDBRecordSS2::Read(void *in)
{
    return reinterpret_cast<cRZSerializableStreamSS *>(this)->Read(in);
}

bool cTSPersistDBRecordSS2::vf32_8(uint32_t order)
{
    return PrimaryOf(this, 0x20)->SetByteOrder(order);
}

bool cTSPersistDBRecordSS2::vf32_9()
{
    return PrimaryOf(this, 0x20)->Reset();
}

bool cTSPersistDBRecordITS3::QueryInterface(uint32_t riid, void **out)
{
    return PrimaryOf(this, 0x38)->QueryInterface(riid, out);
}

uint32_t cTSPersistDBRecordITS3::Release()
{
    return PrimaryOf(this, 0x38)->Release();
}

bool cTSPersistDBRecordITS3::vf56_7(uint32_t order)
{
    return PrimaryOf(this, 0x38)->SetByteOrder(order);
}

bool cTSPersistDBRecordITS3::vf56_8()
{
    return PrimaryOf(this, 0x38)->Reset();
}

bool cTSPersistDBRecordITS3::vf56_18(const void *src, uint32_t n)
{
    return PrimaryOf(this, 0x38)->SetVoid(src, n);
}

void cTSPersistDBRecordITS3::vf56_14(uint32_t a)
{
    uint32_t n = a + VirtualCall<NoArgRet32, 12>(this);
    Obj *p = PrimaryOf(this, 0x38);
    if (n > p->mCapacity) p->Reserve(n);
    p->mSize = n;
}

bool cTSPersistDBRecordITS3::vf56_19(uint8_t value, uint32_t count)
{
    Obj *p = PrimaryOf(this, 0x38);
    // The result of Grow is ignored (as in the original); it fails only when operator new[] fails,
    // which throws instead.
    p->Grow(count);
    std::memset(p->mData + p->mSize, value, count);
    p->mSize += count;
    uint32_t r = VirtualCall<NoArgRet32, cRZSerializableStream::kGetErrorSlot>(p);
    return static_cast<uint8_t>(r) == 0;
}

bool cTSPersistDBRecordITS3::vf56_21(uint32_t a)
{
    Obj *p = PrimaryOf(this, 0x38);
    bool ok = p->Reserve(VirtualCall<NoArgRet32, 12>(this) + a);
    if (ok) p->mSize = VirtualCall<NoArgRet32, 12>(this) + a;
    return ok;
}

bool cTSPersistDBRecordIGZ3::QueryInterface(uint32_t riid, void **out)
{
    return PrimaryOf(this, 0x3c)->QueryInterface(riid, out);
}

uint32_t cTSPersistDBRecordIGZ3::Release()
{
    return PrimaryOf(this, 0x3c)->Release();
}

bool cTSPersistDBRecordIGZ3::vf60_8(void *dst, uint32_t n)
{
    return reinterpret_cast<cRZSerializableStreamIS *>(reinterpret_cast<uint8_t *>(this) - 0x2c)->GetVoid(dst, n);
}

bool cTSPersistDBRecordIGZ3::vf60_9(const void *src, uint32_t n)
{
    return PrimaryOf(this, 0x3c)->SetVoid(src, n);
}
