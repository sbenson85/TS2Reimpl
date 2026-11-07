// cTSStringSet: construction, the readers that go through the loaders (GetStringCount, GetString, GetStringInto, ...), the
// key and sort-order helpers, and the string-set factory's CreateInstance / CreateInstanceByType / Write. Lane DG.
#include "ts/cTSStringSet.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSStringSet.h"
#include "rz/ServiceGetters.h"

#include <new>

namespace {

constexpr uint32_t kFormatSourceText = 0xda6944; // "%s(%s:%d:%d)": text, source name, instance, index (read at run time)
constexpr uint32_t kSegmentKind = 0x656b8f0c;
constexpr uint32_t kOStreamIid = 0xc51b11cb;

struct TempString : cRZString {
    TempString() { Construct(); }
    TempString(const TempString &) = delete;
    TempString &operator=(const TempString &) = delete;
};

struct Ref {
    void *object = nullptr;
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    ~Ref()
    {
        if (object) VirtualSlot<uint32_t(__thiscall *)(void *)>(object, 2)(object);
    }
};

// The text at a 1-based index: the requested language's entry (unless it is empty, for a language other than 0), else
// language 0's. Null when neither has it.
const cRZString *FindText(cTSStringSet *set, uint32_t index, signed char language)
{
    const signed char languages[2] = {language, 0};
    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        cRZStringVector *list = set->GetStrings(languages[attempt]);
        if (list == nullptr || static_cast<int32_t>(index) < 1) continue;
        if (static_cast<uint32_t>(list->Count()) < index) continue;
        const cRZString &entry = list->mFirst[index - 1];
        if (attempt == 0 && language != 0 && entry.mString.empty()) continue;
        return &entry;
    }
    return nullptr;
}

// The text GetString hands back when there is none (the original's function-local static).
const cRZString *EmptyText()
{
    struct Empty : cRZString {
        Empty() { Construct(); }
    };
    static Empty empty;
    return &empty;
}

} // namespace

// The copy of a list in a map copy: the strings are copied into a block of exactly their count.
cTSStringSetStringList::cTSStringSetStringList(const cTSStringSetStringList &other) : cRZStringVector{}
{
    const uint32_t count = static_cast<uint32_t>(other.Count());
    if (count != 0) {
        mFirst = std::allocator<cRZString>().allocate(count);
        mLast = UninitializedCopy(other.mFirst, other.mLast, mFirst, nullptr);
        mEnd = mFirst + count;
    }
}

cTSStringSetStringList &cTSStringSetStringList::operator=(const cTSStringSetStringList &other)
{
    if (this != &other) {
        cTSStringSetStringList copy(other);
        std::swap(mFirst, copy.mFirst);
        std::swap(mLast, copy.mLast);
        std::swap(mEnd, copy.mEnd);
    }
    return *this;
}

// 0x1eace0: an empty set; the maps get their head nodes.
cTSStringSet *cTSStringSet::Construct()
{
    mVTable = GameAddress(kVTable);
    mPersist.mVTable = GameAddress(kPersistVTable);
    mRefCount = 0;
    mKey[0] = mKey[1] = mKey[2] = mKey[3] = 0;
    mName.Construct();
    new (&mStrings) cTSStringSetMap();
    new (&mComments) cTSStringSetMap();
    new (&mSortOrder) std::vector<uint32_t>();
    mShowSource = 0;
    mSourceName.Construct();
    return this;
}

// 0x1eba70 (cdecl): the COM creator for the set's class id.
cTSStringSet *cTSStringSet::Create()
{
    auto *set = static_cast<cTSStringSet *>(::operator new(sizeof(cTSStringSet)));
    return set ? set->Construct() : nullptr;
}

uint32_t cTSStringSet::GetStringCount(int32_t language)
{
    cRZStringVector *list = GetStrings(static_cast<signed char>(language));
    if (list == nullptr) {
        list = GetStrings(0);
        if (list == nullptr) return 0;
    }
    return static_cast<uint32_t>(list->Count());
}

