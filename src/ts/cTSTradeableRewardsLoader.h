#pragma once
// cTSTradeableRewardsLoader (lane LI): the expat content handler that reads the tradeable rewards XML (the record Load
// 0xcb7790 opens) into a cTSTradeableRewardsService: the rewards (element "reward"), the objects and skin families each
// unlocks ("reskey", "familyid" after it), the key map of locked to unlocked bins ("binpair") and the file's version
// ("TradeableRewards"). Game vtable 0xebad1c (12 slots), 0x14 bytes, constructor 0xd08c30, built only by the service's
// ParseRewards 0xcb81c0 (cTSTradeableRewardsService::ParseRewards), its only creation site.
//
// Standalone shape (docs/standalone-shape.md): a real class on cIGZExpatContentHandler. ParseRewards (ours, reached only
// from Load, ours) builds ours, so the game's vtable is referenced only by the replaced constructor; the slots and the
// handlers run through our vtable ("in_effect": "callers").
//
// Elements and attributes, as the original matches them (strcmp against the strings its pointer cells 0x108a6fc..0x108a730
// hold): every element is handled at its start; the end and every other callback answer true.
//   TradeableRewards  version (strtol, base 0)                            -> mVersion
//   reward            a new entry appended to the rewards: ID (atoi), string (hex: table in the high half, entry in the low),
//                     group (hex), familyid (a family id text, into the entry's families), icon (hex, the entry's value),
//                     norandom (atoi > 0: kNoRandom)
//   binpair           lockedkey, unlockedkey (resource key texts) -> inserted into the key map (zero keys where absent)
//   reskey            key (a resource key text) -> inserted into the current entry's object keys (zero where absent)
//   familyid          familyid (a family id text) -> inserted into the current entry's families (zero where absent)
#include "gz/cIGZPropertySetSerializer.h" // cIGZExpatContentHandler
#include "ts/cTSTradeableRewardsService.h"

#include <cstddef>
#include <cstdint>

class cTSTradeableRewardsLoader : public cIGZExpatContentHandler {
public:
    static constexpr uint32_t kVTable = 0xebad1c;
    static constexpr uint32_t kSize = 0x14;
    static constexpr uint32_t kIid = 0x4b6b9c9b; // cIGZExpatContentHandler

    cTSTradeableRewardsLoader() = default; // 0xd08c30: service and count zero, version -1, no current entry
    cTSTradeableRewardsLoader(const cTSTradeableRewardsLoader &) = delete;
    cTSTradeableRewardsLoader &operator=(const cTSTradeableRewardsLoader &) = delete;

    // slot 0 (shared_vf_492810, folded with the want loaders' QueryInterface: never hooked): iid 1 or the handler's iid
    // answer this, AddRef'd.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;  // slot 1 (shared 0x145260): ++count at +8
    uint32_t Release() override; // slot 2 (0xd09850): a count above 1 decremented, else the 0x14 bytes freed (0)
    // slot 3 (0xd096d0, ret 0x10): the element's handler by name (above), `count` the number of attribute strings (name,
    // value pairs). Always true.
    bool StartElement(const char *name, const char **attributes, uint32_t count, uint32_t unused) override;
    // slots 4-11: shared bodies answering true (0x148260 for 4 / 9, 0x1477b0 for 5 / 8 / 10, 0x144910 for 6 / 7, 0x1ac160
    // for 11).
    bool EndElement(const char *) override { return true; }
    bool CharacterData(const char *, uint32_t) override { return true; }
    bool Callback6() override { return true; }
    bool Callback7() override { return true; }
    bool Callback8(uint32_t, uint32_t) override { return true; }
    bool Callback9(uint32_t) override { return true; }
    bool Callback10(uint32_t, uint32_t) override { return true; }
    bool Callback11(uint32_t, uint32_t, uint32_t, uint32_t) override { return true; }

    // The element handlers (__thiscall, ret 8: the attribute list and its count).
    void ReadVersion(const char **attributes, uint32_t count);  // 0xd09300 "TradeableRewards"
    void AddReward(const char **attributes, uint32_t count);    // 0xd09420 "reward" (nothing without a service)
    void AddBinPair(const char **attributes, uint32_t count);   // 0xd09020 "binpair" (nothing without a service)
    void AddObjectKey(const char **attributes, uint32_t count); // 0xd09370 "reskey" (no insert without a current entry)
    void AddFamilyId(const char **attributes, uint32_t count);  // 0xd09140 "familyid" (no insert without a current entry)

    // 0xd08d20 (__thiscall, `this` unused, ret 8): a family id text "a-b-c-d" (each ParseHex32; cut at '-' with
    // cRZString's SplitOff, so a missing part reads as empty and the last part takes the rest) into the four words in
    // order. False (nothing written) for a null text.
    static bool ParseFamilyId(RewardBinKey *out, const char *text);
    // 0xd08ea0 (__thiscall, `this` unused, ret 8): a resource key text "type-group-resource-instance" the same way: the
    // third part is the key's fourth word (resource), the rest its instance. False (nothing written) for a null text.
    static bool ParseResourceKey(cGZPersistResourceKey *out, const char *text);

    cTSTradeableRewardsService *mService = nullptr; // +0x04 (ParseRewards sets it after the first AddRef: 0x1489e0)
    int32_t mRefCount = 0;                           // +0x08
    int32_t mVersion = -1;                           // +0x0c the "version" attribute
    TradeableRewardEntry *mCurrent = nullptr;        // +0x10 the entry the last "reward" appended (in the service's vector)

private:
    ~cTSTradeableRewardsLoader() = default; // Release's sized delete of 0x14 bytes (no destructor in the original)
};

static_assert(sizeof(cTSTradeableRewardsLoader) == cTSTradeableRewardsLoader::kSize, "new 0x14 in ParseRewards");
static_assert(offsetof(cTSTradeableRewardsLoader, mService) == 4 && offsetof(cTSTradeableRewardsLoader, mRefCount) == 8 &&
                  offsetof(cTSTradeableRewardsLoader, mVersion) == 0xc && offsetof(cTSTradeableRewardsLoader, mCurrent) == 0x10,
              "cTSTradeableRewardsLoader layout");
