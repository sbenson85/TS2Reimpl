#include "ts/cTSFamilyRelationship.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZSerializable.h"
#include "ifaces/cITSFamilyRelationship.h"
#include "misc/UtilitiesC.h"

#include <new>

namespace {
// A virtual function declared with its real narrow (bool) result: MSVC then loads the slot into a register for the call, as the
// original does (the generated interfaces' bool methods compile to `call [reg+off]`).
template <int N, class R, class... A> struct NarrowSlot : vtable_detail::Pad<N> {
    virtual R __thiscall call(A...) = 0;
};

// The stream a ReadFrom / WriteTo source hands out: released when the call ends (a counted reference).
struct StreamHolder {
    ifaces::cIGZSerializable *object = nullptr; // only its QueryInterface/Release slots are used; the stream is another interface
    ~StreamHolder()
    {
        if (object) object->Release();
    }
};
} // namespace

cTSFamilyRelationship *cTSFamilyRelationshipSerialFace::Owner()
{
    return reinterpret_cast<cTSFamilyRelationship *>(reinterpret_cast<uint8_t *>(this) - 8);
}

// 0x4f2780: the record class id of a single relationship.
uint32_t cTSFamilyRelationshipSerialFace::GetGZCLSID()
{
    return cTSFamilyRelationship::kClassId;
}

// 0x4f1fd0
cTSFamilyRelationship *cTSFamilyRelationship::Construct()
{
    // The base interfaces' constructors store their own vtables first (dead stores in the original too).
    // (volatile: keeps them as stores, as the original does.)
    *reinterpret_cast<const void *volatile *>(&mPersistVTable) = GameAddress<void>(kBasePersistVTable);
    *reinterpret_cast<const void *volatile *>(&mSerial.mVTable) = GameAddress<void>(kBaseSerialVTable);
    mVTable = GameAddress<void>(kVTable);
    mPersistVTable = GameAddress<void>(kPersistVTable);
    mSerial.mVTable = GameAddress<void>(kSerialVTable);
    mRefCount = 0;
    mKey.type = 0;
    mKey.group = 0;
    mKey.instance = 0;
    mKey.instanceHigh = 0;
    new (&mTies) FamilyTieMap();
    mKey.instance = 0;
    mKey.group = 0;
    mKey.type = kClassId;
    return this;
}

// 0x4f2680
cTSFamilyRelationship *cTSFamilyRelationship::Create()
{
    void *memory = ::operator new(sizeof(cTSFamilyRelationship));
    if (!memory) return nullptr;
    return static_cast<cTSFamilyRelationship *>(memory)->Construct();
}

// 0x4f2e90
bool cTSFamilyRelationship::QueryInterface(uint32_t riid, void **out)
{
    uintptr_t face;
    switch (riid) {
    case kInterfaceId:
    case 1:
        *out = this;
        As<ifaces::cITSFamilyRelationship>(this)->AddRef();
        return true;
    case kPersistResourceId:
        face = reinterpret_cast<uintptr_t>(&mPersistVTable);
        break;
    case kSerializableId:
        face = reinterpret_cast<uintptr_t>(&mSerial);
        break;
    default:
        return false;
    }
    *out = reinterpret_cast<void *>(-static_cast<uintptr_t>(this != nullptr) & face);
    As<ifaces::cITSFamilyRelationship>(this)->AddRef();
    return true;
}

// 0x4f3720
uint32_t cTSFamilyRelationship::Release()
{
    if (mRefCount <= 1) {
        mVTable = GameAddress<void>(kVTable);
        mPersistVTable = GameAddress<void>(kPersistVTable);
        mSerial.mVTable = GameAddress<void>(kSerialVTable);
        mTies.~FamilyTieMap();
        ::operator delete(this, sizeof(cTSFamilyRelationship));
        return 0;
    }
    return --mRefCount;
}

// 0x4f2d50: the sim of the first tie of a type.
bool cTSFamilyRelationship::GetTie(int32_t type, uint16_t *outSim)
{
    auto found = mTies.find(type);
    if (found == mTies.end()) return false;
    if (outSim) *outSim = found->second;
    return true;
}

// 0x4f2d00: the type of the first tie to a sim (no null check of outType).
bool cTSFamilyRelationship::GetTieType(uint16_t sim, int32_t *outType)
{
    for (auto it = mTies.begin(); it != mTies.end(); ++it) {
        if (it->second == sim) {
            *outType = it->first;
            return true;
        }
    }
    return false;
}

