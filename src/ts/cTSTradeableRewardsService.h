#pragma once
// cTSTradeableRewardsService (lane KZ): the tradeable rewards service (system service id 0x51817c0d, priority 0;
// cITSTradeableRewardsService iid 0x5181c4cc). 0x3c bytes, /O2. RVAs 1.18.0.187. It keeps the tradeable rewards: entries
// loaded once, on the first slot call, from an XML resource (the reward list: an id, a string-table text, a value, flags and
// the bins the reward unlocks; the same XML fills a key map, locked bin -> unlocked bin), and the reward codes the player redeemed
// (the STR# table 0x6f37 of the "kissingpoints" package's group, a cTSStringSet). A reward is unlocked by its code (a
// 19-character text that encodes the reward id and eight characters), or by UnlockRandomReward; unlocking unlocks the
// reward's bins (0xcb84c0) and posts message 0x91a4f0d0. Each reward's code is the text the encoder (0xd09cd0) makes from
// the id and the reward's current code.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xeb77d4, 13 slots), +4 cRZUnknownMT (0xeb780c, 6:
// slot 3 the deleting destructor's thunk 0xcb761b), +0x1c cITSTradeableRewardsService (0xeb7828, 15 slots). The original's
// slot bodies take `this` as their face and reach the fields at face-relative offsets; ours are members.
//
// Standalone shape (docs/standalone-shape.md): the factory 0xcb7b50 (hooked: `new` of ours) is registered by address with
// the app's COM director (0xc2bba4) and is the only caller of the constructor 0xcb73b0; the vtables are referenced only by
// the constructor and the destructor 0xcb7540, so every object carries ours (db/status/cTSTradeableRewardsService.jsonl).
// The reward info objects (cTSTradeableRewardInfo) are made only by GetRewards.
//
// Lane LG made the code coder, the bin unlocking (with its filter classes) and AddDirectorySegments 0xcb8730 ours; lane LI
// the reward XML parse 0xcb81c0 (ParseRewards) with its content handler cTSTradeableRewardsLoader.
#include "gz/cGZDBSegmentPackedFile.h"     // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h"  // cGZPersistResourceKey
#include "cas/CASFragmentFilters.h"     // cTSBinFilterBase, cTSSimFragmentFilterBase
#include "cas/cTSBinCatalog.h"          // BinList
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/RZUnknownBase.h"
#include "ts/cTSCustomContentSupport.h"    // ContentString
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

class cIGZString;
namespace ifaces {
class cITSStringSet;
} // namespace ifaces

// cITSTradeableRewardInfo (iid 0xb19d0d60): one reward as GetRewards hands it out. Slots 3-7.
class cITSTradeableRewardInfo : public cIGZUnknown {
public:
    virtual int32_t GetId() = 0;                                  // 3 (shared 0x145770: +0xc)
    virtual uint32_t GetValue() = 0;                              // 4 (shared 0x149cf0: +0x10)
    virtual cIGZString *GetName() = 0;                            // 5 (shared: the string at +0x14)
    virtual cIGZString *GetCode() = 0;                            // 6 (shared: the string at +0x34)
    virtual cIGZString *SetCode(const cIGZString &code) = 0;      // 7 (0xcb8ae0, ret 4): the code's slot 16 (operator=)
};

// GetRewards' output: counted reward faces, elements from the allocator service (growth 0xcb6f00, the shared STL
// instantiation of this element type).
using TradeableRewardRef = cRZAutoRefCount<cITSTradeableRewardInfo>;
using TradeableRewardList = std::vector<TradeableRewardRef, GZServiceAllocator<TradeableRewardRef>>;
static_assert(sizeof(TradeableRewardList) == 0xc, "MSVC 14.40 vector");

