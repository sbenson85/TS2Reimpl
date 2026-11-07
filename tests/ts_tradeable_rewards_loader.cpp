// Differential tests for the tradeable rewards XML parse (lane LI): cTSTradeableRewardsService::ParseRewards and its content
// handler cTSTradeableRewardsLoader (src/ts/cTSTradeableRewardsLoader.h), in the standalone shape.
//
// TradeableRewardsLoader_Parse (0xcb81c0): ParseRewards on a service object (ours on both sides: the parse touches only its
// rewards +0x20 and key map +0x2c) with a recording fake framework (slot 3: the parser refused, refused after handing it
// out, or made) and a fake expat parser whose Parse replays a random element script onto the handler it was given: on
// the original side the game's loader (constructor 0xd08c30, vtable 0xebad1c: Release 0xd09850, StartElement 0xd096d0
// with its handlers 0xd09300 "TradeableRewards", 0xd09420 "reward", 0xd09020 "binpair", 0xd09370 "reskey", 0xd09140
// "familyid"; the key texts' parsers 0xd08d20 / 0xd08ea0; the entry constructor 0xcb71c0, its copy 0xd08a10, the
// vector's growth 0xd08490, the entry's destructor 0xd08cb0, the key map's insert 0xd08340, the family set's insert
// 0xd088d0), on ours ours through our vtable. Scripts: 0..14 elements of the five names and unknown ones (familyid / reskey
// before any reward, rewards past several growths of the vector), each with 0..6 attributes drawn from its own names,
// other elements' names and unknown ones, in any order, repeated, with numbers at the bounds of atoi / strtol (base 0) /
// ParseHex32 and key texts with 0..5 parts, spaces, prefixes and heap-sized lengths; at times a trailing name without
// a value (an odd count: the value read is the list's null, which the key parsers refuse) or a count short of the list.
// The parser holds a reference on the handler after its replay; the test releases it after ParseRewards returns (0: the
// count was back to one, so ParseRewards released its own exactly once; the release frees the loader on both sides, which
// difftest::FreeRecorder sees: both sides' frees reach the process heap).
// Compared: ParseRewards' answer (low byte), the stack balance, every fake call with its arguments, each StartElement /
// EndElement answer and the loader's fields after it (count, version, whether the service is this one, the current
// entry as an index into the rewards), the rewards (size, capacity, every field of every entry, both sets), the key map,
// every fake's count. Skipped: an entry's text (+8 / +0xa), group (+0xc) and value (+0x18) when its element had no
// "string" / "group" / "icon" attribute: the constructor 0xcb71c0 leaves them unwritten (its stores: +0, +4, +0x10, +0x1c,
// the string at +0x20, the set at +0x40), so the original's are the stack bytes of AddReward's local (ours are zero).
//
// TradeableRewardsLoader_Keys (0xd08d20, 0xd08ea0): both key text parsers on the same texts (null, empty, 1..5 parts,
// spaces, "0x", long, non-hex) into an output filled with a pattern; the answer, the output.
//
// TradeableRewardsLoader_Slots: a game loader (0xd08c30 on 0xc5 memory) against ours (new), driven through their own
// vtables: QueryInterface (shared 0x492810) on iids around 1 and 0x4b6b9c9b, AddRef (shared 0x145260), slots 4-11,
// StartElement on a loader without a service ("reward" and "binpair" do nothing) and without a current entry ("reskey"
// and "familyid" insert nothing), "TradeableRewards" (the version), unknown names; then Release (0xd09850) from counts 3
// down (a count of 1 or less frees). Compared: answers (low byte), every field, the stack balance, and whether each Release
// freed the loader's block (the free recorder: only the last does).
//
// The image's CRT atoi 0x830972 / strtol 0x836d08 and ParseHex32 0x1b0e2c (their image copies use the CRT's locale through
// imports) jump to the library's and our verified ParseHex32; the game's operator new 0x824632 and free 0x82540f to ours,
// so both sides' blocks meet.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "misc/HexParse.h"
#include "ts/cTSTradeableRewardsLoader.h"
#include "ts/cTSTradeableRewardsService.h"

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace trl_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSTradeableRewardsService;
using Loader = cTSTradeableRewardsLoader;
using Key = cGZPersistResourceKey;

