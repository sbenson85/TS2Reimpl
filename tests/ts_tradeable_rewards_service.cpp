// Differential tests for the tradeable rewards service and the reward info objects it hands out (lane KZ):
// cTSTradeableRewardsService and cTSTradeableRewardInfo (src/ts/cTSTradeableRewardsService.h), in the standalone shape.
//
// TradeableRewards_Run: a service built by the original factory (0xcb7b50) or constructor (0xcb73b0, on 0xc5 memory)
// against one built by ours (Create, or placement new), each driven through its own vtables by the same random sequence of
// operations, then destroyed by the last Release (the deleting destructor 0xcb7770 behind the +4 thunk 0xcb761b; the
// destructor 0xcb7540). Operations: Init (0xcb7e60), Shutdown (0xcb8b30), QueryInterface (0xcb8300; thunks 0xcb8331 (+4),
// 0xcb8339 (+0x1c)) / AddRef / Release on every face, the base service's accessors, and every cITSTradeableRewardsService
// slot: GetRewards (0xcb7d00), SetRewardCode (0xcb8af0), RedeemCode (0xcb8f00), UnlockRandomReward (0xcb8c70), MakeCode
// (0xcb7bc0), IsMappedValue (0xcb7ef0), IsMappedKey (0xcb7ec0), IsValidCodeText (0xcb8f90), GetMappedKey (0xcb7aa0),
// GetRewardCount (0xcb7c30), GetRewardId (0xcb7c90), GetRewardCode (0xcb7cc0); through them Load (0xcb7790), Find
// (0xcb7c60), IsLocked (0xcb8190), SaveCode (0xcb89a0), AppendCode (0xcb8ab0), Unlock (0xcb8e00) and the text helpers
// 0xcb8350 / 0xcb8bc0. The reward infos GetRewards makes are driven too (the operations of RewardInfo_Run) and released with
// the list.
//
// RewardInfo_Run: a reward info built by the original constructor (0xcb72a0, on 0xc5 memory) against ours, driven through
// its own vtables: QueryInterface (0xcb82d0; thunk 0xcb82f7 (+4)), AddRef / Release / RefCount on both faces, the getters,
// SetCode (0xcb8ae0); destroyed by the last Release (the deleting destructor 0xcb76b0 behind the +4 thunk 0xcb7613).
//
// The reward XML parse ParseRewards 0xcb81c0 runs for real on both sides (the image's with the game's loader, ours with
// ours: lane LI, tests/ts_tradeable_rewards_loader.cpp): the framework hands out a fake expat parser (or refuses) whose
// Parse replays the world's rewards and key pairs as XML elements onto the handler; the world's flags the XML can't express
// and the rewards' codes are set after the replay. Stand-ins on both sides: the directory list 0x1d0c60 and the directory test 0x1c4f94 (and ours: CommonImplSlot5, as2::DirectoryExists),
// cTSString's constructor 0x1d0e90 (a text per table / entry) and cRZRandom::Seed 0x1b8ecb (a seed per case). The
// message constructor 0x1c033d and the message server getter 0x9c5a89 run as the image's on both sides; the code coder
// (0xd09870 / 0xd09af0 / 0xd09cd0), AddDirectorySegments 0xcb8730 and the bin unlocking 0xcb84c0 (with MoveFragments
// 0xcb8430 and the filters 0xcb7ea0, 0xcb7150 / 0xcb8040, 0xcb7120 / 0xcb7f80) are the image's on the original side and
// ours on ours (lane LG). Every object the code reaches is a recording fake (tests/ts_services_fixture.h): the service
// object (Init's service), the persist system (the resource manager, GetResource for the code table,
// OpenSegmentFromValues for the "kissingpoints" package), the resource manager (OpenRecord, slot 27 for the loose-file
// segments), the record (GetSize, the read, Close), the segment (its group), the code table (count, strings, SetString,
// AppendString, SetKey, Commit), the framework (GetClassObject for a new table or a loose-file segment), the new segment
// and its extension table, the app's common implementation (the path), the message server, the bin catalog (slot 30 runs
// the filters through their vtables on two bins and four fragments; SaveFragment), the resource service (a fragment's
// property set, its family id) and the fragments (their keys; slot 18 the moved bin key). Compared after every operation: the result (the low byte where the original returns a bool in
// al), the outputs, the event log (strings by content, messages by content), every field (the rewards, the key map, the
// table, the loaded flag), every fake's count and the stack balance of every slot call.
//
// Not driven: a null persist system (Load uses it untested: ORIGINAL-BUG), a framework that makes no string set (Load uses it
// untested: ORIGINAL-BUG), out-of-memory for a reward info or the unlock message (the message is used untested:
// ORIGINAL-BUG), and SetRewardCode where the code SaveCode 0xcb89a0 makes doesn't decode: SaveCode ignores the decoder's
// result, so the reward's code is set from uninitialised stack characters (ORIGINAL-BUG). That happens for ids with bit 15
// set (the encoder 0xd09cd0 tests the id as a signed short, < 0x33; the decoder 0xd09af0 tests the decoded id unsigned) and
// for codes made from characters outside the alphabet; the test asks the original chain first (CodeDecodes) and skips those.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppInit.h"      // CommonImplSlot5
#include "app/AppServices2.h" // DirectoryExists
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"                    // GZString
#include "misc/HexParse.h"                // ParseHex32 (the original loader's, patched)
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "ts/cTSCustomContentSupport.h" // ContentString
#include "ts/cTSString.h"
#include "ts/cTSTradeableRewardsLoader.h"
#include "ts/cTSTradeableRewardsService.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace trs_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{15, "d"}, {18, "dd"}, {19, "dd"}, {20, "dd"}, {21, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSTradeableRewardsService;
using Info = cTSTradeableRewardInfo;
using Key = cGZPersistResourceKey;

constexpr uint32_t kTag = 0x7e000000;     // the service under test
constexpr uint32_t kTextTag = 0x7e300000; // the test's strings
constexpr uint32_t kInfoTag = 0x7e400000; // a reward info under test (RewardInfo_Run)
constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kCommonImplCell = 0x10cc040;

