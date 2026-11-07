// cTSTradeableRewardsService (lane KZ). Layout and slots: src/ts/cTSTradeableRewardsService.h.
#include "rz/RZMessageServer.h"
#include "ts/cTSTradeableRewardsService.h"

#include "app/AppInit.h"      // CommonImplSlot4, CommonImplSlot5
#include "app/AppServices2.h" // DirectoryExists
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"       // ConvertBytesToString
#include "gz/cGZAny.h"      // GZString
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistDBSegment.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cITSStringSet.h"
#include "rz/RZMemory.h"
#include "rz/RZTextConvert.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZIniFile.h"       // cRZIniStringHelpers::TrimBoth
#include "rz/cRZRandom.h"
#include "ts/cTSString.h"

#include <list>
#include <new>

namespace {

// The persist system (service 0x056b906e, cITSPersistSystem 0x9af5e924) from its cached getter (0xb390c0).
ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

const Key16 *AsKey16(const cGZPersistResourceKey *key) { return reinterpret_cast<const Key16 *>(key); }

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

using tradeable_rewards::CodeAlphabet;
using tradeable_rewards::DecodeCode;
using tradeable_rewards::EncodeCode;

// The XML resources Load reads: the reward lists in the directories of the app's directory list kRewardDirectories (files
// "*.xml" typed kRewardsType in group kRewardsGroup, AddDirectorySegments 0xcb8730), and the reward list's record.
constexpr uint32_t kRewardDirectories = 0xb19c133d;
constexpr uint32_t kRewardsType = 0x7181c501;
constexpr uint32_t kRewardsGroup = 0xf19c3e5b;
constexpr uint32_t kRewardsInstance = 0x319c0fcc;
// The loose-file segment AddDirectorySegments makes per directory (COM class, interface), and the directory list's type.
constexpr uint32_t kLooseSegmentClass = 0xa079ce1b;
constexpr uint32_t kLooseSegmentIid = 0x856c8cf7;
using GZDirectoryList = std::list<GZString, GZServiceAllocator<GZString>>; // AllocateListNodes 0x1e7190
// The "kissingpoints" package (CommonImplSlot4's path option, the segment interface and its name).
constexpr uint32_t kPathOption = 0xf1a54fd7;
constexpr uint32_t kSegmentIid = 0x3b2271bb;
const char kSegmentName[] = "kissingpoints";
// The string set the framework makes when the table is missing (class, interface).
constexpr uint32_t kStringSetClass = 0xdb4cb0f9;
// The service Init asks the framework for (and drops at once).
constexpr uint32_t kInitServiceId = 0x2a4915ea;
constexpr uint32_t kInitServiceIid = 0x4a490e00;
// The STR# text of a reward's name.
constexpr uint32_t kTextType = 0x2026960b;

} // namespace

namespace tradeable_rewards {

bool TextToWide(const cIGZString *src, wchar_t *out, uint32_t max)
{
    if (!out) return false;
    const uint32_t length = src->Strlen();
    const char *data = src->Data();
    ContentString text;
    text.s.mString.assign(data, length);
    reinterpret_cast<cRZIniStringHelpers *>(&text.s)->TrimBoth();
    uint32_t count = static_cast<uint32_t>(text.s.mString.size());
    if (max < count) count = max;
    RZText::ConvertString(*text.Interface(), out, &count, RZText::kUtf16);
    out[count] = 0;
    return true;
}

bool WideToText(const wchar_t *chars, uint32_t length, cIGZString *out)
{
    if (!chars) return false;
    ContentString text;
    ConvertBytesToString(chars, length, RZText::kUtf16, text.Interface());
    reinterpret_cast<cRZIniStringHelpers *>(&text.s)->TrimBoth();
    *out = *text.Interface();
    return true;
}

// ---- the reward code coder ----

namespace {

// Digits but 5 and 0, capitals but O and S, those four, the lower case letters (62); the accented letters (41); punctuation
// (25).
constexpr wchar_t kAlphabet[kAlphabetLength + 1] =
    L"12346789ABCDEFGHIJKLMNPQRTUVWXYZO05Sabcdefghijklmnopqrstuvwxyz"
    L"\x00c0\x00c1\x00c4\x00c6\x00c8\x00c9\x00cc\x00cd\x00d1\x00d2\x00d3\x00d6\x00d9\x00da\x00dc\x0152"
    L"\x00e0\x00e1\x00e2\x00e4\x00e6\x00e7\x00e8\x00e9\x00ea\x00eb\x00ec\x00ed\x00ee\x00ef\x00f1\x00f2\x00f3\x00f4\x00f6"
    L"\x00f9\x00fa\x00fb\x00fc\x00df\x0153"
    L" -.!@#~%^&*()+=:;,\\/?'\"{}";
static_assert(sizeof(kAlphabet) == (kAlphabetLength + 1) * sizeof(wchar_t), "128 characters and the terminator");

constexpr uint32_t kCodeValues = 19;  // values (characters) of a code
constexpr uint32_t kCodeChars = 8;    // the text's characters
constexpr uint32_t kIdWord = 8;       // the reward id's word
constexpr uint32_t kChecksumWord = 9; // the checksum's four words (9..12)
constexpr uint32_t kTailWord = 10;    // the words spread over values 14..18
constexpr uint32_t kTailValue = 14;
constexpr int16_t kMaxId = 0x32;

struct CrcTable {
    uint32_t entries[256];
    constexpr CrcTable() : entries()
    {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            entries[i] = c;
        }
    }
};
constexpr CrcTable kCrcTable;

} // namespace