constexpr uint32_t kBufferTag = 0x7e500000;
constexpr uint32_t kSysTag = 0x7e600000;
enum : unsigned { kFw = 0, kParser = 1 };

long __cdecl LibStrtol(const char *s, char **end, int base) { return std::strtol(s, end, base); }
int __cdecl LibAtoi(const char *s) { return std::atoi(s); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

// Every call is made with its arguments pushed by hand and the stack pointer read back: a body that pops a different
// number of bytes than the caller pushed sets gStackSkew, which is compared.
int32_t gStackSkew;
uint32_t CallChecked(const void *fn, void *self, const uint32_t *args, uint32_t count)
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
template <class... A> uint32_t Slot(void *object, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return CallChecked((*static_cast<void ***>(object))[slot], object, args, uint32_t(sizeof...(A)));
}

// ---- the script ----

struct Element {
    bool start;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    bool lone = false;       // a trailing name without a value (count odd: the value read is the list's null)
    std::string loneName;
    uint32_t shortBy = 0;    // the count given is this many strings short of the list's
};
std::vector<Element> gScript;
// Per reward element in script order: whether it had "string", "group", "icon" (else the original's are stack bytes).
struct RewardSeen {
    bool text, group, icon;
};
std::vector<RewardSeen> gRewards;

const char *const kNumbers[] = {"0", "1", "-1", "7", "2147483647", "2147483648", "-2147483648", "0x10", "010", " 12",
                                "abc", "", "+3", "99999999999"};
const char *const kHex[] = {"0", "1", "ffffffff", "0x1234abcd", "0XAB", "123456789", " 1f", "zz", "", "-1", "7fff8000",
                            "10000"};
const char *const kKeyTexts[] = {"1-2-3-4", "a-b-c", "a-b", "a", "", "1-2-3-4-5", "ffffffff-0-80000000-1",
                                 " 1 - 2 - 3 - 4 ", "--", "---", "0x10-0x20-0x30-0x40", "12345678-9abcdef0-0fedcba9-87654321",
                                 "-1-2-3", "4-3-2-1", "1-1-1-1", "1-1-1-2", "1-1-2-1", "1-2-1-1", "2-1-1-1", "x-y-z-w"};
constexpr uint32_t kNumberCount = sizeof(kNumbers) / sizeof(kNumbers[0]);
constexpr uint32_t kHexCount = sizeof(kHex) / sizeof(kHex[0]);
constexpr uint32_t kKeyTextCount = sizeof(kKeyTexts) / sizeof(kKeyTexts[0]);

const char *const kElements[] = {"TradeableRewards", "reward", "binpair", "reskey", "familyid", "rewards", "Reward", "",
                                 "familyidx", "binpai"};
const char *const kUnknownAttrs[] = {"id", "Id", "", "strings", "keys", "versio", "x"};

// What an attribute's value is drawn from.
std::string Value(std::mt19937 &rng, const std::string &name)
{
    if (name == "ID" || name == "norandom" || name == "version") return kNumbers[rng() % kNumberCount];
    if (name == "string" || name == "group" || name == "icon") return kHex[rng() % kHexCount];
    if (name == "familyid" || name == "key" || name == "lockedkey" || name == "unlockedkey") return kKeyTexts[rng() % kKeyTextCount];
    return rng() % 2 ? kKeyTexts[rng() % kKeyTextCount] : kNumbers[rng() % kNumberCount];
}

void MakeScript(uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 11);
    gScript.clear();
    gRewards.clear();
    const uint32_t n = seed % 7 == 0 ? 0 : rng() % 15;
    for (uint32_t e = 0; e < n; ++e) {
        Element el;
        el.start = rng() % 5 != 0;
        const uint32_t pick = rng() % 20;
        el.name = pick < 2 ? kElements[0] : pick < 8 ? kElements[1] : pick < 11 ? kElements[2] : pick < 14 ? kElements[3]
                  : pick < 17                    ? kElements[4]
                                                 : kElements[5 + rng() % 5];
        if (el.start) {
            // The element's own names (and, at times, another element's or an unknown one).
            std::vector<std::string> own;
            if (el.name == "TradeableRewards") own = {"version"};
            else if (el.name == "reward") own = {"ID", "string", "group", "familyid", "icon", "norandom"};
            else if (el.name == "binpair") own = {"lockedkey", "unlockedkey"};
            else if (el.name == "reskey") own = {"key"};
            else if (el.name == "familyid") own = {"familyid"};
            static const char *const kAll[] = {"version", "ID", "string", "group", "familyid", "icon",
                                               "norandom", "lockedkey", "unlockedkey", "key"};
            const uint32_t count = rng() % 7;
            for (uint32_t a = 0; a < count; ++a) {
                std::string name;
                const uint32_t r = rng() % 10;
                if (r < 7 && !own.empty()) name = own[rng() % own.size()];
                else if (r < 9) name = kAll[rng() % 10];
                else name = kUnknownAttrs[rng() % 7];
                el.attrs.push_back({name, Value(rng, name)});
            }
            if (rng() % 6 == 0) {
                // A lone trailing name: only names the key parsers read (they refuse the null) or no handler reads (atoi,
                // strtol and ParseHex32 take no null).
                static const char *const kLoneSafe[] = {"familyid", "key", "lockedkey", "unlockedkey", "x"};
                el.lone = true;
                el.loneName = kLoneSafe[rng() % 5];
            }
            if (!el.lone && el.attrs.size() > 1 && rng() % 6 == 0) el.shortBy = 2 * (1 + rng() % (el.attrs.size() - 1));
            if (el.name == "reward") {
                RewardSeen seen{false, false, false};
                const size_t used = (el.attrs.size() * 2 - el.shortBy) / 2;
                for (size_t a = 0; a < used; ++a) {
                    seen.text |= el.attrs[a].first == "string";
                    seen.group |= el.attrs[a].first == "group";
                    seen.icon |= el.attrs[a].first == "icon";
                }
                gRewards.push_back(seen);
            }
        }
        gScript.push_back(std::move(el));
    }
}

