#include "rz/StaticGetters2.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/TokenExpanderInline.h"

using namespace ifaces;

// The stages of the string-token expansion that name families, sims, neighborhoods and lots (see TokenExpander.h).

namespace tokens {

namespace {

// ORIGINAL-BUG: $TokenNameLocal can never finish. After asking the object for its 0x5a69d3d5 interface it reads the dword
// at address 0 (the compiler dereferenced a pointer that was never loaded) and the access violation ends the program.
// Reproduced as the same read (after the lookup-list call that precedes it).
[[noreturn]] void ReadAddressZero()
{
    static volatile uint32_t *const nothing = nullptr;
    const uint32_t ignored = *nothing;
    (void)ignored;
    std::terminate();
}

} // namespace

// ---- the tokens a person or a family holds ----------------------------------------------------------------------------

// "$TokenPersonalCountedLocalName:a:b" and its seven siblings: local a names a person (or a family), local b the index
// of one of its tokens; the token's definition gives the name or the description. The sibling markers differ in whom
// they ask (the person's token set, or the family's) and in which of the token set's two accessors they use. The
// family markers use the accessors the other way round than the person ones (Counted with the slot the person's
// Singular uses): probably a swap in the original, reproduced.
void StringTokenExpander::ExpandTokenName(const char *marker, bool personal, bool viaCountedSlot)
{
    ExpandTokenText(marker, personal, viaCountedSlot, false);
}

void StringTokenExpander::ExpandTokenDescription(const char *marker, bool personal, bool viaCountedSlot)
{
    ExpandTokenText(marker, personal, viaCountedSlot, true);
}

void StringTokenExpander::ExpandTokenText(const char *marker, bool personal, bool viaCountedSlot, bool description)
{
    ForEachMarker(marker, true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        if (personal && !FindPerson(LocalInt16(at.mFirst))) return;
        GscLookupList *lookup = As<GscLookupList>(LookupList());
        const uint32_t ownerId = LocalWord(at.mFirst);
        TokenSet *tokenSet = personal ? lookup->FindPersonTokenSet(ownerId) : lookup->FindFamilyTokenSet(ownerId);
        if (!tokenSet) return;
        // The token set fills the scratch cell with a pointer to the reference holder of the token and returns the cell.
        void *scratchCell = nullptr;
        const uint32_t index = LocalWord(at.mSecond);
        void **cell = static_cast<void **>(viaCountedSlot ? tokenSet->GetCountedToken(&scratchCell, index) : tokenSet->GetSingularToken(&scratchCell, index));
        HeldReference token;
        token.AddRefAndHold(static_cast<RZAutoRef *>(*cell)->mObject);
        // ORIGINAL-BUG: a token set that holds no token there (a null token) crashes the next call.
        void *record = mDefinitions->FindRecord(As<TokenInstance>(token.Get())->GetDefinitionId());
        if (!record) return;
        ScopedText text;
        if (description) {
            As<TokenDefinition>(record)->GetDescriptionInto(text.Interface());
        } else {
            As<TokenDefinition>(record)->GetNameInto(text.Interface());
        }
        Replace(at, text.CString());
    });
}

// ---- families, sims, neighborhoods ----------------------------------------------------------------------------------------

// "$FamilyAssets:n": the funds of the family entry whose id local n holds, as the language writes numbers.
void StringTokenExpander::ExpandFamilyAssets()
{
    ForEachMarker("$FamilyAssets", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        TokenFamilyEntry *family = As<TokenFamilyEntry>(FindEntry(LocalInt16(at.mFirst)));
        if (!family) return;
        ScopedText text;
        FormatNumber([&] { return family->GetFunds(); }, text.Get());
        Replace(at, text.CString());
    });
}

// "$Family": the name of the family of the stack object's person; also reports the family number through the caller's
// out pair (entry 1).
void StringTokenExpander::ExpandFamily()
{
    ForEachMarker("$Family", false, [&](const Occurrence &at) {
        TokenPerson *person = As<TokenPerson>(FindPerson(mStackObjectId));
        if (!person) return;
        TokenFamilyEntry *family = As<TokenFamilyEntry>(FindEntry(person->GetProperty(kPersonPropertyFamily)));
        if (!family) return;
        Replace(at, family->GetNameString()->ToChar());
        if (mOutPair) mOutPair[1] = family->GetFamilyNumber();
    });
}

