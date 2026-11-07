#include "simantics/TokenExpander.h"

#include "game/vtable.h"
#include "ifaces/ifaces.h"
#include "rz/ServiceGetters.h"
#include "simantics/TokenExpanderInline.h"

using namespace ifaces;

// cTSObject::vf262 (0x535130): see TokenExpander.h. This file holds the entry point, the expander's skeleton and the
// shared lookups; the stages are in TokenExpanderPeople.cpp, TokenExpanderValues.cpp, TokenExpanderObjects.cpp and
// TokenExpanderStrings.cpp.

void TokenObject::ExpandStringTokens(cIGZString *text, int16_t stackObjectId, cTSTreeStackElem *frame, int16_t *outPair, void **outPerson)
{
    tokens::StringTokenExpander expander(this, text, stackObjectId, frame, outPair, outPerson);
    expander.Run();
}

cTSObject *TokenObject::FindObject(int16_t id) const
{
    TokenObjectTableOwner *const owner = mObjectTableOwner;
    if (id <= 0) return nullptr;
    const int32_t count = static_cast<int32_t>(reinterpret_cast<intptr_t>(owner->mLast) - reinterpret_cast<intptr_t>(owner->mFirst)) >> 2;
    if (static_cast<uint32_t>(id) > static_cast<uint32_t>(count)) return nullptr;
    void *const entry = owner->mFirst[id - 1];
    if (!entry) return nullptr;
    return As<cTSObject>(entry)->GetResolvedObject();
}

namespace tokens {

void *GetSecondLanguageService()
{
    return CachedServiceGetter<kSecondLanguageCacheRva, appinit::kLanguageServiceFirstId, appinit::kLanguageServiceSecondId>();
}

// ---- the pieces ---------------------------------------------------------------------------------------------------

StringTableText::~StringTableText() noexcept
{
    if (mAlive) Text().Destruct();
}

void StringTableText::ConstructEmpty()
{
    mLoaded.ConstructEmpty();
    mAlive = true;
}

void StringTableText::Load(uint32_t table, uint32_t entry, uint32_t group)
{
    mLoaded.Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, kStringTextType);
    mAlive = true;
}

void StringTableText::Assign(const StringTableText &other)
{
    mLoaded.Assign(&other.mLoaded);
}

void HeldReference::AddRefAndHold(void *object)
{
    // The original stores the pointer first, but its cleanup only knows the holder once the AddRef returned: an error
    // thrown by the AddRef leaves the reference unreleased. Holding after the AddRef is the same.
    if (object) As<TokenInstance>(object)->AddRef();
    mRef.mObject = object;
}

// ---- the expander -----------------------------------------------------------------------------------------------------

StringTokenExpander::StringTokenExpander(TokenObject *object, cIGZString *text, int16_t stackObjectId, cTSTreeStackElem *frame, int16_t *outPair,
                                         void **outPerson)
    : mObject(object), mCallerText(text), mFrame(frame), mOutPair(outPair), mOutPerson(outPerson)
{
    // The four tables of the context, fetched in this order, and the working copy of the text.
    mEntries = As<GscContext>(object->mContext)->GetEntryTable();
    mPeople = As<GscContext>(object->mContext)->GetPeopleTable();
    mNeighborhoods = As<GscContext>(object->mContext)->GetNeighborhoodTable();
    mDefinitions = As<GscContext>(object->mContext)->GetObjectDefTable();
    mWork.Construct(*text);
    // 0 means the frame's own stack object.
    if (stackObjectId == 0 && frame) {
        mStackObjectId = static_cast<int16_t>(frame->GetStackObjectId());
    } else {
        mStackObjectId = stackObjectId;
    }
}

StringTokenExpander::~StringTokenExpander() noexcept
{
    mWork.Destruct();
}

