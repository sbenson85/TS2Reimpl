// cTSTradeableRewardsLoader and the service's ParseRewards (lane LI). See the header.
#include "ts/cTSTradeableRewardsLoader.h"

#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "misc/HexParse.h"    // ParseHex32
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/cRZIniFile.h"    // cRZIniStringHelpers::SplitOff
#include "rz/cRZString.h"

#include <cstdlib>
#include <cstring>

namespace {

// The framework's expat parser object (class 0x6b6b01d1, iid 0x6b6b01cd) and the slots ParseRewards calls on it.
constexpr uint32_t kParserClass = 0x6b6b01d1;
constexpr uint32_t kParserIid = 0x6b6b01cd;
constexpr unsigned kSlotParse = 3;      // (bytes, size, 1: the last chunk)
constexpr unsigned kSlotSetHandler = 8; // (cIGZExpatContentHandler *)

// The element and attribute names (the original reads them through pointer cells 0x108a6fc..0x108a730).
constexpr const char kTradeableRewards[] = "TradeableRewards";
constexpr const char kReward[] = "reward";
constexpr const char kBinPair[] = "binpair";
constexpr const char kResKey[] = "reskey";
constexpr const char kVersion[] = "version";
constexpr const char kId[] = "ID";
constexpr const char kString[] = "string";
constexpr const char kGroup[] = "group";
constexpr const char kFamilyId[] = "familyid"; // an element and an attribute
constexpr const char kIcon[] = "icon";
constexpr const char kNoRandom[] = "norandom";
constexpr const char kKey[] = "key";
constexpr const char kUnlockedKey[] = "unlockedkey";
constexpr const char kLockedKey[] = "lockedkey";
constexpr const char kPartDelimiter[] = "-";

bool Is(const char *text, const char *name) { return std::strcmp(text, name) == 0; }

// The text before the next '-' (SplitOff's out: raw memory until SplitOff constructs it).
struct TextPart {
    union {
        cRZString value;
    };
    TextPart() {}
    ~TextPart() { value.Destruct(); }
    TextPart(const TextPart &) = delete;
    TextPart &operator=(const TextPart &) = delete;
};

// Four hexadecimal words of a "a-b-c-d" text, in text order: the first three cut off at '-', the last the rest.
void ParseFourParts(const char *text, uint32_t (&parts)[4])
{
    ContentString rest(text);
    for (int i = 0; i < 3; ++i) {
        TextPart part;
        reinterpret_cast<cRZIniStringHelpers *>(&rest.s)->SplitOff(&part.value, kPartDelimiter);
        parts[i] = hexparse::ParseHex32(part.value.ToChar());
    }
    parts[3] = hexparse::ParseHex32(rest.s.ToChar());
}

} // namespace

// ---- cTSTradeableRewardsService::ParseRewards ----

bool cTSTradeableRewardsService::ParseRewards(const char *bytes, uint32_t size)
{
    if (!bytes || !size) return false;
    cRZAutoRefCount<cIGZUnknown> parser;
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (!static_cast<uint8_t>(com->GetClassObject(kParserClass, kParserIid, parser.AsPPVoid()))) return false;
    auto *const loader = new cTSTradeableRewardsLoader;
    loader->AddRef();
    loader->mService = this;
    VirtualCall<void(__thiscall *)(void *, cIGZExpatContentHandler *), kSlotSetHandler>(parser.Get(), loader);
    VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, uint32_t), kSlotParse>(parser.Get(), bytes, size, 1);
    loader->Release();
    return true;
}

// ---- cTSTradeableRewardsLoader ----