void __cdecl CodeAlphabet(wchar_t *out) { std::memcpy(out, kAlphabet, sizeof(kAlphabet)); }

uint16_t __cdecl AlphabetIndex(const wchar_t *alphabet, wchar_t ch)
{
    const wchar_t wanted = ch == L'_' ? L' ' : ch;
    for (uint16_t i = 0; i < kAlphabetLength; ++i) {
        if (alphabet[i] == wanted) return i;
    }
    return 0;
}

uint32_t __cdecl CodeChecksum(const void *bytes, uint32_t count)
{
    const auto *p = static_cast<const uint8_t *>(bytes);
    uint32_t crc = UINT32_MAX;
    for (; count; --count) crc = (crc >> 8) ^ kCrcTable.entries[(crc ^ *p++) & 0xff];
    return ~crc;
}

void ScrambleCode(uint16_t *values, uint32_t tailBits)
{
    uint16_t scrambled[kCodeValues] = {};
    for (uint32_t row = 0; row < 2; ++row) {
        for (uint32_t bit = 0; bit < 7; ++bit) {
            for (uint32_t k = 0; k < 5; ++k) {
                if (values[row * 5 + k] >> bit & 1) scrambled[row * 7 + bit] |= uint16_t(1u << k);
            }
        }
    }
    for (uint32_t u = 0; u < 24; ++u) {
        if (tailBits >> u & 1) scrambled[kTailValue + u % 5] |= uint16_t(1u << (u / 5));
    }
    for (uint32_t i = 0; i < kCodeValues; ++i) values[i] = scrambled[i] & 0xff;
}

bool __cdecl UnscrambleCode(uint16_t *values)
{
    uint8_t words[kCodeValues] = {};
    for (uint32_t row = 0; row < 2; ++row) {
        for (uint32_t bit = 0; bit < 7; ++bit) {
            for (uint32_t k = 0; k < 5; ++k) {
                if (values[row * 7 + bit] >> k & 1) words[row * 5 + k] |= uint8_t(1u << bit);
            }
        }
    }
    uint32_t tail = 0;
    for (uint32_t u = 0; u < 24; ++u) {
        if (values[kTailValue + u % 5] >> (u / 5) & 1) tail |= 1u << u;
    }
    words[kTailWord] = uint8_t(tail & 0x7f);
    words[kTailWord + 1] = uint8_t(tail >> 8 & 0x7f);
    words[kTailWord + 2] = uint8_t(tail >> 16 & 0x7f);
    for (uint32_t i = 0; i < kCodeValues; ++i) values[i] = words[i];
    return true;
}