enum : unsigned {
    kSvcObj = 0,
    kPersist,
    kManager,
    kRecord,
    kSegment,
    kStrings,
    kFw,
    kCommon,
    kServer,
    kInitSvc,
    kInfoFake0,
    kInfoFakeCount = 3,
    kCatalog = kInfoFake0 + kInfoFakeCount, // the bin catalog (UnlockBins)
    kResources,                             // the resource service (the family filter's property sets)
    kProps,                                 // a property set
    kBin0,                                  // two bins
    kFrag0 = kBin0 + 2,                     // four fragments
    kFragCount = 4,
    kNewSegment = kFrag0 + kFragCount, // a loose-file segment AddDirectorySegments makes (its slot 3: the package fake)
    kLabels,                           // its extension table (slot 12)
};
constexpr uint32_t kCatalogCell = 0x10faf78;
constexpr uint32_t kResourcesCell = 0x10faf74;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte
bool gOurs;
difftest::Context *gCtx;

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
bool IsFake(uint32_t p)
{
    const uint32_t n = svcfx::Norm(p);
    return n >= 200 && n < 200 + svcfx::kFakes;
}
void LogText(const std::string &text)
{
    Ev(uint32_t(text.size()));
    for (char c : text) Ev(uint8_t(c));
}
cRZString *Str(uint32_t at) { return reinterpret_cast<cRZString *>(uintptr_t(at)); }
void LogString(uint32_t at) { LogText(Str(at)->mString); }
void LogChars(uint32_t at)
{
    const char *text = reinterpret_cast<const char *>(uintptr_t(at));
    LogText(text ? std::string(text) : std::string("<null>"));
}
void Call(Obj *self, unsigned slot)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(slot);
}
void LogKey(uint32_t at)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(at));
    for (int i = 0; i < 4; ++i) Ev(words[i]);
}
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// UTF-16 (the code coder's) to UTF-8 (cIGZString's).
std::string Utf8(const wchar_t *text)
{
    std::string out;
    for (; *text; ++text) {
        const uint32_t c = uint16_t(*text);
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        } else {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3f));
            out += char(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// ---- the world for a seed ----

// The key map's order as the original's (0x5f24ad: type, group, the fourth word, the instance): the test builds the map
// with it, so a lookup in a different order (ours) misses.
struct TestKeyLess {
    bool operator()(const Key &a, const Key &b) const
    {
        if (a.type != b.type) return a.type < b.type;
        if (a.group != b.group) return a.group < b.group;
        if (a.resource != b.resource) return a.resource < b.resource;
        return a.instance < b.instance;
    }
};
using TestKeyMap = std::map<Key, Key, TestKeyLess>;
static_assert(sizeof(TestKeyMap) == sizeof(RewardKeyMap), "same layout");

// Keys that differ in one word each (the order's every step), and their maps.
const Key kKeys[] = {
    {1, 1, 1, 1}, {1, 1, 2, 0}, {1, 1, 0, 2}, {1, 2, 0, 0}, {2, 0, 0, 0}, {0x80000000, 0, 0xffffffff, 0},
    {1, 1, 1, 0x80000000}, {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
};
constexpr uint32_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

// Reward ids: the coder's range is 0..0x32 (the encoder tests the id as a signed short, the decoder unsigned); duplicates
// (Find takes the first), ids past the range, and ids whose low half is in range.
const int32_t kIds[] = {0, 1, 2, 5, 0x32, 0x33, 100, 0x7fff, 0x10001, -1, -32767, 0x8001};
constexpr uint32_t kIdCount = sizeof(kIds) / sizeof(kIds[0]);
constexpr uint32_t kSafeIdCount = 9; // the ids SetRewardCode may take (bit 15 clear: see the file comment)
// Family ids (16 bytes, compared as bytes): each differs from the next in one byte, at the start, middle and end.
const RewardBinKey kFamilies[] = {{{1}}, {{1, 0, 0, 0, 0, 0, 0, 0, 2}}, {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80}},
                                  {{0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}};
constexpr uint32_t kFamilyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);
const uint32_t kFlags[] = {0, 1, 2, 3, 4, 5, 6, 7, 0x101, 0x80000001, 0xfffffffe};
const uint32_t kMasks[] = {0, 1, 2, 3, 4, 7, 0x100, 0x80000000, 0xffffffff};
const uint32_t kCounts[] = {0, 1, 2, 3, 5, 8};
const uint32_t kTableCounts[] = {0, 1, 2, 3, 6, 0x80000000, 0xffffffff};
const uint32_t kSizes[] = {0, 1, 7, 64, 64, 7, 1, 64}; // 0: ParseRewards refuses the record (no parser asked for)
const uint32_t kIndexes[] = {0, 1, 2, 4, 7, 8, 0x7fffffff, 0x80000000, 0xffffffff};

// Texts: the rewards' current codes, the arguments of MakeCode / SetRewardCode / IsValidCodeText, the names. The code
// alphabet (0xd09870) has digits but 5 and 0 at the end, letters but S and O there, then lower case and accented letters.
const char *const kTexts[] = {"",          "ABCD1234", "  ABCD1234  ", "abcdefgh", "ABCDEFGHI", "abc", "\tZZ\t",
                              "SO05",      "A_B C",    "!@#$",         "\xc3\x80\xc3\x89x",       "12345678901234567890",
                              "a b c d e", "_",        "XYZ", "ABCDEFGH!", "ABCDEFG!", "AB_ ", "1 2",
                              "ABCDEFGH~", "ABCDEFGH#", "ABCDEFGH\x7f", "ABCDEFG~",
                              // texts whose codes decode (the checksum's low bytes are "hij": tests/ts_tradeable_rewards_code.cpp)
                              "GFGGM", "GFGGMXYZ", "  GFGGMa ", "GWURG", "UXTXZ"};
constexpr uint32_t kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);
const char *const kPaths[] = {"", "C:\\Games\\kissingpoints.package", "a path longer than fifteen characters"};

std::wstring gAlphabet; // the code alphabet (0xd09870), read once

struct World {
    bool manager;      // the persist system hands out its resource manager
    bool record;       // the manager opens the reward record
    bool segment;      // the persist system opens the package
    bool table;        // the persist system hands out the code table
    bool initService;  // the framework answers Init's service
    uint32_t entries;  // rewards the XML load makes
    uint32_t pairs;    // key pairs the key map load makes
    uint32_t size;     // the record's size
    uint32_t tableCount;
    uint32_t group;    // the segment's group
    uint32_t shift;    // where the table's codes start
    uint32_t path;     // the package path
    uint32_t binFlags[2];           // the bins' flags (bit 0: locked)
    uint32_t fragBin[kFragCount];   // each fragment's bin (0 / 1)
    uint32_t fragBinKey[kFragCount]; // each fragment's bin key (kKeys)
    uint32_t fragKey[kFragCount];   // each fragment's own key (kKeys)
    uint32_t props;                 // the resource service: 0 none, 1 a set whose family follows the key, 2 refused
    uint32_t nullFragment;          // the catalog also lists a null, at this position from the end (0: none)
    uint32_t dirCount;              // the reward directories the app lists
    uint32_t dirExists;             // bit i: directory i exists
    uint32_t nullSegmentAt;         // the segment made at this count is null (past the list: none)
    uint32_t segmentsMade;
    uint32_t parser; // the framework's expat parser: 0 refused, 1 refused after handing it out, 2 made
};
World gWorld;
uint32_t gRandSeed;

// ---- codes ----
//
// The encoder 0xd09cd0's codes decode only for a few texts (ORIGINAL-BUG, lane LG: its scrambler puts "hij" where the
// checksum's low bytes belong; tests/ts_tradeable_rewards_code.cpp), so the success paths of RedeemCode and Load need codes
// the test makes itself (SaveCode's are reached with the texts kTexts ends with). They are built backwards from what the decoder
// checks: the decoder maps each of the 19 characters to its first index in the alphabet (0xd09e40: '_' as ' ', 0 when
// absent), unscrambles the indexes with 0xd0a090 (a bit permutation, probed once below: each index bit to the bit it lands
// on), and wants the CRC 0xd09ac0 of the first 9 bytes of the 8 character indexes in the 7-bit words 9..12 and the id in
// word 8.
struct BitTo {
    uint8_t word, bit;
};
BitTo gBitTo[19][7];

uint32_t AlphabetIndex(wchar_t c)
{
    if (c == L'_') c = L' ';
    for (uint32_t i = 0; i < 0x80 && i < gAlphabet.size(); ++i)
        if (gAlphabet[i] == c) return i;
    return 0;
}

// The unscrambler's bit map; false (and the test fails) unless every index bit lands on exactly one bit of words 0..18
// and every bit 0..6 of words 0..12 is reached.
bool ProbeUnscrambler(difftest::Context &ctx)
{
    using Unscramble = uint32_t(__cdecl *)(uint16_t *);
    bool reached[13][7] = {};
    for (uint32_t w = 0; w < 19; ++w) {
        for (uint32_t b = 0; b < 7; ++b) {
            uint16_t v[20] = {};
            v[w] = uint16_t(1u << b);
            ctx.Original<Unscramble>(0xd0a090)(v);
            int found = 0;
            for (uint32_t ow = 0; ow < 19; ++ow) {
                for (uint32_t ob = 0; ob < 16; ++ob) {
                    if (v[ow] >> ob & 1) {
                        ++found;
                        gBitTo[w][b] = {uint8_t(ow), uint8_t(ob)};
                    }
                }
            }
            if (found > 1) {
                fprintf(stderr, "unscrambler: index %u bit %u lands on %d bits\n", w, b, found);
                return false;
            }
            if (!found) gBitTo[w][b] = {0xff, 0xff}; // a bit the unscrambler drops
            const BitTo &to = gBitTo[w][b];
            if (to.word < 13 && to.bit < 7) reached[to.word][to.bit] = true;
        }
    }
    bool all = true;
    for (uint32_t w = 0; w < 13; ++w)
        for (uint32_t b = 0; b < 7; ++b)
            if (!reached[w][b]) {
                fprintf(stderr, "unscrambler: word %u bit %u unreached\n", w, b);
                all = false;
            }
    return all;
}