// cITSTradeableRewardsService (iid 0x5181c4cc): slots 3-14. Every slot but 10 and 11 loads the rewards first (Load).
class cITSTradeableRewardsService : public cIGZUnknown {
public:
    // 3 (0xcb7d00, ret 8): a new reward info for every entry whose flags share a bit with `mask`, appended to `out` (not
    // emptied first); the list's size.
    virtual uint32_t GetRewards(TradeableRewardList *out, uint32_t mask) = 0;
    // 4 (0xcb8af0, ret 8): the reward's code set to `code`, then saved (SaveCode); false for an unknown id.
    virtual bool SetRewardCode(int32_t id, const cIGZString *code) = 0;
    // 5 (0xcb8f00, ret 4): redeems a code: decoded into an id and eight characters, the reward unlocked, its code set from
    // the characters, the code appended to the table. False when the code doesn't decode or the reward is unknown.
    virtual bool RedeemCode(const cIGZString *code) = 0;
    // 6 (0xcb8c70): one of the locked rewards without the "norandom" flag, drawn at random, unlocked and its code made and
    // appended to the table; its id, or -1.
    virtual int32_t UnlockRandomReward() = 0;
    // 7 (0xcb7bc0, ret 0xc): the code for reward `id` made from `text`'s first eight characters into `out`; false for an
    // unknown id or when the encoder fails.
    virtual bool MakeCode(cIGZString *out, int32_t id, const cIGZString *text) = 0;
    // 8 (0xcb7ef0, ret 4): whether `key` is a value of the key map.
    virtual bool IsMappedValue(const cGZPersistResourceKey *key) = 0;
    // 9 (0xcb7ec0, ret 4): whether `key` is a key of the key map.
    virtual bool IsMappedKey(const cGZPersistResourceKey *key) = 0;
    // 10 (0xcb8f90, ret 4): whether every character of `text` (its first eight, trimmed) is in the code alphabet.
    virtual bool IsValidCodeText(const cIGZString *text) = 0;
    // 11 (0xcb7aa0, ret 8): the key `key` maps to, or the key that maps to `key`, into `out`; false for neither.
    virtual bool GetMappedKey(const cGZPersistResourceKey *key, cGZPersistResourceKey *out) = 0;
    // 12 (0xcb7c30): the number of rewards.
    virtual uint32_t GetRewardCount() = 0;
    // 13 (0xcb7c90, ret 4): the id of reward `index` (through slot 12), -1 past the end.
    virtual int32_t GetRewardId(uint32_t index) = 0;
    // 14 (0xcb7cc0, ret 8): the reward's code into `out` (its slot 16); false for an unknown id.
    virtual bool GetRewardCode(int32_t id, cIGZString *out) = 0;
};

// The 16-byte keys of an entry's bins (std::less<Blob4<16>> 0xcb75b0: the words compared as bytes, in memory order).
// Four-byte aligned (the image's Blob4<16>): the set node keeps it at +0x10, where the family filter's search 0xcb8040
// and std::less<Blob4<16>> 0xcb75b0 read it (an unaligned key would sit at +0xe).
struct alignas(4) RewardBinKey {
    uint8_t bytes[16];
};
struct RewardBinKeyLess {
    bool operator()(const RewardBinKey &a, const RewardBinKey &b) const { return std::memcmp(a.bytes, b.bytes, 16) < 0; }
};
using RewardBinKeys = std::set<RewardBinKey, RewardBinKeyLess>; // node 0x20
static_assert(sizeof(RewardBinKeys) == 8, "MSVC 14.40 set");

// The key map's order (0x5f24ad): type, group, the fourth word, then the instance, each unsigned.
struct RewardKeyLess {
    bool operator()(const cGZPersistResourceKey &a, const cGZPersistResourceKey &b) const
    {
        if (a.type != b.type) return a.type < b.type;
        if (a.group != b.group) return a.group < b.group;
        if (a.resource != b.resource) return a.resource < b.resource;
        return a.instance < b.instance;
    }
};
using RewardKeyMap = std::map<cGZPersistResourceKey, cGZPersistResourceKey, RewardKeyLess>; // node 0x30
static_assert(sizeof(RewardKeyMap) == 8, "MSVC 14.40 map");
// The keys of the objects an entry unlocks, in the key map's order (the loader's insert 0xd08340 and the object filter's
// search 0xcb7f80 compare type, group, the fourth word, the instance).
using RewardObjectKeys = std::set<cGZPersistResourceKey, RewardKeyLess>; // node 0x20
static_assert(sizeof(RewardObjectKeys) == 8, "MSVC 14.40 set");

// One reward of the list (0x48 bytes). The XML loader's AddReward (0xd09420, src/ts/cTSTradeableRewardsLoader.h) constructs
// one on its stack, appends a copy to the service's vector (copy 0xd08a10 in place, or the vector's growth 0xd08490),
// destroys it (0xd08cb0) and fills the copy from the attributes.
struct TradeableRewardEntry {
    static constexpr uint32_t kLocked = 1;    // unlocking clears it
    static constexpr uint32_t kUnlocked = 2;  // set when unlocked
    static constexpr uint32_t kNoRandom = 4;  // never drawn by UnlockRandomReward ("norandom")