// ---- the world ----

struct World {
    uint32_t create;    // 0 refused, 1 refused after handing the parser out, 2 made
    bool bytes;         // a buffer (else null)
    uint32_t size;
    uint32_t setResult; // SetHandler's answer
    uint32_t parseResult;
};
World gWorld;
char gBuffer[0x40];
bool gOurs;
Sys *gSys;
void *gHandler;
Blob *gOut;

int32_t CurrentIndex(const Loader *loader)
{
    const auto *current = reinterpret_cast<const uint8_t *>(loader->mCurrent);
    if (!current) return -1;
    const auto *begin = reinterpret_cast<const uint8_t *>(gSys->mEntries.data());
    const ptrdiff_t off = current - begin;
    if (off < 0 || off % 0x48 || off / 0x48 >= ptrdiff_t(gSys->mEntries.size())) return -2;
    return int32_t(off / 0x48);
}
// The loader's fields, through the original's offsets (the game's loader has no C++ type here).
void DumpLoader(Blob &b, const void *handler)
{
    const auto *loader = static_cast<const Loader *>(handler);
    b.push_back(uint32_t(loader->mRefCount));
    b.push_back(uint32_t(loader->mVersion));
    b.push_back(loader->mService == gSys ? 1 : loader->mService ? 2 : 0);
    b.push_back(uint32_t(CurrentIndex(loader)));
}

