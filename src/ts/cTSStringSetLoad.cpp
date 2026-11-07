// cTSStringSet: the loaders (GetStrings, GetComments, OpenResourceRecord, LoadAll), the writer (WriteStrings, Commit)
// and IsValidUtf8. Lane DG; the record formats are in notes/feedback/DA.md ("Record format"). All /O2.
#include "ts/cTSStringSet.h"

#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "ifaces/ComFramework.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSPersistDBRecord.h"
#include "ifaces/cITSStringSet.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"

#include <cstring>
#include <string>

namespace {

constexpr uint32_t kCurrentLanguageCache = 0xff275c; // signed char, -1 until the language service has been asked
constexpr uint32_t kCrLfText = 0xda31d8;             // the line break the texts are stored with (read at run time)
constexpr uint32_t kCrText = 0xda6954;               // what it is replaced by (read at run time)

constexpr uint32_t kFileRefClass = 0xb54e37d;     // cTSObjectFileRef, framework class
constexpr uint32_t kFileRefIid = 0xeb54e473;      // cITSObjectFileRef
constexpr uint32_t kRecordClass = 0x2b70a71b;     // cTSPersistDBRecord, framework class
constexpr uint32_t kRecordIid = 0x3b2271c0;       // cITSPersistDBRecord
constexpr uint32_t kSerialRecordIid = 0xc56b8f08; // cIGZPersistDBSerialRecord
constexpr uint32_t kSegmentKind = 0x656b8f0c;     // what FindSegment is asked for
constexpr int kLanguageServiceGetId = 7;          // TokenLanguageService::GetLanguageId
constexpr int kPersistGetResourceManager = 3;     // cITSPersistSystem
constexpr int kPersistFindSegment = 34;           // cITSPersistSystem
constexpr int kManagerOpenRecord = 39;            // cIGZPersistResourceManager
constexpr uint32_t kOpenForRead = 4;              // cITSObjectFileRef::Open mode of the loaders
constexpr uint32_t kRecordNameSize = 0x40;        // the name the record starts with
constexpr signed char kBulletLanguage = 0x12;     // language byte 19 (index 18): bullets become zero-width spaces

// A string living on the stack: the game's cRZString, constructed, and destroyed with its std::string.
struct TempString : cRZString {
    TempString() { Construct(); }
    TempString(const TempString &) = delete;
    TempString &operator=(const TempString &) = delete;
};

// A held COM reference (the original's smart pointers): released through slot 2 when it goes out of scope.
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

// The language argument: -1 means the current language, which the language service tells once (cached in 0xff275c).
signed char ResolveLanguage(signed char language)
{
    signed char *cache = GameAddress<signed char>(kCurrentLanguageCache);
    if (*cache == -1) {
        signed char id = static_cast<signed char>(
            VirtualCall<int32_t(__thiscall *)(void *), kLanguageServiceGetId>(appinit::GetLanguageService()));
        if (id == -1 || id == 0) id = 1;
        *cache = static_cast<signed char>(id - 1);
    }
    return language == -1 ? *cache : language;
}

uint32_t CStringLength(const uint8_t *text)
{
    return static_cast<uint32_t>(std::strlen(reinterpret_cast<const char *>(text)));
}

// Line breaks are stored as CRLF and used as CR: every occurrence is replaced (the replacement goes through
// operator=, slot 16, as in the original).
void ReplaceLineBreaks(cRZString &text)
{
    const char *crlf = GameAddress<const char>(kCrLfText);
    const char *cr = GameAddress<const char>(kCrText);
    for (int32_t at = text.Find(crlf, 0, true); at != -1; at = text.Find(crlf, 0, true)) text.Assign(*text.Replace(at, cr, 2)->AsInterface());
}

// Language byte 19 only: U+2022 (bullet) becomes U+200B. The search resumes at the last match, as the original does.
void ReplaceBullets(cRZString &text)
{
    TempString bullet, space;
    bullet.mString = "\xe2\x80\xa2";
    space.mString = "\xe2\x80\x8b";
    for (int32_t at = text.Find(*bullet.AsInterface(), 0, true); at != -1; at = text.Find(*bullet.AsInterface(), at, true))
        text.Replace(at, *space.AsInterface());
}

// A text read as raw bytes that is not valid UTF-8 is taken to be in the system code page.
void MakeUtf8(cRZString &text)
{
    if (cTSStringSet::IsValidUtf8(reinterpret_cast<const uint8_t *>(text.mString.data()), static_cast<int32_t>(text.mString.size())))
        return;
    TempString converted;
    as2::ConvertStringFrom(text.AsInterface(), -1, converted.AsInterface());
    text.Copy(*converted.AsInterface());
}

// The string the file's reader hands back, fixed up for use (GetStrings): line breaks and, for one language, bullets.
void FinishText(cRZString &text, signed char language)
{
    ReplaceLineBreaks(text);
    if (language == kBulletLanguage) ReplaceBullets(text);
}

// Opens the record of a key: through the file reference of the key's group, else (instance with a high half) through the
// resource manager. Both stay open until the references go out of scope.
void OpenKeyRecord(const uint32_t *key, Ref &file, Ref &record)
{
    auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
    if (static_cast<uint8_t>(framework->GetClassObject(kFileRefClass, kFileRefIid, &file.object)) != 0) {
        auto *fileRef = As<ifaces::cITSObjectFileRef>(file.object);
        if (fileRef->Open(key[1], kOpenForRead) == 0) fileRef->OpenRecord(key[0], key[2], &record.object);
    }
    if (record.object == nullptr && (key[2] & 0xffff0000) != 0) cTSStringSet::OpenResourceRecord(key, &record.object);
}

// ---- GetStrings: one parser per record format ----

// 0xFFFD: u16 count, then {u8 language + 1; text; comment}; keeps the texts of the requested language.
void ReadTextsFFFD(cRZStringVector *list, const uint8_t *data, signed char language)
{
    const uint8_t *p = data + 4;
    for (int16_t count = static_cast<int16_t>(data[3] * 0x100 + data[2]); count > 0; --count) {
        const uint8_t languageByte = *p;
        const uint8_t *text = p + 1;
        const uint32_t length = CStringLength(text);
        if (static_cast<int>(static_cast<uint8_t>(languageByte - 1)) == language) {
            TempString item;
            if (cTSStringSet::IsValidUtf8(text, static_cast<int32_t>(length)))
                item.mString.assign(reinterpret_cast<const char *>(text), length);
            else
                ConvertBytesToString(text, length, -1, item.AsInterface());
            FinishText(item, language);
            list->PushBackAny(item);
        }
        const uint8_t *comment = text + length + 1;
        p = comment + CStringLength(comment) + 1;
    }
}

// 0xFFFE (text and comment) and 0xFFFF (text only): language 0 only.
void ReadTextsFFFE(cRZStringVector *list, const uint8_t *data, signed char language, bool hasComments)
{
    const uint8_t *p = data + 4;
    for (int16_t count = static_cast<int16_t>(data[3] * 0x100 + data[2]); count > 0; --count) {
        const uint32_t length = CStringLength(p);
        TempString item;
        item.mString.assign(reinterpret_cast<const char *>(p), length);
        MakeUtf8(item);
        FinishText(item, language);
        list->PushBackAny(item);
        p += length + 1;
        if (hasComments) p += CStringLength(p) + 1;
    }
}

// 0xFFF6: read through the record's stream; per language {s8 language; s16 n; n x {u8, s8, text, comment}}.
// Stops after the language it was asked for.
void ReadTextsFFF6(cRZStringVector *list, ifaces::cITSPersistDBRecord *record, signed char language)
{
    ifaces::cIGZIStream *in = record->GetIStream();
    int16_t format, languageCount;
    in->GetSint16(&format); // the format word again
    in->GetSint16(&languageCount);
    for (int16_t i = 0; i < languageCount; ++i) {
        signed char entryLanguage;
        int16_t count;
        in->GetSint8(&entryLanguage);
        in->GetSint16(&count);
        for (int16_t j = 0; j < count; ++j) {
            TempString text, comment;
            uint8_t ignoredUnsigned;
            signed char ignoredSigned;
            in->GetUint8(&ignoredUnsigned);
            in->GetSint8(&ignoredSigned);
            in->GetGZStr(text.AsInterface());
            in->GetGZStr(comment.AsInterface());
            if (entryLanguage == language) {
                MakeUtf8(text);
                FinishText(text, language);
                list->PushBackAny(text);
            }
        }
        if (entryLanguage == language) break;
    }
}

// The oldest format: a big-endian count, then Pascal strings.
void ReadTextsOld(cRZStringVector *list, const uint8_t *data, signed char language)
{
    const uint8_t *p = data + 2;
    const uint32_t count = static_cast<uint32_t>(data[0]) << 8 | data[1];
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t length = static_cast<signed char>(p[0]); // ORIGINAL-BUG: a length byte >= 0x80 is negative (huge string, throws)
        // ORIGINAL-BUG: the original means to turn the code-page bytes 0xA1 and 0xD5 into 'o' and an apostrophe in the
        // record's buffer, but compares a signed char with 0xA1 / 0xD5, which never matches: the loop changes nothing.
        TempString item;
        item.mString.assign(reinterpret_cast<const char *>(p + 1), static_cast<size_t>(length));
        FinishText(item, language);
        list->PushBackAny(item);
        p += 1 + length;
    }
}