// The 19 code characters whose unscrambled indexes are want[0..12] (character indexes, id, checksum) and zeros.
void Scramble(const uint16_t (&want)[13], wchar_t *out)
{
    uint16_t indexes[19] = {};
    for (uint32_t w = 0; w < 19; ++w)
        for (uint32_t b = 0; b < 7; ++b) {
            const BitTo &to = gBitTo[w][b];
            if (to.word < 13 && (want[to.word] >> to.bit & 1)) indexes[w] |= uint16_t(1u << b);
        }
    for (uint32_t w = 0; w < 19; ++w) out[w] = gAlphabet[indexes[w]];
    out[19] = 0;
}

void MakeCode(const uint16_t (&chars)[8], uint16_t id, wchar_t *out)
{
    uint16_t want[13] = {};
    for (uint32_t i = 0; i < 8; ++i) want[i] = chars[i];
    want[8] = id;
    const uint32_t crc = gCtx->Original<uint32_t(__cdecl *)(const void *, uint32_t)>(0xd09ac0)(want, 9);
    want[9] = uint16_t(crc >> 24 & 0x7f);
    want[10] = uint16_t(crc >> 16 & 0x7f);
    want[11] = uint16_t(crc >> 8 & 0x7f);
    want[12] = uint16_t(crc & 0x7f);
    Scramble(want, out);
}

// Codes: decodable codes for ids around the decoder's bound (0x32 / 0x33) and the rewards' ids, from random alphabet
// characters (the space among them: the decoder trims trailing spaces), some with spaces around (trimmed), some broken (a
// character changed, one short, one long), and junk. The table hands them out and RedeemCode is given them.
std::vector<std::string> gCodeTexts;
std::unique_ptr<ContentString[]> gCodes, gTexts;
uint32_t gCodeCount;
ContentString *gOut;
const uint16_t kCodeIds[] = {0, 1, 2, 5, 0x32, 0x33, 0x7f};

std::string MakeValidCode(std::mt19937 &rng, uint16_t id)
{
    uint16_t chars[8];
    const uint32_t space = AlphabetIndex(L' ');
    const uint32_t length = rng() % 9;
    for (uint32_t i = 0; i < 8; ++i) chars[i] = uint16_t(i < length ? (rng() % 5 ? rng() % 62 : space) : space);
    wchar_t out[20];
    MakeCode(chars, id, out);
    return Utf8(out);
}

void BuildCodes(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 5);
    gCodeTexts.clear();
    for (uint32_t i = 0; i < 10; ++i) {
        std::string code = MakeValidCode(rng, kCodeIds[rng() % 7]);
        switch (rng() % 7) {
        case 0: code = "  " + code + " "; break; // trimmed
        case 1: code[rng() % code.size()] = 'S'; break; // the checksum breaks (mostly)
        case 2: code.pop_back(); break; // too short
        case 3: code += "A"; break; // too long
        default: break;
        }
        gCodeTexts.push_back(code);
    }
    gCodeTexts.push_back("");
    gCodeTexts.push_back("not a code at all");
    gCodeCount = uint32_t(gCodeTexts.size());
    gCodes = std::make_unique<ContentString[]>(gCodeCount);
    for (uint32_t i = 0; i < gCodeCount; ++i) gCodes[i].s.mString.assign(gCodeTexts[i]);
}

void *CodeArg(uint32_t a) { return &gCodes[a % gCodeCount].s; }
void *TextArg(uint32_t a) { return &gTexts[a % kTextCount].s; }

// ---- fake handlers ----

uint32_t __fastcall HoldMessage(Obj *self, void *, uint32_t message);

uint32_t ServiceQuery(Obj *, uint32_t *a)
{
    if (a[0] == 0x2a4915ea && a[1] == 0x4a490e00 && gWorld.initService) return HandOut(a[2], F(kInitSvc));
    return 0;
}
uint32_t BoolResult(Obj *, uint32_t *) { return Bool(); }
uint32_t ManagerOf(Obj *, uint32_t *) { return gWorld.manager ? P(&F(kManager)) : 0; }

// The persist system: 5 GetResource (the code table), 21 OpenSegmentFromValues.
uint32_t __fastcall PersistGetResource(Obj *self, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5)
{
    Call(self, 5);
    LogKey(key);
    Ev(iid);
    Ev(svcfx::Norm(out));
    Ev(a4);
    Ev(a5);
    if (!gWorld.table) return 0x100;
    HandOut(out, F(kStrings));
    return 0x101;
}
uint32_t __fastcall PersistOpenSegment(Obj *self, void *, uint32_t iid, uint32_t out, uint32_t a3, uint32_t path, uint32_t name,
                                       uint32_t a6, uint32_t a7)
{
    Call(self, 21);
    Ev(iid);
    Ev(svcfx::Norm(out));
    Ev(a3);
    LogChars(path);
    LogChars(name);
    Ev(a6);
    Ev(a7);
    if (!gWorld.segment) return 0x100 | Draw(2);
    HandOut(out, F(kSegment));
    return Bool();
}

// The resource manager: 39 OpenRecord.
uint32_t __fastcall ManagerOpenRecord(Obj *self, void *, uint32_t key, uint32_t out, uint32_t write)
{
    Call(self, 39);
    LogKey(key);
    Ev(svcfx::Norm(out));
    Ev(write);
    if (!gWorld.record) return 0x100;
    HandOut(out, F(kRecord));
    return 0x101;
}

// The record: 8 the read (fills the buffer).
uint32_t __fastcall RecordRead(Obj *self, void *, uint32_t buffer, uint32_t size)
{
    Call(self, 8);
    Ev(size);
    auto *bytes = reinterpret_cast<uint8_t *>(uintptr_t(buffer));
    for (uint32_t i = 0; i < size; ++i) bytes[i] = uint8_t(i * 37 + gWorld.shift);
    return Bool();
}

// The code table: 6 GetString, 10 SetString, 13 AppendString, 17 SetKey.
uint32_t __fastcall StringsGet(Obj *self, void *, uint32_t index, uint32_t language)
{
    Call(self, 6);
    Ev(index);
    Ev(language);
    return P(&gCodes[(index + gWorld.shift) % gCodeCount].s);
}
uint32_t __fastcall StringsSet(Obj *self, void *, uint32_t text, uint32_t index, uint32_t language)
{
    Call(self, 10);
    LogString(text);
    Ev(index);
    Ev(language);
    return gRng();
}
uint32_t __fastcall StringsAppend(Obj *self, void *, uint32_t text, uint32_t language)
{
    Call(self, 13);
    LogString(text);
    Ev(language);
    return gRng();
}
uint32_t __fastcall StringsSetKey(Obj *self, void *, uint32_t key)
{
    Call(self, 17);
    LogKey(key);
    return gRng();
}

// The framework: 3 GetClassObject: a loose-file segment for AddDirectorySegments (null at the world's count), the expat
// parser for ParseRewards (per the world), else a string set (Load uses it untested).
Obj gParser; // not one of the fixture's fakes (all taken): its count is compared by Finish
void *gService; // the service under test (its rewards get the world's flags and codes after the parse: the XML has neither)
void *gHandler; // the handler ParseRewards gave the parser
uint32_t __fastcall ParserSetHandler(Obj *self, void *, uint32_t handler);
uint32_t __fastcall ParserParse(Obj *self, void *, uint32_t bytes, uint32_t size, uint32_t last);
uint32_t __fastcall FwClassObject(Obj *self, void *, uint32_t clsid, uint32_t iid, uint32_t out)
{
    Call(self, 3);
    Ev(clsid);
    Ev(iid);
    Ev(svcfx::Norm(out));
    if (clsid == 0x6b6b01d1) {
        if (gWorld.parser >= 1) HandOut(out, gParser);
        return gWorld.parser == 2 ? 0x101 : 0x100;
    }
    if (clsid == 0xa079ce1b) {
        if (gWorld.segmentsMade++ != gWorld.nullSegmentAt) HandOut(out, F(kNewSegment));
    } else {
        HandOut(out, F(kStrings));
    }
    return Bool();
}

// ---- AddDirectorySegments 0xcb8730 ----