    // 0xcb71c0: +0 = 100, the id -1, the flags 1 (locked), the sets and the code empty. The original leaves the text, group
    // and value as they were (stack bytes in AddReward, so a reward without "string", "group" or "icon" keeps garbage
    // there); ours zeroes them.
    TradeableRewardEntry() = default;

    uint16_t mWord0 = 100;      // +0x00 100 (never read here)
    int32_t mId = -1;           // +0x04 "ID"
    uint16_t mTextTable = 0;    // +0x08 "string": the string table id (high word)
    uint16_t mTextEntry = 0;    // +0x0a "string": the entry (low word)
    uint32_t mTextGroup = 0;    // +0x0c "group": the string table's group
    RewardBinKeys mFamilyIds;   // +0x10 "familyid": the skin families it unlocks
    uint32_t mValue = 0;        // +0x18 "icon": the value the reward info reports
    uint32_t mFlags = kLocked;  // +0x1c kLocked / kUnlocked / kNoRandom
    ContentString mCode;       // +0x20 the reward's code text
    RewardObjectKeys mObjectKeys; // +0x40 the objects it unlocks
};
static_assert(offsetof(TradeableRewardEntry, mId) == 4 && offsetof(TradeableRewardEntry, mTextTable) == 8 &&
                  offsetof(TradeableRewardEntry, mTextGroup) == 0xc && offsetof(TradeableRewardEntry, mFamilyIds) == 0x10 &&
                  offsetof(TradeableRewardEntry, mValue) == 0x18 && offsetof(TradeableRewardEntry, mFlags) == 0x1c &&
                  offsetof(TradeableRewardEntry, mCode) == 0x20 && offsetof(TradeableRewardEntry, mObjectKeys) == 0x40 &&
                  sizeof(TradeableRewardEntry) == 0x48,
              "TradeableRewardEntry layout");
using TradeableRewardEntries = std::vector<TradeableRewardEntry>; // operator new (destroy range 0xcb6e80)
static_assert(sizeof(TradeableRewardEntries) == 0xc, "MSVC 14.40 vector");

// cTSTradeableRewardsService::cTSTradeableRewardInfo (0x54 bytes): cITSTradeableRewardInfo at +0 (vtable 0xeb7750, 8 slots)
// and cRZUnknown at +4 (0xeb7774, 6: QueryInterface thunk 0xcb82f7, the deleting destructor's thunk 0xcb7613), count at +8.
class cTSTradeableRewardInfo : public cITSTradeableRewardInfo, public rz::cRZUnknown {
public:
    static constexpr uint32_t kVTable = 0xeb7750;
    static constexpr uint32_t kUnknownVTable = 0xeb7774;
    static constexpr uint32_t kSize = 0x54;
    static constexpr uint32_t kIid = 0xb19d0d60; // cITSTradeableRewardInfo