uint32_t FwClassObject(Obj *, uint32_t *args)
{
    if (gWorld.create >= 1) {
        ++F(kParser).refs;
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(kParser);
    }
    return gWorld.create == 2 ? 0x101 : 0x100;
}
uint32_t ParserSetHandler(Obj *, uint32_t *args)
{
    gHandler = reinterpret_cast<void *>(uintptr_t(args[0]));
    svcfx::gEv.back() = gHandler ? 0x4a4a : 0; // the handler (a heap block on each side) by presence
    const bool game = *static_cast<void **>(gHandler) == GameAddress<void>(Loader::kVTable);
    Ev(gOurs ? !game : game); // each side's own vtable
    DumpLoader(svcfx::gEv, gHandler);
    return gWorld.setResult;
}
uint32_t ParserParse(Obj *, uint32_t *)
{
    for (const Element &e : gScript) {
        Ev(0x540);
        if (e.start) {
            std::vector<const char *> attrs;
            for (auto &[k, v] : e.attrs) attrs.push_back(k.c_str()), attrs.push_back(v.c_str());
            if (e.lone) attrs.push_back(e.loneName.c_str());
            uint32_t count = uint32_t(attrs.size()) - e.shortBy;
            attrs.push_back(nullptr);
            attrs.push_back(nullptr);
            Ev(count);
            Ev(Slot(gHandler, 3, e.name.c_str(), attrs.data(), count, 0x1234u) & 0xff);
        } else {
            Ev(Slot(gHandler, 4, e.name.c_str()) & 0xff);
        }
        DumpLoader(svcfx::gEv, gHandler);
    }
    Ev(Slot(gHandler, 1)); // the parser's own reference, dropped by the test after ParseRewards
    return gWorld.parseResult;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 5);
    svcfx::ResetAll();
    gWorld.create = rng() % 6 == 0 ? rng() % 2 : 2;
    gWorld.bytes = rng() % 9 != 0;
    static const uint32_t kSizes[] = {0, 1, 0x40, 0xffffffff};
    gWorld.size = rng() % 6 == 0 ? kSizes[rng() % 4] : 0x40;
    gWorld.setResult = rng() % 2 ? 0x101 : 0;
    gWorld.parseResult = rng() % 2 ? 0x101 : 0x100;
    for (uint32_t i = 0; i < sizeof(gBuffer); ++i) gBuffer[i] = char(rng());
    svcfx::gFrameworkIndex = kFw;
    F(kFw).Set(3, "ddp", 0, &FwClassObject);
    F(kParser).Set(8, "p", 0, &ParserSetHandler);
    F(kParser).Set(3, "pdd", 0, &ParserParse);
    gHandler = nullptr;
}

template <class Set> void DumpSet(Blob &b, const Set &set)
{
    b.push_back(uint32_t(set.size()));
    for (const auto &key : set) {
        uint32_t words[4];
        std::memcpy(words, &key, 16);
        b.insert(b.end(), words, words + 4);
    }
}
void DumpService(Blob &b)
{
    const TradeableRewardEntries &entries = gSys->mEntries;
    b.push_back(uint32_t(entries.size()));
    b.push_back(uint32_t(entries.capacity()));
    for (size_t i = 0; i < entries.size(); ++i) {
        const TradeableRewardEntry &e = entries[i];
        const RewardSeen seen = i < gRewards.size() ? gRewards[i] : RewardSeen{true, true, true};
        b.push_back(e.mWord0);
        b.push_back(uint32_t(e.mId));
        b.push_back(seen.text ? e.mTextTable : 0xdead);
        b.push_back(seen.text ? e.mTextEntry : 0xdead);
        b.push_back(seen.group ? e.mTextGroup : 0xdead);
        b.push_back(seen.icon ? e.mValue : 0xdead);
        b.push_back(e.mFlags);
        b.push_back(*reinterpret_cast<const uint32_t *>(&e.mCode) == P(GameAddress<void>(cRZString::kVTable)));
        b.push_back(e.mCode.s.mRefCount);
        b.push_back(uint32_t(e.mCode.s.mString.size()));
        b.push_back(uint32_t(e.mCode.s.mString.capacity()));
        DumpSet(b, e.mFamilyIds);
        DumpSet(b, e.mObjectKeys);
    }
    b.push_back(uint32_t(gSys->mKeyMap.size()));
    for (const auto &pair : gSys->mKeyMap) {
        b.push_back(pair.first.type);
        b.push_back(pair.first.group);
        b.push_back(pair.first.instance);
        b.push_back(pair.first.resource);
        b.push_back(pair.second.type);
        b.push_back(pair.second.group);
        b.push_back(pair.second.instance);
        b.push_back(pair.second.resource);
    }
}

