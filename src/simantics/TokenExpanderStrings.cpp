#include "rz/Singleton_20ddfb.h"
#include "simantics/TokenExpanderInline.h"

using namespace ifaces;

// The stages of the string-token expansion that load texts from the string tables (see TokenExpander.h).

namespace tokens {

// ---- days -------------------------------------------------------------------------------------------------------------------

// "$DayLocal:n": the name of the weekday numbered local n + 1 in the day table.
void StringTokenExpander::ExpandDayLocal()
{
    ForEachMarker("$DayLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        StringTableText day;
        day.Load(kTableDays, static_cast<uint16_t>(LocalWord(at.mFirst) + 1), kGroupGeneral);
        Replace(at, day.CString());
    });
}

// "$DayFlags:n": the day names whose bit is set in local n, separated by ", ". Bit i takes entry i + 2 of the day
// table, except that bit 6 takes entry 1 (the week starts at entry 1 and bit 0 is the second day).
void StringTokenExpander::ExpandDayFlags()
{
    ForEachMarker("$DayFlags", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        const int32_t flags = LocalInt16(at.mFirst);
        ScopedText days;
        bool any = false;
        for (int32_t bit = 0; bit < 7; ++bit) {
            if ((flags & (1 << bit)) == 0) continue;
            const uint32_t entry = (bit + 2 == 8) ? 1 : bit + 2;
            StringTableText loaded;
            loaded.Load(kTableDays, entry, kGroupGeneral);
            ScopedText day;
            day.String().assign(loaded.Text().mString);
            if (any) {
                ScopedText separator;
                separator.String() = ", ";
                day.Get().Insert(0, *separator.Get().AsInterface());
            }
            days.String().append(day.String());
            any = true;
        }
        Replace(at, days.CString());
    });
}

// ---- texts from a table the markers name ------------------------------------------------------------------------------------

// "$JobTitle:e:o": entry (local e) + 1 of the job title table, in the string group of the object whose id local o
// holds.
void StringTokenExpander::ExpandJobTitle()
{
    ForEachMarker("$JobTitle", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        const uint16_t entry = LocalWord(at.mFirst);
        cTSObject *object = FindObject(LocalInt16(at.mSecond));
        if (!object) return;
        // ORIGINAL-BUG: an object without a record crashes the next call.
        const uint32_t group = object->GetObjectInfo()->GetStringGroup();
        StringTableText job;
        job.Load(kTableJobTitles, static_cast<uint16_t>(entry + 1), group);
        Replace(at, job.CString());
    });
}

// "$PrivateString:t:e": entry e of table t, in the stack object's private string group.
void StringTokenExpander::ExpandPrivateString()
{
    ForEachMarker("$PrivateString", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        const uint16_t entry = LocalWord(at.mSecond);
        const uint16_t table = LocalWord(at.mFirst);
        cTSObject *object = FindObject(mStackObjectId);
        if (!object) return;
        ScopedText text; // stays empty when the object has no record: the marker is replaced by nothing
        if (TokenObjectInfo *info = object->GetObjectInfo()) {
            // ORIGINAL-BUG: a record without a string owner crashes the next call.
            const uint32_t group = info->GetStringOwner()->GetPrivateGroup();
            StringTableText loaded;
            loaded.Load(table, entry, group);
            text.String().assign(loaded.Text().mString);
        }
        Replace(at, text.CString());
    });
}

// "$SemiGlobalString:t:e": entry e of table t, in the stack object's semi-global group.
void StringTokenExpander::ExpandSemiGlobalString()
{
    ForEachMarker("$SemiGlobalString", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        const uint16_t entry = LocalWord(at.mSecond);
        const uint16_t table = LocalWord(at.mFirst);
        cTSObject *object = FindObject(mStackObjectId);
        if (!object) return;
        TokenObjectInfo *info = object->GetObjectInfo();
        StringTableText text;
        text.ConstructEmpty();
        if (info && info->GetSemiGlobalOwner() != nullptr) {
            const uint32_t group = info->GetSemiGlobalOwner()->GetPrivateGroup();
            StringTableText loaded;
            loaded.Load(table, entry, group);
            text.Assign(loaded);
        }
        Replace(at, text.CString());
    });
}

// "$BehaviorString:t:e" and the other two with a fixed group: entry e of table t, locals t and e (the first read is the
// entry, then the table).
void StringTokenExpander::ExpandStringFromTable(const char *marker, uint32_t group)
{
    ForEachMarker(marker, true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        const uint16_t entry = LocalWord(at.mSecond);
        const uint16_t table = LocalWord(at.mFirst);
        StringTableText text;
        text.Load(table, entry, group);
        Replace(at, text.CString());
    });
}

void StringTokenExpander::ExpandBehaviorString()
{
    ExpandStringFromTable("$BehaviorString", kGroupBehavior);
}

void StringTokenExpander::ExpandAspirationString()
{
    ExpandStringFromTable("$AspirationString", kGroupGeneral);
}

void StringTokenExpander::ExpandGlobalString()
{
    ExpandStringFromTable("$GlobalString", kGroupGlobal);
}