// ---- GetComments ----

void ReadCommentsFFFD(cRZStringVector *list, const uint8_t *data, signed char language)
{
    const uint8_t *p = data + 4;
    for (int16_t count = static_cast<int16_t>(data[3] * 0x100 + data[2]); count > 0; --count) {
        const signed char entryLanguage = static_cast<signed char>(*p - 1);
        const uint8_t *text = p + 1;
        const uint8_t *comment = text + CStringLength(text) + 1;
        TempString item;
        item.mString.assign(reinterpret_cast<const char *>(comment), CStringLength(comment));
        if (entryLanguage == language) list->PushBackAny(item);
        p = comment + item.mString.size() + 1;
    }
}

void ReadCommentsFFFE(cRZStringVector *list, const uint8_t *data)
{
    const uint8_t *p = data + 4;
    for (int16_t count = static_cast<int16_t>(data[3] * 0x100 + data[2]); count > 0; --count) {
        const uint8_t *comment = p + CStringLength(p) + 1;
        TempString item;
        item.mString.assign(reinterpret_cast<const char *>(comment), CStringLength(comment));
        list->PushBackAny(item);
        p = comment + item.mString.size() + 1;
    }
}

void ReadCommentsFFF6(cRZStringVector *list, ifaces::cITSPersistDBRecord *record, signed char language)
{
    ifaces::cIGZIStream *in = record->GetIStream();
    int16_t format, languageCount;
    in->GetSint16(&format);
    in->GetSint16(&languageCount);
    for (int16_t i = 0; i < languageCount; ++i) {
        signed char entryLanguage;
        int16_t count;
        in->GetSint8(&entryLanguage);
        in->GetSint16(&count);
        for (int16_t j = 0; j < count; ++j) {
            TempString text, comment;
            uint8_t ignoredUnsigned;
            signed char ignoredSigned;
            in->GetUint8(&ignoredUnsigned);
            in->GetSint8(&ignoredSigned);
            in->GetGZStr(text.AsInterface());
            in->GetGZStr(comment.AsInterface());
            if (entryLanguage == language) list->PushBackAny(comment);
        }
        if (entryLanguage == language) break;
    }
}

} // namespace