    // 0xcb72a0 (ret 0x10): the id, the value and copies of the two strings.
    cTSTradeableRewardInfo(int32_t id, uint32_t value, const cRZString &name, const cRZString &code);
    ~cTSTradeableRewardInfo() override; // deleting 0xcb76b0 (+4 thunk 0xcb7613)
    cTSTradeableRewardInfo(const cTSTradeableRewardInfo &) = delete;
    cTSTradeableRewardInfo &operator=(const cTSTradeableRewardInfo &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0xcb82d0; thunk 0xcb82f7 (+4)
    uint32_t AddRef() override;                              // folded 0x1dab20: cRZUnknown's
    uint32_t Release() override;                             // folded 0x1e3970: cRZUnknown's

    int32_t GetId() override { return mId; }
    uint32_t GetValue() override { return mValue; }
    cIGZString *GetName() override { return mName.Interface(); }
    cIGZString *GetCode() override { return mCode.Interface(); }
    cIGZString *SetCode(const cIGZString &code) override;

    // +0x00 cITSTradeableRewardInfo vtable, +0x04 cRZUnknown vtable, +0x08 its count
    int32_t mId;         // +0x0c
    uint32_t mValue;     // +0x10
    ContentString mName; // +0x14 the reward's text (from its string table)
    ContentString mCode; // +0x34 the reward's code
};
static_assert(sizeof(cTSTradeableRewardInfo) == cTSTradeableRewardInfo::kSize, "new 0x54 in GetRewards");
static_assert(offsetof(cTSTradeableRewardInfo, mId) == 0xc && offsetof(cTSTradeableRewardInfo, mName) == 0x14 &&
                  offsetof(cTSTradeableRewardInfo, mCode) == 0x34,
              "cTSTradeableRewardInfo layout");

class cTSTradeableRewardsService : public rz::cRZSystemService, public cITSTradeableRewardsService {
public:
    static constexpr uint32_t kVTable = 0xeb77d4;
    static constexpr uint32_t kVTable4 = 0xeb780c;
    static constexpr uint32_t kRewardsVTable = 0xeb7828;
    static constexpr uint32_t kSize = 0x3c;
    static constexpr uint32_t kServiceId = 0x51817c0d;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kIid = 0x5181c4cc; // cITSTradeableRewardsService
    static constexpr uint32_t kStringSetType = 0x53545223; // 'STR#'
    static constexpr uint32_t kStringSetInstance = 0x6f37;
    static constexpr uint32_t kStringSetIid = 0xfb4cb0de;  // cITSStringSet
    static constexpr uint32_t kUnlockMessage = 0x91a4f0d0;
    static constexpr uint32_t kCodeLength = 0x13; // characters of a code

    cTSTradeableRewardsService();                    // 0xcb73b0
    ~cTSTradeableRewardsService() noexcept override; // 0xcb7540 (deleting 0xcb7770; +4 thunk 0xcb761b)
    cTSTradeableRewardsService(const cTSTradeableRewardsService &) = delete;
    cTSTradeableRewardsService &operator=(const cTSTradeableRewardsService &) = delete;

    // 0xcb7b50 (__cdecl): the COM director's factory: a new service (null when new failed).
    static cTSTradeableRewardsService *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xcb8300; thunks 0xcb8331 (+4) and 0xcb8339 (+0x1c)
    uint32_t AddRef() override;                              // folded (the base's)
    uint32_t Release() override;                             // folded (the base's)

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xcb7e60
    bool Shutdown() override; // 0xcb8b30

    // ---- cITSTradeableRewardsService (+0x1c) ----
    uint32_t GetRewards(TradeableRewardList *out, uint32_t mask) override;
    bool SetRewardCode(int32_t id, const cIGZString *code) override;
    bool RedeemCode(const cIGZString *code) override;
    int32_t UnlockRandomReward() override;
    bool MakeCode(cIGZString *out, int32_t id, const cIGZString *text) override;
    bool IsMappedValue(const cGZPersistResourceKey *key) override;
    bool IsMappedKey(const cGZPersistResourceKey *key) override;
    bool IsValidCodeText(const cIGZString *text) override;
    bool GetMappedKey(const cGZPersistResourceKey *key, cGZPersistResourceKey *out) override;
    uint32_t GetRewardCount() override;
    int32_t GetRewardId(uint32_t index) override;
    bool GetRewardCode(int32_t id, cIGZString *out) override;

    cITSTradeableRewardsService *RewardsService() { return this; }

    // ---- the filters UnlockBins hands the bin catalog (built on its stack only; vtables referenced by their inlined
    // constructors in UnlockBins alone) ----

    // vtable 0xeb7790 (base cTSBinFilterBase, constructor 0xb64490 inlined): the locked bins.
    class cLockedBinFilter : public nTSCASCore::cTSBinFilterBase {
    public:
        static constexpr uint32_t kVTable = 0xeb7790;
        bool Accept(cIGZUnknown *bin) override; // 0xcb7ea0: a bin whose flags (its slot 27) have bit 0
    };
    static_assert(sizeof(cLockedBinFilter) == 8, "cLockedBinFilter");

