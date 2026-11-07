#pragma once
#include "gz/cIGZString.h"
#include "rz/RZAutoRef.h"
#include "rz/cRZString.h"
#include "simantics/TokenFinder.h"
#include "ts/cTSString.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ifaces {
class cTSObject;
class cTSTreeStackElem;
class GscEntryTable;
class GscObjectDefTable;
class GscPeopleTable;
class TokenNeighborhoodTable;
} // namespace ifaces

// String-token expansion (cTSObject::vf262, 0x535130, vtable slot 262, shared by cTSObject, cTSPerson and
// cEdithMTObject): replaces the "$Token" markers of a text with what the game knows about the running tree's
// objects. Text shown to the player (dialogs, pie menu entries, notifications) is written with markers such as
// "$Neighbor", "$Local1" (the value of local variable 1 of the running tree), "$Money2" (locals 2 and 3 as an
// amount), "$GlobalString:1:2" (entry local 2 of the string table local 1 names) or "$FullNameLocal1" (the full name of
// the person whose id local 1 holds). The numbers after a marker are always indices into the frame's locals (only
// $NameParam reads its frame's arguments). SimAntics primitives such as MakeActionString (0x32) and Dialog (0x24)
// call it with their running frame.
//
// The expansion is a fixed sequence of 50 stages, one per marker, run in the order of StringTokenExpander::Run: each
// stage finds every occurrence of its marker in the text, case-insensitively and with the optional ":n" / ":n:m"
// numeric parameters FindToken reads, and replaces it with the text its lookups give. A stage whose lookups fail
// leaves the marker as it was. After a stage replaced an occurrence the search goes on one character after the
// occurrence's start, so replacement text containing a marker is expanded too (by the same stage, or a later one).

// ---- the object -----------------------------------------------------------------------------------------------

// The table that gives objects by id: a vector of object pointers inside the owner at this+0x1c4 (id 1 is the first
// entry).
struct TokenObjectTableOwner {
    uint8_t mOwnerFields[0x1a24];
    void **mFirst; // +0x1a24
    void **mLast;  // +0x1a28
};
static_assert(offsetof(TokenObjectTableOwner, mFirst) == 0x1a24, "object table layout");
static_assert(offsetof(TokenObjectTableOwner, mLast) == 0x1a28, "object table layout");

// The part of the object vf262 reads (the same on cTSObject, cTSPerson and cEdithMTObject).
struct TokenObject {
    void **mVTable;                              // +0x00 primary vtable (the game's)
    void **mSecondary;                           // +0x04 the cTSTreeSim subobject's vtable
    uint8_t mFieldsBefore[0x38];                 // +0x08..+0x3f
    void *mContext;                              // +0x40 the context holder (slots 6, 8, 9, 10, 24 give the tables below)
    uint8_t mFieldsBetween[0x180];               // +0x44..+0x1c3
    TokenObjectTableOwner *mObjectTableOwner;    // +0x1c4

    // 0x535130 (__thiscall, five stack arguments, ret 0x14). Expands the markers of `text` in place.
    //   stackObjectId: the id of the object that "$Me"/"$Object" style markers mean; 0 takes the frame's own stack
    //                  object id (when there is a frame)
    //   frame:         the running tree stack frame; its local variables are the markers' numeric parameters
    //                  (may be null: then only the markers that need no parameter are expanded)
    //   outPair:       optional {id of the person named by $Neighbor/$FullName, family number of $Family}
    //   outPerson:     optional: set to the person info of $Neighbor/$FullName, or the linked info of $Object
    void ExpandStringTokens(cIGZString *text, int16_t stackObjectId, ifaces::cTSTreeStackElem *frame, int16_t *outPair, void **outPerson);

    // The object with id `id` (1-based, as the sims 2 object ids are), or null: ids outside the table, empty
    // entries. The table entry answers its resolved object (slot 280).
    ifaces::cTSObject *FindObject(int16_t id) const;
};
static_assert(offsetof(TokenObject, mContext) == 0x40, "TokenObject layout");
static_assert(offsetof(TokenObject, mObjectTableOwner) == 0x1c4, "TokenObject layout");