bool __cdecl DecodeCode(const wchar_t *code, int16_t *id, wchar_t *chars)
{
    if (std::char_traits<wchar_t>::length(code) != kCodeValues) return false;
    wchar_t alphabet[256];
    CodeAlphabet(alphabet);
    uint16_t values[kCodeValues];
    for (uint32_t i = 0; i < kCodeValues; ++i) values[i] = AlphabetIndex(alphabet, code[i]);
    if (!UnscrambleCode(values)) return false;
    const uint32_t crc = CodeChecksum(values, 9);
    if (uint8_t(values[kChecksumWord]) != (crc >> 24 & 0x7f) || uint8_t(values[kChecksumWord + 1]) != (crc >> 16 & 0x7f) ||
        uint8_t(values[kChecksumWord + 2]) != (crc >> 8 & 0x7f) || uint8_t(values[kChecksumWord + 3]) != (crc & 0x7f))
        return false;
    for (uint32_t i = 0; i < kCodeChars; ++i) chars[i] = alphabet[values[i]];
    chars[kCodeChars] = 0;
    *id = int16_t(values[kIdWord]);
    // Trailing spaces and underscores cut (none left: nothing written).
    int32_t last = int32_t(std::char_traits<wchar_t>::length(chars)) - 1;
    while (last >= 0 && (chars[last] == L'_' || chars[last] == L' ')) --last;
    if (last >= 0 && uint32_t(last + 1) <= kCodeChars) chars[last + 1] = 0;
    return uint16_t(*id) <= uint16_t(kMaxId);
}

bool __cdecl EncodeCode(wchar_t *out, int32_t id, const wchar_t *chars)
{
    const size_t length = std::char_traits<wchar_t>::length(chars);
    const int16_t shortId = int16_t(id);
    if (length > kCodeChars || shortId > kMaxId) return false;
    wchar_t alphabet[256];
    CodeAlphabet(alphabet);
    uint16_t values[kCodeValues + 1];
    for (size_t i = 0; i < length; ++i) values[i] = AlphabetIndex(alphabet, chars[i]);
    if (length < kCodeChars) {
        const uint16_t space = AlphabetIndex(alphabet, L' ');
        for (size_t i = length; i < kCodeChars; ++i) values[i] = space;
    }
    values[kIdWord] = uint16_t(shortId);
    const uint32_t crc = CodeChecksum(values, 9);
    values[kChecksumWord] = uint16_t(crc >> 24 & 0x7f);
    values[kChecksumWord + 1] = uint16_t(crc >> 16 & 0x7f);
    values[kChecksumWord + 2] = uint16_t(crc >> 8 & 0x7f);
    values[kChecksumWord + 3] = uint16_t(crc & 0x7f);
    for (uint32_t i = kChecksumWord + 4; i < kCodeValues; ++i) values[i] = 0;
    // ORIGINAL-BUG: the scrambler 0xd09e80 should spread words 10..12 (the checksum's three low bytes) over values 14..18;
    // it reads those three bytes from its caller's frame at a fixed offset instead, which here is the low bytes of the
    // alphabet's characters 43..45 ("hij"). So the code carries "hij" where the checksum belongs, and the decoder rejects
    // it unless the checksum's low bytes are those.
    const uint32_t tail = uint32_t(alphabet[43] & 0xff) | uint32_t(alphabet[44] & 0xff) << 8 | uint32_t(alphabet[45] & 0xff) << 16;
    ScrambleCode(values, tail);
    for (uint32_t i = 0; i < kCodeValues; ++i) out[i] = alphabet[values[i]];
    out[kCodeValues] = 0;
    return true;
}

} // namespace tradeable_rewards

using tradeable_rewards::TextToWide;
using tradeable_rewards::WideToText;

// ---- cTSTradeableRewardInfo ----

// 0xcb72a0
cTSTradeableRewardInfo::cTSTradeableRewardInfo(int32_t id, uint32_t value, const cRZString &name, const cRZString &code)
    : mId(id), mValue(value), mName(name), mCode(code)
{
}

// 0xcb76b0: the code, then the name, then the cRZUnknown part.
cTSTradeableRewardInfo::~cTSTradeableRewardInfo() = default;

// 0xcb82d0: cITSTradeableRewardInfo -> +0 (counted through slot 1), else cRZUnknown's.
bool cTSTradeableRewardInfo::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZUnknown::QueryInterface(riid, out);
    *out = static_cast<cITSTradeableRewardInfo *>(this);
    AddRef();
    return true;
}