// Directories (one past the small-string limit, one empty) and the stand-ins for the directory list (0x1d0c60 and ours)
// and the directory test (0x1c4f94 and ours), each logging what it is handed.
const char *const kDirs[] = {"C:\\Rewards\\", "D:\\a directory past fifteen\\", "", "E:\\Ep\\"};
uint32_t __cdecl StandDirectories(uint32_t option, uint32_t list, uint32_t a, uint32_t b, uint32_t c)
{
    Ev(0x1d0c60);
    Ev(option);
    Ev(a);
    Ev(b);
    Ev(c);
    auto *dirs = reinterpret_cast<std::list<GZString, GZServiceAllocator<GZString>> *>(uintptr_t(list));
    Ev(uint32_t(dirs->size()));
    for (uint32_t i = 0; i < gWorld.dirCount; ++i) dirs->push_back(GZString(kDirs[i]));
    return Bool();
}
uint32_t gDirIndex;
bool __cdecl StandDirectoryExists(cRZString *path)
{
    Ev(0x1c4f94);
    LogText(path->mString);
    Ev(svcfx::Norm(*reinterpret_cast<uint32_t *>(path))); // its vtable
    return (gWorld.dirExists >> (gDirIndex++ % 32)) & 1;
}
// The segment's slot 9 (its directory, a cRZString) and the extension table's slot 3 (type, the extension string).
uint32_t __fastcall SegmentSetDirectory(Obj *self, void *, uint32_t path)
{
    Call(self, 9);
    LogString(path);
    return gRng();
}
uint32_t __fastcall LabelsSet(Obj *self, void *, uint32_t type, uint32_t extension)
{
    Call(self, 3);
    Ev(type);
    Ev(svcfx::Norm(extension));
    LogString(extension);
    return gRng();
}
uint32_t SegmentFace(Obj *, uint32_t *) { return P(&F(kSegment)); }
uint32_t SegmentLabels(Obj *, uint32_t *) { return P(&F(kLabels)); }

// The app's common implementation: 4 (the package path into the string).
uint32_t __fastcall CommonSlot4(Obj *self, void *, uint32_t option, uint32_t string, uint32_t a3, uint32_t a4, uint32_t a5)
{
    Call(self, 4);
    Ev(option);
    Ev(svcfx::Norm(string));
    Ev(a3);
    Ev(a4);
    Ev(a5);
    Str(string)->mString.assign(kPaths[gWorld.path]);
    return Bool();
}

// ---- the bin catalog and the resource service (the bin unlocking 0xcb84c0) ----

// A filter's Accept (slot 3) through its own vtable: the low byte (a bool in al).
uint32_t FilterAccepts(uint32_t filter, Obj &object)
{
    using AcceptFn = uint32_t(__thiscall *)(uint32_t, void *);
    return (*reinterpret_cast<AcceptFn **>(uintptr_t(filter)))[3](filter, &object) & 0xff;
}

// The catalog's slot 30: each bin the bin filter takes, then each of its fragments the fragment filter takes into the list
// (and a null when the world says), every answer logged.
uint32_t __fastcall CatalogFragments(Obj *self, void *, uint32_t out, uint32_t binFilter, uint32_t fragmentFilter)
{
    Call(self, 30);
    Ev(svcfx::Norm(out));
    Ev(svcfx::Norm(binFilter));
    Ev(svcfx::Norm(fragmentFilter));
    auto *list = reinterpret_cast<nTSCASCore::BinList *>(uintptr_t(out));
    Ev(uint32_t(list->size()));
    for (uint32_t bin = 0; bin < 2; ++bin) {
        const uint32_t takesBin = FilterAccepts(binFilter, F(kBin0 + bin));
        Ev(takesBin);
        if (!takesBin) continue;
        for (uint32_t i = 0; i < kFragCount; ++i) {
            if (gWorld.fragBin[i] != bin) continue;
            const uint32_t takes = FilterAccepts(fragmentFilter, F(kFrag0 + i));
            Ev(takes);
            if (takes) list->push_back(nTSCASCore::BinRef(reinterpret_cast<cIGZUnknown *>(&F(kFrag0 + i))));
        }
    }
    if (gWorld.nullFragment) list->insert(list->end() - std::min<size_t>(gWorld.nullFragment - 1, list->size()), nTSCASCore::BinRef());
    return gRng();
}

// A fragment: slot 19 its bin key, 21 its key, 18 sets its bin key (logged by content: it points into the key map).
uint32_t FragmentBinKey(Obj *self, uint32_t *) { return P(&kKeys[self->value[0]]); }
uint32_t FragmentKey(Obj *self, uint32_t *) { return P(&kKeys[self->value[1]]); }
uint32_t __fastcall FragmentSetBinKey(Obj *self, void *, uint32_t key)
{
    Call(self, 18);
    LogKey(key);
    return gRng();
}

// The resource service's slot 37: the property set of a fragment's key (its family follows the key's words), per world.
uint32_t __fastcall ResourcesGet(Obj *self, void *, uint32_t key, uint32_t iid, uint32_t out)
{
    Call(self, 37);
    LogKey(key);
    Ev(iid);
    Ev(svcfx::Norm(out));
    if (gWorld.props == 0) return 0x100;
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(key));
    F(kProps).value[0] = (words[0] + words[1] * 3 + words[2] * 5 + words[3] * 7) % (kFamilyCount + 1);
    HandOut(out, F(kProps));
    return gWorld.props == 1 ? 0x101 : 0x100; // 2: handed out but refused (released after)
}
// A property set's slot 17: its family id (one past the table: a family no reward has).
const RewardBinKey kOtherFamily{{9, 9, 9, 9}};
uint32_t PropsFamily(Obj *self, uint32_t *)
{
    return self->value[0] < kFamilyCount ? P(&kFamilies[self->value[0]]) : P(&kOtherFamily);
}