// 0x1b285f (cdecl): every character is a well-formed UTF-8 sequence, up to six bytes long (the pre-2003 range), with the
// overlong forms of the three-, four-, five- and six-byte lengths rejected. Checks the remaining length before reading.
bool cTSStringSet::IsValidUtf8(const uint8_t *bytes, int32_t length)
{
    const uint8_t *const end = bytes + length;
    while (bytes < end) {
        const uint8_t lead = *bytes;
        int32_t step;
        if (lead < 0x80) {
            step = 1;
        } else {
            if (lead < 0xc2) break;
            const auto continuation = [&](int at) { return (bytes[at] ^ 0x80) <= 0x3f; };
            if (lead < 0xe0) {
                if (length < 2 || !continuation(1)) break;
                step = 2;
            } else if (lead < 0xf0) {
                if (length < 3 || !continuation(1) || !continuation(2) || (lead < 0xe1 && bytes[1] < 0xa0)) break;
                step = 3;
            } else if (lead < 0xf8) {
                if (length < 4 || !continuation(1) || !continuation(2) || !continuation(3) || (lead < 0xf1 && bytes[1] < 0x90)) break;
                step = 4;
            } else if (lead < 0xfc) {
                if (length < 5 || !continuation(1) || !continuation(2) || !continuation(3) || !continuation(4) ||
                    (lead < 0xf9 && bytes[1] < 0x88))
                    break;
                step = 5;
            } else {
                if (lead > 0xfd || length < 6 || !continuation(1) || !continuation(2) || !continuation(3) || !continuation(4) ||
                    !continuation(5) || (lead < 0xfd && bytes[1] < 0x84))
                    break;
                step = 6;
            }
        }
        length -= step;
        bytes += step;
    }
    return bytes == end;
}