bool __fastcall OursParse(Sys *self, void *, const char *bytes, uint32_t size) { return self->ParseRewards(bytes, size); }

struct Scope {
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    shape::CodePatch strtolPatch{0x836d08, shape::CodeOf(&LibStrtol)};
    shape::CodePatch atoiPatch{0x830972, shape::CodeOf(&LibAtoi)};
    shape::CodePatch hexPatch{0x1b0e2c, shape::CodeOf(&hexparse::ParseHex32)};
    Scope() = default;
    ~Scope()
    {
        svcfx::gRanges.clear();
        svcfx::gEv.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

bool Same(difftest::Context &ctx, const char *what, uint32_t seed, const Blob &a, const Blob &b)
{
    if (a == b) return true;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    ctx.Fail("%s seed %u: differs at word %u (sizes %u / %u): %08x / %08x", what, unsigned(seed), unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    return false;
}

} // namespace trl_test

using namespace trl_test;

DIFF_TEST(TradeableRewardsLoader_Parse, 0xcb81c0)
{
    // Also reached (see the file comment): 0xd08c30, 0xd09850, 0xd096d0, 0xd09300, 0xd09420, 0xd09020, 0xd09370, 0xd09140,
    // 0xd08d20, 0xd08ea0, 0xcb71c0, 0xd08a10, 0xd08490 (and its move 0xd08740), 0xd08cb0, 0xd08340, 0xd088d0.
    Scope scope;
    const auto original = ctx.Original<const void *>(0xcb81c0);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        MakeScript(seed);
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            Blob &b = results[side];
            gSys = new Sys;
            svcfx::AddRange(gSys, sizeof(Sys), kSysTag);
            svcfx::AddRange(gBuffer, sizeof(gBuffer), kBufferTag);
            const char *bytes = gWorld.bytes ? gBuffer : nullptr;
            const uint32_t args[] = {P(bytes), gWorld.size};
            const uint32_t r = CallChecked(gOurs ? reinterpret_cast<const void *>(&OursParse) : original, gSys, args, 2);
            b.push_back(r & 0xff);
            b.push_back(uint32_t(gStackSkew));
            gStackSkew = 0;
            if (gHandler) {
                // The parser's reference: 0, and the loader is freed (seen by the free recorder: a heap walk's answer for
                // a block just freed depends on the heap's layout, and differed in the full suite, seed 816).
                difftest::FreeRecorder freed;
                b.push_back(Slot(gHandler, 2));
                freed.Stop();
                b.push_back(freed.Freed(gHandler));
            }
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            DumpService(b);
            svcfx::CaptureFakes(b);
            b.push_back(uint32_t(gStackSkew));
            gStackSkew = 0;
            delete gSys;
            gSys = nullptr;
            svcfx::gRanges.clear();
        }
        if (!Same(ctx, "parse", seed, results[0], results[1])) return;
    }
}

DIFF_TEST(TradeableRewardsLoader_Keys, 0xd08d20)
{
    // Also 0xd08ea0 (the resource key parser).
    Scope scope;
    using Parse = uint32_t(__thiscall *)(void *, void *, const char *);
    const char *const extra[] = {nullptr, "1-2-3-4", "  0x1f-  2-3  -4  ", "123456789-1-1-1", "g-h-i-j", "1--2", "-",
                                 "aaaaaaaabbbbbbbbccccccccdddddddd-1"};
    std::vector<const char *> texts(std::begin(extra), std::end(extra));
    for (const char *t : kKeyTexts) texts.push_back(t);
    for (const char *t : kHex) texts.push_back(t);
    for (const char *text : texts) {
        for (int which = 0; which < 2; ++which) {
            Blob results[2];
            for (int side = 0; side < 2; ++side) {
                uint32_t out[4] = {0xa5a5a5a5, 0x5a5a5a5a, 0xc3c3c3c3, 0x3c3c3c3c};
                uint32_t r;
                if (side == 0) {
                    r = ctx.Original<Parse>(which ? 0xd08ea0 : 0xd08d20)(reinterpret_cast<void *>(0x5a5a), out, text) & 0xff;
                } else {
                    r = which ? Loader::ParseResourceKey(reinterpret_cast<Key *>(out), text)
                              : Loader::ParseFamilyId(reinterpret_cast<RewardBinKey *>(out), text);
                }
                results[side].push_back(r);
                results[side].insert(results[side].end(), out, out + 4);
            }
            if (results[0] != results[1])
                ctx.Fail("%s(\"%s\"): %u %08x %08x %08x %08x / %u %08x %08x %08x %08x", which ? "0xd08ea0" : "0xd08d20",
                         text ? text : "<null>", results[0][0], results[0][1], results[0][2], results[0][3], results[0][4],
                         results[1][0], results[1][1], results[1][2], results[1][3], results[1][4]);
        }
    }
}

