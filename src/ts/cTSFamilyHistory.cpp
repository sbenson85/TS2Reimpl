#include "ts/cTSFamilyHistory.h"

#include "game/game.h"
#include "game/vtable.h"
#include "misc/UtilitiesC.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

namespace {

// The serialization streams (cIGZIStream / cIGZOStream) by slot: 5 byte, 6 16-bit, 8 32-bit signed, 9 32-bit unsigned,
// 15 string, 18 error state, 19 set metadata, 20 get metadata. Reads take a pointer, writes the value.
template <typename R, typename... A> R StreamCall(void *stream, unsigned slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(stream, slot)(stream, args...);
}

uint8_t StreamFailed(void *stream)
{
    return StreamCall<uint8_t>(stream, 18);
}

// The version a stream was given through its metadata: slot 20's object, slot 78 of that returns a pointer to it.
uint32_t StreamVersion(void *stream)
{
    void *metadata = StreamCall<void *>(stream, 20);
    const uint32_t *version = VirtualCall<const uint32_t *(__thiscall *)(void *), 78>(metadata);
    return *version;
}

} // namespace

// ---- FamilyHistoryText ----

FamilyHistoryText::FamilyHistoryText(const cRZString &from)
{
    new (&value) cRZString{GameAddress<void>(cRZString::kVTable), std::string(from.mString.data(), from.mString.size()), 0};
}

FamilyHistoryText &FamilyHistoryText::operator=(const FamilyHistoryText &other)
{
    value.mString = other.value.mString;
    return *this;
}

FamilyHistoryText::~FamilyHistoryText()
{
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 33>(&value, 0);
}

// ---- FamilyHistoryEntry ----

FamilyHistoryEntry::FamilyHistoryEntry()
    : words{}, value10(0), value14(0), word18(0), word1A(0), values1C{}, value34(0), value38(0), word3C(0), word3E(0),
      flag40(0), flag41(0), flag42(0), pad43(0), texts(), word50(0), pad52(0)
{
}

FamilyHistoryEntry::FamilyHistoryEntry(const FamilyHistoryEntry &other) : FamilyHistoryEntry()
{
    *this = other;
}

FamilyHistoryEntry &FamilyHistoryEntry::operator=(const FamilyHistoryEntry &other)
{
    for (int i = 0; i < 8; ++i) words[i] = other.words[i];
    value10 = other.value10;
    value14 = other.value14;
    word18 = other.word18;
    word1A = other.word1A;
    for (int i = 0; i < 6; ++i) values1C[i] = other.values1C[i];
    value34 = other.value34;
    value38 = other.value38;
    word50 = other.word50;
    word3C = other.word3C;
    word3E = other.word3E;
    flag40 = other.flag40;
    flag41 = other.flag41;
    flag42 = other.flag42;
    if (&texts != &other.texts) texts.assign(other.texts.begin(), other.texts.end());
    return *this;
}

uint8_t FamilyHistoryEntry::Read(void *stream)
{
    const uint32_t version = StreamVersion(stream);
    for (auto &word : words) StreamCall<uint8_t>(stream, 6, &word);
    // A failed 16-bit read leaves the scratch word as it was (a stack word in the original).
    int16_t scratch = 0;
    if (version < 0x29) {
        StreamCall<uint8_t>(stream, 6, &scratch);
        value10 = scratch;
        StreamCall<uint8_t>(stream, 6, &scratch);
        value14 = scratch;
    } else {
        StreamCall<uint8_t>(stream, 8, &value10);
        StreamCall<uint8_t>(stream, 8, &value14);
    }
    StreamCall<uint8_t>(stream, 6, &word18);
    StreamCall<uint8_t>(stream, 6, &word1A);
    uint32_t unused = 0; // five retired 16-bit fields
    for (int i = 0; i < 5; ++i) StreamCall<uint8_t>(stream, 6, &unused);
    if (version == 0) {
        for (auto &value : values1C) {
            StreamCall<uint8_t>(stream, 6, &scratch);
            value = scratch;
        }
    } else {
        for (auto &value : values1C) StreamCall<uint8_t>(stream, 8, &value);
    }
    StreamCall<uint8_t>(stream, 8, &value34);
    StreamCall<uint8_t>(stream, 8, &value38);
    StreamCall<uint8_t>(stream, 6, &word50);
    if (version > 0x4d) {
        StreamCall<uint8_t>(stream, 6, &word3C);
        StreamCall<uint8_t>(stream, 6, &word3E);
        uint8_t a = 0, b = 0, c = 0;
        StreamCall<uint8_t>(stream, 5, &a);
        flag40 = a != 0;
        StreamCall<uint8_t>(stream, 5, &b);
        flag41 = b != 0;
        StreamCall<uint8_t>(stream, 5, &c);
        flag42 = c != 0;
        texts.clear();
        uint32_t count = 0;
        StreamCall<uint8_t>(stream, 9, &count);
        for (uint32_t i = 0; i < count; ++i) {
            cRZString text{GameAddress<void>(cRZString::kVTable), std::string(), 0};
            StreamCall<uint8_t>(stream, 15, &text);
            texts.push_back(FamilyHistoryText(text));
        }
    }
    return StreamFailed(stream) == 0;
}

