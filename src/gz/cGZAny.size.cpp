// The property sets' own instantiations of the cIGZAny values (0x1bexxx), AnyString's constructors and AnyStringSet
// (0x236xxx-0x238xxx): /O1 code. See gz/cGZAny.h.
#include "gz/cGZAny.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZAny.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZString.h"
#include "rz/nGZLog.h"

#include <new>

namespace {
// The type id of any cIGZAny, through its own vtable (slot 4).
uint32_t TypeOf(const void *any)
{
    return As<ifaces::cIGZAny>(any)->GetType();
}
} // namespace

// ---- AnySint32, and the bodies AnyFloat32 and AnyAny share with it ----

void *AnySint32::DeletingDestructor(uint32_t flags)
{
    mVTable = gzany::AnyVTable();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool AnySint32::Assign(const void *source)
{
    const uint32_t sourceType = TypeOf(source);
    if (sourceType == TypeOf(this)) {
        mValue = static_cast<const AnySint32 *>(source)->mValue;
        return true;
    }
    return false;
}

int32_t AnySint32::Compare(const void *other) const
{
    const int32_t value = static_cast<const AnySint32 *>(other)->mValue;
    if (mValue < value) return -1;
    return mValue > value ? 1 : 0;
}

AnySint32 *AnySint32::Clone() const
{
    auto *copy = static_cast<AnySint32 *>(::operator new(sizeof(AnySint32)));
    if (copy) {
        copy->mVTable = gzany::Sint32VTable();
        copy->mValue = mValue;
        return copy;
    }
    return nullptr;
}

uint32_t AnySint32::GetType() const
{
    return gzany::kTypeSint32;
}

const char *AnySint32::GetTypeName() const
{
    return ts2_rva_da3d60;
}

// ---- AnyFloat32 (lane GZ: the standalone shape) ----

namespace gz {

// The folded body 0x1bf068 (AnySint32's and AnyAny's slot 1 too): the dword copied from a value of the same type.
bool AnyFloat32::Assign(const cIGZAny &source)
{
    const uint32_t sourceType = source.GetType();
    if (sourceType == GetType()) {
        mValue = static_cast<const AnyFloat32 &>(source).mValue;
        return true;
    }
    return false;
}

int32_t AnyFloat32::Compare(const cIGZAny &other) const
{
    const float value = static_cast<const AnyFloat32 &>(other).mValue;
    if (value > mValue) return -1;
    return mValue > value ? 1 : 0;
}

cIGZAny *AnyFloat32::Clone() const
{
    return new AnyFloat32(*this);
}

uint32_t AnyFloat32::GetType() const
{
    return gzany::kTypeFloat32;
}

const char *AnyFloat32::GetTypeName() const
{
    return "AnyFloat32";
}

template struct AnyAdapters<AnyFloat32>;

} // namespace gz

// ---- AnyAny ----

int32_t AnyAny::Compare(const void *other) const
{
    const uintptr_t value = reinterpret_cast<uintptr_t>(static_cast<const AnyAny *>(other)->mValue);
    const uintptr_t mine = reinterpret_cast<uintptr_t>(mValue);
    if (value > mine) return -1;
    return value < mine ? 1 : 0;
}

AnyAny *AnyAny::Clone() const
{
    auto *copy = static_cast<AnyAny *>(::operator new(sizeof(AnyAny)));
    if (copy) {
        copy->mVTable = gzany::AnyAnyVTable();
        copy->mValue = mValue;
        return copy;
    }
    return nullptr;
}

uint32_t AnyAny::GetType() const
{
    return gzany::kTypeAnyAny;
}

const char *AnyAny::GetTypeName() const
{
    return ts2_rva_da3e18;
}

// ---- AnyString ----

AnyString *AnyString::Construct(const AnyString &source)
{
    mVTable = gzany::StringVTable();
    mString = source.mString;
    if (mString) mString->AddRef();
    return this;
}

// The new string is stored through RZUnknownSlot::Assign (AddRef, store, Release of the null before it). If the
// string's constructor throws, the original's frame frees the block and resets the vtable (the destructor of the
// half-built Any); ours lets the exception through with the block freed by nobody: a bad_alloc from a 0x20-byte
// copy, which the game does not survive either way.
AnyString *AnyString::Construct(const cIGZString &text)
{
    mVTable = gzany::StringVTable();
    mString = nullptr;
    auto *block = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    cIGZString *made = block ? reinterpret_cast<cIGZString *>(block->Construct(text)) : nullptr;
    reinterpret_cast<RZUnknownSlot *>(&mString)->Assign(made);
    return this;
}

AnyString *AnyString::Construct(const char *text)
{
    mVTable = gzany::StringVTable();
    mString = nullptr;
    auto *block = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    cIGZString *made = block ? reinterpret_cast<cIGZString *>(block->Construct(text)) : nullptr;
    reinterpret_cast<RZUnknownSlot *>(&mString)->Assign(made);
    return this;
}

void AnyString::Destruct()
{
    if (mString) mString->Release();
    mVTable = gzany::AnyVTable();
}

void *AnyString::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool AnyString::Assign(const void *source)
{
    const uint32_t sourceType = TypeOf(source);
    if (sourceType == TypeOf(this)) {
        auto *from = static_cast<const AnyString *>(source);
        if (&from->mString != &mString) reinterpret_cast<RZUnknownSlot *>(&mString)->Assign(from->mString);
        return true;
    }
    return false;
}

int32_t AnyString::Compare(const void *other) const
{
    // Not a byte match either way: MSVC loads the slot into a register, the original calls through memory.
    return mString->CompareTo(*static_cast<const AnyString *>(other)->mString, true);
}

AnyString *AnyString::Clone() const
{
    auto *copy = static_cast<AnyString *>(::operator new(sizeof(AnyString)));
    if (copy) return copy->Construct(*this);
    return nullptr;
}

uint32_t AnyString::GetType() const
{
    return gzany::kTypeString;
}

const char *AnyString::GetTypeName() const
{
    return ts2_rva_da3db0;
}

// ---- the string set ----

size_t GZStringHash::operator()(const GZString &text) const
{
    return nGZLog::HashChars(text.c_str());
}

std::pair<GZStringSet::iterator, bool> GZStringSetOps::InsertSize(const GZString &text)
{
    return mSet.insert(text);
}

GZStringSet::iterator GZStringSetOps::Find(const GZString &text)
{
    return mSet.find(text);
}

// ---- AnyStringSet ----

// The set is built from source's strings in source's list order (the range constructor: default buckets, then one
// insert per string), not copied bucket for bucket.
AnyStringSet *AnyStringSet::Construct(const AnyStringSet &source)
{
    mVTable = GameAddress(gzany::kStringSetVTable);
    new (&mSet) GZStringSet(source.mSet.begin(), source.mSet.end());
    return this;
}

void AnyStringSet::Destruct()
{
    mSet.~GZStringSet();
    mVTable = gzany::AnyVTable();
}

bool AnyStringSet::Contains(const GZString &text) const
{
    return const_cast<GZStringSetOps *>(reinterpret_cast<const GZStringSetOps *>(&mSet))->Find(text) != mSet.end();
}

void *AnyStringSet::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool AnyStringSet::Assign(const void *source)
{
    const uint32_t sourceType = TypeOf(source);
    if (sourceType == TypeOf(this)) {
        mSet = static_cast<const AnyStringSet *>(source)->mSet;
        return true;
    }
    return false;
}

// Not an ordering: 1 when every string of each set is in the other, 0 otherwise or when other is null. other is
// taken to be an AnyStringSet without looking at its type.
int32_t AnyStringSet::Compare(const void *other) const
{
    auto *set = static_cast<const AnyStringSet *>(other);
    if (set) {
        for (auto it = mSet.begin(); it != mSet.end(); ++it)
            if (!set->Contains(*it)) return 0;
        for (auto it = set->mSet.begin(); it != set->mSet.end(); ++it)
            if (!Contains(*it)) return 0;
        return 1;
    }
    return 0;
}

AnyStringSet *AnyStringSet::Clone() const
{
    auto *copy = static_cast<AnyStringSet *>(::operator new(sizeof(AnyStringSet)));
    if (!copy) return nullptr;
    try {
        return copy->Construct(*this);
    } catch (...) { // the original's frame frees the block when the copy throws
        ::operator delete(copy, sizeof(AnyStringSet));
        throw;
    }
}

uint32_t AnyStringSet::GetType() const
{
    return gzany::kTypeStringSet;
}

const char *AnyStringSet::GetTypeName() const
{
    return GameAddress<const char>(gzany::kStringSetVTable + 0x18);
}