// 0x1ec1c0 (cdecl): opens the record of `key` through the resource manager of the persist system. A record that is itself a
// cITSPersistDBRecord is used as it is; a serial record (cIGZPersistDBSerialRecord) is copied into a new one. Either way the
// position ends after the name (at most 0x40 bytes in). True once the manager has the record, even if neither interface
// answered (then *record stays as the caller had it).
bool cTSStringSet::OpenResourceRecord(const uint32_t *key, void **record)
{
    Ref dbRecord;
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *manager = VirtualSlot<void *(__thiscall *)(void *)>(persist, kPersistGetResourceManager)(persist);
    using OpenRecordFn = uint32_t(__thiscall *)(void *, const uint32_t *, void **, uint32_t);
    if (static_cast<uint8_t>(VirtualSlot<OpenRecordFn>(manager, kManagerOpenRecord)(manager, key, &dbRecord.object, 0)) == 0)
        return false;

    using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
    using SeekPastNameFn = uint32_t(__thiscall *)(void *, uint32_t);
    const auto seekPastName = [&]() {
        // The position after the name: own position plus the bytes left, capped at 0x40, then slot 13; slot 16(0).
        const uint32_t position = VirtualSlot<uint32_t(__thiscall *)(void *)>(*record, 12)(*record);
        const uint32_t bytesLeft = VirtualSlot<uint32_t(__thiscall *)(void *)>(*record, 10)(*record);
        const uint32_t total = position + bytesLeft;
        VirtualSlot<SeekPastNameFn>(*record, 13)(*record, total < kRecordNameSize ? total : kRecordNameSize);
        VirtualSlot<SeekPastNameFn>(*record, 16)(*record, 0);
    };
    if (static_cast<uint8_t>(VirtualSlot<QueryFn>(dbRecord.object, 0)(dbRecord.object, kRecordIid, record)) != 0) {
        seekPastName();
        return true;
    }
    Ref serial;
    if (static_cast<uint8_t>(VirtualSlot<QueryFn>(dbRecord.object, 0)(dbRecord.object, kSerialRecordIid, &serial.object)) != 0) {
        auto *serialRecord = As<ifaces::cIGZPersistDBSerialRecord>(serial.object);
        const uint32_t size = serialRecord->GetSize();
        As<ifaces::ComFramework>(GetDirectorFrameworkObject())->GetClassObject(kRecordClass, kRecordIid, record);
        auto *copy = As<ifaces::cITSPersistDBRecord>(*record);
        VirtualSlot<SeekPastNameFn>(copy, 21)(copy, size); // reserve and set the size
        // The buffer is the record's current data; the original pushes `size` before this call and the data call leaves it
        // on the stack as the second argument of the field read.
        serialRecord->GetFieldVoid(copy->GetCurrentData(), size);
        const void *dbKey = VirtualSlot<const void *(__thiscall *)(void *)>(dbRecord.object, 3)(dbRecord.object);
        copy->SetKey(static_cast<const cGZPersistResourceKey *>(dbKey));
        seekPastName();
        VirtualSlot<uint32_t(__thiscall *)(void *)>(dbRecord.object, 6)(dbRecord.object); // Close
    }
    return true;
}

cRZStringVector *cTSStringSet::GetStrings(signed char language)
{
    language = ResolveLanguage(language);
    const auto found = mStrings.find(language);
    if (found != mStrings.end()) return &found->second;
    if (mKey[1] == 0) return nullptr;
    // The language's entry exists from here on, even when the record cannot be read (a later call gets the empty list).
    cRZStringVector *list = &mStrings[language];

    Ref file, record;
    OpenKeyRecord(mKey, file, record);
    if (record.object == nullptr) return nullptr;
    auto *rec = As<ifaces::cITSPersistDBRecord>(record.object);
    rec->GetName(mName.AsInterface());
    auto *data = static_cast<const uint8_t *>(rec->GetCurrentData());
    switch (static_cast<uint16_t>(data[1] * 0x100 + data[0])) {
    case 0xfffd:
        ReadTextsFFFD(list, data, language);
        return list;
    case 0xfffe:
        if (language != 0) return nullptr;
        ReadTextsFFFE(list, data, language, true);
        return list;
    case 0xffff:
        if (language != 0) return nullptr;
        ReadTextsFFFE(list, data, language, false);
        return list;
    case 0xfff6:
        ReadTextsFFF6(list, rec, language);
        return list;
    default:
        if (language != 0) return nullptr;
        ReadTextsOld(list, data, language);
        return list;
    }
}

