// cRZIniFile's file access and indexing (slots 5, 17-19), the enumerations (13, 14) and the binary values (8, 9,
// 12) with their hex helpers. Lane LZ (F4c). See cRZIniFile.h for the layout and cRZIniFileTables.h for the
// tables; the read (slot 6) is in cRZIniFileRead.size.cpp.
#include "rz/cRZIniFile.h"

#include "app/ReportBlockers.h" // AssignText
#include "game/vtable.h"
#include "ifaces/cIGZFile.h"
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "rz/cRZIniFileTables.h"
#include "rz/cRZStringSub.h"
#include "rz/cRZThread.h"

#include <cstring>
#include <ctime>
#include <list>
#include <new>
#include <utility>
#include <vector>

namespace {

using rzini::IniKey;
using rzini::SecondTable;
using rzini::SectionTable;

// This object's own slots, called through its vtable like the original (subclasses override some).
constexpr int kSlotClear = 5;
constexpr int kSlotGetString = 6;
constexpr int kSlotSetString = 10;
constexpr int kSlotLoad = 17;
constexpr int kSlotReadLine = 18;
constexpr int kSlotOpen = 19;
using ClearFn = bool(__thiscall *)(void *);
using GetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, cIGZString *);
using SetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, const cIGZString *);
using LoadFn = bool(__thiscall *)(void *, bool);
using ReadLineFn = bool(__thiscall *)(void *, cIGZString *);
using OpenFn = bool(__thiscall *)(void *, bool);

// cIGZFile open modes: read, or read and write (access); open existing, or open always (creation); reading
// shares, writing does not.
constexpr uint32_t kAccessRead = 1;
constexpr uint32_t kAccessReadWrite = 3;
constexpr uint32_t kCreationOpenExisting = 2;
constexpr uint32_t kCreationOpenAlways = 3;
constexpr uint32_t kShareWhenReading = 1;
constexpr uint32_t kShareNone = 0;

constexpr uint32_t kOpenRetrySleep = 250000; // microseconds
constexpr int32_t kOpenRetrySeconds = 2;

constexpr uint32_t kLineChunk = 0x28;
constexpr size_t kLineBufferSize = 0x2a;

const cRZString &AsRZ(const cIGZString &s)
{
    return *reinterpret_cast<const cRZString *>(&s);
}

ifaces::cIGZFile *File(const cRZIniFile *self)
{
    return As<ifaces::cIGZFile>(self->mFile);
}

SectionTable &Sections(cRZIniFile *self)
{
    return *reinterpret_cast<SectionTable *>(self->mEntries);
}

SecondTable &Names(cRZIniFile *self)
{
    return *reinterpret_cast<SecondTable *>(self->mSecondTable);
}

void ClearIfDisabled(cRZIniFile *self)
{
    if (!self->mEnabled) VirtualCall<ClearFn, kSlotClear>(self);
}

// Loaded already, or loads now (slot 17, for reading); a failed load clears a disabled object.
bool EnsureLoaded(cRZIniFile *self)
{
    if (self->mLoaded || VirtualCall<LoadFn, kSlotLoad>(self, false)) return true;
    ClearIfDisabled(self);
    return false;
}

bool NextLine(cRZIniFile *self, cRZString &line)
{
    return VirtualCall<ReadLineFn, kSlotReadLine>(self, line.AsInterface());
}

bool IsLineBreak(char c)
{
    return c == '\r' || c == '\n';
}

// A key and its value, as slot 13 collects them. Copied, never moved (like the original's pair of cRZStrings,
// also when the vector grows).
struct KeyValue {
    IniKey key;
    IniKey value;
    KeyValue(const cRZString &k, const cRZString &v) : key(k), value(v) {}
    KeyValue(const KeyValue &) = default;
    KeyValue &operator=(const KeyValue &) = delete;
};
static_assert(sizeof(KeyValue) == 0x40, "two cRZStrings");

} // namespace

// 0x1afb5e.
bool __cdecl IniHexDecode(const char *text, uint32_t length, uint8_t *out)
{
    bool ok = true;
    const char *end = text + length;
    while (text < end) {
        *out = 0;
        uint8_t value = 0;
        for (int shift = 4; shift >= 0; shift -= 4) {
            const signed char c = static_cast<signed char>(*text++);
            uint8_t digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<uint8_t>(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<uint8_t>(c - 'A' + 10);
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<uint8_t>(c - 'a' + 10);
            } else {
                digit = 0;
                ok = false;
            }
            value = static_cast<uint8_t>((digit << shift) + value);
            *out = value;
        }
        ++out;
    }
    return ok;
}

// 0x1afbd7.
void __cdecl IniHexEncode(const uint8_t *data, uint32_t length, char *out)
{
    static const char kDigits[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < length; ++i) {
        *out++ = kDigits[data[i] >> 4];
        *out++ = kDigits[data[i] & 0xf];
    }
    *out = '0';
}