namespace tokens {

// ---- constants -------------------------------------------------------------------------------------------------

constexpr uint32_t kQueryUiId = 0x5a69d3d5;         // QueryInterface id of the interface the want/fear markers ask objects for
constexpr uint32_t kStringTextType = 0x2026960b;    // the resource type of string tables
constexpr uint32_t kGroupGlobal = 0x7fd46cd0;       // the default string group
constexpr uint32_t kGroupGeneral = 0x7f96c284;      // group of the shared day/skill/aspiration tables
constexpr uint32_t kGroupBehavior = 0x7fe59fd0;     // group of the behavior strings
constexpr uint32_t kGroupWordPoints = 0x7fa7dc7c;   // group of the word-points strings
constexpr uint32_t kTableDays = 0x99;               // string table of the weekday names
constexpr uint32_t kTableJobTitles = 0x93;          // job titles
constexpr uint32_t kTableSkills = 0xcb;             // skill names
constexpr uint32_t kTableWordPoints = 0xcc;         // word points ("point", "points")
constexpr uint32_t kPersonPropertyFamily = 0x3d;    // the property of a person record that names the family / lot entry
constexpr int16_t kErrorBadInterface = 0x1c;        // thrown by $TokenNameLocal (see ExpandTokenNameLocal)
constexpr uint32_t kSecondLanguageCacheRva = 0x10cc2dc; // the cache global of GetSecondLanguageService

// ---- the original callees without a replacement yet ---------------------------------------------------------------------

// The language service under its second cache (0x20de26, cache global 0x10cc2dc: the same service ids as GetLanguageService,
// 0x9c5a5d, which keeps its own cache). The full-name tokens use this one.
void *GetSecondLanguageService();

// 0x5307b0 (__cdecl, ret 0; src/simantics/PropertyText.cpp): turns the text of a loaded property string into its display form
// in `out`. The result is the byte the language service's formatter answered (0 when nothing printable came out), nothing
// else of eax: callers test al.
uint8_t __cdecl FormatPropertyText(const char *source, cRZString *out);

// ---- a text loaded from the string tables --------------------------------------------------------------------

// The game's string-table loader object (cTSString, vtable 0xda5128, 0x34 bytes): a cRZString that fills itself from the
// string tables when constructed. The members are ours (cTSString); the destructor is the cRZString one, as in the original.
class StringTableText {
public:
    StringTableText() = default;
    StringTableText(const StringTableText &) = delete;
    StringTableText &operator=(const StringTableText &) = delete;
    ~StringTableText() noexcept;

    void ConstructEmpty();
    // cTSString::Construct without flags (0x1d0e90): (table word, entry word, group, resource type).
    void Load(uint32_t table, uint32_t entry, uint32_t group);
    void Assign(const StringTableText &other);

    cRZString &Text() { return mLoaded.mString; }
    const char *CString() { return mLoaded.mString.mString.c_str(); }

private:
    cTSString mLoaded = {};
    bool mAlive = false;
};

// A cRZString local that is destroyed at the end of its scope.
class ScopedText {
public:
    ScopedText() { mText.Construct(); }
    ScopedText(const ScopedText &) = delete;
    ScopedText &operator=(const ScopedText &) = delete;
    ~ScopedText() noexcept { mText.Destruct(); }
    std::string &String() { return mText.mString; }
    const char *CString() { return mText.mString.c_str(); }
    cRZString &Get() { return mText; }
    void *Interface() { return &mText; } // the cIGZString * the game's methods fill

private:
    cRZString mText;
};

// A local that owns one reference: Release when it goes (also while an error unwinds).
class HeldReference {
public:
    HeldReference() = default;
    HeldReference(const HeldReference &) = delete;
    HeldReference &operator=(const HeldReference &) = delete;
    ~HeldReference() noexcept { mRef.ReleaseUnguarded(); }
    // 0x304910: AddRefs the object and holds it (an earlier one is not released).
    void AddRefAndHold(void *object);
    // Holds a reference somebody else already added (the result of a QueryInterface).
    void **Out() { return &mRef.mObject; }
    void *Get() const { return mRef.mObject; }

private:
    RZAutoRef mRef = {nullptr};
};

// ---- the expansion -----------------------------------------------------------------------------------------------

// One occurrence of a marker: where, how long (the marker and its ":n:m" parameters) and the parameters (-1 when
// absent).
struct Occurrence {
    int32_t mPosition = -1;
    int32_t mLength = 0;
    int32_t mFirst = -1;  // the first numeric parameter, usually the index of a local variable
    int32_t mSecond = -1; // the second one
};

// A pointer to a begin/end pair of element pointers: a std::vector<void *> as the game's services hand it out.
struct ElementList {
    void **mFirst;
    void **mLast;
};

// The state of one expansion. Created by ExpandStringTokens; the stages are its members.
class StringTokenExpander {
public:
    StringTokenExpander(TokenObject *object, cIGZString *text, int16_t stackObjectId, ifaces::cTSTreeStackElem *frame, int16_t *outPair,
                        void **outPerson);
    StringTokenExpander(const StringTokenExpander &) = delete;
    StringTokenExpander &operator=(const StringTokenExpander &) = delete;
    ~StringTokenExpander() noexcept;