uint32_t cTSTradeableRewardInfo::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cTSTradeableRewardInfo::Release() { return rz::cRZUnknown::Release(); }

// 0xcb8ae0: the code's operator= (its slot 16).
cIGZString *cTSTradeableRewardInfo::SetCode(const cIGZString &code) { return &(*mCode.Interface() = code); }

// ---- cTSTradeableRewardsService ----

// 0xcb73b0: the base part (id 0x51817c0d, priority 0), no rewards, an empty key map, no table, not loaded.
cTSTradeableRewardsService::cTSTradeableRewardsService() : rz::cRZSystemService(kServiceId, kPriority), mLoaded(0) {}

// 0xcb7540: the table released, the key map and the rewards destroyed (the members' destructors), then the base part's.
cTSTradeableRewardsService::~cTSTradeableRewardsService() noexcept = default;

// 0xcb7b50
cTSTradeableRewardsService *__cdecl cTSTradeableRewardsService::Create() { return new (std::nothrow) cTSTradeableRewardsService; }

// ---- cIGZUnknown ----

// 0xcb8300: cITSTradeableRewardsService -> +0x1c (counted through the primary face's AddRef), else the service's.
bool cTSTradeableRewardsService::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSTradeableRewardsService *>(this);
    AddRef();
    return true;
}

uint32_t cTSTradeableRewardsService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSTradeableRewardsService::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0xcb7e60: the framework asked for the service 0x2a4915ea (interface 0x4a490e00), which is released at once; true.
bool cTSTradeableRewardsService::Init()
{
    if (!rz::cRZSystemService::Init()) return false;
    ScopedRef held{nullptr};
    if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kInitServiceId, kInitServiceIid, &held.mObject);
    held.ReleaseObject();
    return true;
}

// 0xcb8b30: the rewards and the key map emptied, the table dropped (nulled before its release), not loaded; the base's
// Shutdown.
bool cTSTradeableRewardsService::Shutdown()
{
    mEntries.clear();
    mKeyMap.clear();
    mStrings.AsPPVoid();
    mLoaded = 0;
    return rz::cRZSystemService::Shutdown();
}

// ---- cITSTradeableRewardsService ----

uint32_t cTSTradeableRewardsService::GetRewards(TradeableRewardList *out, uint32_t mask)
{
    Load();
    for (TradeableRewardEntry &entry : mEntries) {
        if (!(entry.mFlags & mask)) continue;
        cTSString name;
        name.Construct(entry.mTextTable, entry.mTextEntry, entry.mTextGroup, kTextType);
        cTSTradeableRewardInfo *info = new (std::nothrow) cTSTradeableRewardInfo(entry.mId, entry.mValue, name.mString, entry.mCode.s);
        {
            const TradeableRewardRef ref(info);
            out->push_back(ref);
        }
        name.Destruct();
    }
    return static_cast<uint32_t>(out->size());
}

bool cTSTradeableRewardsService::SetRewardCode(int32_t id, const cIGZString *code)
{
    Load();
    TradeableRewardEntry *entry = Find(id);
    if (!entry) return false;
    *entry->mCode.Interface() = *code;
    SaveCode(id);
    return true;
}

bool cTSTradeableRewardsService::RedeemCode(const cIGZString *code)
{
    Load();
    wchar_t text[kCodeLength + 1];
    TextToWide(code, text, kCodeLength);
    int16_t id;
    wchar_t chars[10];
    if (!DecodeCode(text, &id, chars) || !Unlock(id)) return false;
    TradeableRewardEntry *entry = Find(id);
    if (!entry) return false;
    WideToText(chars, 8, entry->mCode.Interface());
    AppendCode(code);
    return true;
}