uint8_t FamilyHistoryEntry::Write(void *stream) const
{
    for (int16_t word : words) StreamCall<uint8_t>(stream, 6, word);
    StreamCall<uint8_t>(stream, 8, value10);
    StreamCall<uint8_t>(stream, 8, value14);
    StreamCall<uint8_t>(stream, 6, word18);
    StreamCall<uint8_t>(stream, 6, word1A);
    for (int i = 0; i < 5; ++i) StreamCall<uint8_t>(stream, 6, int16_t(0));
    for (int32_t value : values1C) StreamCall<uint8_t>(stream, 8, value);
    StreamCall<uint8_t>(stream, 8, value34);
    StreamCall<uint8_t>(stream, 8, value38);
    StreamCall<uint8_t>(stream, 6, word50);
    StreamCall<uint8_t>(stream, 6, word3C);
    StreamCall<uint8_t>(stream, 6, word3E);
    StreamCall<uint8_t>(stream, 5, uint8_t(flag40 != 0));
    StreamCall<uint8_t>(stream, 5, uint8_t(flag41 != 0));
    StreamCall<uint8_t>(stream, 5, uint8_t(flag42 != 0));
    StreamCall<uint8_t>(stream, 9, uint32_t(texts.size()));
    for (const auto &text : texts) StreamCall<uint8_t>(stream, 15, &text.value);
    return StreamFailed(stream) == 0;
}

// ---- cTSFamilyHistory ----

FILE *(__cdecl *cTSFamilyHistory::sOpen)(const char *path, const char *mode) = &fopen;
int(__cdecl *cTSFamilyHistory::sPrint)(FILE *file, const char *format, ...) = &fprintf;
int(__cdecl *cTSFamilyHistory::sClose)(FILE *file) = &fclose;

cITSFamilyHistory *cTSFamilyHistory::Create() { return new (std::nothrow) cTSFamilyHistory; }

cTSFamilyHistory::~cTSFamilyHistory() {}

bool cTSFamilyHistory::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 0x7ab5bcbc:
    case 1:
    case 0xe4fda3d4:
        *out = static_cast<cITSFamilyHistory *>(this);
        AddRef();
        return true;
    case 0x456b8f1d:
        *out = static_cast<cIGZPersistResource *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSFamilyHistory::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

cGZPersistResourceKey cTSFamilyHistory::GetKey()
{
    cGZPersistResourceKey key;
    std::memcpy(&key, mRecordKey, sizeof(key));
    return key;
}

bool cTSFamilyHistory::SetKey(const cGZPersistResourceKey &key)
{
    std::memcpy(mRecordKey, &key, sizeof(mRecordKey));
    return true;
}

namespace {
// The header row's column titles (each printed with "%s\t"); "Cheats Used\t" ends the row as its own format string.
const char *const kColumnTitles[] = {
    "Lot#", "#Members", "#Adult Males", "#Adult Females", "#Kid Males", "#Kid Females", "#Workers", "#Objects",
    "Objects Value", "Architecture Value", "House Square Feet", "House Size", "$Income", "$Bills", "$Food", "$Maintenance",
    "$Objects", "$Misc", "$Arch", "$Funds", "#Friends", "Max Aspiration Score", "Min Aspiration Score", "Aging Turned On",
    "Free Will Turned On", "On Community Lot",
};
} // namespace