const cRZString *cTSStringSet::GetString(uint32_t index, int32_t language)
{
    const cRZString *text = FindText(this, index, static_cast<signed char>(language));
    return text ? text : EmptyText();
}

bool cTSStringSet::GetStringInto(cIGZString *out, uint32_t index, int32_t language)
{
    const cRZString *text = FindText(this, index, static_cast<signed char>(language));
    if (text == nullptr) return false;
    out->Copy(*text->AsInterface());
    if (mShowSource != 0 && static_cast<signed char>(language) == -1) {
        TempString annotated;
        const int16_t instance = static_cast<int16_t>(As<ifaces::cITSStringSet>(this)->GetInstance());
        const char *chars = out->ToChar();
        annotated.Sprintf(GameAddress<const char>(kFormatSourceText), chars, mSourceName.mString.c_str(), static_cast<int>(instance),
            index);
        *out = *annotated.AsInterface();
    }
    return true;
}

bool cTSStringSet::GetStringExact(cIGZString *out, uint32_t index, int32_t language)
{
    cRZStringVector *list = GetStrings(static_cast<signed char>(language));
    if (list != nullptr && static_cast<int32_t>(index) > 0 && index <= static_cast<uint32_t>(list->Count())) {
        out->Copy(*list->mFirst[index - 1].AsInterface());
        return true;
    }
    return false;
}

// DA listed this as ret 8; the original pops 12 bytes: the third argument (a language) is not used.
bool cTSStringSet::GetComment(cIGZString *out, uint32_t index, int32_t)
{
    cRZStringVector *list = GetComments(0);
    if (list != nullptr && static_cast<int32_t>(index) > 0 && index <= static_cast<uint32_t>(list->Count())) {
        out->Copy(*list->mFirst[index - 1].AsInterface());
        return true;
    }
    return false;
}

bool cTSStringSet::GetName(cIGZString *out)
{
    GetStrings(0); // loads the name
    out->Copy(*mName.AsInterface());
    return true;
}

void cTSStringSet::SetName(const cIGZString *text)
{
    for (signed char language = 0; language < kLanguageCount; ++language) GetStrings(language);
    mName.Copy(*text);
}

bool cTSStringSet::IsWritable()
{
    Ref segment;
    using FindSegmentFn = uint32_t(__thiscall *)(void *, const uint32_t *, uint32_t, void **);
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    if (static_cast<uint8_t>(VirtualSlot<FindSegmentFn>(persist, 34)(persist, mKey, kSegmentKind, &segment.object)) == 0) return false;
    persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    return static_cast<uint8_t>(VirtualSlot<uint32_t(__thiscall *)(void *, void *)>(persist, 28)(persist, segment.object)) != 0;
}

// Slot 4: takes over another set's texts, comments and name (the other is asked for its cTSStringSet face).
void cTSStringSet::CopyFrom(void *other)
{
    if (other == nullptr) return;
    Ref held;
    VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t, void **)>(other, 0)(other, kClassIid, &held.object);
    if (held.object != nullptr) {
        auto *source = static_cast<cTSStringSet *>(held.object);
        source->LoadAllLanguages();
        source->GetComments(0);
        As<ifaces::cITSStringSet>(source)->GetName(mName.AsInterface());
        mStrings = source->mStrings;
        mComments = source->mComments;
        mKey[0] = mKey[1] = mKey[2] = 0;
    }
}

uint32_t cTSStringSet::AppendStringFrom(void *source, uint32_t sourceIndex)
{
    if (static_cast<int32_t>(sourceIndex) < 1) return 0;
    const uint32_t index = As<ifaces::cITSStringSet>(this)->GetStringCount(0) + 1;
    As<ifaces::cITSStringSet>(this)->CopyStringFrom(static_cast<ifaces::cITSStringSet *>(source), sourceIndex, index);
    return index;
}