    // vtable 0xeb77a4 (0x20 bytes; constructor 0xcb7150, deleting destructor 0xcb7650): the fragments whose property set
    // (the resource service's slot 37, iid 0x8c1571d6) has a family id (its slot 17) of the set and whose bin key (the
    // fragment's slot 19) is a key of the key map.
    class cSkinFamilyIdFragmentFilter : public nTSCASCore::cTSSimFragmentFilterBase {
    public:
        static constexpr uint32_t kVTable = 0xeb77a4;
        cSkinFamilyIdFragmentFilter(const RewardBinKeys *families, const RewardKeyMap *keyMap); // 0xcb7150 (ret 8)
        ~cSkinFamilyIdFragmentFilter() override;                                                // 0xcb7650 (deleting)
        bool Accept(cIGZUnknown *fragment) override;                                            // 0xcb8040

        const RewardBinKeys *mFamilies;   // +0x14
        cIGZUnknown *mResourceService;    // +0x18 the resource service (counted; released by the destructor)
        const RewardKeyMap *mKeyMap;      // +0x1c
    };
    static_assert(sizeof(cSkinFamilyIdFragmentFilter) == 0x20, "cSkinFamilyIdFragmentFilter");

    // vtable 0xeb77bc (0x1c bytes; constructor 0xcb7120, deleting destructor 0xcb7630): the fragments whose outfit key (their
    // slot 21) is in the set and whose bin key (slot 19) is a key of the key map.
    class cObjectKeyFragmentFilter : public nTSCASCore::cTSSimFragmentFilterBase {
    public:
        static constexpr uint32_t kVTable = 0xeb77bc;
        cObjectKeyFragmentFilter(const RewardObjectKeys *objects, const RewardKeyMap *keyMap); // 0xcb7120 (ret 8)
        bool Accept(cIGZUnknown *fragment) override;                                           // 0xcb7f80

        const RewardObjectKeys *mObjects; // +0x14
        const RewardKeyMap *mKeyMap;      // +0x18
    };
    static_assert(sizeof(cObjectKeyFragmentFilter) == 0x1c, "cObjectKeyFragmentFilter");

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSTradeableRewardsService face's vtable
    TradeableRewardEntries mEntries;                 // +0x20 the rewards (filled by the XML load 0xcb81c0)
    RewardKeyMap mKeyMap;                            // +0x2c locked bin key -> unlocked bin key
    cRZAutoRefCount<ifaces::cITSStringSet> mStrings; // +0x34 the redeemed codes (STR# 0x6f37)
    uint8_t mLoaded;                                 // +0x38 Load ran (with a resource manager)

