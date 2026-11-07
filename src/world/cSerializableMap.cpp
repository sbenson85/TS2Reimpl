#include "world/cSerializableMap.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZRefCount.h"

#include <new>

namespace nTSWorld {

template <class Key, class Less> bool cSerializableMap<Key, Less>::QueryInterface(uint32_t riid, void **out)
{
    if (out == nullptr) return false;
    if (riid != kSerializableIid) return reinterpret_cast<RZUnknownQueryInterface *>(this)->QueryInterfaceUnknown(riid, out);
    *out = this != nullptr ? reinterpret_cast<char *>(this) + 8 : nullptr;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef; the result is not used
    return true;
}

template <class Key, class Less> bool cSerializableMap<Key, Less>::Face::QueryInterface(uint32_t riid, void **out)
{
    return Owner()->QueryInterface(riid, out);
}

template <class Key, class Less> uint32_t cSerializableMap<Key, Less>::Face::GetGZCLSID() const { return kClassId; }

// GUID 0x81ac60.
template <> cGuidMap *cGuidMap::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kGuidMapVTable);
    mSerializable = GameAddress<void>(kGuidMapFaceVTable);
    reinterpret_cast<cRZUnknown *>(this)->Destruct();
    mMap.~Tree();
    if (flags & 1) ::operator delete(this, sizeof(cGuidMap));
    return this;
}

// GUID 0x81d0f0: always true; the stream's errors are not looked at. The count goes out signed, keys and values unsigned.
template <> bool cGuidMap::Face::Write(ifaces::cIGZOStream *stream)
{
    cGuidMap *self = Owner();
    stream->SetUint32(VirtualCall<uint32_t(__thiscall *)(void *), 5>(this)); // the class id, through the face's own table
    stream->SetUint32(kVersion);
    stream->SetUint32(self->mWord);
    stream->SetSint32(static_cast<int32_t>(self->mMap.size()));
    for (const auto &entry : self->mMap) {
        stream->SetUint32(entry.first);
        stream->SetUint32(entry.second);
    }
    return true;
}

// GUID 0x81bc20: class id and version checked (false otherwise), the word read straight into the object, then the count
// and its pairs; a key already in the map keeps its value. The stream's errors are not looked at.
template <> bool cGuidMap::Face::Read(ifaces::cIGZIStream *stream)
{
    cGuidMap *self = Owner();
    uint32_t classId; // uninitialised stack locals in the original: a failed read leaves them as they were
    stream->GetUint32(&classId);
    if (classId != VirtualCall<uint32_t(__thiscall *)(void *), 5>(this)) return false;
    int32_t version;
    stream->GetSint32(&version);
    if (version != static_cast<int32_t>(kVersion)) return false;
    stream->GetUint32(&self->mWord);
    int32_t count;
    stream->GetSint32(&count);
    // One key and one value for the whole loop, as in the original: a failed read leaves the previous pair's.
    uint32_t key, value;
    for (int32_t i = 0; i < count; ++i) {
        stream->GetUint32(&key);
        stream->GetUint32(&value);
        self->mMap.try_emplace(key, value); // the search first, the node only when the key is new
    }
    return true;
}

// ---- the string copy (lane VQ) ----

// String 0x81aca0: as the GUID copy's; the tree's nodes destroy both strings (their text freed) before each node.
template <> cStringPairMap *cStringPairMap::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kStringPairMapVTable);
    mSerializable = GameAddress<void>(kStringPairMapFaceVTable);
    reinterpret_cast<cRZUnknown *>(this)->Destruct();
    mMap.~Tree();
    if (flags & 1) ::operator delete(this, sizeof(cStringPairMap));
    return this;
}

// String 0x81d190: as the GUID copy's Write, with each key and value written as a GZStr (the node's own string).
template <> bool cStringPairMap::Face::Write(ifaces::cIGZOStream *stream)
{
    cStringPairMap *self = Owner();
    stream->SetUint32(VirtualCall<uint32_t(__thiscall *)(void *), 5>(this)); // the class id, through the face's own table
    stream->SetUint32(kVersion);
    stream->SetUint32(self->mWord);
    stream->SetSint32(static_cast<int32_t>(self->mMap.size()));
    for (auto &entry : self->mMap) {
        stream->SetGZStr(const_cast<cMapString *>(&entry.first));
        stream->SetGZStr(&entry.second);
    }
    return true;
}

// String 0x81bd90: as the GUID copy's Read, except that each entry gets two fresh empty strings, copied into a pair
// that the insert 0x81a8a0 copies again into a node when the key is new (case-insensitively).
template <> bool cStringPairMap::Face::Read(ifaces::cIGZIStream *stream)
{
    cStringPairMap *self = Owner();
    uint32_t classId; // uninitialised stack locals in the original: a failed read leaves them as they were
    stream->GetUint32(&classId);
    if (classId != VirtualCall<uint32_t(__thiscall *)(void *), 5>(this)) return false;
    int32_t version;
    stream->GetSint32(&version);
    if (version != static_cast<int32_t>(kVersion)) return false;
    stream->GetUint32(&self->mWord);
    // The original reads the count into the stream argument's own stack slot: a read that fails without writing leaves
    // the stream's address as the count.
    int32_t count = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stream));
    stream->GetSint32(&count);
    for (int32_t i = 0; i < count; ++i) {
        cMapString key, value;
        stream->GetGZStr(&key);
        stream->GetGZStr(&value);
        const Tree::value_type entry(key, value);
        self->mMap.Insert(entry);
    }
    return true;
}

template struct cSerializableMap<uint32_t, cGZGUIDLessThan>;
template struct cSerializableMap<cMapString, cRZStringLessThanNoCase>;
} // namespace nTSWorld