int32_t cTSTradeableRewardsService::UnlockRandomReward()
{
    Load();
    std::vector<int32_t> ids;
    for (const TradeableRewardEntry &entry : mEntries) {
        if (IsLocked(entry.mId) && !(entry.mFlags & TradeableRewardEntry::kNoRandom)) ids.push_back(entry.mId);
    }
    const int32_t count = static_cast<int32_t>(ids.size());
    if (count > 0) {
        cRZRandom random;
        random.ConstructAt(UINT32_MAX, 0xcb8d27);
        const int32_t index = static_cast<int32_t>(random.RandomUint32Uniform(static_cast<uint32_t>(count - 1)));
        if (index >= 0 && index < count) {
            const int32_t id = ids[index];
            TradeableRewardEntry *entry;
            if (Unlock(id) && (entry = Find(id)) != nullptr) {
                ContentString code;
                if (RewardsService()->MakeCode(code.Interface(), id, entry->mCode.Interface())) AppendCode(code.Interface());
                return id;
            }
        }
    }
    return -1;
}

bool cTSTradeableRewardsService::MakeCode(cIGZString *out, int32_t id, const cIGZString *text)
{
    Load();
    if (!Find(id)) return false;
    wchar_t chars[10];
    TextToWide(text, chars, 8);
    wchar_t code[20];
    if (!EncodeCode(code, id, chars)) return false;
    WideToText(code, kCodeLength, out);
    return true;
}

bool cTSTradeableRewardsService::IsMappedValue(const cGZPersistResourceKey *key)
{
    Load();
    for (const auto &pair : mKeyMap) {
        const cGZPersistResourceKey &value = pair.second;
        if (value.type == key->type && value.group == key->group && value.instance == key->instance &&
            value.resource == key->resource)
            return true;
    }
    return false;
}

bool cTSTradeableRewardsService::IsMappedKey(const cGZPersistResourceKey *key)
{
    Load();
    return mKeyMap.find(*key) != mKeyMap.end();
}

bool cTSTradeableRewardsService::IsValidCodeText(const cIGZString *text)
{
    wchar_t chars[10];
    TextToWide(text, chars, 8);
    wchar_t alphabet[256];
    CodeAlphabet(alphabet);
    const size_t alphabetLength = std::char_traits<wchar_t>::length(alphabet);
    const size_t length = std::char_traits<wchar_t>::length(chars);
    for (size_t i = 0; i < length; ++i) {
        size_t j = 0;
        while (j < alphabetLength && alphabet[j] != chars[i]) ++j;
        if (j == alphabetLength) return false;
    }
    return true;
}

bool cTSTradeableRewardsService::GetMappedKey(const cGZPersistResourceKey *key, cGZPersistResourceKey *out)
{
    const auto found = mKeyMap.find(*key);
    if (found != mKeyMap.end()) {
        *out = found->second;
        return true;
    }
    for (const auto &pair : mKeyMap) {
        const cGZPersistResourceKey &value = pair.second;
        if (value.type == key->type && value.group == key->group && value.instance == key->instance &&
            value.resource == key->resource) {
            *out = pair.first;
            return true;
        }
    }
    return false;
}

// 0xcb7c30
uint32_t cTSTradeableRewardsService::GetRewardCount()
{
    Load();
    return static_cast<uint32_t>(mEntries.size());
}

int32_t cTSTradeableRewardsService::GetRewardId(uint32_t index)
{
    Load();
    if (index < RewardsService()->GetRewardCount()) return mEntries.data()[index].mId;
    return -1;
}

bool cTSTradeableRewardsService::GetRewardCode(int32_t id, cIGZString *out)
{
    Load();
    TradeableRewardEntry *entry = Find(id);
    if (!entry) return false;
    *out = *entry->mCode.Interface();
    return true;
}

// ---- helpers ----

// 0xcb7c60: the first reward with the id, null for none.
TradeableRewardEntry *cTSTradeableRewardsService::Find(int32_t id)
{
    for (TradeableRewardEntry &entry : mEntries) {
        if (entry.mId == id) return &entry;
    }
    return nullptr;
}

// 0xcb8190: the reward's locked flag; false for an unknown id.
bool cTSTradeableRewardsService::IsLocked(int32_t id)
{
    Load();
    const TradeableRewardEntry *entry = Find(id);
    return entry && (entry->mFlags & TradeableRewardEntry::kLocked) != 0;
}