// 0x9c5ecb. The second table is left as it is.
bool cRZIniFile::Clear()
{
    if (File(this)->IsOpen()) File(this)->Close();
    Sections(this).clear();
    mLoaded = false;
    return true;
}

// 0x9c65f1.
bool cRZIniFile::Open(bool write)
{
    const uint32_t access = write ? kAccessReadWrite : kAccessRead;
    const uint32_t creation = write ? kCreationOpenAlways : kCreationOpenExisting;
    const uint32_t sharing = write ? kShareNone : kShareWhenReading;
    if (File(this)->IsOpen()) {
        uint32_t openAccess = 0;
        uint32_t openCreation = 0;
        uint32_t openSharing = 0;
        File(this)->GetOpenModes(&openAccess, &openCreation, &openSharing);
        if (openAccess == access) return true;
    }
    const uint32_t start = static_cast<uint32_t>(_time64(nullptr));
    for (;;) {
        if (File(this)->Open(access, creation, sharing)) {
            mOpenForWrite = write;
            return true;
        }
        if (!File(this)->Exists()) return false;
        const uint32_t now = static_cast<uint32_t>(_time64(nullptr));
        cRZThread::SleepMicroseconds(kOpenRetrySleep);
        if (static_cast<int32_t>(now - start) > kOpenRetrySeconds) return false;
    }
}

// 0x9c623d.
bool cRZIniFile::ReadLine(cIGZString *outArg)
{
    cRZString *out = reinterpret_cast<cRZString *>(outArg);
    std::string text;
    bool readAny = false;
    bool ended = false;
    int32_t start = File(this)->Position();
    uint32_t remaining = File(this)->Length() - static_cast<uint32_t>(start);
    out->mString.resize(0);
    char chunk[kLineBufferSize];
    std::memset(chunk, 0, sizeof(chunk));
    while (remaining != 0) {
        const uint32_t count = remaining < kLineChunk ? remaining : kLineChunk;
        if (!File(this)->Read(chunk, count)) break;
        remaining -= count;
        readAny = true;
        uint32_t i = 0;
        for (; i < count; ++i) {
            if (chunk[i] == 0) {
                chunk[i] = ' ';
            } else if (IsLineBreak(chunk[i])) {
                chunk[i] = 0;
                ended = true;
                // Back to the break, then past it and every break after it (blank lines are skipped).
                File(this)->SeekToPosition(start + static_cast<int32_t>(i));
                char next;
                while (File(this)->Read(&next, 1)) {
                    if (!IsLineBreak(next)) {
                        File(this)->SeekToRelativePosition(-1);
                        break;
                    }
                }
                break;
            }
        }
        chunk[i] = 0;
        text.append(chunk, std::strlen(chunk));
        if (ended) break;
        start = File(this)->Position();
    }
    appbase::AssignText(&text, out);
    return readAny;
}

// 0x9c63da.
bool cRZIniFile::Load(bool write)
{
    Sections(this).clear();
    if (!File(this)->IsOpen()) {
        File(this)->SetPath(mPath.AsInterface());
        if (!VirtualCall<OpenFn, kSlotOpen>(this, write)) return false;
    }
    cRZString line;
    line.Construct();
    cRZString written;
    written.Construct();
    cRZString name;
    name.Construct();
    File(this)->SeekToBegin();
    uint32_t position = static_cast<uint32_t>(File(this)->Position());
    while (NextLine(this, line)) {
        reinterpret_cast<cRZIniStringHelpers *>(&line)->TrimLeading();
        const std::string &text = line.mString;
        if (!text.empty() && text[0] == '[') {
            for (uint32_t close = 1; close < text.size(); ++close) {
                if (text[close] != ']') continue;
                if (close >= 2) {
                    // The name between the brackets, as written and lowercased.
                    cRZString rest;
                    Sub(line).Mid(&rest, 1, 0xffffffff);
                    written.mString.assign(rest.ToChar(), close - 1);
                    rest.Destruct();
                    if (&name.mString != &written.mString) name.mString.assign(written.mString.c_str(), written.mString.size());
                    reinterpret_cast<cRZStringHelpers *>(&name)->LowerCase();
                    {
                        std::pair<IniKey, uint32_t> entry{IniKey(name), position};
                        Sections(this).emplace(std::move(entry));
                    }
                    {
                        std::pair<IniKey, IniKey> entry{IniKey(name), IniKey(written)};
                        Names(this).emplace(std::move(entry));
                    }
                }
                break;
            }
        }
        position = static_cast<uint32_t>(File(this)->Position());
    }
    mLoaded = true;
    name.Destruct();
    written.Destruct();
    line.Destruct();
    return true;
}