void StringTokenExpander::Run()
{
    // The names and descriptions of the tokens a person or a family holds (counted: the plural form).
    ExpandTokenName("$TokenPersonalCountedLocalName", true, true);
    ExpandTokenName("$TokenPersonalSingularLocalName", true, false);
    ExpandTokenName("$TokenFamilyCountedLocalName", false, false);
    ExpandTokenName("$TokenFamilySingularLocalName", false, true);
    ExpandTokenDescription("$TokenPersonalCountedLocalDesc", true, true);
    ExpandTokenDescription("$TokenPersonalSingularLocalDesc", true, false);
    ExpandTokenDescription("$TokenFamilyCountedLocalDesc", false, false);
    ExpandTokenDescription("$TokenFamilySingularLocalDesc", false, true);
    // Families, sims and neighborhoods.
    ExpandFamilyAssets();
    ExpandFamily();
    ExpandLastName();
    ExpandNeighborLocal();
    ExpandNeighborhoodLocal();
    ExpandPrimaryNeighborhood();
    ExpandNeighbor();
    ExpandMe();
    ExpandObject();
    // Numbers and times from the frame's locals and the object's words.
    ExpandLocal();
    ExpandMoney();
    ExpandCash();
    ExpandTimeLocal();
    ExpandAttrib();
    // Names of objects and sims.
    ExpandNameAttrib();
    ExpandNameLocal();
    ExpandNameParam();
    ExpandFullNameLocal();
    ExpandFullName();
    ExpandTokenNameLocal();
    ExpandLotNameLocalByLotId();
    ExpandLotNameLocal();
    ExpandCatalogLocal();
    // Texts from the string tables.
    ExpandDayLocal();
    ExpandDayFlags();
    ExpandJobTitle();
    ExpandPrivateString();
    ExpandBehaviorString();
    ExpandAspirationString();
    ExpandSemiGlobalString();
    ExpandGlobalString();
    ExpandSkillName();
    ExpandWordPoints();
    ExpandWantFear("$WantDescriptionLocal", false, false);
    ExpandWantFear("$FearDescriptionLocal", true, false);
    ExpandWantFear("$WantHelpTextLocal", false, true);
    ExpandWantFear("$FearHelpTextLocal", true, true);
    ExpandGroupName();
    ExpandPropertyString("$PropertyPrivateString", 0);
    ExpandPropertyString("$PropertySemiGlobalString", 1);
    ExpandPropertyString("$PropertyGlobalString", 2);
    ExpandGuidCatalogNameLocal();
    // The caller's text becomes the expanded copy.
    mCallerText->Copy(*mWork.AsInterface());
}

// ---- replacing ------------------------------------------------------------------------------------------------------

void StringTokenExpander::Replace(const Occurrence &at, const char *text)
{
    mWork.mString.replace(at.mPosition, at.mLength, text, std::strlen(text));
}

void StringTokenExpander::ReplaceAll(const Occurrence &at, const std::string &text)
{
    mWork.mString.replace(at.mPosition, at.mLength, text.data(), text.size());
}

// ---- the frame ----------------------------------------------------------------------------------------------------------

bool StringTokenExpander::HasLocal(int32_t index, int32_t extra) const
{
    if (!mFrame || index < 0) return false;
    return index + extra < static_cast<int32_t>(mFrame->GetLocalCount());
}

bool StringTokenExpander::HasArgument(int32_t index) const
{
    if (!mFrame || index < 0) return false;
    return index < static_cast<int32_t>(mFrame->GetArgCount());
}

uint16_t StringTokenExpander::LocalWord(int32_t index) const
{
    return static_cast<uint16_t>(mFrame->GetLocal(index));
}

// ---- lookups --------------------------------------------------------------------------------------------------------------

void *StringTokenExpander::FindPerson(int32_t id) const
{
    return mPeople->FindPerson(id, 1);
}

void *StringTokenExpander::FindEntry(int32_t id) const
{
    return mEntries->FindEntry(id);
}

void *StringTokenExpander::LookupList() const
{
    return As<GscContext>(mObject->mContext)->GetLookupList();
}

} // namespace tokens