cRZStringVector *cTSStringSet::GetComments(signed char language)
{
    language = ResolveLanguage(language);
    const auto found = mComments.find(language);
    if (found != mComments.end()) return &found->second;
    if (mKey[1] == 0) return nullptr;
    cRZStringVector *list = &mComments[language];

    Ref file, record;
    OpenKeyRecord(mKey, file, record);
    if (record.object == nullptr) return nullptr;
    auto *rec = As<ifaces::cITSPersistDBRecord>(record.object);
    rec->GetName(mName.AsInterface());
    const auto *data = static_cast<const uint8_t *>(rec->GetCurrentData());
    switch (static_cast<uint16_t>(data[1] * 0x100 + data[0])) {
    case 0xfffd:
        ReadCommentsFFFD(list, data, language);
        break;
    case 0xfffe:
        if (language != 0) return nullptr;
        ReadCommentsFFFE(list, data);
        break;
    case 0xfff6:
        ReadCommentsFFF6(list, rec, language);
        break;
    default: // 0xFFFF and the old format carry no comments: the list stays empty
        break;
    }
    return list;
}

cRZStringVector *cTSStringSet::GetOrAddStrings(signed char language)
{
    language = ResolveLanguage(language);
    const auto found = mStrings.find(language);
    if (found != mStrings.end()) return &found->second;
    return &mStrings[language];
}

void cTSStringSet::LoadAllLanguages()
{
    for (signed char language = 0; language < kLanguageCount; ++language) GetStrings(language);
}

// Slot 23: loads every language and the comments, then puts the name back (each load overwrites it with the record's).
void cTSStringSet::LoadAll()
{
    const std::string savedName = mName.mString;
    LoadAllLanguages();
    GetComments(0);
    mName.mString.assign(savedName.data(), savedName.size());
}

// 0x1ef4e0: 0xFFFD, the total count, then per language {language + 1, text, 0, comment, 0}, the comments of that language
// (or, without any, those of language 0). Only the languages loaded so far are written.
bool cTSStringSet::WriteStrings(void *stream)
{
    auto *out = static_cast<ifaces::cIGZOStream *>(stream);
    out->SetSint16(-3);
    int32_t total = 0;
    for (const auto &entry : mStrings) total += entry.second.Count();
    out->SetSint16(static_cast<int16_t>(total));
    for (const auto &entry : mStrings) {
        const signed char language = entry.first;
        auto comments = mComments.find(language);
        if (comments == mComments.end()) comments = mComments.find(0);
        const cRZStringVector *commentList = comments == mComments.end() ? nullptr : &comments->second;
        const int32_t count = entry.second.Count();
        for (int32_t i = 0; i < count; ++i) {
            out->SetUint8(static_cast<uint8_t>(language + 1));
            const cRZString &text = entry.second.mFirst[i];
            const uint32_t length = text.Strlen();
            out->SetVoid(const_cast<char *>(text.ToChar()), length);
            out->SetUint8(0);
            if (commentList != nullptr && static_cast<uint32_t>(i) < static_cast<uint32_t>(commentList->Count())) {
                const cRZString &comment = commentList->mFirst[i];
                const uint32_t commentLength = comment.Strlen();
                out->SetVoid(const_cast<char *>(comment.ToChar()), commentLength);
            }
            out->SetUint8(0);
        }
    }
    return true;
}

// Slot 21: writes all languages into a new record and adds it to the key's file. Always 0 (the original's result is not
// tested by anyone). A record that could not be made is still handed to AddRecord as null.
uint32_t cTSStringSet::Commit()
{
    As<ifaces::cITSStringSet>(this)->LoadAll();
    Ref segment;
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    using FindSegmentFn = uint32_t(__thiscall *)(void *, const uint32_t *, uint32_t, void **);
    if (static_cast<uint8_t>(VirtualSlot<FindSegmentFn>(persist, kPersistFindSegment)(persist, mKey, kSegmentKind, &segment.object)) == 0)
        return 0;

    Ref record;
    if (static_cast<uint8_t>(As<ifaces::ComFramework>(GetDirectorFrameworkObject())->GetClassObject(kRecordClass, kRecordIid, &record.object)) != 0) {
        auto *rec = As<ifaces::cITSPersistDBRecord>(record.object);
        rec->GetPosition(); // the result is not used
        WriteStrings(rec->GetOStream());
    }
    Ref file;
    As<ifaces::ComFramework>(GetDirectorFrameworkObject())->GetClassObject(kFileRefClass, kFileRefIid, &file.object);
    auto *fileRef = As<ifaces::cITSObjectFileRef>(file.object);
    fileRef->Open(mKey[1], 0);
    fileRef->AddRecord(static_cast<ifaces::cITSPersistDBRecord *>(record.object), mKey[0],
        static_cast<uint16_t>(mKey[2]), mName.AsInterface(), 1);
    fileRef->Close(0);
    return 0;
}
