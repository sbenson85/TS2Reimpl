// cIGZAny's own destructor, AnyUint32 and AnyBoolean (0x14xxxx-0x179xxx), and the string set's members from the
// want tree manager's translation unit (0x551xxx-0x557xxx): code built for speed. See gz/cGZAny.h.
#include "gz/cGZAny.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZAny.h"

#include <new>

void *cGZAny::DeletingDestructor(uint32_t flags)
{
    mVTable = gzany::AnyVTable();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

void *cGZAny::ValueDeletingDestructor(uint32_t flags)
{
    mVTable = gzany::AnyVTable();
    if (flags & 1) ::operator delete(this, 8);
    return this;
}

namespace gz {

// ---- AnyUint32 ----

bool AnyUint32::Assign(const cIGZAny &source)
{
    const uint32_t sourceType = source.GetType();
    if (sourceType == GetType()) {
        mValue = static_cast<const AnyUint32 &>(source).mValue;
        return true;
    }
    return false;
}

int32_t AnyUint32::Compare(const cIGZAny &other) const
{
    const uint32_t mine = mValue;
    const uint32_t value = static_cast<const AnyUint32 &>(other).mValue;
    if (value > mine) return -1;
    return value < mine ? 1 : 0;
}

// operator new, then the copy constructed only when the block is not null (the compiler's test after operator new,
// /Zc:throwingNew-), as the original.
cIGZAny *AnyUint32::Clone() const
{
    return new AnyUint32(*this);
}

uint32_t AnyUint32::GetType() const
{
    return gzany::kTypeUint32;
}

const char *AnyUint32::GetTypeName() const
{
    return "AnyUint32";
}

// ---- AnyBoolean ----

bool AnyBoolean::Assign(const cIGZAny &source)
{
    const uint32_t sourceType = source.GetType();
    if (sourceType == GetType()) {
        mValue = static_cast<const AnyBoolean &>(source).mValue;
        return true;
    }
    return false;
}

int32_t AnyBoolean::Compare(const cIGZAny &other) const
{
    const uint8_t mine = mValue;
    const uint8_t value = static_cast<const AnyBoolean &>(other).mValue;
    if (value > mine) return -1;
    return value < mine ? 1 : 0;
}

cIGZAny *AnyBoolean::Clone() const
{
    return new AnyBoolean(*this);
}

uint32_t AnyBoolean::GetType() const
{
    return gzany::kTypeBoolean;
}

const char *AnyBoolean::GetTypeName() const
{
    return "AnyBoolean";
}

template struct AnyAdapters<AnyUint32>;
template struct AnyAdapters<AnyBoolean>;

} // namespace gz

// ---- the string set's members from the want tree manager's unit ----

GZStringSetOps *GZStringSetOps::Construct()
{
    new (&mSet) GZStringSet();
    return this;
}

void GZStringSetOps::Destruct()
{
    mSet.~GZStringSet();
}

std::pair<GZStringSet::iterator, bool> GZStringSetOps::Insert(const GZString &text)
{
    return mSet.insert(text);
}