// "$SkillName:n": entry n of the skill name table (nothing for n <= 0).
void StringTokenExpander::ExpandSkillName()
{
    ForEachMarker("$SkillName", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        const int16_t skill = LocalInt16(at.mFirst);
        if (skill <= 0) return;
        StringTableText name;
        name.Load(kTableSkills, static_cast<uint16_t>(skill), kGroupGeneral);
        Replace(at, name.CString());
    });
}

// "$WordPoints:n": the word for n points: entry 1 for one, 3 for five or more, else entry 2.
void StringTokenExpander::ExpandWordPoints()
{
    ForEachMarker("$WordPoints", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        const int16_t points = LocalInt16(at.mFirst);
        const uint32_t entry = points == 1 ? 1 : (points >= 5 ? 3 : 2);
        StringTableText word;
        word.Load(kTableWordPoints, entry, kGroupWordPoints);
        Replace(at, word.CString());
    });
}

// "$GroupName:n": the name of group n, when the group name service knows it.
void StringTokenExpander::ExpandGroupName()
{
    ForEachMarker("$GroupName", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        void *singleton = GetCachedSingleton();
        if (!singleton) return;
        TokenGroupNameService *names = As<TokenSingleton>(singleton)->GetGroupNameService();
        if (!names) return;
        ScopedText text;
        if (names->GetGroupName(LocalInt16(at.mFirst), text.Interface())) Replace(at, text.CString());
    });
}

// ---- wants and fears -------------------------------------------------------------------------------------------------------

// "$WantDescriptionLocal:o:i", "$FearDescriptionLocal", "$WantHelpTextLocal", "$FearHelpTextLocal": the text of the
// want or fear number i in the list of the category of the object whose id local o holds. Only the want descriptions
// tolerate a missing list element (they write nothing); the other three crash on one.
void StringTokenExpander::ExpandWantFear(const char *marker, bool fear, bool helpText)
{
    const bool elementMayBeMissing = !fear && !helpText;
    ForEachMarker(marker, true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        cTSObject *object = FindObject(LocalInt16(at.mFirst));
        HeldReference withInterface;
        if (!object || !object->QueryInterface(kQueryUiId, withInterface.Out())) return;
        TokenWantFearService *service = As<TokenSingleton>(GetCachedSingleton())->GetWantFearService();
        if (!service) return;
        const uint32_t category = As<TokenQueried>(withInterface.Get())->GetCategoryId();
        ElementList *list = reinterpret_cast<ElementList *>(fear ? service->GetFearList(category) : service->GetWantList(category));
        const int32_t index = LocalInt16(at.mSecond);
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<intptr_t>(list->mLast) - reinterpret_cast<intptr_t>(list->mFirst)) >> 2);
        if (static_cast<uint32_t>(index) >= count) return;
        TokenListElement *element = As<TokenListElement>(list->mFirst[index]);
        ScopedText text;
        if (helpText) {
            element->GetHelpTextInto(text.Interface());
        } else if (!elementMayBeMissing || element) {
            element->GetDescriptionInto(text.Interface(), 0, 10, 10);
        }
        Replace(at, text.CString());
    });
}

// ---- property texts ----------------------------------------------------------------------------------------------------------

// "$PropertyPrivateString:t:e", "$PropertySemiGlobalString", "$PropertyGlobalString": entry e of table t (private,
// semi-global or global group; the first two from the stack object's record), run through the property text
// formatter. Nothing is written when the formatter says no.
void StringTokenExpander::ExpandPropertyString(const char *marker, int source)
{
    ForEachMarker(marker, true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        const uint16_t entry = LocalWord(at.mSecond);
        const uint16_t table = LocalWord(at.mFirst);
        uint32_t group = kGroupGlobal;
        if (source != 2) {
            cTSObject *object = FindObject(mStackObjectId);
            if (!object) return;
            TokenObjectInfo *info = object->GetObjectInfo();
            if (!info) return;
            TokenStringOwner *owner = info->GetStringOwner();
            group = source == 0 ? owner->GetPrivateGroup() : owner->GetSemiGlobalGroup();
        }
        StringTableText loaded;
        loaded.Load(table, entry, group);
        ScopedText text;
        if (FormatPropertyText(loaded.CString(), &text.Get()) != 0) Replace(at, text.CString());
    });
}

// "$GUIDCatalogNameLocal:lo:hi": the name of the catalog record whose id is local hi (high word) and local lo (low
// word).
void StringTokenExpander::ExpandGuidCatalogNameLocal()
{
    ForEachMarker("$GUIDCatalogNameLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst) || !HasLocal(at.mSecond)) return;
        const uint32_t high = static_cast<uint32_t>(Widen(LocalInt16(at.mSecond))) << 16;
        const uint32_t packedId = high | LocalWord(at.mFirst);
        GscObjectDefTable *definitions = As<TokenSingleton>(GetCachedSingleton())->GetObjectDefTable();
        void *record = definitions->FindRecord(packedId);
        ScopedText text;
        if (record && As<TokenDefinition>(record)->GetNameInto(text.Interface())) Replace(at, text.CString());
    });
}

} // namespace tokens