    // 0xcb8730 (ret 0x14; `this` unused): a loose-file segment (COM class 0xa079ce1b) for each existing directory of the
    // app's directory list `option` (cTSAppCommonImpl slot 5, walked from its last entry), its files of `extension` typed
    // `type` in group `group`, added to `manager` (its slot 27). True when the last directory visited that existed got its
    // segment; false when none existed or a segment could not be made (the walk stops there). Public for its test (Load
    // ignores the answer).
    bool AddDirectorySegments(cIGZUnknown *manager, uint32_t option, uint32_t type, uint32_t group, cRZString *extension);
    // 0xcb81c0 (ret 8): the reward XML (`size` bytes at `bytes`) parsed by a new expat parser object (the framework's class
    // 0x6b6b01d1) with a new cTSTradeableRewardsLoader on this service as its handler: the rewards appended to mEntries,
    // the pairs inserted into mKeyMap. False for no bytes, no size, or no parser; else true (whatever the parse answered).
    // Public for its test.
    bool ParseRewards(const char *bytes, uint32_t size);

private:
    TradeableRewardEntry *Find(int32_t id);          // 0xcb7c60 (ret 4)
    bool IsLocked(int32_t id);                       // 0xcb8190 (ret 4)
    bool SaveCode(int32_t id);                       // 0xcb89a0 (ret 4)
    bool AppendCode(const cIGZString *code);         // 0xcb8ab0 (ret 4)
    bool Unlock(int32_t id);                         // 0xcb8e00 (ret 4)
    void Load();                                     // 0xcb7790
    // 0xcb84c0 (ret 8): the fragments of the locked bins whose family ids / object keys are the reward's moved to their
    // unlocked bins (MoveFragments); true when any was.
    bool UnlockBins(const RewardBinKeys *families, const RewardObjectKeys *objects);
    // 0xcb8430 (ret 4): each fragment of the list whose bin key the key map maps gets the mapped key (its slot 18) and is
    // saved by the bin catalog (its slot 21); true when any save succeeded.
    bool MoveFragments(const nTSCASCore::BinList *fragments);
};

static_assert(sizeof(cTSTradeableRewardsService) == cTSTradeableRewardsService::kSize, "new 0x3c in the factory");
static_assert(offsetof(cTSTradeableRewardsService, mEntries) == 0x20 && offsetof(cTSTradeableRewardsService, mKeyMap) == 0x2c &&
                  offsetof(cTSTradeableRewardsService, mStrings) == 0x34 && offsetof(cTSTradeableRewardsService, mLoaded) == 0x38,
              "cTSTradeableRewardsService layout");

namespace tradeable_rewards {

// 0xcb8350 (__thiscall, `this` unused, ret 0xc): `src`'s text (Strlen, then Data), trimmed, as UTF-16 into `out`: at most
// `max` characters, then a terminator. False (nothing written) for a null `out`.
bool TextToWide(const cIGZString *src, wchar_t *out, uint32_t max);
// 0xcb8bc0 (__thiscall, `this` unused, ret 0xc): `length` UTF-16 characters at `chars` into a string, trimmed, assigned to
// `out` (its slot 16). False for null `chars`.
bool WideToText(const wchar_t *chars, uint32_t length, cIGZString *out);

// ---- the reward code coder (lane LG) ----
//
// A code is 19 characters of the alphabet. It carries 19 five-bit values (the characters' alphabet indexes, all below 32),
// which unscramble into 13 seven-bit words: the eight characters' alphabet indexes (0..7), the reward id (8) and the CRC-32
// of the first 9 bytes of words 0..4 (the indexes as 16-bit words: four characters and the low byte of the fifth; the id
// is not covered), 7 bits of each of its four bytes, high byte first (9..12). The scrambling transposes words 0..4 and 5..9
// (bit j of word k is bit k of value j, values 0..6 and 7..13) and spreads words 10..12 (24 bits) over values 14..18 (bit u
// to bit u / 5 of value 14 + u % 5).
//
// The encoder's codes don't decode (ORIGINAL-BUG, see EncodeCode): the scrambler 0xd09e80 reads the bits of words 10..12
// from a fixed place of its caller's frame instead of its argument, which in the encoder (its only caller) is the alphabet's
// characters 43..45, "hij". A code it makes passes the decoder's checksum only when the CRC's three low bytes are those
// (one text in 2^21; "GFGGM" is one: the test shows it).

constexpr uint32_t kAlphabetLength = 128;
// 0xd09870 (__cdecl): the code alphabet (UTF-16) and its terminator into `out` (129 characters). The original builds it once
// into a global (0x1107b10): an ASCII string widened character by character (the high bytes sign-extended), the 41
// accented letters then written over the high bytes, the terminator at 128; ours is the finished table.
void __cdecl CodeAlphabet(wchar_t *out);
// 0xd09e40 (__cdecl): the first index of `ch` ('_' read as ' ') among the alphabet's first 128 characters, 0 when absent.
uint16_t __cdecl AlphabetIndex(const wchar_t *alphabet, wchar_t ch);
// 0xd09ac0 (__cdecl): the CRC-32 (reflected, polynomial 0xedb88320; the image's table at 0xebad58) of `count` bytes.
uint32_t __cdecl CodeChecksum(const void *bytes, uint32_t count);
// 0xd09e80: the 19 values of words 0..9 and the 24 `tailBits` (the original takes words 10..12's low bytes from its
// caller's frame: see above), each value's low byte written back.
void ScrambleCode(uint16_t *values, uint32_t tailBits);
// 0xd0a090 (__cdecl): the 19 values unscrambled into words 0..12 (words 13..18 zero); true.
bool __cdecl UnscrambleCode(uint16_t *values);
// 0xd09af0 (__cdecl): a code of exactly 19 characters decoded: false unless its checksum holds; else the id (word 8) into
// `id`, the eight characters and a terminator into `chars` with trailing spaces / underscores cut, and whether the id is
// below 0x33.
bool __cdecl DecodeCode(const wchar_t *code, int16_t *id, wchar_t *chars);
// 0xd09cd0 (__cdecl): the code of reward `id` (its low half, a signed short below 0x33) and up to eight characters (padded
// with spaces) and a terminator into `out`; false for more than eight characters or a larger id.
bool __cdecl EncodeCode(wchar_t *out, int32_t id, const wchar_t *chars);

} // namespace tradeable_rewards