// 0x9c66b2. A text shorter than 2 * size decodes only what it has (an odd length reads its terminator, which is
// not hex: the bytes before it are written and the result is 0).
uint32_t cRZIniFile::GetBinary(const cIGZString &section, const cIGZString &key, void *out, uint32_t size)
{
    cRZString text;
    text.Construct();
    uint32_t result = 0;
    if (VirtualCall<GetStringFn, kSlotGetString>(this, &section, &key, text.AsInterface())) {
        uint32_t length = size + size;
        if (length > text.mString.size()) length = static_cast<uint32_t>(text.mString.size());
        if (IniHexDecode(text.ToChar(), length, static_cast<uint8_t *>(out))) result = length >> 1;
    }
    text.Destruct();
    return result;
}

// 0x9c6722.
uint32_t cRZIniFile::GetBinaryAlloc(const cIGZString &section, const cIGZString &key, void **out)
{
    cRZString text;
    text.Construct();
    uint32_t result;
    if (VirtualCall<GetStringFn, kSlotGetString>(this, &section, &key, text.AsInterface())) {
        const uint32_t length = static_cast<uint32_t>(text.mString.size());
        auto *block = static_cast<uint8_t *>(::operator new[](length >> 1));
        *out = block;
        // ORIGINAL-BUG (not reproduced): an odd length makes the original decode one byte past the block (the
        // terminator's pair, which then fails). The failure is the same here without the write.
        if ((length & 1) == 0 && IniHexDecode(text.ToChar(), length, block)) {
            result = length >> 1;
        } else {
            ::operator delete[](*out);
            *out = nullptr;
            result = 0;
        }
    } else {
        result = static_cast<uint32_t>(text.mString.size()) >> 1;
    }
    text.Destruct();
    return result;
}

// 0x9c6b28. Reads the names' lengths as cRZStrings, like the original.
bool cRZIniFile::SetBinary(const cIGZString &section, const cIGZString &key, const void *data, uint32_t size)
{
    if (AsRZ(section).mString.empty() || AsRZ(key).mString.empty()) return false;
    char *hex = static_cast<char *>(::operator new[](size * 2 + 1));
    IniHexEncode(static_cast<const uint8_t *>(data), size, hex);
    hex[size * 2] = 0;
    cRZString text;
    text.Construct(hex);
    const bool result = VirtualCall<SetStringFn, kSlotSetString>(this, &section, &key, text.AsInterface());
    text.Destruct();
    ::operator delete[](hex);
    return result;
}

// 0x9c5ef2. Keys keep their case; lines without a key or a value are skipped. A section whose header line cannot
// be read gives false.
bool cRZIniFile::ForEachKey(const cIGZString &section, KeyCallback callback, void *context)
{
    if (!EnsureLoaded(this)) return false;
    cRZString lower;
    lower.Construct(AsRZ(section), 0, 0xffffffff);
    reinterpret_cast<cRZStringHelpers *>(&lower)->LowerCase();
    bool result = false;
    SectionTable &sections = Sections(this);
    auto found = sections.find(*reinterpret_cast<const IniKey *>(&lower));
    if (found == sections.end() ||
        static_cast<uint32_t>(File(this)->SeekToPosition(static_cast<int32_t>(found->second))) >= File(this)->Length()) {
        ClearIfDisabled(this);
    } else {
        cRZString key;
        key.Construct();
        cRZString line;
        line.Construct();
        if (!NextLine(this, line)) { // the section's own header line
            ClearIfDisabled(this);
        } else {
            std::vector<KeyValue> pairs;
            auto *helpers = reinterpret_cast<cRZIniStringHelpers *>(&line);
            while (NextLine(this, line)) {
                helpers->TrimLeading();
                if (line.mString.empty() || line.mString[0] == ';') continue;
                if (line.mString[0] == '[') break;
                cRZString prefix;
                helpers->SplitOff(&prefix, "=");
                if (&key.mString != &prefix.mString) key.mString.assign(prefix.mString.c_str(), prefix.mString.size());
                prefix.Destruct();
                reinterpret_cast<cRZIniStringHelpers *>(&key)->TrimTrailing();
                helpers->TrimBoth();
                if (key.mString.empty() || line.mString.empty()) continue;
                const KeyValue entry(key, line);
                pairs.push_back(entry);
            }
            for (const KeyValue &entry : pairs) callback(entry.key.key.AsInterface(), entry.value.key.AsInterface(), context);
            ClearIfDisabled(this);
            result = true;
        }
        line.Destruct();
        key.Destruct();
    }
    lower.Destruct();
    return result;
}

// 0x9c615f. Each name is the one as written when the second table has it (it always does after a load).
bool cRZIniFile::ForEachSection(SectionCallback callback, void *context)
{
    if (!EnsureLoaded(this)) return false;
    cRZString unused;
    unused.Construct();
    {
        std::list<IniKey> names;
        SecondTable &written = Names(this);
        for (const auto &entry : Sections(this)) {
            auto found = written.find(entry.first);
            names.push_back(found == written.end() ? entry.first : found->second);
        }
        for (const IniKey &name : names) callback(name.key.AsInterface(), unused.AsInterface(), context);
        ClearIfDisabled(this);
    }
    unused.Destruct();
    return true;
}
