#include "ts/cTSStringSet.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSStringSet.h"
#include <cstring>

// The map value's destructor: the vector's strings are destroyed and freed (Tidy 0x1441eb), as _Erase_tree 0x1eab00 needs.
// (The copy constructor and assignment are declared in the header and not yet used by any replacement.)
cTSStringSetStringList::~cTSStringSetStringList() { Tidy(); }

// Structure and vtable facts: notes/feedback/DA.md ("cTSStringSet"). Each member is the original's body
// (0x1ee330, 0x1ee280, ...); the RVAs are in the status file and the hook table.

uint32_t cTSStringSet::Release()
{
    if (mRefCount <= 1) {
        As<ifaces::cITSStringSet>(this)->DeletingDestructor(1);
        return 0;
    }
    return --mRefCount;
}

bool cTSStringSet::QueryInterface(uint32_t riid, void **out)
{
    constexpr uint32_t kPersistIid = 0x456b8f1d; // cIGZPersistResource: the +4 face
    switch (riid) {
    case 1:
    case kIid:
    case kClassIid:
        *out = this;
        break;
    case kPersistIid:
        *out = this ? &mPersist : nullptr;
        break;
    default:
        return false;
    }
    As<ifaces::cITSStringSet>(this)->AddRef();
    return true;
}

void cTSStringSet::Clear()
{
    mStrings.clear();
    mComments.clear();
}

// The body of the destructor (0x1eae50): vtables back, then the members in reverse order, as the compiler's own
// destructor does.
void cTSStringSet::Destruct()
{
    mVTable = GameAddress(kVTable);
    mPersist.mVTable = GameAddress(kPersistVTable);
    mSourceName.~cRZString();
    mSortOrder.~vector();
    mComments.~cTSStringSetMap();
    mStrings.~cTSStringSetMap();
    mName.~cRZString();
}

void *cTSStringSet::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

void cTSStringSet::SetInstance(int16_t instance)
{
    mKey[2] = static_cast<uint32_t>(static_cast<int32_t>(instance));
}

void cTSStringSet::SetKey(const uint32_t *key)
{
    if (key[0] != mKey[0] || key[1] != mKey[1] || key[2] != mKey[2] || key[3] != mKey[3])
        As<ifaces::cITSStringSet>(this)->Clear();
    std::memcpy(mKey, key, sizeof mKey);
    if (mKey[0] == 0) mKey[0] = kTypeSTR;
}

uint32_t cTSStringSet::LoadFromFile(void *file, uint32_t instance)
{
    As<ifaces::cITSStringSet>(this)->SetKeyFromFile(static_cast<ifaces::cITSObjectFileRef *>(file), instance, mKey[0]);
    return As<ifaces::cITSStringSet>(this)->Load();
}

uint32_t cTSStringSet::SaveToFile(void *file, uint32_t instance)
{
    As<ifaces::cITSStringSet>(this)->SetKeyFromFile(static_cast<ifaces::cITSObjectFileRef *>(file), instance, mKey[0]);
    return As<ifaces::cITSStringSet>(this)->Commit();
}

// cTSResourceFactoryStringSet (STR# and CTSS): vtable 0xda68cc. Create is the cdecl creator 0x1eba40 (not a
// Ghidra function); DeletingDestructor is slot 7 (0x1eb200).
cTSResourceFactoryStringSet *cTSResourceFactoryStringSet::Create()
{
    auto *factory = static_cast<cTSResourceFactoryStringSet *>(::operator new(sizeof(cTSResourceFactoryStringSet)));
    if (factory) {
        factory->mVTable = GameAddress(kVTable);
        factory->mRefCount = 0;
    }
    return factory;
}

void *cTSResourceFactoryStringSet::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}