bool cTSTradeableRewardsLoader::QueryInterface(uint32_t iid, void **out)
{
    if (iid != 1 && iid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSTradeableRewardsLoader::AddRef() { return uint32_t(++mRefCount); }

uint32_t cTSTradeableRewardsLoader::Release()
{
    if (mRefCount > 1) return uint32_t(--mRefCount);
    delete this;
    return 0;
}

bool cTSTradeableRewardsLoader::StartElement(const char *name, const char **attributes, uint32_t count, uint32_t)
{
    if (Is(name, kTradeableRewards)) {
        ReadVersion(attributes, count);
    } else if (Is(name, kReward)) {
        AddReward(attributes, count);
    } else if (Is(name, kBinPair)) {
        AddBinPair(attributes, count);
    } else if (Is(name, kResKey)) {
        AddObjectKey(attributes, count);
    } else if (Is(name, kFamilyId)) {
        AddFamilyId(attributes, count);
    }
    return true;
}

void cTSTradeableRewardsLoader::ReadVersion(const char **attributes, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 2)
        if (Is(attributes[i], kVersion)) mVersion = std::strtol(attributes[i + 1], nullptr, 0);
}

void cTSTradeableRewardsLoader::AddReward(const char **attributes, uint32_t count)
{
    if (!mService) return;
    const TradeableRewardEntry blank; // destroyed at the end, as the original's stack entry
    mService->mEntries.push_back(blank);
    mCurrent = &mService->mEntries.back();
    for (uint32_t i = 0; i < count; i += 2) {
        const char *const attribute = attributes[i];
        const char *const value = attributes[i + 1];
        if (Is(attribute, kId)) {
            mCurrent->mId = std::atoi(value);
        } else if (Is(attribute, kString)) {
            const uint32_t text = hexparse::ParseHex32(value);
            mCurrent->mTextTable = uint16_t(text >> 16);
            mCurrent->mTextEntry = uint16_t(text);
        } else if (Is(attribute, kGroup)) {
            mCurrent->mTextGroup = hexparse::ParseHex32(value);
        } else if (Is(attribute, kFamilyId)) {
            RewardBinKey family{};
            ParseFamilyId(&family, value);
            mCurrent->mFamilyIds.insert(family);
        } else if (Is(attribute, kIcon)) {
            mCurrent->mValue = hexparse::ParseHex32(value);
        } else if (Is(attribute, kNoRandom)) {
            if (std::atoi(value) > 0) mCurrent->mFlags |= TradeableRewardEntry::kNoRandom;
        }
    }
}

void cTSTradeableRewardsLoader::AddBinPair(const char **attributes, uint32_t count)
{
    if (!mService) return;
    cGZPersistResourceKey locked{};
    cGZPersistResourceKey unlocked{};
    for (uint32_t i = 0; i < count; i += 2) {
        if (Is(attributes[i], kLockedKey)) {
            ParseResourceKey(&locked, attributes[i + 1]);
        } else if (Is(attributes[i], kUnlockedKey)) {
            ParseResourceKey(&unlocked, attributes[i + 1]);
        }
    }
    mService->mKeyMap.insert({locked, unlocked}); // an existing key keeps its value
}

void cTSTradeableRewardsLoader::AddObjectKey(const char **attributes, uint32_t count)
{
    cGZPersistResourceKey key{};
    for (uint32_t i = 0; i < count; i += 2)
        if (Is(attributes[i], kKey)) ParseResourceKey(&key, attributes[i + 1]);
    if (mCurrent) mCurrent->mObjectKeys.insert(key);
}

void cTSTradeableRewardsLoader::AddFamilyId(const char **attributes, uint32_t count)
{
    RewardBinKey family{};
    for (uint32_t i = 0; i < count; i += 2)
        if (Is(attributes[i], kFamilyId)) ParseFamilyId(&family, attributes[i + 1]);
    if (mCurrent) mCurrent->mFamilyIds.insert(family);
}

bool cTSTradeableRewardsLoader::ParseFamilyId(RewardBinKey *out, const char *text)
{
    if (!text) return false;
    uint32_t parts[4];
    ParseFourParts(text, parts);
    std::memcpy(out->bytes, parts, sizeof(parts));
    return true;
}

bool cTSTradeableRewardsLoader::ParseResourceKey(cGZPersistResourceKey *out, const char *text)
{
    if (!text) return false;
    uint32_t parts[4];
    ParseFourParts(text, parts);
    out->type = parts[0];
    out->group = parts[1];
    out->resource = parts[2];
    out->instance = parts[3];
    return true;
}