// "$LastName": the family name of the stack object's person.
void StringTokenExpander::ExpandLastName()
{
    ForEachMarker("$LastName", false, [&](const Occurrence &at) {
        TokenPerson *person = As<TokenPerson>(FindPerson(mStackObjectId));
        if (!person) return;
        TokenPersonInfo *info = person->GetPersonInfo();
        if (!info) return;
        ScopedText text;
        info->GetFamilyNameSource()->GetNameInto(text.Interface());
        Replace(at, text.CString());
    });
}

// "$NeighborLocal:n": the first name of the person whose id local n holds.
void StringTokenExpander::ExpandNeighborLocal()
{
    ForEachMarker("$NeighborLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        TokenPerson *person = As<TokenPerson>(FindPerson(LocalInt16(at.mFirst)));
        if (!person) return;
        TokenPersonInfo *info = person->GetPersonInfo();
        if (!info) return;
        ScopedText text;
        info->GetFirstNameInto(text.Interface());
        Replace(at, text.CString());
    });
}

// "$NeighborhoodLocal:n": the name of the neighborhood whose id local n holds.
void StringTokenExpander::ExpandNeighborhoodLocal()
{
    ForEachMarker("$NeighborhoodLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        TokenNeighborhood *neighborhood = mNeighborhoods->FindNeighborhood(LocalInt16(at.mFirst));
        if (!neighborhood) return;
        Replace(at, neighborhood->GetNameString()->ToChar());
    });
}

// "$PrimaryNeighborhood": the name of the neighborhood the game is in (the cached service 23 knows its key).
void StringTokenExpander::ExpandPrimaryNeighborhood()
{
    ForEachMarker("$PrimaryNeighborhood", false, [&](const Occurrence &at) {
        TokenCurrentHood *current = As<TokenService23>(GetCachedSlot23())->GetCurrentNeighborhood();
        if (!current) return;
        const uint32_t key = current->GetKeyHolder()->GetKey();
        TokenNeighborhood *neighborhood = mNeighborhoods->FindNeighborhoodByKey(key);
        if (!neighborhood) return;
        Replace(at, neighborhood->GetNameString()->ToChar());
    });
}

// "$Neighbor": the first name of the stack object's person. Reports the person info and the id through the caller's
// out parameters.
void StringTokenExpander::ExpandNeighbor()
{
    ForEachMarker("$Neighbor", false, [&](const Occurrence &at) {
        TokenPerson *person = As<TokenPerson>(FindPerson(mStackObjectId));
        if (!person) return;
        TokenPersonInfo *info = person->GetPersonInfo();
        if (!info) return;
        if (mOutPerson) *mOutPerson = info;
        ScopedText text;
        info->GetFirstNameInto(text.Interface());
        Replace(at, text.CString());
        if (mOutPair) mOutPair[0] = mStackObjectId;
    });
}

// "$Me": the name of this object's record.
void StringTokenExpander::ExpandMe()
{
    ForEachMarker("$Me", false, [&](const Occurrence &at) {
        TokenObjectInfo *info = As<cTSObject>(mObject)->GetObjectInfo();
        if (!info) return;
        ScopedText text;
        info->GetNameInto(text.Interface());
        ReplaceAll(at, text.String());
    });
}

// "$Object": the display name of the stack object. Reports the object's linked info through the caller's out pointer.
void StringTokenExpander::ExpandObject()
{
    ForEachMarker("$Object", false, [&](const Occurrence &at) {
        cTSObject *object = FindObject(mStackObjectId);
        if (!object) return;
        ScopedText text;
        object->GetNameInto(text.Interface(), 1);
        ReplaceAll(at, text.String());
        if (!mOutPerson) return;
        TokenObjectInfo *info = object->GetObjectInfo();
        if (!info) return;
        if (info->GetLinkedInfo() == nullptr) {
            *mOutPerson = info; // no link: the info itself
        } else {
            TokenObjectInfo *linked = info->GetLinkedInfo();
            if (linked) *mOutPerson = linked;
        }
    });
}

// ---- full names ----------------------------------------------------------------------------------------------------------------

