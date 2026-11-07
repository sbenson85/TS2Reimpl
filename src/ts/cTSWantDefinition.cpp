// cTSWantDefinition (lane IL): see the header. Property names and values are the image's strings, read through their
// pointer cells at run time; the literals the original passes by address are read at their addresses.
#include "ts/cTSWantDefinition.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "rz/ServiceGetters.h"
#include "ts/cTSString.h"

#include <cstddef>
#include <new>

static_assert(offsetof(cTSWantDefinition, mRefCount) == 0x08, "mRefCount");
static_assert(offsetof(cTSWantDefinition, mKey) == 0x0c, "mKey");
static_assert(offsetof(cTSWantDefinition, mId) == 0x1c, "mId");
static_assert(offsetof(cTSWantDefinition, mIsFear) == 0x21, "mIsFear");
static_assert(offsetof(cTSWantDefinition, mLevel) == 0x24, "mLevel");
static_assert(offsetof(cTSWantDefinition, mInfluence) == 0x2c, "mInfluence");
static_assert(offsetof(cTSWantDefinition, mPrimaryIcon) == 0x30, "mPrimaryIcon");
static_assert(offsetof(cTSWantDefinition, mSecondaryIcon) == 0x40, "mSecondaryIcon");
static_assert(offsetof(cTSWantDefinition, mStrings) == 0x50, "mStrings");
static_assert(offsetof(cTSWantDefinition, mCheckTree) == 0xf0, "mCheckTree");
static_assert(offsetof(cTSWantDefinition, mFeedbackTree) == 0x110, "mFeedbackTree");
static_assert(offsetof(cTSWantDefinition, mSimArrayTree) == 0x130, "mSimArrayTree");
static_assert(offsetof(cTSWantDefinition, mGameVersionFlags) == 0x150, "mGameVersionFlags");
static_assert(offsetof(cTSWantDefinition, mIntegerMultiplier) == 0x15c, "mIntegerMultiplier");
static_assert(offsetof(cTSWantDefinition, mNodeText) == 0x160, "mNodeText");
static_assert(offsetof(cTSWantDefinition, mCategory) == 0x180, "mCategory");

namespace {

// Pointer cells of the image's strings: property names, then values.
enum Cell : uint32_t {
    kId = 0x1017278, kScore = 0x101727c, kInfluence = 0x1017280, kIsFear = 0x1017284, kLevel = 0x1017288,
    kName = 0x101728c, kNodeText = 0x1017290, kCategory = 0x1017294, kFolder = 0x1017298, kPrimaryIcon = 0x101729c,
    kSecondaryIcon = 0x10172a0, kStringSet = 0x10172a4, kCheckTree = 0x10172a8, kFeedbackTree = 0x10172ac,
    kSimArrayTree = 0x10172b0, kEventRequiresSim = 0x10172b4, kEventRequiresObject = 0x10172b8, kObjectType = 0x10172bc,
    kIntegerOperator = 0x10172c0, kIntegerMultiplier = 0x10172c4, kGameVersionFlags = 0x10172c8,
    kMemorable = 0x10172cc, kPower = 0x10172d0,
    kDefaultFeedbackTree = 0x1011cd4,
};
// Literals the original passes by address.
constexpr uint32_t kUnnamedFormat = 0xdd8604;      // "<unnamed want 0x%08x>"
constexpr uint32_t kMissingStringFormat = 0xdd85e4; // "#### missing string for <%s>"
constexpr uint32_t kDefaultCategory = 0xdd85d4;    // 13 characters
constexpr uint32_t kDefaultCategoryLength = 0xd;
constexpr uint32_t kCloseAngle = 0xdcb1c0;         // ">"
constexpr uint32_t kWantsGroupName = 0xdcb4e8;     // the persist system's group name of the want string tables

constexpr uint32_t kIconType = 0x856ddbac; // the icon keys' type and group
constexpr uint32_t kIconGroup = 0x499db772;

// The value names of objectType and integerOperator, in the order tested.
struct Mapping {
    uint32_t cell;
    uint32_t value;
};
constexpr Mapping kObjectTypes[] = {
    {0x10172d4, 1}, {0x10172d8, 3}, {0x10172dc, 2}, {0x10172e0, 5}, {0x10172e4, 4}, {0x10172e8, 6},
};
constexpr Mapping kOperators[] = {
    {0x10172ec, 1}, {0x10172f0, 2}, {0x10172f4, 3}, {0x10172f8, 4}, {0x10172fc, 5},
};

const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }

// The persist system (cached getter 0xb390c0): slot 10 gives a group id by name.
void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

// cIGZString's virtual slots the original calls (through the string's own vtable).
bool IsEqual(cIGZString *s, const char *text)
{
    return VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, bool), 9>(s, text, 0xffffffffu, true);
}
void AssignFrom(cRZString *target, cIGZString *source)
{
    VirtualCall<cRZString *(__thiscall *)(void *, cIGZString *), 16>(target, source);
}

// The value locals of ReadProperties / WriteProperties: AnySint32 and AnyString are the game's layouts (the game's vtables:
// no class of ours yet); AnyUint32 and AnyBoolean are ours.
AnySint32 Sint32(int32_t value) { return {GameAddress<void>(gzany::kSint32VTable), value}; }

bool Get(cIGZPropertySetString *set, uint32_t cell, void *value)
{
    const GZServiceString key(Text(cell));
    return set->GetPropertyValue(&key, value);
}
void Put(cIGZPropertySetString *set, uint32_t cell, void *value)
{
    const GZServiceString key(Text(cell));
    set->SetProperty(&key, value);
}

// The first mapping whose name the string equals (case-sensitive), or none.
template <size_t N> bool Match(cIGZString *s, const Mapping (&map)[N], uint32_t &out)
{
    for (const Mapping &m : map)
        if (IsEqual(s, Text(m.cell))) {
            out = m.value;
            return true;
        }
    return false;
}

} // namespace

cTSWantDefinition::cTSWantDefinition()
    : mPrimaryIcon{kIconType, kIconGroup, 0, 0}, mSecondaryIcon{kIconType, kIconGroup, 0, 0}
{
    for (cRZString &s : mStrings) s.Construct();
    mCheckTree.Construct();
    mFeedbackTree.Construct();
    mSimArrayTree.Construct();
    mNodeText.Construct();
    mCategory.Construct();
}

cTSWantDefinition::~cTSWantDefinition()
{
    mCategory.Destruct();
    mNodeText.Destruct();
    mSimArrayTree.Destruct();
    mFeedbackTree.Destruct();
    mCheckTree.Destruct();
    for (uint32_t i = kStringCount; i-- > 0;) mStrings[i].Destruct();
}