// 0xcb89a0: the reward's code made from its current code (slot 7, through the vtable), its eight characters decoded back
// into the reward's code, and the code stored in the table at id + 1 (language 0) and committed. False for an unknown id,
// when the code can't be made, or without a table.
bool cTSTradeableRewardsService::SaveCode(int32_t id)
{
    Load();
    TradeableRewardEntry *entry = Find(id);
    if (!entry) return false;
    ContentString code;
    bool saved = false;
    if (RewardsService()->MakeCode(code.Interface(), id, entry->mCode.Interface())) {
        wchar_t text[kCodeLength + 1];
        TextToWide(code.Interface(), text, kCodeLength);
        int16_t decodedId;
        // ORIGINAL-BUG: the decoder's result is ignored: when the code doesn't decode, the reward's code is set from the
        // uninitialised characters.
        wchar_t chars[10];
        DecodeCode(text, &decodedId, chars);
        WideToText(chars, 8, entry->mCode.Interface());
        if (mStrings) {
            mStrings->SetString(code.Interface(), static_cast<uint32_t>(id + 1), 0);
            mStrings->Commit();
            saved = true;
        }
    }
    return saved;
}

// 0xcb8ab0: the code appended to the table (language 0) and committed; false without a table.
bool cTSTradeableRewardsService::AppendCode(const cIGZString *code)
{
    if (!mStrings) return false;
    mStrings->AppendString(const_cast<cIGZString *>(code), 0);
    mStrings->Commit();
    return true;
}

// 0xcb8e00: a locked reward whose bins unlock (0xcb84c0) becomes unlocked and message 0x91a4f0d0 is posted. True whenever
// the reward exists.
bool cTSTradeableRewardsService::Unlock(int32_t id)
{
    Load();
    TradeableRewardEntry *entry = Find(id);
    if (!entry) return false;
    if (IsLocked(id) && UnlockBins(&entry->mFamilyIds, &entry->mObjectKeys)) {
        entry->mFlags = (entry->mFlags & ~TradeableRewardEntry::kLocked) | TradeableRewardEntry::kUnlocked;
        void *message = nullptr;
        if (void *memory = RZMemory::Malloc(0x2c)) message = static_cast<cRZMessage2Standard *>(memory)->Construct();
        if (message) As<ifaces::cIGZMessage2Standard>(message)->AddRef();
        // ORIGINAL-BUG: a failed allocation is not checked: SetType is called on the null message.
        As<ifaces::cIGZMessage2Standard>(message)->SetType(kUnlockMessage);
        As<ifaces::cIGZMessageServer>(GetCachedMessageServer())->MessageSend(message);
        if (message) As<ifaces::cIGZMessage2Standard>(message)->Release();
    }
    return true;
}

// ---- unlocking a reward's bins ----

namespace {

// The bin catalog (service 0x0c4f252f, cITSBinCatalog 0x2c4f2954) and the resource service (0x4bcf1bb0, 0x0bcf1b58) from
// their cached getters (0xb38e50, 0xb390a0..).
nTSCASCore::cITSBinCatalog *BinCatalog()
{
    return static_cast<nTSCASCore::cITSBinCatalog *>(CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>());
}
cIGZUnknown *ResourceService() { return static_cast<cIGZUnknown *>(CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>()); }

constexpr uint32_t kPropertiesIid = 0x8c1571d6; // a fragment's property set (the resource service's slot 37)

// A fragment's keys: its bin's (slot 19), its own (slot 21); slot 18 sets its bin's.
const cGZPersistResourceKey *FragmentBinKey(cIGZUnknown *fragment) { return RawSlot<19, const cGZPersistResourceKey *>(fragment); }
const cGZPersistResourceKey *FragmentKey(cIGZUnknown *fragment) { return RawSlot<21, const cGZPersistResourceKey *>(fragment); }

} // namespace

// 0xcb7ea0
bool cTSTradeableRewardsService::cLockedBinFilter::Accept(cIGZUnknown *bin) { return bin && (RawSlot<27>(bin) & 1) != 0; }

// 0xcb7150: the base part (dropping duplicates: unused here), the set, the resource service counted, the key map.
cTSTradeableRewardsService::cSkinFamilyIdFragmentFilter::cSkinFamilyIdFragmentFilter(const RewardBinKeys *families,
                                                                                     const RewardKeyMap *keyMap)
    : cTSSimFragmentFilterBase(true), mFamilies(families), mResourceService(ResourceService()), mKeyMap(keyMap)
{
    if (mResourceService) mResourceService->AddRef();
}