DIFF_TEST(TradeableRewardsLoader_Slots, 0xd09850)
{
    // Also 0xd08c30 (the game loader's constructor), 0xd096d0 (StartElement without a service or current entry), the shared
    // slots 0x492810, 0x145260, 0x148260, 0x1477b0, 0x144910, 0x1ac160.
    Scope scope;
    static const uint32_t kIids[] = {0, 1, 2, 0x4b6b9c9a, 0x4b6b9c9b, 0x4b6b9c9c, 0xffffffff, 0x6b6b01cd};
    const char *const names[] = {"reward", "binpair", "reskey", "familyid", "TradeableRewards", "other"};
    const char *const attrs[] = {"key", "1-2-3-4", "familyid", "5-6-7-8", "version", "0x20", "lockedkey", "1-1-1-1",
                                 "ID", "3", nullptr};
    for (uint32_t start = 0; start <= 3; ++start) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = results[side];
            void *loader;
            if (side == 0) {
                void *memory = ::operator new(Loader::kSize);
                std::memset(memory, 0xc5, Loader::kSize);
                loader = ctx.Original<void *(__thiscall *)(void *)>(0xd08c30)(memory);
                b.push_back(loader == memory);
            } else {
                loader = new Loader;
                b.push_back(1);
            }
            const bool game = *static_cast<void **>(loader) == GameAddress<void>(Loader::kVTable);
            b.push_back(side == 0 ? game : !game);
            gSys = nullptr;
            DumpLoader(b, loader);
            for (uint32_t i = 0; i < start; ++i) b.push_back(Slot(loader, 1));
            for (uint32_t iid : kIids) {
                void *out = reinterpret_cast<void *>(0x77);
                b.push_back(Slot(loader, 0, iid, &out) & 0xff);
                b.push_back(out == loader ? 1 : P(out));
                DumpLoader(b, loader);
            }
            b.push_back(Slot(loader, 4, "reward") & 0xff);
            b.push_back(Slot(loader, 5, "text", 4u) & 0xff);
            b.push_back(Slot(loader, 6) & 0xff);
            b.push_back(Slot(loader, 7) & 0xff);
            b.push_back(Slot(loader, 8, 1u, 2u) & 0xff);
            b.push_back(Slot(loader, 9, 1u) & 0xff);
            b.push_back(Slot(loader, 10, 1u, 2u) & 0xff);
            b.push_back(Slot(loader, 11, 1u, 2u, 3u, 4u) & 0xff);
            for (const char *name : names) {
                for (uint32_t count : {0u, 2u, 10u}) {
                    b.push_back(Slot(loader, 3, name, attrs, count, 0u) & 0xff);
                    DumpLoader(b, loader);
                }
            }
            // Release down to the free: a count of 1 or less frees (0), else the count less one.
            int32_t count = static_cast<Loader *>(loader)->mRefCount;
            difftest::FreeRecorder freed;
            for (;;) {
                const uint32_t r = Slot(loader, 2);
                b.push_back(r);
                if (count <= 1) break;
                b.push_back(freed.Freed(loader)); // 0: not before the last
                --count;
                DumpLoader(b, loader);
            }
            freed.Stop();
            b.push_back(freed.Freed(loader)); // 1: the last Release freed it
            b.push_back(uint32_t(gStackSkew));
            gStackSkew = 0;
        }
        if (!Same(ctx, "slots", start, results[0], results[1])) return;
    }
}
