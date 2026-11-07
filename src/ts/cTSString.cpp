#include "ts/cTSString.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "rz/RZAutoRef.h"

#include <new>

namespace {

// Strings the loader searches and replaces with (their text is UTF-8).
constexpr char kBulletUtf8[] = "\xe2\x80\xa2";        // U+2022 BULLET
constexpr char kZeroWidthSpaceUtf8[] = "\xe2\x80\x8b"; // U+200B ZERO WIDTH SPACE

// A string that lives until the process ends (the original builds these on first use and destroys them at exit).
struct StaticText {
    cRZString mText;
    explicit StaticText(const char *utf8) { mText.Construct(utf8); }
    StaticText(const StaticText &) = delete;
    StaticText &operator=(const StaticText &) = delete;
    ~StaticText() noexcept { mText.Destruct(); }
};

// Owns the reference a resource lookup hands out: released when the scope ends, also while an error unwinds. As in the original, an
// error from Release itself ends the process.
struct HeldResource {
    RZAutoRef mRef = {nullptr};
    HeldResource() = default;
    HeldResource(const HeldResource &) = delete;
    HeldResource &operator=(const HeldResource &) = delete;
    ~HeldResource() noexcept { mRef.ReleaseUnguarded(); }
};

} // namespace

cTSString *cTSString::ConstructEmpty()
{
    // (A default-constructed std::string clears its whole inline buffer, as the original does.)
    new (&mString.mString) std::string();
    mString.mRefCount = 0;
    mString.mVTable = GameAddress(kVTable);
    mResourceType = 0;
    mGroup = 0;
    mEntry = 0;
    mTable = 0;
    mKeyWord3 = 0;
    mLoaded = 0;
    mLoadFlags = 0;
    return this;
}

cTSString *cTSString::Construct(uint16_t table, uint16_t entry, uint32_t group, uint8_t loadFlags, uint32_t resourceType)
{
    new (&mString.mString) std::string();
    mString.mRefCount = 0;
    mString.mVTable = GameAddress(kVTable);
    mResourceType = resourceType;
    mGroup = group;
    mEntry = entry;
    mTable = table;
    mKeyWord3 = 0;
    mLoaded = 0;
    mLoadFlags = static_cast<int8_t>(loadFlags);
    try {
        Load();
    } catch (...) {
        mString.Destruct(); // (the original unwinds into the string's destructor)
        throw;
    }
    return this;
}

cTSString *cTSString::Construct(uint16_t table, uint16_t entry, uint32_t group, uint32_t resourceType)
{
    new (&mString.mString) std::string();
    mString.mRefCount = 0;
    mString.mVTable = GameAddress(kVTable);
    mResourceType = resourceType;
    mGroup = group;
    mEntry = entry;
    mTable = table;
    mKeyWord3 = 0;
    mLoaded = 0;
    mLoadFlags = 0;
    try {
        Load();
    } catch (...) {
        mString.Destruct();
        throw;
    }
    return this;
}

bool cTSString::Load()
{
    bool found = false;
    void *const manager = tsstring::GetResourceManager();
    if (manager != nullptr) {
        {
            HeldResource held;
            found = (As<ifaces::cIGZPersistResourceManager>(manager)->GetResource(ResourceKey(), cRZString::kGZIID_cIGZString, &held.mRef.mObject,
                                                                                 mLoadFlags, 0) & 0xff) != 0;
            if (found) {
                mLoaded = 1;
                const cIGZString *const resourceText = static_cast<const cIGZString *>(held.mRef.mObject);
                // (The length is asked before the data, as in the original.)
                const uint32_t length = resourceText->Strlen();
                const char *const data = resourceText->Data();
                mString.mString.assign(data, length);

                // The string tables mark list items with a bullet; the game shows a zero width space there instead.
                static StaticText bullet(kBulletUtf8);
                static StaticText zeroWidthSpace(kZeroWidthSpaceUtf8);
                int32_t position = mString.Find(*bullet.mText.AsInterface(), 0, true);
                while (position != -1) {
                    mString.Replace(static_cast<uint32_t>(position), *zeroWidthSpace.mText.AsInterface());
                    position = mString.Find(*bullet.mText.AsInterface(), static_cast<uint32_t>(position), true);
                }
            }
        } // the resource is released here, before a failed load clears the text
        if (found) return true;
    }
    mString.mString.clear();
    return false;
}

cTSString *cTSString::Assign(const cTSString *other)
{
    if (other != this) {
        mString.mString.assign(other->mString.mString.data(), other->mString.mString.size());
        mResourceType = other->mResourceType;
        mGroup = other->mGroup;
        mEntry = other->mEntry;
        mTable = other->mTable;
        mKeyWord3 = other->mKeyWord3;
        mLoaded = other->mLoaded;
        mLoadFlags = other->mLoadFlags;
    }
    return this;
}

cRZString *cTSString::AssignString(const cIGZString &other)
{
    return mString.Assign(other);
}

cTSString *cTSString::DeletingDestructor(uint32_t flags)
{
    mString.Destruct();
    if (flags & 1) operator delete(this, sizeof(cTSString));
    return this;
}