// 0xcb7650: the resource service released, then the base part.
cTSTradeableRewardsService::cSkinFamilyIdFragmentFilter::~cSkinFamilyIdFragmentFilter()
{
    if (mResourceService) mResourceService->Release();
}

// 0xcb8040
bool cTSTradeableRewardsService::cSkinFamilyIdFragmentFilter::Accept(cIGZUnknown *fragment)
{
    const cGZPersistResourceKey *key = FragmentKey(fragment);
    cRZAutoRefCount<cIGZUnknown> properties;
    // ORIGINAL-BUG: the resource service (null when its getter finds none) is used without a null test.
    if (!RawSlot<37, bool>(mResourceService, key, kPropertiesIid, properties.AsPPVoid())) return false;
    const auto *family = RawSlot<17, const RewardBinKey *>(properties.Get());
    if (mFamilies->find(*family) == mFamilies->end()) return false;
    return mKeyMap->find(*FragmentBinKey(fragment)) != mKeyMap->end();
}

// 0xcb7120
cTSTradeableRewardsService::cObjectKeyFragmentFilter::cObjectKeyFragmentFilter(const RewardObjectKeys *objects,
                                                                               const RewardKeyMap *keyMap)
    : cTSSimFragmentFilterBase(true), mObjects(objects), mKeyMap(keyMap)
{
}

// 0xcb7f80
bool cTSTradeableRewardsService::cObjectKeyFragmentFilter::Accept(cIGZUnknown *fragment)
{
    if (mObjects->find(*FragmentKey(fragment)) == mObjects->end()) return false;
    return mKeyMap->find(*FragmentBinKey(fragment)) != mKeyMap->end();
}

// 0xcb8430
bool cTSTradeableRewardsService::MoveFragments(const nTSCASCore::BinList *fragments)
{
    bool moved = false;
    nTSCASCore::cITSBinCatalog *catalog = BinCatalog();
    for (const nTSCASCore::BinRef &ref : *fragments) {
        cIGZUnknown *fragment = ref.Get();
        if (!fragment) continue;
        const auto found = mKeyMap.find(*FragmentBinKey(fragment));
        if (found == mKeyMap.end()) continue;
        RawSlot<18, void>(fragment, &found->second);
        if (catalog->SaveFragment(fragment)) moved = true;
    }
    return moved;
}

// 0xcb84c0: the catalog's fragments of the locked bins (slot 30) taken by the family filter, then by the object filter
// (each only when its set has keys), moved.
bool cTSTradeableRewardsService::UnlockBins(const RewardBinKeys *families, const RewardObjectKeys *objects)
{
    bool moved = false;
    cLockedBinFilter locked;
    nTSCASCore::cITSBinCatalog *catalog = BinCatalog();
    if (!families->empty()) {
        nTSCASCore::BinList fragments;
        cSkinFamilyIdFragmentFilter filter(families, &mKeyMap);
        RawSlot<30, void>(catalog, &fragments, static_cast<nTSCASCore::cITSBinFilter *>(&locked),
                          static_cast<nTSCASCore::cITSSimFragmentFilter *>(&filter));
        moved = MoveFragments(&fragments);
    }
    if (!objects->empty()) {
        nTSCASCore::BinList fragments;
        cObjectKeyFragmentFilter filter(objects, &mKeyMap);
        RawSlot<30, void>(catalog, &fragments, static_cast<nTSCASCore::cITSBinFilter *>(&locked),
                          static_cast<nTSCASCore::cITSSimFragmentFilter *>(&filter));
        if (MoveFragments(&fragments)) moved = true;
    }
    return moved;
}