template <class Fn> void Shim(Obj &obj, unsigned slot, unsigned args, Fn fn)
{
    obj.Set(slot, args);
    obj.table[slot] = reinterpret_cast<void *>(fn);
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    svcfx::ResetAll();
    gWorld.manager = rng() % 6 != 0;
    gWorld.record = rng() % 5 != 0;
    gWorld.segment = rng() % 5 != 0;
    gWorld.table = rng() % 3 != 0;
    gWorld.initService = rng() % 2 != 0;
    gWorld.entries = kCounts[rng() % 6];
    gWorld.pairs = kCounts[rng() % 5];
    gWorld.size = kSizes[rng() % 8];
    gWorld.parser = rng() % 8 == 0 ? rng() % 2 : 2;
    gWorld.tableCount = kTableCounts[rng() % 7];
    gWorld.group = rng() % 3 ? 0x7f000000u + rng() % 0x100 : rng();
    gWorld.shift = rng() % 64;
    gWorld.path = rng() % 3;
    gRandSeed = rng();
    static const uint32_t kBinFlags[] = {0, 1, 2, 3, 0x101, 0xfffffffe};
    for (uint32_t &flags : gWorld.binFlags) flags = kBinFlags[rng() % 6];
    for (uint32_t i = 0; i < kFragCount; ++i) {
        gWorld.fragBin[i] = rng() % 2;
        gWorld.fragBinKey[i] = rng() % kKeyCount;
        gWorld.fragKey[i] = rng() % kKeyCount;
    }
    gWorld.props = rng() % 4 ? 1 : rng() % 3;
    gWorld.nullFragment = rng() % 3 == 0 ? 1 + rng() % 4 : 0;
    gWorld.dirCount = rng() % 5;
    gWorld.dirExists = rng() % 4 ? rng() : rng() % 2 ? 0 : 0xffffffffu;
    gWorld.nullSegmentAt = rng() % 3 ? 99 : rng() % 4;
    gWorld.segmentsMade = 0;
    gDirIndex = 0;

    svcfx::gFrameworkIndex = kFw;
    Obj &catalog = F(kCatalog);
    Shim(catalog, 30, 3, &CatalogFragments);
    catalog.Set(21, "p", 0, &BoolResult); // SaveFragment
    Shim(F(kResources), 37, 3, &ResourcesGet);
    Obj &newSegment = F(kNewSegment);
    newSegment.Set(3, 0, 0, &SegmentFace);
    newSegment.Set(4, 0, rng());
    newSegment.Set(6, "dd", rng());
    Shim(newSegment, 9, 1, &SegmentSetDirectory);
    newSegment.Set(10, "d", rng());
    newSegment.Set(12, 0, 0, &SegmentLabels);
    newSegment.Set(15, "dd", rng());
    newSegment.Set(16, "d", rng());
    Shim(F(kLabels), 3, 2, &LabelsSet);
    F(kManager).Set(27, "p", rng());
    F(kProps).Set(17, 0, 0, &PropsFamily);
    for (uint32_t bin = 0; bin < 2; ++bin) F(kBin0 + bin).Set(27, 0, gWorld.binFlags[bin]);
    for (uint32_t i = 0; i < kFragCount; ++i) {
        Obj &fragment = F(kFrag0 + i);
        fragment.value[0] = gWorld.fragBinKey[i];
        fragment.value[1] = gWorld.fragKey[i];
        fragment.Set(19, 0, 0, &FragmentBinKey);
        fragment.Set(21, 0, 0, &FragmentKey);
        Shim(fragment, 18, 1, &FragmentSetBinKey);
    }
    F(kSvcObj).Set(5, "ddp", 0, &ServiceQuery);
    Obj &persist = F(kPersist);
    persist.Set(3, 0, 0, &ManagerOf);
    Shim(persist, 5, 5, &PersistGetResource);
    Shim(persist, 21, 7, &PersistOpenSegment);
    Shim(F(kManager), 39, 3, &ManagerOpenRecord);
    Obj &record = F(kRecord);
    record.Set(10, 0, gWorld.size);
    Shim(record, 8, 2, &RecordRead);
    record.Set(6, 0, 0, &BoolResult);
    F(kSegment).Set(4, 0, gWorld.group);
    Obj &strings = F(kStrings);
    strings.Set(5, 1, gWorld.tableCount);
    Shim(strings, 6, 2, &StringsGet);
    Shim(strings, 10, 3, &StringsSet);
    Shim(strings, 13, 2, &StringsAppend);
    Shim(strings, 17, 1, &StringsSetKey);
    strings.Set(21, 0, 0, &BoolResult);
    Shim(F(kFw), 3, 3, &FwClassObject);
    gParser.Reset(240);
    Shim(gParser, 8, 1, &ParserSetHandler);
    Shim(gParser, 3, 3, &ParserParse);
    gHandler = nullptr;
    Shim(F(kCommon), 4, 5, &CommonSlot4);
    Shim(F(kServer), 3, 1, &HoldMessage);
    for (unsigned i = 0; i < kInfoFakeCount; ++i) F(kInfoFake0 + i).Set(3, 0, 0x1000 + i);

    *GameAddress<void *>(kServiceCell) = rng() % 9 ? &F(kSvcObj) : nullptr;
    *GameAddress<void *>(svcfx::kPersistCell) = &F(kPersist);
    *GameAddress<void *>(kCommonImplCell) = &F(kCommon);
    *GameAddress<void *>(svcfx::kMessageServerCell) = &F(kServer);
    *GameAddress<void *>(kCatalogCell) = &F(kCatalog);
    *GameAddress<void *>(kResourcesCell) = &F(kResources); // never null: the family filter uses it untested (ORIGINAL-BUG)
}

// ---- the expat parser (ParseRewards 0xcb81c0) ----

std::string Hex(uint32_t value)
{
    char text[12];
    snprintf(text, sizeof(text), value & 1 ? "%X" : "%x", value); // either case
    return text;
}
// A resource key's text as the loader reads it: type-group-resource-instance.
std::string KeyText(const Key &key)
{
    return Hex(key.type) + "-" + Hex(key.group) + "-" + Hex(key.resource) + "-" + Hex(key.instance);
}
// A family id's text: its four words (in memory order).
std::string FamilyText(const RewardBinKey &family)
{
    uint32_t words[4];
    std::memcpy(words, family.bytes, 16);
    return Hex(words[0]) + "-" + Hex(words[1]) + "-" + Hex(words[2]) + "-" + Hex(words[3]);
}

void StartElement(const char *name, const std::vector<std::pair<std::string, std::string>> &attrs)
{
    std::vector<const char *> list;
    for (const auto &[k, v] : attrs) list.push_back(k.c_str()), list.push_back(v.c_str());
    list.push_back(nullptr);
    Ev(0x540);
    Ev(VirtualCall<uint32_t(__thiscall *)(void *, const char *, const char **, uint32_t, uint32_t), 3>(
           gHandler, name, list.data(), uint32_t(list.size() - 1), 0u) & 0xff);
}
void EndElement(const char *name)
{
    Ev(0x541);
    Ev(VirtualCall<uint32_t(__thiscall *)(void *, const char *), 4>(gHandler, name) & 0xff);
}

// 8 SetHandler: the handler (a heap block on each side) logged by presence and by whose vtable it carries.
uint32_t __fastcall ParserSetHandler(Obj *self, void *, uint32_t handler)
{
    Call(self, 8);
    gHandler = reinterpret_cast<void *>(uintptr_t(handler));
    Ev(handler != 0);
    const bool game = *static_cast<void **>(gHandler) == GameAddress<void>(cTSTradeableRewardsLoader::kVTable);
    Ev(gOurs ? !game : game);
    return Bool();
}
// 3 Parse: the record's bytes (by content) replayed as the world's reward XML: the version, the key pairs (binpair; the
// map's insert keeps a key's first value), the rewards (reward with ID / string / group / icon / norandom and its first
// family id, then familyid and reskey elements for the rest). Then each reward whose world flags the XML can't express
// (other than locked, with or without norandom) gets them, and every reward its world code.
uint32_t __fastcall ParserParse(Obj *self, void *, uint32_t bytes, uint32_t size, uint32_t last)
{
    Call(self, 3);
    Ev(size);
    Ev(last);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; ++i) sum = sum * 31 + reinterpret_cast<const uint8_t *>(uintptr_t(bytes))[i];
    Ev(sum);
    StartElement("TradeableRewards", {{"version", std::to_string(gWorld.shift)}});
    for (uint32_t i = 0; i < gWorld.pairs; ++i) {
        const Key &key = kKeys[(gWorld.shift + i * 3) % kKeyCount];
        const Key &value = kKeys[(gWorld.shift + i * 5 + 1) % kKeyCount];
        if (i % 2) StartElement("binpair", {{"unlockedkey", KeyText(value)}, {"lockedkey", KeyText(key)}});
        else StartElement("binpair", {{"lockedkey", KeyText(key)}, {"unlockedkey", KeyText(value)}});
        EndElement("binpair");
    }
    struct Late {
        uint32_t flags;
        const char *code;
    };
    std::vector<Late> late;
    std::mt19937 rng(gWorld.shift * 131 + gWorld.entries);
    for (uint32_t i = 0; i < gWorld.entries; ++i) {
        const int32_t id = kIds[rng() % kIdCount];
        const uint32_t table = rng() & 0xffff;
        const uint32_t entry = rng() & 0xffff;
        const uint32_t group = rng();
        const uint32_t value = rng();
        const uint32_t flags = kFlags[rng() % 11];
        late.push_back({flags, kTexts[rng() % kTextCount]});
        std::vector<std::pair<std::string, std::string>> attrs = {
            {"ID", std::to_string(id)}, {"string", Hex(table << 16 | entry)}, {"group", Hex(group)}, {"icon", Hex(value)}};
        if (flags == 5) attrs.push_back({"norandom", "1"});
        std::vector<RewardBinKey> families;
        for (uint32_t k = rng() % 3; k; --k) families.push_back(kFamilies[rng() % kFamilyCount]);
        if (!families.empty() && rng() % 2) {
            attrs.push_back({"familyid", FamilyText(families.back())});
            families.pop_back();
        }
        std::shuffle(attrs.begin(), attrs.end(), rng);
        StartElement("reward", attrs);
        for (const RewardBinKey &family : families) StartElement("familyid", {{"familyid", FamilyText(family)}});
        for (uint32_t k = rng() % 3; k; --k) StartElement("reskey", {{"key", KeyText(kKeys[rng() % kKeyCount])}});
        EndElement("reward");
    }
    EndElement("TradeableRewards");
    auto *entries = reinterpret_cast<TradeableRewardEntries *>(Face(gService, 0x20));
    for (size_t i = 0; i < late.size() && i < entries->size(); ++i) {
        TradeableRewardEntry &entry = (*entries)[i];
        if (late[i].flags != 1 && late[i].flags != 5) entry.mFlags = late[i].flags;
        entry.mCode.s.mString.assign(late[i].code);
    }
    return Bool();
}