// 0x4f2c90: appends the child sims to out (not cleared first); true if out is then non-empty.
bool cTSFamilyRelationship::GetChildren(SimIdVector *out)
{
    if (!out) return false;
    for (auto it = mTies.begin(); it != mTies.end(); ++it) {
        if (it->first == kFamilyTieChild) out->push_back(it->second);
    }
    return !out->empty();
}

// 0x4f2e10: as GetChildren for the siblings.
bool cTSFamilyRelationship::GetSiblings(SimIdVector *out)
{
    if (!out) return false;
    for (auto it = mTies.begin(); it != mTies.end(); ++it) {
        if (it->first == kFamilyTieSibling) out->push_back(it->second);
    }
    return !out->empty();
}

// 0x4f3cd0: lists (children, siblings) get the sim added unless it is already there; the other types replace the sim.
bool cTSFamilyRelationship::SetTie(int32_t type, uint16_t sim)
{
    if (type == kFamilyTieChild || type == kFamilyTieSibling) {
        for (auto it = mTies.begin(); it != mTies.end(); ++it) {
            if (it->first == type && it->second == sim) return false;
        }
    } else {
        auto found = mTies.find(type);
        if (found != mTies.end()) {
            found->second = sim;
            return true;
        }
    }
    const FamilyTieMap::value_type tie(type, sim);
    mTies.insert(tie);
    return true;
}

// 0x4f37e0: erases the first tie to a sim.
bool cTSFamilyRelationship::RemoveSim(uint16_t sim)
{
    for (auto it = mTies.begin(); it != mTies.end(); ++it) {
        if (it->second == sim) {
            mTies.erase(it);
            return true;
        }
    }
    return false;
}

// 0x4f3840: erases ONE tie of the type even when several share it.
bool cTSFamilyRelationship::RemoveTie(int32_t type)
{
    auto found = mTies.find(type);
    if (found == mTies.end()) return false;
    mTies.erase(found);
    return true;
}

// 0x4f3a90: the source's stream reader, read through the serializable face.
bool cTSFamilyRelationship::ReadFrom(void *source)
{
    StreamHolder stream;
    bool result;
    if (VirtualSlot<bool(__thiscall *)(void *, uint32_t, void *)>(source, 0)(source, kStreamReaderId, &stream.object))
        result = VirtualSlot<bool(__thiscall *)(void *, void *)>(&mSerial, 4)(&mSerial, stream.object);
    else
        result = false;
    return result;
}

// 0x4f3b90: the source's stream writer, written through the serializable face.
bool cTSFamilyRelationship::WriteTo(void *target)
{
    StreamHolder stream;
    bool result;
    if (static_cast<NarrowSlot<0, bool, uint32_t, void *> *>(target)->call(kStreamWriterId, &stream.object))
        result = static_cast<NarrowSlot<3, bool, void *> *>(static_cast<void *>(&mSerial))->call(stream.object);
    else
        result = false;
    return result;
}

// 0x4f3dc0 (serial face slot 3): version, metadata, count, then (type u32, sim u16) per tie. Never reports a stream error.
bool cTSFamilyRelationshipSerialFace::Write(ifaces::cIGZOStream *stream)
{
    cTSFamilyRelationship *owner = Owner();
    stream->SetUint32(cTSFamilyRelationship::kWriteVersion);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(cTSFamilyRelationship::kWriteVersion);
    stream->SetMetadata(metadata);
    stream->SetUint32(static_cast<uint32_t>(owner->mTies.size()));
    for (auto it = owner->mTies.begin(); it != owner->mTies.end(); ++it) {
        stream->SetUint32(it->first);
        stream->SetUint16(it->second);
    }
    return true;
}

// 0x4f2f80 (serial face slot 4): version (ignored), count, then (type u32, sim u16) per tie, added to what is there. Never
// clears the map or checks the stream; a failed read keeps the previous values.
bool cTSFamilyRelationshipSerialFace::Read(ifaces::cIGZIStream *stream)
{
    cTSFamilyRelationship *owner = Owner();
    uint32_t version = 0, count = 0;
    stream->GetUint32(&version);
    stream->GetUint32(&count);
    uint32_t type = 0;
    uint16_t sim = 0;
    for (uint32_t i = 0; i < count; ++i) {
        stream->GetUint32(&type);
        stream->GetUint16(&sim);
        owner->mTies.emplace(static_cast<int32_t>(type), sim);
    }
    return true;
}