uint32_t cTSStringSet::AppendString(const cIGZString *text, int32_t language)
{
    const uint32_t index = As<ifaces::cITSStringSet>(this)->GetStringCount(0) + 1;
    As<ifaces::cITSStringSet>(this)->SetString(const_cast<cIGZString *>(text), index, language);
    return index;
}

void cTSStringSet::SetKeyFromFile(void *file, uint32_t instance, uint32_t type)
{
    uint32_t key[4];
    key[0] = type;
    key[1] = 0;
    if (file != nullptr) key[1] = static_cast<ifaces::cITSObjectFileRef *>(file)->GetGroup();
    key[2] = static_cast<uint16_t>(instance);
    key[3] = 0;
    As<ifaces::cITSStringSet>(this)->SetKey(key);
}

// Positions 0..count (count + 1 entries) in order; the sort starts from here.
void cTSStringSet::ResetSortOrder()
{
    mSortOrder.clear();
    const int32_t count = static_cast<int32_t>(As<ifaces::cITSStringSet>(this)->GetStringCount(0));
    for (int32_t i = 0; i < count + 1; ++i) mSortOrder.push_back(static_cast<uint32_t>(i));
}

uint32_t cTSStringSet::GetSortedIndex(uint32_t position)
{
    if (position < mSortOrder.size()) return mSortOrder[position];
    ResetSortOrder(); // inlined in the original, through the same slot-5 call
    return position;
}

// ---- cTSResourceFactoryStringSet ----

bool cTSResourceFactoryStringSet::CreateInstance(void *record, uint32_t riid, void **out, uint32_t, uint32_t)
{
    const uint32_t *recordKey = VirtualSlot<const uint32_t *(__thiscall *)(void *)>(record, 3)(record);
    const uint32_t key[4] = {recordKey[0], recordKey[1], recordKey[2], recordKey[3]};
    auto *set = static_cast<cTSStringSet *>(::operator new(sizeof(cTSStringSet)));
    set->Construct();
    As<ifaces::cITSStringSet>(set)->AddRef();
    As<ifaces::cITSStringSet>(set)->SetKey(const_cast<uint32_t *>(key));
    const bool answered = As<ifaces::cITSStringSet>(set)->QueryInterface(riid, out);
    As<ifaces::cITSStringSet>(set)->Release();
    return answered;
}

bool cTSResourceFactoryStringSet::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t, uint32_t)
{
    auto *set = static_cast<cTSStringSet *>(::operator new(sizeof(cTSStringSet)));
    set->Construct();
    As<ifaces::cITSStringSet>(set)->AddRef();
    VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(&set->mPersist, 8)(&set->mPersist, type); // SetType
    const bool answered = As<ifaces::cITSStringSet>(set)->QueryInterface(riid, out);
    As<ifaces::cITSStringSet>(set)->Release();
    return answered;
}

// Slot 6: the record's stream gets the name field (padded to 0x40 bytes) and then every loaded text and comment.
bool cTSResourceFactoryStringSet::Write(void *resource, void *record)
{
    using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
    Ref set;
    if (static_cast<uint8_t>(VirtualSlot<QueryFn>(resource, 0)(resource, cTSStringSet::kClassIid, &set.object)) == 0) return false;
    Ref stream;
    if (static_cast<uint8_t>(VirtualSlot<QueryFn>(record, 0)(record, kOStreamIid, &stream.object)) == 0) return false;
    auto *out = static_cast<ifaces::cIGZOStream *>(stream.object);
    auto *target = static_cast<cTSStringSet *>(set.object);
    const uint32_t nameLength = target->mName.Strlen();
    out->SetVoid(const_cast<char *>(target->mName.ToChar()), nameLength);
    for (int32_t padding = static_cast<int32_t>(0x40 - target->mName.Strlen()); padding > 0; --padding) out->SetUint8(0);
    target->WriteStrings(stream.object);
    return true;
}