// 0x1d0e90 (cTSString's constructor, thiscall, ret 0x10): a text per table / entry, the key fields set.
cTSString *__fastcall StandText(cTSString *self, void *, uint32_t table, uint32_t entry, uint32_t group, uint32_t type)
{
    Ev(0x1d0e90);
    Ev(table & 0xffff);
    Ev(entry & 0xffff);
    Ev(group);
    Ev(type);
    self->mString.Construct(kTexts[(table + entry * 3) % kTextCount]);
    self->mResourceType = type;
    self->mGroup = group;
    self->mEntry = uint16_t(entry);
    self->mTable = uint16_t(table);
    self->mKeyWord3 = 0;
    self->mLoaded = 1;
    self->mLoadFlags = 0;
    return self;
}

// cRZRandom::Seed 0x1b8ecb: the case's seed in place of the clock's. The original asks for the clock's (0xffffffff), ours
// gets the case's from the override: both log 1; any other seed is logged.
void __fastcall StandSeed(cRZRandom *self, void *, uint32_t seed)
{
    Ev(0x1b8ecb);
    Ev(seed == UINT32_MAX || seed == gRandSeed ? 1u : seed);
    self->mIntegerSeed = gRandSeed;
    self->mRealSeed = int32_t(gRandSeed);
}
uint32_t ClockSeed(int, uint32_t) { return gRandSeed; }

// The game's new / free over ours, so both sides' blocks meet.
long __cdecl LibStrtol(const char *s, char **end, int base) { return std::strtol(s, end, base); }
int __cdecl LibAtoi(const char *s) { return std::atoi(s); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

// The message server's MessageSend (slot 3): logs the message by content and holds a reference until the end of the
// operation, when the message's count is logged (1: only the held one) and the reference dropped.
std::vector<uint32_t> gHeld;
uint32_t __fastcall HoldMessage(Obj *self, void *, uint32_t message)
{
    Call(self, 3);
    svcfx::LogMessage(message);
    if (message) {
        using AddRefFn = uint32_t(__thiscall *)(uint32_t);
        (*reinterpret_cast<AddRefFn **>(uintptr_t(message)))[1](message);
        gHeld.push_back(message);
    }
    return 0x100;
}
void DropHeld()
{
    using ReleaseFn = uint32_t(__thiscall *)(uint32_t);
    for (uint32_t message : gHeld) {
        Ev(reinterpret_cast<const uint32_t *>(uintptr_t(message))[1]);
        (*reinterpret_cast<ReleaseFn **>(uintptr_t(message)))[2](message);
    }
    gHeld.clear();
}

// ---- driving ----

// Every slot is called with its arguments pushed by hand and the stack pointer read back: a body that pops a different
// number of bytes than the caller pushed (a wrong declaration) sets gStackSkew, which each step logs.
int32_t gStackSkew;
uint32_t CallChecked(void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    static_assert(sizeof(R) == 4, "dword result");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return R(CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A))));
}

// A reward info's state: whether each face carries its side's vtable, the count and the fields.
void DumpInfo(Blob &b, const void *info)
{
    static const uint32_t kFaces[2][2] = {{0, Info::kVTable}, {4, Info::kUnknownVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(info, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(gOurs ? !game : game);
    }
    b.push_back(Word(info, 8));
    b.push_back(Word(info, 0xc));
    b.push_back(Word(info, 0x10));
    const auto *m = static_cast<const uint8_t *>(info);
    for (uint32_t off : {0x14u, 0x34u}) {
        const auto *s = reinterpret_cast<const cRZString *>(m + off);
        b.push_back(Word(m, off) == P(GameAddress<void>(cRZString::kVTable)));
        b.push_back(s->mRefCount);
        b.push_back(uint32_t(s->mString.size()));
        for (char c : s->mString) b.push_back(uint8_t(c));
    }
}

const uint32_t kInfoIids[] = {1, Info::kIid, Sys::kIid, 0x12345678};

// One reward info operation on the info whose +0 face is `info`.
void InfoOp(Blob &b, void *info, uint32_t a, uint32_t c)
{
    void *const unknown = Face(info, 4);
    switch (a % 7) {
    case 0: // the getters
        b.push_back(Slot<uint32_t>(info, 3));
        b.push_back(Slot<uint32_t>(info, 4));
        b.push_back(svcfx::Norm(Slot<uint32_t>(info, 5)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(info, 6)));
        break;
    case 1: // SetCode
        b.push_back(svcfx::Norm(Slot<uint32_t>(info, 7, a % 3 ? TextArg(c) : CodeArg(c))));
        break;
    case 2: { // QueryInterface on a face, AddRef / Release through the others
        void *faces[] = {info, unknown};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[(a >> 4) % 2], 0, kInfoIids[(a >> 5) % 4], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 2], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 9) % 2], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 3: b.push_back(Slot<uint32_t>(unknown, 5)); break; // RefCount
    default: b.push_back(Slot<uint32_t>(info, 3)); break;
    }
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

// ---- TradeableRewards_Run ----

bool gFactory;
void *BuildService(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *object = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xcb7b50)();
        b.push_back(object ? 1 : 0);
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0xcb73b0)(memory);
    b.push_back(r == memory);
    return memory;
}

// Whether SaveCode 0xcb89a0, given the reward's code `text`, gets a code back from the decoder: the original's own text
// helpers (0xcb8350 to UTF-16, eight characters; 0xcb8bc0 back), the encoder 0xd09cd0 and the decoder 0xd09af0, as SaveCode
// chains them. True when the encoder refuses (SaveCode stops there). The encoder takes any characters, but a code made from
// characters outside the alphabet need not decode, nor one whose id has bit 15 set.
bool CodeDecodes(int32_t id, void *text)
{
    using ToWide = bool(__stdcall *)(const void *, wchar_t *, uint32_t);
    using ToText = bool(__stdcall *)(const wchar_t *, uint32_t, void *);
    wchar_t chars[10] = {};
    gCtx->Original<ToWide>(0xcb8350)(text, chars, 8);
    wchar_t code[20] = {};
    if (!gCtx->Original<bool(__cdecl *)(wchar_t *, int32_t, const wchar_t *)>(0xd09cd0)(code, id, chars)) return true;
    ContentString made;
    gCtx->Original<ToText>(0xcb8bc0)(code, 19, made.Interface());
    wchar_t back[20] = {};
    gCtx->Original<ToWide>(0xcb8350)(made.Interface(), back, 19);
    uint16_t decoded = 0;
    wchar_t out[10] = {};
    return (gCtx->Original<uint32_t(__cdecl *)(const wchar_t *, uint16_t *, wchar_t *)>(0xd09af0)(back, &decoded, out) & 0xff) != 0;
}

template <class Set> void DumpSet(Blob &b, const Set &set)
{
    b.push_back(uint32_t(set.size()));
    for (const auto &key : set)
        for (int i = 0; i < 16; i += 4) b.push_back(Word(&key, uint32_t(i)));
}

void DumpService(Blob &b, void *object)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kRewardsVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(gOurs ? !game : game);
    }
    // +0x19..+0x1b (after the running flag) are never written: the 0xc5 fill, or heap leftovers from the factory.
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(off == 0x18 && gFactory ? m[0x18] : Word(m, off));
    const auto *entries = reinterpret_cast<const TradeableRewardEntries *>(m + 0x20);
    b.push_back(uint32_t(entries->size()));
    b.push_back(uint32_t(entries->capacity()));
    for (const TradeableRewardEntry &e : *entries) {
        b.push_back(e.mWord0);
        b.push_back(uint32_t(e.mId));
        b.push_back(e.mTextTable);
        b.push_back(e.mTextEntry);
        b.push_back(e.mTextGroup);
        b.push_back(e.mValue);
        b.push_back(e.mFlags);
        b.push_back(Word(&e.mCode, 0) == P(GameAddress<void>(cRZString::kVTable)));
        b.push_back(e.mCode.s.mRefCount);
        b.push_back(uint32_t(e.mCode.s.mString.size()));
        for (char ch : e.mCode.s.mString) b.push_back(uint8_t(ch));
        DumpSet(b, e.mFamilyIds);
        DumpSet(b, e.mObjectKeys);
    }
    const auto *map = reinterpret_cast<const TestKeyMap *>(m + 0x2c);
    b.push_back(uint32_t(map->size()));
    for (const auto &pair : *map) {
        b.push_back(pair.first.type);
        b.push_back(pair.first.group);
        b.push_back(pair.first.instance);
        b.push_back(pair.first.resource);
        b.push_back(pair.second.type);
        b.push_back(pair.second.group);
        b.push_back(pair.second.instance);
        b.push_back(pair.second.resource);
    }
    b.push_back(svcfx::Norm(Word(m, 0x34)));
    b.push_back(m[0x38]);
}