// The display name of a sim: the language puts first and last name together; without one it is "first last" (or
// whichever of the two exists).
void StringTokenExpander::ComposeFullName(void *personInfo, cRZString &full)
{
    TokenPersonInfo *info = As<TokenPersonInfo>(personInfo);
    ScopedText first;
    ScopedText last;
    info->GetFirstNameInto(first.Interface());
    info->GetFamilyNameSource()->GetNameInto(last.Interface());
    void *service = GetSecondLanguageService();
    TokenLanguage *language = service ? As<TokenLanguageService>(service)->GetLanguage(0) : nullptr;
    if (language) {
        language->FormatFullName(first.Interface(), last.Interface(), &full);
    } else if (last.String().empty() || first.String().empty()) {
        // One of them is missing: the other (the last name when both are missing is the empty first name).
        const std::string &only = last.String().empty() ? first.String() : last.String();
        full.mString.assign(only);
    } else {
        full.mString.assign(first.String() + " " + last.String());
    }
}

// "$FullNameLocal:n": the full name of the person whose id local n holds, else of the stack object's person.
void StringTokenExpander::ExpandFullNameLocal()
{
    ForEachMarker("$FullNameLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        TokenPerson *person = As<TokenPerson>(FindPerson(LocalInt16(at.mFirst)));
        if (!person) person = As<TokenPerson>(FindPerson(mStackObjectId));
        if (!person) return;
        TokenPersonInfo *info = person->GetPersonInfo();
        if (!info) return;
        ScopedText full;
        ComposeFullName(info, full.Get());
        Replace(at, full.CString());
    });
}

// "$FullName": the full name of the stack object's person; reports the person info and id like $Neighbor.
void StringTokenExpander::ExpandFullName()
{
    ForEachMarker("$FullName", false, [&](const Occurrence &at) {
        TokenPerson *person = As<TokenPerson>(FindPerson(mStackObjectId));
        if (!person) return;
        TokenPersonInfo *info = person->GetPersonInfo();
        if (!info) return;
        if (mOutPerson) *mOutPerson = info;
        ScopedText full;
        ComposeFullName(info, full.Get());
        Replace(at, full.CString());
        if (mOutPair) mOutPair[0] = mStackObjectId;
    });
}

// "$TokenNameLocal:n": broken in the original (see ReadAddressZero). It asks this object for the 0x5a69d3d5 interface
// and, if it got one, throws error 0x1c (the test is the wrong way round); if it got none it asks the context for its
// lookup list and reads address 0.
void StringTokenExpander::ExpandTokenNameLocal()
{
    ForEachMarker("$TokenNameLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        HeldReference unused;
        As<cTSObject>(mObject)->QueryInterface(kQueryUiId, unused.Out());
        if (unused.Get() != nullptr) throw kErrorBadInterface; // the held reference is released while the error passes
        LookupList();
        ReadAddressZero();
    });
}

// ---- lots ---------------------------------------------------------------------------------------------------------------------------

// "$LotNameLocalByLotId:n": the name of the lot whose id local n holds.
void StringTokenExpander::ExpandLotNameLocalByLotId()
{
    ForEachMarker("$LotNameLocalByLotId", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        const int32_t lotId = LocalInt16(at.mFirst);
        TokenLotList *lots = As<GscLookupList>(LookupList())->GetLotList();
        TokenLot *lot = lots->FindLot(lotId);
        if (!lot) return;
        Replace(at, lot->GetNameString()->ToChar());
    });
}

// "$LotNameLocal:n": the name of the lot the family of the person (whose id local n holds) lives on.
void StringTokenExpander::ExpandLotNameLocal()
{
    ForEachMarker("$LotNameLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        TokenPerson *person = As<TokenPerson>(FindPerson(LocalInt16(at.mFirst)));
        if (!person) return;
        const int32_t entryId = person->GetProperty(kPersonPropertyFamily);
        // The context is asked for its entry table afresh here, not the one the prologue kept.
        TokenFamilyEntry *entry = As<TokenFamilyEntry>(As<GscContext>(mObject->mContext)->GetEntryTable()->FindEntry(entryId));
        if (!entry) return;
        const uint32_t lotHandle = entry->GetLotHandle();
        TokenLotList *lots = As<GscLookupList>(LookupList())->GetLotList();
        TokenLot *lot = lots->FindLot(lotHandle);
        if (!lot) return;
        Replace(at, lot->GetNameString()->ToChar());
    });
}

} // namespace tokens
