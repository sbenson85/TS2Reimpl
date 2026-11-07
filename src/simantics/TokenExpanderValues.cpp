#include "simantics/TokenExpanderInline.h"

using namespace ifaces;

// The stages of the string-token expansion that write numbers, times and object names from the frame's locals and the
// object's words (see TokenExpander.h).

namespace tokens {

// ---- numbers --------------------------------------------------------------------------------------------------------------

// "$Local:n": local n as a signed number.
void StringTokenExpander::ExpandLocal()
{
    ForEachMarker("$Local", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        ScopedText text;
        FormatNumber([&] { return static_cast<int32_t>(LocalInt16(at.mFirst)); }, text.Get());
        Replace(at, text.CString());
    });
}

// "$Money:n": local n times local n+1 (just local n when the second is 0).
void StringTokenExpander::ExpandMoney()
{
    ForEachMarker("$Money", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst, 1)) return;
        const int32_t amount = LocalInt16(at.mFirst);
        const int32_t factor = LocalInt16(at.mFirst + 1);
        ScopedText text;
        FormatNumber([&] { return factor != 0 ? factor * amount : amount; }, text.Get());
        Replace(at, text.CString());
    });
}

// "$Cash:n": local n plus 10000 times local n+1 (an amount kept in two 16-bit words).
void StringTokenExpander::ExpandCash()
{
    ForEachMarker("$Cash", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst, 1)) return;
        const int32_t low = LocalInt16(at.mFirst);
        const int32_t high = LocalInt16(at.mFirst + 1);
        ScopedText text;
        FormatNumber([&] { return low + high * 10000; }, text.Get());
        Replace(at, text.CString());
    });
}

// "$TimeLocal:n": the hour in local n (0 to 23), as the language writes it.
void StringTokenExpander::ExpandTimeLocal()
{
    ForEachMarker("$TimeLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        const int32_t hour = LocalInt16(at.mFirst);
        if (hour < 0 || hour >= 24) return;
        ScopedText text;
        // ORIGINAL-BUG: no check that the language service and its language exist.
        As<TokenLanguageService>(appinit::GetLanguageService())->GetLanguage(0)->FormatHour(hour, 0, -1, text.Interface());
        Replace(at, text.CString());
    });
}

// "$Attrib:n": word n of the stack object's word table (the table at +0x88) as a signed number.
void StringTokenExpander::ExpandAttrib()
{
    ForEachMarker("$Attrib", true, [&](const Occurrence &at) {
        // ORIGINAL-BUG: no check that there is a frame, nor that the frame's stack object exists: both crash.
        cTSObject *object = FindObject(static_cast<int16_t>(mFrame->GetStackObjectId()));
        if (at.mFirst < 0 || at.mFirst >= static_cast<int32_t>(object->GetOwnerWordCount())) return;
        ScopedText text;
        FormatNumber([&] { return static_cast<int32_t>(object->GetOwnerWord(at.mFirst)); }, text.Get());
        Replace(at, text.CString());
    });
}

// ---- names of objects -----------------------------------------------------------------------------------------------------------

// The display name of the object with that id, if there is one (flag 1: the full display form).
void StringTokenExpander::ReplaceWithObjectName(const Occurrence &at, int16_t objectId)
{
    cTSObject *named = FindObject(objectId);
    if (!named) return;
    ScopedText text;
    named->GetNameInto(text.Interface(), 1);
    Replace(at, text.CString());
}

// "$NameAttrib:n": the name of the object whose id is word n of the stack object's word table.
void StringTokenExpander::ExpandNameAttrib()
{
    ForEachMarker("$NameAttrib", true, [&](const Occurrence &at) {
        // ORIGINAL-BUG: as in $Attrib, no check of the frame and the stack object.
        cTSObject *object = FindObject(static_cast<int16_t>(mFrame->GetStackObjectId()));
        if (at.mFirst < 0 || at.mFirst >= static_cast<int32_t>(object->GetOwnerWordCount())) return;
        ReplaceWithObjectName(at, object->GetOwnerWord(at.mFirst));
    });
}

// "$NameLocal:n": the name of the object whose id local n holds.
void StringTokenExpander::ExpandNameLocal()
{
    ForEachMarker("$NameLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        ReplaceWithObjectName(at, LocalInt16(at.mFirst));
    });
}

// "$NameParam:n": the name of the object whose id argument n of the frame holds.
void StringTokenExpander::ExpandNameParam()
{
    ForEachMarker("$NameParam", true, [&](const Occurrence &at) {
        if (!HasArgument(at.mFirst)) return;
        ReplaceWithObjectName(at, static_cast<int16_t>(mFrame->GetArg(at.mFirst)));
    });
}

// "$CatalogLocal:n": the catalog description of the object whose id local n holds.
void StringTokenExpander::ExpandCatalogLocal()
{
    ForEachMarker("$CatalogLocal", true, [&](const Occurrence &at) {
        if (!HasLocal(at.mFirst)) return;
        cTSObject *object = FindObject(LocalInt16(at.mFirst));
        if (!object) return;
        // ORIGINAL-BUG: an object without a record crashes the next call.
        TokenObjectInfo *info = object->GetObjectInfo();
        ScopedText text;
        info->GetDescriptionInto(text.Interface());
        Replace(at, text.CString());
    });
}

} // namespace tokens