std::vector<Op> MakeServiceOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 3);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 24;
    if (rng() % 5) ops.push_back({0, uint32_t(rng()), uint32_t(rng())}); // usually Init first
    for (unsigned i = 0; i < n; ++i) ops.push_back({unsigned(rng() % 18), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

const Key *KeyArg(uint32_t a, Key &local)
{
    if (a % 5 == 0) {
        local = {a, a >> 3, 1, 0};
        return &local;
    }
    return &kKeys[a % kKeyCount];
}

void ServiceOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const face = Face(object, 0x1c);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(primary, 8) & 0xff); break; // Init
    case 1: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 2: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697, Info::kIid, 0x12345678};
        void *faces[] = {primary, Face(object, 4), face};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kIids[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 10) % 3], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 3: // the base service's accessors
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(primary, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    case 4: { // GetRewards into a list holding 0-2 fake infos, then operations on the infos, then the list released
        TradeableRewardList list;
        const uint32_t n = a % 3;
        if ((a >> 2) % 2) list.reserve(n + (a >> 3) % 3);
        for (uint32_t i = 0; i < n; ++i)
            list.push_back(TradeableRewardRef(reinterpret_cast<cITSTradeableRewardInfo *>(&F(kInfoFake0 + (c >> i) % kInfoFakeCount))));
        b.push_back(Slot<uint32_t>(face, 3, &list, kMasks[(a >> 5) % 9]));
        b.push_back(uint32_t(list.size()));
        b.push_back(uint32_t(list.capacity()));
        const size_t ranges = svcfx::gRanges.size();
        uint32_t k = 0;
        for (const TradeableRewardRef &ref : list) {
            const uint32_t p = P(ref.Get());
            if (!p || IsFake(p)) {
                b.push_back(svcfx::Norm(p));
                continue;
            }
            svcfx::AddRange(ref.Get(), Info::kSize, kInfoTag + 0x1000 * k);
            DumpInfo(b, ref.Get());
            InfoOp(b, ref.Get(), (a >> 9) + k * 7, c + k);
            DumpInfo(b, ref.Get());
            ++k;
        }
        svcfx::gRanges.resize(ranges);
        break;
    }
    case 5: { // SetRewardCode, unless the code it makes wouldn't decode (see the file comment)
        const int32_t id = kIds[a % kSafeIdCount];
        void *text = (a >> 4) % 2 ? TextArg(c) : CodeArg(c);
        if (!CodeDecodes(id, text)) {
            b.push_back(0xdead);
            break;
        }
        b.push_back(Slot<uint32_t>(face, 4, id, text) & 0xff);
        break;
    }
    case 6: b.push_back(Slot<uint32_t>(face, 5, (a >> 4) % 4 ? CodeArg(c) : TextArg(c)) & 0xff); break; // RedeemCode
    case 7: b.push_back(Slot<uint32_t>(face, 6)); break; // UnlockRandomReward
    case 8: // MakeCode
        gOut->s.mString.assign("old");
        b.push_back(Slot<uint32_t>(face, 7, gOut->Interface(), kIds[a % kIdCount], (a >> 4) % 3 ? TextArg(c) : CodeArg(c)) & 0xff);
        LogText(gOut->s.mString);
        break;
    case 9: { // IsMappedValue
        Key local;
        b.push_back(Slot<uint32_t>(face, 8, KeyArg(a, local)) & 0xff);
        break;
    }
    case 10: { // IsMappedKey
        Key local;
        b.push_back(Slot<uint32_t>(face, 9, KeyArg(a, local)) & 0xff);
        break;
    }
    case 11: b.push_back(Slot<uint32_t>(face, 10, (a >> 4) % 3 ? TextArg(c) : CodeArg(c)) & 0xff); break; // IsValidCodeText
    case 12: { // GetMappedKey
        Key local;
        Key out{0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e};
        b.push_back(Slot<uint32_t>(face, 11, KeyArg(a, local), &out) & 0xff);
        b.push_back(out.type);
        b.push_back(out.group);
        b.push_back(out.instance);
        b.push_back(out.resource);
        break;
    }
    case 13: b.push_back(Slot<uint32_t>(face, 12)); break; // GetRewardCount
    case 14: b.push_back(Slot<uint32_t>(face, 13, kIndexes[a % 9])); break; // GetRewardId
    case 15: // GetRewardCode
        gOut->s.mString.assign("old");
        b.push_back(Slot<uint32_t>(face, 14, kIds[a % kIdCount], gOut->Interface()) & 0xff);
        LogText(gOut->s.mString);
        break;
    case 16: // RedeemCode then GetRewards of the unlocked ones
        b.push_back(Slot<uint32_t>(face, 5, CodeArg(c)) & 0xff);
        b.push_back(Slot<uint32_t>(face, 13, kIndexes[a % 9]));
        break;
    default: // UnlockRandomReward twice
        b.push_back(Slot<uint32_t>(face, 6));
        b.push_back(Slot<uint32_t>(face, 6));
        break;
    }
}

void Fill(std::unique_ptr<ContentString[]> &strings, const char *const *texts, uint32_t count)
{
    strings = std::make_unique<ContentString[]>(count);
    for (uint32_t i = 0; i < count; ++i) strings[i].s.mString.assign(texts[i]);
}

void Compare(difftest::Context &ctx, const char *name, uint32_t seed, std::vector<Blob> (&results)[2], const std::vector<Op> &ops,
             uint32_t &stop)
{
    for (size_t i = 0; i < results[0].size() || i < results[1].size(); ++i) {
        const Blob empty;
        const Blob &x = i < results[0].size() ? results[0][i] : empty;
        const Blob &y = i < results[1].size() ? results[1][i] : empty;
        if (!svcfx::Same(ctx, x, y, name, seed)) {
            if (svcfx::gTrace)
                for (size_t w = 0; w < x.size() || w < y.size(); ++w)
                    fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < x.size() ? x[w] : 0, w < y.size() ? y[w] : 0);
            const size_t op = i ? i - 1 : 0;
            ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2), i && op < ops.size() ? ops[op].kind : 99u);
            stop = 1;
            return;
        }
    }
}

void Finish(Blob &b)
{
    DropHeld();
    b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
    gStackSkew = 0;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    b.push_back(uint32_t(gParser.refs));
}