// 0xcb8730
bool cTSTradeableRewardsService::AddDirectorySegments(cIGZUnknown *manager, uint32_t option, uint32_t type, uint32_t group,
                                                      cRZString *extension)
{
    bool added = false;
    GZDirectoryList directories;
    appinit::CommonImplSlot5(option, Word(&directories), 1, 0, 0xffffffffu);
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) { // last listed first
        cRZString path;
        path.Construct(it->c_str());
        if (as2::DirectoryExists(&path)) {
            cRZAutoRefCount<cIGZUnknown> segment;
            As<cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(kLooseSegmentClass, kLooseSegmentIid, segment.AsPPVoid());
            if (!segment) {
                path.Destruct();
                added = false;
                break;
            }
            added = true;
            cIGZUnknown *s = segment.Get();
            RawSlot<15, void>(s, 1u, 1u);
            RawSlot<16, void>(s, 1u);
            RawSlot<10, void>(s, group);
            RawSlot<3, void>(RawSlot<12, void *>(s), type, extension);
            RawSlot<4, void>(s);
            RawSlot<9, void>(s, &path);
            RawSlot<6, void>(s, 1u, 1u);
            RawSlot<27, void>(manager, RawSlot<3, void *>(s));
        }
        path.Destruct();
    }
    return added;
}

// 0xcb7790: once (with the persist system's resource manager): the reward directories' segments (0xcb8730) and the reward
// list with its key map (ParseRewards 0xcb81c0, from the record's bytes) loaded, then the redeemed codes: the STR# table 0x6f37 of the "kissingpoints" package's group. Each
// code in it that decodes unlocks its reward (flags only: the bins are not unlocked again) and sets its code. Without the
// table, a new string set is made for that key.
void cTSTradeableRewardsService::Load()
{
    if (mLoaded) return;
    // ORIGINAL-BUG: the persist system is used without a null test.
    ifaces::cITSPersistSystem *persist = PersistSystem();
    auto *manager = reinterpret_cast<ifaces::cIGZPersistResourceManager *>(persist->GetResourceManager());
    if (!manager) return;
    mLoaded = 1;
    {
        ContentString extension("xml");
        AddDirectorySegments(reinterpret_cast<cIGZUnknown *>(manager), kRewardDirectories, kRewardsType, kRewardsGroup,
                             &extension.s);
    }
    const cGZPersistResourceKey key{kRewardsType, kRewardsGroup, kRewardsInstance, 0};
    cRZAutoRefCount<ifaces::cIGZPersistDBRecord> record;
    if (!manager->OpenRecord(&key, record.AsPPVoid(), 0)) return;
    const uint32_t size = record->GetSize();
    char *bytes = new char[size];
    record->ReadBytes(bytes, size);
    ParseRewards(bytes, size);
    ::operator delete[](bytes);
    record->Close();

    ContentString path;
    appinit::CommonImplSlot4(kPathOption, Word(&path.s), 1, 0, 0xffffffffu);
    cRZAutoRefCount<cIGZUnknown> segment;
    persist->OpenSegmentFromValues(kSegmentIid, segment.AsPPVoid(), 1, Word(path.s.mString.c_str()), Word(kSegmentName), 1, 1);
    if (!segment) return;
    cGZPersistResourceKey tableKey{kStringSetType, 0, 0, 0};
    tableKey.group = As<ifaces::cITSPersistDBSegment>(segment.Get())->GetGroup();
    tableKey.instance = kStringSetInstance;
    if (persist->GetResource(AsKey16(&tableKey), kStringSetIid, mStrings.AsPPVoid(), 0, 0)) {
        const int32_t count = static_cast<int32_t>(mStrings->GetStringCount(0));
        for (int32_t i = 1; i <= count; ++i) {
            const auto *code = static_cast<const cIGZString *>(mStrings->GetString(static_cast<uint32_t>(i), 0));
            wchar_t text[kCodeLength + 1];
            TextToWide(code, text, kCodeLength);
            int16_t id;
            wchar_t chars[10];
            if (DecodeCode(text, &id, chars)) {
                if (TradeableRewardEntry *entry = Find(id)) {
                    entry->mFlags = (entry->mFlags & ~TradeableRewardEntry::kLocked) | TradeableRewardEntry::kUnlocked;
                    WideToText(chars, 8, entry->mCode.Interface());
                }
            }
        }
    } else {
        auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
        void **slot = mStrings.AsPPVoid();
        com->GetClassObject(kStringSetClass, kStringSetIid, slot);
        // ORIGINAL-BUG: the new string set is used without a null test.
        mStrings->SetKey(&tableKey);
    }
}