    // Runs the 50 stages and hands the result back to the caller's string.
    void Run();

private:
    // ---- finding and replacing ------------------------------------------------------------------------------
    // The loop every stage is built on: for each occurrence of the marker, `body(occurrence)`; the search for the
    // next one starts one character after the previous occurrence's start.
    template <typename Body> void ForEachMarker(const char *marker, bool withParameters, Body body)
    {
        const std::string markerText(marker);
        Occurrence at;
        int32_t position = FindToken(mWork.mString, markerText, 0, &at.mLength, withParameters ? &at.mFirst : nullptr, withParameters ? &at.mSecond : nullptr);
        while (position != -1) {
            at.mPosition = position;
            body(at);
            position = FindToken(mWork.mString, markerText, static_cast<uint32_t>(position) + 1, &at.mLength, withParameters ? &at.mFirst : nullptr,
                                 withParameters ? &at.mSecond : nullptr);
        }
    }
    void Replace(const Occurrence &at, const char *text);             // the occurrence becomes `text` (up to its NUL)
    void ReplaceAll(const Occurrence &at, const std::string &text);   // ... the whole std::string, NULs included

    // ---- the frame -------------------------------------------------------------------------------------------
    bool HasLocal(int32_t index, int32_t extra = 0) const;  // a frame, index >= 0, and local index + extra exists
    bool HasArgument(int32_t index) const;                  // the same for the frame's arguments
    uint16_t LocalWord(int32_t index) const;
    int16_t LocalInt16(int32_t index) const { return static_cast<int16_t>(LocalWord(index)); }

    // ---- lookups -------------------------------------------------------------------------------------------------
    ifaces::cTSObject *FindObject(int16_t id) const { return mObject->FindObject(id); }
    void *FindPerson(int32_t id) const;                    // the people table
    void *FindEntry(int32_t id) const;                     // the entry table
    void *LookupList() const;                              // the context's slot 6
    // The number as the language service writes it ("%d" when there is no language). `value` is asked at the
    // moment the original asks: after the language lookups.
    template <typename ValueFn> void FormatNumber(ValueFn value, cRZString &out);
    // The sim's display name from its person info: through the name service, or "first last".
    void ComposeFullName(void *personInfo, cRZString &full);

    // ---- the stages, in the order Run calls them -----------------------------------------------------------------
    void ExpandTokenName(const char *marker, bool personal, bool viaCountedSlot);
    void ExpandTokenDescription(const char *marker, bool personal, bool viaCountedSlot);
    void ExpandTokenText(const char *marker, bool personal, bool viaCountedSlot, bool description);
    void ExpandFamilyAssets();
    void ExpandFamily();
    void ExpandLastName();
    void ExpandNeighborLocal();
    void ExpandNeighborhoodLocal();
    void ExpandPrimaryNeighborhood();
    void ExpandNeighbor();
    void ExpandMe();
    void ExpandObject();
    void ExpandLocal();
    void ExpandMoney();
    void ExpandCash();
    void ExpandTimeLocal();
    void ExpandAttrib();
    void ExpandNameAttrib();
    void ExpandNameLocal();
    void ExpandNameParam();
    void ExpandFullNameLocal();
    void ExpandFullName();
    void ExpandTokenNameLocal();
    void ExpandLotNameLocalByLotId();
    void ExpandLotNameLocal();
    void ExpandCatalogLocal();
    void ExpandDayLocal();
    void ExpandDayFlags();
    void ExpandJobTitle();
    void ExpandPrivateString();
    void ExpandBehaviorString();
    void ExpandAspirationString();
    void ExpandSemiGlobalString();
    void ExpandGlobalString();
    void ExpandSkillName();
    void ExpandWordPoints();
    void ExpandWantFear(const char *marker, bool fear, bool helpText);
    void ExpandGroupName();
    void ExpandPropertyString(const char *marker, int source);
    void ExpandGuidCatalogNameLocal();

    // Shared bodies.
    void ExpandStringFromTable(const char *marker, uint32_t group);
    void ReplaceWithObjectName(const Occurrence &at, int16_t objectId);

    TokenObject *mObject;
    cRZString mWork;                          // the working copy of the caller's text
    cIGZString *mCallerText;                  // where the result goes
    ifaces::cTSTreeStackElem *mFrame;
    int16_t *mOutPair;
    void **mOutPerson;
    int16_t mStackObjectId;                   // the id argument, or the frame's own
    ifaces::GscEntryTable *mEntries;          // the context's slot 9
    ifaces::GscPeopleTable *mPeople;          // slot 10
    ifaces::TokenNeighborhoodTable *mNeighborhoods; // slot 8
    ifaces::GscObjectDefTable *mDefinitions;  // slot 24
};

} // namespace tokens