bool cTSWantDefinition::QueryInterface(uint32_t iid, void **out)
{
    if (iid == 1 || iid == kIid) {
        *out = static_cast<cITSWantDefinition *>(this);
    } else if (iid == kPersistResourceIid) {
        *out = static_cast<cIGZPersistResource *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cTSWantDefinition::AddRef() { return uint32_t(++mRefCount); }

uint32_t cTSWantDefinition::Release()
{
    if (mRefCount > 1) return uint32_t(--mRefCount);
    delete this;
    return 0;
}

uint32_t cTSWantDefinition::GetId() { return mId; }

cRZString *cTSWantDefinition::GetName()
{
    if (mNodeText.mString.empty()) mNodeText.Sprintf(GameAddress<const char>(kUnnamedFormat), mId);
    return &mNodeText;
}

cRZString *cTSWantDefinition::GetCategory() { return &mCategory; }
int32_t cTSWantDefinition::GetScore() { return mScore; }
int32_t cTSWantDefinition::GetInfluence() { return mInfluence; }
uint8_t cTSWantDefinition::IsFear() { return mIsFear; }
uint32_t cTSWantDefinition::GetLevel() { return mLevel; }
cGZPersistResourceKey *cTSWantDefinition::GetPrimaryIcon() { return &mPrimaryIcon; }
cGZPersistResourceKey *cTSWantDefinition::GetSecondaryIcon() { return &mSecondaryIcon; }

void cTSWantDefinition::GetString(uint32_t index, cIGZString *out)
{
    cRZString &s = mStrings[index];
    if (!s.mString.empty()) {
        VirtualCall<void(__thiscall *)(void *, const cIGZString *), 17>(out, s.AsInterface());
        return;
    }
    // "<" + the name + ">"
    alignas(4) uint8_t bytes[sizeof(cRZString)];
    auto *const text = reinterpret_cast<cRZString *>(bytes);
    text->Construct();
    text->mString.assign(1, '<');
    const cRZString *const name = GetName();
    text->mString.append(name->mString.data(), name->mString.size());
    text->mString.append(GameAddress<const char>(kCloseAngle), 1);
    VirtualCall<void(__thiscall *)(void *, const cIGZString *), 17>(out, text->AsInterface());
    text->Destruct();
}

cRZString *cTSWantDefinition::GetCheckTree() { return &mCheckTree; }
cRZString *cTSWantDefinition::GetFeedbackTree() { return &mFeedbackTree; }
cRZString *cTSWantDefinition::GetSimArrayTree() { return &mSimArrayTree; }
uint32_t cTSWantDefinition::GetObjectType() { return mObjectType; }
uint32_t cTSWantDefinition::GetIntegerOperator() { return mIntegerOperator; }
int32_t cTSWantDefinition::GetIntegerMultiplier() { return mIntegerMultiplier; }
uint32_t cTSWantDefinition::GetGameVersionFlags() { return mGameVersionFlags; }

void cTSWantDefinition::WriteProperties(cIGZPropertySetString *set)
{
    {
        gz::AnyUint32 value(mId);
        Put(set, kId, &value);
    }
    {
        AnySint32 value = Sint32(mScore);
        Put(set, kScore, &value);
    }
    {
        AnySint32 value = Sint32(mInfluence);
        Put(set, kInfluence, &value);
    }
    {
        gz::AnyUint32 value(mIsFear);
        Put(set, kIsFear, &value);
    }
    // nodeText: a new string with the node text's characters (its slots 7 and 6), held by an AnyString.
    const uint32_t length = VirtualCall<uint32_t(__thiscall *)(void *), 7>(&mNodeText);
    const char *const characters = VirtualCall<const char *(__thiscall *)(void *), 6>(&mNodeText);
    auto *const copy = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    copy->Construct();
    copy->mString.assign(characters, length);
    AnyString value{GameAddress<void>(gzany::kStringVTable), nullptr};
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(copy);
    value.mString = copy->AsInterface();
    Put(set, kNodeText, &value);
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(value.mString);
}

void cTSWantDefinition::ReadProperties(cIGZPropertySetString *set)
{
    // One value local per type, reused from property to property (a failed lookup leaves it as it was).
    AnyString text{GameAddress<void>(gzany::kStringVTable), nullptr};
    gz::AnyUint32 number(0);
    AnySint32 signedNumber = Sint32(0);
    gz::AnyBoolean flag(0);

    if (Get(set, kId, &number)) mId = number.mValue;
    if (Get(set, kScore, &signedNumber)) mScore = signedNumber.mValue;
    if (Get(set, kInfluence, &signedNumber)) mInfluence = signedNumber.mValue;
    mIsFear = Get(set, kIsFear, &flag) ? flag.mValue : uint8_t(uint32_t(mScore) >> 31);
    if (Get(set, kLevel, &text)) {
        if (IsEqual(text.mString, Text(kPower))) mLevel = 2;
        else mLevel = IsEqual(text.mString, Text(kMemorable)) ? 1 : 0;
    }
    if (Get(set, kPrimaryIcon, &number)) mPrimaryIcon.instance = number.mValue;
    if (Get(set, kSecondaryIcon, &number)) mSecondaryIcon.instance = number.mValue;
    if (Get(set, kCheckTree, &text)) AssignFrom(&mCheckTree, text.mString);
    if (Get(set, kFeedbackTree, &text)) AssignFrom(&mFeedbackTree, text.mString);
    else mFeedbackTree.mString.assign(Text(kDefaultFeedbackTree));
    if (Get(set, kSimArrayTree, &text)) AssignFrom(&mSimArrayTree, text.mString);
    if (Get(set, kEventRequiresSim, &flag) && flag.mValue) mObjectType = 1;
    if (Get(set, kEventRequiresObject, &flag) && flag.mValue) mObjectType = 2;
    if (Get(set, kGameVersionFlags, &number)) mGameVersionFlags = number.mValue;
    if (Get(set, kObjectType, &text)) Match(text.mString, kObjectTypes, mObjectType);
    if (Get(set, kIntegerOperator, &text)) Match(text.mString, kOperators, mIntegerOperator);
    if (Get(set, kIntegerMultiplier, &signedNumber)) mIntegerMultiplier = signedNumber.mValue;
    if (Get(set, kNodeText, &text) || Get(set, kName, &text)) AssignFrom(&mNodeText, text.mString);
    if (Get(set, kCategory, &text) || Get(set, kFolder, &text)) AssignFrom(&mCategory, text.mString);
    else mCategory.mString.assign(GameAddress<const char>(kDefaultCategory), kDefaultCategoryLength);

    // stringSet: entries 1-4 of that string table (group "Wants") become mStrings[1..4]; a missing entry 1 says so, a
    // missing 2 or 3 repeats entry 1, a missing 4 stays empty.
    if (Get(set, kStringSet, &number) && number.mValue < 0x8000 && number.mValue != 0) {
        const uint16_t table = uint16_t(number.mValue);
        const uint32_t group = VirtualCall<uint32_t(__thiscall *)(void *, const char *), 10>(
            PersistSystem(), GameAddress<const char>(kWantsGroupName));
        for (uint16_t entry = 1; entry < kStringCount; ++entry) {
            alignas(4) uint8_t loaderBytes[sizeof(cTSString)];
            auto *const loader = reinterpret_cast<cTSString *>(loaderBytes);
            loader->Construct(table, entry, group, cTSString::kStringTableType);
            alignas(4) uint8_t bytes[sizeof(cRZString)];
            auto *const value = reinterpret_cast<cRZString *>(bytes);
            value->Construct();
            value->mString.assign(loader->mString.mString);
            if (value->mString.empty()) {
                if (entry == 1) value->Sprintf(GameAddress<const char>(kMissingStringFormat), GetName()->mString.c_str());
                else if (entry != 4) value->mString.assign(mStrings[1].mString);
            }
            mStrings[entry].mString.assign(value->mString);
            value->Destruct();
            loader->Destruct();
        }
    }
    if (text.mString) VirtualCall<uint32_t(__thiscall *)(void *), 2>(text.mString);
}

cGZPersistResourceKey cTSWantDefinition::GetKey() { return mKey; }

bool cTSWantDefinition::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

uint32_t cTSWantDefinition::GetType() { return mKey.type; }
uint32_t cTSWantDefinition::GetGroup() { return mKey.group; }
uint32_t cTSWantDefinition::GetInstance() { return mKey.instance; }

bool cTSWantDefinition::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

bool cTSWantDefinition::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

bool cTSWantDefinition::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

uint32_t cTSWantDefinition::GetRefCount() { return uint32_t(mRefCount); }

cTSWantDefinition *__fastcall cTSWantDefinitionAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSWantDefinition;
}