struct Scope {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    shape::StandIn dirs{0x1d0c60, &appinit::CommonImplSlot5, &StandDirectories};
    shape::StandIn dirExists{0x1c4f94, &as2::DirectoryExists, &StandDirectoryExists};
    // The image's CRT atoi / strtol and ParseHex32 (their copies use the CRT's locale through imports): the library's and
    // our verified ParseHex32, for the original loader.
    shape::CodePatch atoiPatch{0x830972, shape::CodeOf(&LibAtoi)};
    shape::CodePatch strtolPatch{0x836d08, shape::CodeOf(&LibStrtol)};
    shape::CodePatch hexPatch{0x1b0e2c, shape::CodeOf(&hexparse::ParseHex32)};
    shape::StandIn text{0x1d0e90, static_cast<cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint32_t)>(&cTSString::Construct),
                        &StandText};
    shape::StandIn seed{0x1b8ecb, &cRZRandom::Seed, &StandSeed};
    decltype(gRZRandomClockSeedOverride) savedClock = gRZRandomClockSeedOverride;
    explicit Scope(difftest::Context &ctx)
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        globals.SetWord(kServiceCell, 0);
        globals.SetWord(svcfx::kPersistCell, 0);
        globals.SetWord(kCommonImplCell, 0);
        globals.SetWord(svcfx::kMessageServerCell, 0);
        globals.SetWord(kCatalogCell, 0);
        globals.SetWord(kResourcesCell, 0);
        gRZRandomClockSeedOverride = &ClockSeed;
        gCtx = &ctx;
        wchar_t alphabet[256] = {};
        ctx.Original<void(__cdecl *)(wchar_t *)>(0xd09870)(alphabet);
        gAlphabet = alphabet;
        if (!ProbeUnscrambler(ctx)) ctx.Fail("the unscrambler 0xd0a090 is not the bit permutation the codes are built on");
        // The construction checked against the image's decoder: "ABCD" and id 0x32 back.
        wchar_t code[20], chars[10] = {};
        const uint16_t want[8] = {uint16_t(AlphabetIndex(L'A')), uint16_t(AlphabetIndex(L'B')), uint16_t(AlphabetIndex(L'C')),
                                  uint16_t(AlphabetIndex(L'D')), uint16_t(AlphabetIndex(L' ')), uint16_t(AlphabetIndex(L' ')),
                                  uint16_t(AlphabetIndex(L' ')), uint16_t(AlphabetIndex(L' '))};
        MakeCode(want, 0x32, code);
        uint16_t id = 0;
        const uint32_t ok = ctx.Original<uint32_t(__cdecl *)(const wchar_t *, uint16_t *, wchar_t *)>(0xd09af0)(code, &id, chars);
        if (!(ok & 0xff) || id != 0x32 || std::wstring(chars) != L"ABCD") ctx.Fail("the test's codes don't decode");
        Fill(gTexts, kTexts, kTextCount);
        gOut = new ContentString;
    }
    ~Scope()
    {
        gRZRandomClockSeedOverride = savedClock;
        delete gOut;
        gTexts.reset();
        gCodes.reset();
        svcfx::gFrameworkIndex = 0;
        svcfx::gRanges.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

void AddTextRanges()
{
    svcfx::AddRange(gOut, sizeof(ContentString), kTextTag);
    svcfx::AddRange(gTexts.get(), sizeof(ContentString) * kTextCount, kTextTag + 0x1000);
    svcfx::AddRange(gCodes.get(), sizeof(ContentString) * gCodeCount, kTextTag + 0x2000);
}

// ---- RewardInfo_Run ----

void *BuildInfo(difftest::Context &ctx, Blob &b, bool ours, int32_t id, uint32_t value, const cRZString &name, const cRZString &code)
{
    void *memory = ::operator new(Info::kSize);
    std::memset(memory, 0xc5, Info::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Info(id, value, name, code))
                   : ctx.Original<void *(__thiscall *)(void *, int32_t, uint32_t, const cRZString *, const cRZString *)>(0xcb72a0)(
                         memory, id, value, &name, &code);
    b.push_back(r == memory);
    return memory;
}

} // namespace trs_test

using namespace trs_test;

DIFF_TEST(TradeableRewards_Run, 0xcb7790)
{
    // Also reached (see the file comment): 0xcb7b50 (factory), 0xcb73b0 (constructor), 0xcb7540 (destructor), 0xcb7770
    // (deleting destructor), 0xcb761b (its +4 thunk), 0xcb8300 (QueryInterface), 0xcb8331 / 0xcb8339 (its thunks), 0xcb7e60,
    // 0xcb8b30, 0xcb7d00, 0xcb8af0, 0xcb8f00, 0xcb8c70, 0xcb7bc0, 0xcb7ef0, 0xcb7ec0, 0xcb8f90, 0xcb7aa0, 0xcb7c30, 0xcb7c90,
    // 0xcb7cc0, 0xcb7c60, 0xcb8190, 0xcb89a0, 0xcb8ab0, 0xcb8e00, 0xcb8350, 0xcb8bc0; the reward infos' 0xcb72a0, 0xcb76b0,
    // 0xcb7613, 0xcb82d0, 0xcb82f7, 0xcb8ae0. The list's growth 0xcb6f00 is the original's on its side, compared through the
    // list (size, capacity, elements) and the infos' counts.
    Scope scope(ctx);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeServiceOps(seed);
        gFactory = seed % 4 == 0;
        BuildCodes(seed);
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            AddTextRanges();
            Blob first;
            void *object = BuildService(ctx, first, gOurs);
            gService = object;
            svcfx::AddRange(object, Sys::kSize, kTag);
            DumpService(first, object);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    ServiceOp(b, object, ops[i]);
                    DumpService(b, object);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys the service
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        uint32_t stop = 0;
        Compare(ctx, "TradeableRewards_Run", seed, results, ops, stop);
        if (stop) break;
    }
}

DIFF_TEST(RewardInfo_Run, 0xcb72a0)
{
    // Also reached (see the file comment): 0xcb76b0 (deleting destructor), 0xcb7613 (its +4 thunk), 0xcb82d0
    // (QueryInterface), 0xcb82f7 (its thunk), 0xcb8ae0 (SetCode).
    Scope scope(ctx);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng(seed * 97 + 1);
        BuildCodes(seed);
        const int32_t id = kIds[rng() % kIdCount];
        const uint32_t value = rng();
        const uint32_t nameText = rng(), codeText = rng();
        std::vector<Op> ops;
        const unsigned n = 1 + rng() % 16;
        for (unsigned i = 0; i < n; ++i) ops.push_back({0, uint32_t(rng()), uint32_t(rng())});
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            AddTextRanges();
            Blob first;
            const cRZString &name = *static_cast<cRZString *>(TextArg(nameText));
            const cRZString &code = *static_cast<cRZString *>(codeText % 2 ? TextArg(codeText) : CodeArg(codeText));
            void *info = BuildInfo(ctx, first, gOurs, id, value, name, code);
            svcfx::AddRange(info, Info::kSize, kInfoTag);
            DumpInfo(first, info);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(info, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    InfoOp(b, info, ops[i].arg, ops[i].arg2);
                    DumpInfo(b, info);
                } else {
                    b.push_back(Slot<uint32_t>(info, 2)); // the last release destroys it
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        uint32_t stop = 0;
        Compare(ctx, "RewardInfo_Run", seed, results, ops, stop);
        if (stop) break;
    }
}

DIFF_TEST(TradeableRewards_AddDirectorySegments, 0xcb8730)
{
    // Called directly (Load ignores its answer): every directory count 0..4 (the list walked from its end) against every
    // existence pattern of four directories and the segment the framework can't make at each count (or none), with the
    // option, type, group and extension drawn. Compared: the answer (al), the stack balance, the event log (the list
    // request, each directory by content, the segment's slots with their arguments, the manager's slot 27) and every fake's
    // counts (the segments released).
    Scope scope(ctx);
    auto member = &Sys::AddDirectorySegments;
    void *ours;
    std::memcpy(&ours, &member, sizeof(ours));
    void *const original = GameAddress<void>(0xcb8730);
    std::vector<uint8_t> self(Sys::kSize, 0xc5);
    ContentString extension;
    for (uint32_t seed = 0; seed < 5 * 16 * 6 * 2; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed + 1);
            gWorld.dirCount = seed % 5;
            gWorld.dirExists = (seed / 5) % 16;
            gWorld.nullSegmentAt = (seed / 80) % 6 < 4 ? (seed / 80) % 6 : 99;
            extension.s.mString.assign(seed / 480 ? "a longer extension text" : "xml");
            svcfx::AddRange(self.data(), Sys::kSize, kTag);
            svcfx::AddRange(&extension, sizeof(extension), kTextTag);
            gRng.seed(seed * 7 + 1);
            const uint32_t args[] = {P(&F(kManager)), seed % 3 ? 0xb19c133du : gRng(), seed % 2 ? 0x7181c501u : gRng(),
                                     seed % 4 ? 0xf19c3e5bu : gRng(), P(&extension.s)};
            Blob &b = results[side];
            b.push_back(CallChecked(side ? ours : original, self.data(), args, 5) & 0xff);
            Finish(b);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "TradeableRewards_AddDirectorySegments", seed)) break;
    }
}