void cTSFamilyHistory::WriteText(cIGZString *path)
{
    FILE *file = sOpen(path->ToChar(), "w");
    if (!file) return;
    for (int row = -1; row < int(mEntries.size()); ++row) {
        if (row == -1) {
            for (const char *title : kColumnTitles) sPrint(file, "%s\t", title);
            sPrint(file, "Cheats Used\t");
        } else {
            const FamilyHistoryEntry &entry = mEntries[row];
            for (int16_t word : entry.words) sPrint(file, "%d\t", int(word));
            sPrint(file, "%d\t", entry.value10);
            sPrint(file, "%d\t", entry.value14);
            sPrint(file, "%d\t", int(entry.word18));
            sPrint(file, "%d\t", int(entry.word1A));
            for (int32_t value : entry.values1C) sPrint(file, "%d\t", value);
            sPrint(file, "%d\t", entry.value34);
            sPrint(file, "%d\t", entry.value38);
            sPrint(file, "%d\t", int(entry.word50));
            sPrint(file, "%d\t", int(entry.word3C));
            sPrint(file, "%d\t", int(entry.word3E));
            sPrint(file, "%d\t", unsigned(entry.flag40));
            sPrint(file, "%d\t", unsigned(entry.flag41));
            sPrint(file, "%d\t", unsigned(entry.flag42));
            std::string texts;
            for (const FamilyHistoryText &text : entry.texts) texts.append(text.value.mString + ". ");
            sPrint(file, "%s\t", texts.c_str());
        }
        sPrint(file, "\n");
    }
    sClose(file);
}

bool cTSFamilyHistory::Write(ifaces::cIGZOStream *out)
{
    void *stream = out;
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(kVersion);
    StreamCall<void>(stream, 19, metadata);
    const uint32_t classId = GetGZCLSID();
    StreamCall<uint8_t>(stream, 9, classId);
    StreamCall<uint8_t>(stream, 9, kVersion);
    StreamCall<uint8_t>(stream, 9, uint32_t(0));
    StreamCall<uint8_t>(stream, 5, uint8_t(0));
    const uint32_t count = uint32_t(mEntries.size());
    StreamCall<uint8_t>(stream, 9, count);
    for (uint32_t i = 0; i < count; ++i) mEntries[i].Write(stream);
    return StreamFailed(stream) == 0;
}

bool cTSFamilyHistory::Read(ifaces::cIGZIStream *in)
{
    void *stream = in;
    // Failed reads leave these as they were (stack words in the original): they start at zero.
    uint32_t classId = 0, version = 0, spare = 0, count = 0;
    uint8_t unusedFlag = 0;
    StreamCall<uint8_t>(stream, 9, &classId);
    StreamCall<uint8_t>(stream, 9, &version);
    StreamCall<uint8_t>(stream, 9, &spare);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(version);
    StreamCall<void>(stream, 19, metadata);
    StreamCall<uint8_t>(stream, 5, &unusedFlag);
    StreamCall<uint8_t>(stream, 9, &count);
    mEntries.clear();
    {
        const FamilyHistoryEntry blank;
        mEntries.insert(mEntries.begin(), count, blank);
    }
    for (uint32_t i = 0; i < count; ++i) mEntries[i].Read(stream);
    return StreamFailed(stream) == 0;
}

uint32_t cTSFamilyHistory::GetGZCLSID()
{
    return kClassId;
}

void cTSFamilyHistory::AddEntry(const FamilyHistoryEntry &entry)
{
    while (mEntries.size() > kMaxEntries - 1) mEntries.erase(mEntries.begin());
    FamilyHistoryEntry copy(entry);
    mEntries.push_back(std::move(copy));
}

void cTSFamilyHistory::GetEntries(FamilyHistoryEntryServiceVector *out)
{
    for (const auto &entry : mEntries) {
        // ORIGINAL-BUG: the original copies each entry's texts into a temporary it never uses (an allocation and a
        // free through the allocator service per entry with texts).
        const FamilyHistoryTextVector unused(entry.texts);
        out->push_back(entry);
    }
}

// ---- adapters (the RVA hooks) ----

using Adapters = cTSFamilyHistoryAdapters;

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

bool __fastcall Adapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}

bool __fastcall Adapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}

uint32_t __fastcall Adapters::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}

void __fastcall Adapters::AddEntry(Self *self, void *, const FamilyHistoryEntry &entry)
{
    [[msvc::forceinline_calls]] return self->Self::AddEntry(entry);
}

void __fastcall Adapters::GetEntries(Self *self, void *, FamilyHistoryEntryServiceVector *out)
{
    [[msvc::forceinline_calls]] return self->Self::GetEntries(out);
}

void __fastcall Adapters::WriteText(Self *self, void *, cIGZString *path)
{
    [[msvc::forceinline_calls]] return self->Self::WriteText(path);
}

// 0x49ba80: the scalar deleting destructor (the destructor is inline in the original too).
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
